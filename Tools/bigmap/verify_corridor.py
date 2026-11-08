"""Checks corridors of a built map against the merge they were cut into (<work>/merged/mapNNN):
render meshes: only the triangles inside the cut boxes are gone, the new meshes lie in the corridor;
PhysX meshes: re-cooked meshes hold the original triangles minus the cut, others are byte-identical;
walk (colp): floor without gaps or steps along the corridor, bounded slope, head room, nothing blocking the way
(lanes pass beside the covers);
bullets (colb): free line along the corridor; closure: no ray leaves the corridor through walls, floor or ceiling;
covers: free passage beside them, solid for walking and bullets, floor around, head room on top;
rooms: the corridor is covered by exactly one room everywhere (also above the covers) and its portals go both ways."""
import argparse
import collections
import math
import os
import sys

import corridor as cr
import geomutil as gu
import layout as layout_file
import nifedit as ne
import physx_cook as pc
from nifio import Nif

STEP = 25.0
MAX_STEP = 12.0
MAX_SLOPE = 20.0
HEAD_ROOM = 560.0
HEIGHTS = (40.0, 120.0, 250.0, 400.0, 500.0)
APPROACH = 150.0
LANE_CLEAR = 40.0
MIN_PASSAGE = 160.0


def render_sets(nif):
    par = ne.parents(nif)
    out = {}
    for i, b in enumerate(nif.blocks):
        if b.base == 'NiMesh' and b.data['Primitive Type'] == 0 and 'INDEX0' in cr.streams(nif, b):
            _, V, idx = cr.render_triangles(nif, i, par)
            out[(i, nif.name(b), cr.texture_of(nif, b))] = [(V[idx[j]], V[idx[j + 1]], V[idx[j + 2]]) for j in range(0, len(idx) - 2, 3)]
    return out


def tri_key(t, q=0.05):
    p = [tuple(round(c / q) for c in v) for v in t]
    k = p.index(min(p))
    return tuple(p[k:] + p[:k])


def on_plane_of(t, olds):
    for o in olds:
        n = gu.normal(o)
        if all(abs(gu.dot(n, gu.sub(p, o[0]))) < 0.5 for p in t) and                 all(min(q[k] for q in o) - 0.5 <= p[k] <= max(q[k] for q in o) + 0.5 for p in t for k in range(3)):
            return True
    return False


def check_render(before, after, cs, errors):
    cuts = [x for c in cs for x in c['cuts'] if x['file'] == 'World.nif']
    boxes = [(tuple(x['box'][0]), tuple(x['box'][1])) for x in cuts]
    clip_boxes = [(tuple(x['box'][0]), tuple(x['box'][1])) for x in cuts if x.get('mode') == 'clip']
    removed = 0
    for key, old in before.items():
        new = after.get(key)
        if new is None:
            errors.append('render mesh %s missing' % (key,))
            continue
        a, b = collections.Counter(map(tri_key, old)), collections.Counter(map(tri_key, new))
        gone = [t for t in old if tri_key(t) in (a - b)]
        for t in gone:
            removed += 1
            if not any(gu.overlaps_box(t, bx) for bx in boxes):
                errors.append('render mesh %s lost a triangle outside the cuts at %s' % (key, t[0]))
        for t in [t for t in new if tri_key(t) in (b - a)]:
            if not clip_boxes or not on_plane_of(t, gone):
                errors.append('render mesh %s has a new triangle at %s' % (key, t[0]))
            elif any(all(gu.inside_box(p, bx, -0.5) for p in t) for bx in clip_boxes):
                errors.append('render mesh %s keeps a triangle inside a cut at %s' % (key, t[0]))
    if not clip_boxes:
        expected = sum(x.get('expect', 0) for x in cuts)
        if removed != expected:
            errors.append('render triangles removed %d, expected %d' % (removed, expected))
    new = [k for k in after if k not in before]
    hulls = [corridor_box(c, 100) for c in cs]
    for k in new:
        for t in after[k]:
            if not any(all(gu.inside_box(p, h) for p in t) for h in hulls):
                errors.append('new render mesh %s outside the corridors' % (k,))
                break
    return removed, new


def corridor_box(c, pad):
    fr = cr.Frame(c)
    a = [p[0] for p in c['profile']]
    for kind in ('render', 'colp', 'colb'):
        for key in ('floor', 'ceiling'):
            a += c[kind].get(key, [])
        for w in c[kind].get('walls', []):
            a += w
        a += [p[0] for p in c[kind].get('ceiling_end', [])]
    zs = [p[1] for p in c['profile']] + [p[2] for p in c['profile']] + [cap['top'] for cap in c.get('caps', [])]
    lo = fr.world(min(a) - pad, fr.b0 - pad, min(zs) - pad)
    hi = fr.world(max(a) + pad, fr.b1 + pad, max(zs) + pad)
    return tuple(min(x, y) for x, y in zip(lo, hi)), tuple(max(x, y) for x, y in zip(lo, hi))


def actor_tris(nif):
    out = []
    for i, name, pose, md in cr.physx_actors(nif):
        blob = nif.blocks[md].data['Mesh Data']['Data']
        out.append((name, blob, cr.actor_world_triangles(nif, md, pose)))
    return out


def expected_after_cuts(tris, cuts):
    for cut in cuts:
        box = (tuple(cut['box'][0]), tuple(cut['box'][1]))
        new = []
        for t in tris:
            if (cut.get('vertical') and abs(gu.normal(t)[2]) >= cr.VERTICAL) or not gu.overlaps_box(t, box):
                new.append(t)
            elif cut['mode'] == 'remove':
                if not all(gu.inside_box(p, box) for p in t):
                    new.append(t)
            else:
                rest, inside = gu.subtract_box(t, box)
                new += rest if inside > 0.01 else [t]
        tris = new
    return tris


def check_physx(fname, before, after, cs, errors, log):
    cuts = [x for c in cs for x in c['cuts'] if x['file'] == fname]
    extra = len(after) - len(before)
    if extra != len(cs):
        errors.append('%s: %d new actors, expected %d' % (fname, extra, len(cs)))
    for k, ((n0, b0, t0), (n1, b1, t1)) in enumerate(zip(before, after)):
        if n0 != n1:
            errors.append('%s actor %d renamed %s -> %s' % (fname, k, n0, n1))
            continue
        if b0 == b1:
            continue
        mine = [x for x in cuts if not x.get('names') or n0 in x['names']]
        want_t = [t for t in expected_after_cuts(t0, mine) if gu.area(t) > 0.01]
        got_t = [t for t in t1 if gu.area(t) > 0.01]
        want = collections.Counter(tri_key(t, 0.1) for t in want_t)
        got = collections.Counter(tri_key(t, 0.1) for t in got_t)
        miss, more = want - got, got - want
        miss_t = [t for t in want_t if tri_key(t, 0.1) in miss]
        more_t = [t for t in got_t if tri_key(t, 0.1) in more]
        for t in list(miss_t):
            j = next((k for k, u in enumerate(more_t) if pc.near(t, u, 0.05)), None)
            if j is not None:
                more_t.pop(j)
                miss_t.remove(t)
        miss, more = collections.Counter(map(tri_key, miss_t)), collections.Counter(map(tri_key, more_t))
        log('%s %s#%d re-cooked: %d -> %d triangles, %d expected missing, %d unexpected'
            % (fname, n0, k, len(t0), len(t1), sum(miss.values()), sum(more.values())))
        if sum(miss.values()) + sum(more.values()) > 2:
            errors.append('%s %s#%d: re-cooked mesh differs from the cut original' % (fname, n0, k))


def cover_spans(c):
    fr = cr.Frame(c)
    k = 0 if c['axis'] == 'x' else 1
    out = []
    for cover in c.get('covers', []):
        lo, hi = cr.cover_boxes(c, cover)[0]
        out.append((lo[k], hi[k], lo[1 - k], hi[1 - k], cover))
    assert all(fr.b0 <= s0 < s1 <= fr.b1 for _, _, s0, s1, _ in out), 'cover outside the corridor walls'
    return out


def free_lane(c, a, b):
    fr = cr.Frame(c)
    for a0, a1, s0, s1, cover in cover_spans(c):
        if a0 - APPROACH <= a <= a1 + APPROACH:
            lo, hi = (s1, fr.b1) if cover['side'] == 0 else (fr.b0, s0)
            return min(max(b, lo + LANE_CLEAR), hi - LANE_CLEAR)
    return b


def cover_checks(c, colp, colb, errors, log):
    if not c.get('covers'):
        return
    fr = cr.Frame(c)
    idx = gu.TriIndex([(n, t) for n, _, ts in colp for t in ts])
    bidx = gu.TriIndex([(n, t) for n, _, ts in colb for t in ts])
    k = 0 if c['axis'] == 'x' else 1
    least = 1e9
    for a0, a1, s0, s1, cover in cover_spans(c):
        at = cover['at']
        f, cl = fr.at(at)
        away = 1.0 if cover['side'] == 0 else -1.0
        b_wall, b_far, b_face = (fr.b0, fr.b1, s1) if cover['side'] == 0 else (fr.b1, fr.b0, s0)
        for lo, hi in cr.cover_boxes(c, cover):
            face = hi[1 - k] if cover['side'] == 0 else lo[1 - k]
            for a in (lo[k] + 5, 0.5 * (lo[k] + hi[k]), hi[k] - 5):
                for h in (40.0, 120.0, 250.0, 400.0):
                    z = f + h
                    if not lo[2] + 1 < z < hi[2] - 1:
                        continue
                    start, end = b_far - 2 * away, b_wall + 2 * away
                    for name, index in (('colp', idx), ('colb', bidx)):
                        hit = index.segment(fr.world(a, start, z), fr.world(a, end, z))
                        b_hit = None if not hit else start + (end - start) * hit[0]
                        if b_hit is None or abs(b_hit - face) > 1.0:
                            errors.append('%s: cover at %.0f: first hit across at %s, open face at %.0f (a=%.0f h=%.0f)'
                                          % (name, at, None if b_hit is None else round(b_hit), face, a, h))
                            continue
                        least = min(least, abs(b_far - face))
        mid_b = 0.5 * (s0 + s1)
        top = cr.cover_boxes(c, cover)[-1][1][2]
        if not idx.segment(fr.world(a0 - 100, mid_b, f + 40), fr.world(a1 + 100, mid_b, f + 40)):
            errors.append('colp: cover at %.0f does not block walking' % at)
        if not bidx.segment(fr.world(a0 - 100, mid_b, top - 30), fr.world(a1 + 100, mid_b, top - 30)):
            errors.append('colb: cover at %.0f does not stop bullets' % at)
        ring = [(a0 - 30, mid_b), (a1 + 30, mid_b), (0.5 * (a0 + a1), b_face + 30 * away)]
        for a, b in ring:
            x, y, _ = fr.world(a, b, 0)
            floors = [z for z, _, nz in idx.column(x, y) if nz > 0.5 and abs(z - fr.at(a)[0]) < 15]
            if not floors:
                errors.append('no floor beside the cover at %.0f, %.0f' % (x, y))
        lo, hi = cr.cover_boxes(c, cover)[-1]
        x, y = lo[0] + 0.37 * (hi[0] - lo[0]), lo[1] + 0.61 * (hi[1] - lo[1])
        col = [z for z, _, _ in idx.column(x, y)]
        below = [z for z in col if f < z < top + 1.0]
        above = [z for z in col if z >= top + 1.0]
        if not below or abs(max(below) - top) > 1.0 or not above:
            errors.append('cover at %.0f: surfaces %s, top expected at %.0f' % (at, [round(z) for z in col], top))
        else:
            log('cover at %.0f side %d: %d box(es), top %.0f above the floor, head room on top %.0f, passage %.0f'
                % (at, cover['side'], len(cover['boxes']), top - f, min(above) - top, abs(b_far - b_face)))
    log('covers: %d, least free passage %.0f (at least %.0f)' % (len(c['covers']), least, MIN_PASSAGE))
    if least < MIN_PASSAGE:
        errors.append('passage %.0f beside a cover' % least)


def walk_checks(c, colp, colb, errors, log):
    fr = cr.Frame(c)
    idx = gu.TriIndex([(n, t) for n, _, ts in colp for t in ts])
    bidx = gu.TriIndex([(n, t) for n, _, ts in colb for t in ts])
    a0, a1 = c['walk']['from'], c['walk']['to']
    mid = 0.5 * (fr.b0 + fr.b1)
    lanes = [mid + d for d in c['walk'].get('lanes', [0.0])]
    worst_step = worst_slope = 0.0
    min_room = 1e9
    for lane in lanes:
        prev = None
        n = int(abs(a1 - a0) / STEP)
        for i in range(n + 1):
            a = a0 + (a1 - a0) * i / n
            b = free_lane(c, a, lane)
            x, y, _ = fr.world(a, b, 0)
            guess = fr.at(a)[0]
            col = idx.column(x, y)
            floors = [z for z, _, nz in col if nz > 0.5 and z <= guess + 60]
            if not floors:
                errors.append('no floor at %.0f, %.0f' % (x, y))
                prev = None
                continue
            z = max(floors)
            above = [zz for zz, _, _ in col if zz > z + 1.0]
            room = (min(above) - z) if above else 1e9
            min_room = min(min_room, room)
            if room < c['walk'].get('head_room', HEAD_ROOM):
                errors.append('head room %.0f at %.0f, %.0f' % (room, x, y))
            if prev is not None:
                dz = abs(z - prev[1])
                worst_step = max(worst_step, dz)
                worst_slope = max(worst_slope, math.degrees(math.atan2(dz, STEP)))
                if dz > MAX_STEP:
                    errors.append('step %.1f at %.0f, %.0f' % (dz, x, y))
                for h in HEIGHTS:
                    p0 = fr.world(prev[0], prev[2], prev[1] + h)
                    p1 = fr.world(a, b, z + h)
                    if idx.segment(p0, p1):
                        errors.append('walk blocked at %.0f, %.0f height %.0f' % (x, y, h))
            prev = (a, z, b)
    log('walk: lanes %d, worst step %.1f per %.0f (slope %.1f deg), least head room %.0f'
        % (len(lanes), worst_step, STEP, worst_slope, min_room))
    if worst_slope > c['walk'].get('max_slope', MAX_SLOPE):
        errors.append('slope %.1f deg' % worst_slope)
    blocked = 0
    for h in (100.0, 200.0, 300.0):
        n = int(abs(a1 - a0) / 100)
        for i in range(n):
            u, v = a0 + (a1 - a0) * i / n, a0 + (a1 - a0) * (i + 1) / n
            hit = bidx.segment(fr.world(u, free_lane(c, u, mid), fr.at(u)[0] + h),
                               fr.world(v, free_lane(c, v, mid), fr.at(v)[0] + h))
            if hit:
                blocked += 1
                errors.append('bullet line blocked by %s near a=%.0f h=%.0f' % (hit[1], u, h))
    log('bullets: line along the corridor at 3 heights, %d blocked' % blocked)
    leaks = 0
    boxes = [box for cover in c.get('covers', []) for box in cr.cover_boxes(c, cover)]
    inside = c['walk'].get('inside', [a0, a1])
    for name, index in (('colp', idx), ('colb', bidx)):
        n = int(abs(inside[1] - inside[0]) / 50)
        for i in range(n + 1):
            a = inside[0] + (inside[1] - inside[0]) * i / n
            f, cl = fr.at(a)
            for b in (fr.b0 + 30, mid, fr.b1 - 30):
                for zz in (f + 30, 0.5 * (f + cl), cl - 30):
                    o = fr.world(a, b, zz)
                    if any(gu.inside_box(o, box, 5.0) for box in boxes):
                        continue
                    for d in ((0, 0, 3000), (0, 0, -3000), fr.world(0, 3000, 0), fr.world(0, -3000, 0)):
                        if not index.segment(o, tuple(o[k] + d[k] for k in range(3))):
                            leaks += 1
                            errors.append('%s: ray leaves the corridor from %s towards %s' % (name, tuple(round(v) for v in o), d))
    log('closure: rays up, down and sideways from inside the corridor, %d leaks' % leaks)


def room_checks(c, world, errors, log):
    """Every point a character or the camera takes along the walk is in exactly one room (the client draws only the
    last room when the camera is in none and keeps characters in their last room, 0xD7D330 / 0x8E5A70), and every
    portal has its way back."""
    if not c.get('rooms'):
        return
    fr = cr.Frame(c)
    par = ne.parents(world)
    grp = ne.find(world, 'PORTAL_NDLRG')
    rooms = world.blocks[grp].data['Rooms']
    walls = {r: cr.world_walls(world, r, par) for r in rooms}
    a0, a1 = c['walk']['from'], c['walk']['to']
    mid = 0.5 * (fr.b0 + fr.b1)
    bad = 0
    n = int(abs(a1 - a0) / STEP)
    for i in range(n + 1):
        a = a0 + (a1 - a0) * i / n
        f, cl = fr.at(a)
        for b in (fr.b0 + 30, mid, fr.b1 - 30):
            for z in (f + 100, min(f + 500, cl - 20)):
                p = fr.world(a, b, z)
                hits = [world.name(world.blocks[r]) for r in rooms if cr.inside(walls[r], p)]
                strict = [r for r in rooms if cr.inside(walls[r], p, -0.5)]
                if not hits or len(strict) > 1:
                    bad += 1
                    errors.append('point %s in rooms %s' % (tuple(round(v) for v in p), hits))
    for cover in c.get('covers', []):
        (lo, hi), cl = cr.cover_boxes(c, cover)[-1], fr.at(cover['at'])[1]
        for z in (hi[2] + 100, min(hi[2] + 500, cl - 20)):
            for x in (lo[0] + 5, hi[0] - 5):
                for y in (lo[1] + 5, hi[1] - 5):
                    hits = [world.name(world.blocks[r]) for r in rooms if cr.inside(walls[r], (x, y, z))]
                    if len(hits) != 1:
                        bad += 1
                        errors.append('point %s above a cover in rooms %s' % ((round(x), round(y), round(z)), hits))
    back = 0
    for r in rooms:
        for k in world.blocks[r].data['Out Portals']:
            adj = world.blocks[k].data['Adjoiner']
            if not any(world.blocks[q].data['Adjoiner'] == r for q in world.blocks[adj].data['Out Portals']):
                back += 1
                errors.append('portal %s has no way back' % world.name(world.blocks[k]))
    log('rooms: walk points not in exactly one room %d, one-way portals %d' % (bad, back))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('layout')
    ap.add_argument('merged', help='<work>/merged/mapNNN (before the corridors)')
    ap.add_argument('built', help='the built mapNNN folder')
    a = ap.parse_args()
    layout = layout_file.load(a.layout)
    errors = []
    log = print
    before = {n: Nif.load(os.path.join(a.merged, n)) for n in ('World.nif', 'World_colp.nif', 'World_colb.nif')}
    after = {n: Nif.load(os.path.join(a.built, n)) for n in ('World.nif', 'World_colp.nif', 'World_colb.nif')}
    cs = layout.get('corridors', [])
    removed, new = check_render(render_sets(before['World.nif']), render_sets(after['World.nif']), cs, errors)
    log('World.nif: %d render triangles removed or clipped, %d new meshes' % (removed, len(new)))
    colp = actor_tris(after['World_colp.nif'])
    colb = actor_tris(after['World_colb.nif'])
    check_physx('World_colp.nif', actor_tris(before['World_colp.nif']), colp, cs, errors, log)
    check_physx('World_colb.nif', actor_tris(before['World_colb.nif']), colb, cs, errors, log)
    for c in cs:
        log('corridor %s' % c['name'])
        walk_checks(c, colp, colb, errors, log)
        cover_checks(c, colp, colb, errors, log)
        room_checks(c, after['World.nif'], errors, log)
    for e in errors[:120]:
        print('  ', e)
    print('problems: %d' % len(errors))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
