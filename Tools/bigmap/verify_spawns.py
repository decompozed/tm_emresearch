"""Checks the TDM spawns of a built merged map one by one, and the team balance. Each spawn: floor of World_colp.nif
under it (FLOOR_GAP), free room above it (HEAD_ROOM), no wall nearer than WALL_CLEAR at three heights, at least
MOUTH_CLEAR from every corridor mouth and ENEMY_CLEAR from the other team's spawns, JUMP_CLEAR from jump pads, inside
a room, and from a source map of its team (tdm_team). Red and blue at most BALANCE apart. Prints the TDM and FFA
spawns per arena; --image draws the map from above with the TDM spawns (red / blue) and the corridor mouths.

Usage: python verify_spawns.py <layout.json> <sources> <built mapNNN> [--image <png>]"""
import argparse
import collections
import math
import os
import sys

import corridor as cr
import geomutil as gu
import layout as layout_file
import nifedit as ne
from nifio import Nif

FLOOR_GAP = (-5.0, 210.0)
HEAD_ROOM = 200.0
WALL_CLEAR = 50.0
WALL_HEIGHTS = (40.0, 100.0, 160.0)
MOUTH_CLEAR = 500.0
ENEMY_CLEAR = 500.0
JUMP_CLEAR = 150.0
BALANCE = 2
SAME = 0.5


def spawn_nodes(nif):
    par = ne.parents(nif)
    out = []
    for i, b in enumerate(nif.blocks):
        if b.base != 'NiNode' or b.data.get('Collision Object', -1) < 0 or i not in par:
            continue
        p = nif.name(nif.blocks[par[i]])
        group = '%s/%s' % (nif.name(nif.blocks[par[par[i]]]), p) if p in ('REDTEAM', 'BLUETEAM') else p
        out.append((group, nif.name(b), ne.world(nif, i, par)[0]))
    return out


def mouth_lines(layout):
    out = []
    for c in layout.get('corridors', []):
        fr = cr.Frame(c)
        for a, floor, _ in (fr.profile[0], fr.profile[-1]):
            out.append((c['name'], fr.world(a, fr.b0, floor), fr.world(a, fr.b1, floor)))
    return out


def dist_segment_2d(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)


class Scene:
    def __init__(self, layout, built):
        colp = Nif.load(os.path.join(built, 'World_colp.nif'))
        tris, self.jumps = [], []
        for _, name, pose, md in cr.physx_actors(colp):
            ts = cr.actor_world_triangles(colp, md, pose)
            tris += [(name, t) for t in ts]
            if 'jumper' in name.lower():
                ps = [p for t in ts for p in t]
                self.jumps.append(([min(p[k] for p in ps) for k in range(2)], [max(p[k] for p in ps) for k in range(2)]))
        self.tris = tris
        self.index = gu.TriIndex(tris)
        self.world = Nif.load(os.path.join(built, 'World.nif'))
        par = ne.parents(self.world)
        grp = ne.find(self.world, 'PORTAL_NDLRG')
        self.rooms = {self.world.name(self.world.blocks[r]): cr.world_walls(self.world, r, par)
                      for r in self.world.blocks[grp].data['Rooms']}
        self.mouths = mouth_lines(layout)

    def measure(self, p):
        x, y, z = p
        col = self.index.column(x, y)
        floors = [h for h, _, nz in col if nz > 0.5 and h <= z - FLOOR_GAP[0]]
        m = dict(floor=max(floors) if floors else None)
        if m['floor'] is None:
            return m
        f = m['floor']
        m['gap'] = z - f
        above = [h for h, _, _ in col if h > max(z, f + 1.0)]
        m['head'] = min(above) - z if above else math.inf
        clear = math.inf
        for h in WALL_HEIGHTS:
            for k in range(16):
                a = 2 * math.pi * k / 16
                hit = self.index.segment((x, y, f + h), (x + 300 * math.cos(a), y + 300 * math.sin(a), f + h))
                if hit:
                    clear = min(clear, hit[0] * 300)
        m['wall'] = clear
        m['mouth'], m['mouth_name'] = min((dist_segment_2d(p, a, b), n) for n, a, b in self.mouths)
        m['jump'] = min((math.hypot(max(b0[0] - x, 0, x - b1[0]), max(b0[1] - y, 0, y - b1[1])) for b0, b1 in self.jumps),
                        default=math.inf)
        m['rooms'] = [n for n, w in self.rooms.items() if cr.inside(w, (x, y, f + 100.0))]
        return m


def problems(m, enemy):
    out = []
    if m['floor'] is None:
        return ['no floor']
    if not FLOOR_GAP[0] <= m['gap'] <= FLOOR_GAP[1]:
        out.append('%.0f above the floor' % m['gap'])
    if m['head'] < HEAD_ROOM:
        out.append('head room %.0f' % m['head'])
    if m['wall'] < WALL_CLEAR:
        out.append('wall at %.0f' % m['wall'])
    if m['mouth'] < MOUTH_CLEAR:
        out.append('%.0f from the %s mouth' % (m['mouth'], m['mouth_name']))
    if enemy < ENEMY_CLEAR:
        out.append('%.0f from an enemy spawn' % enemy)
    if m['jump'] < JUMP_CLEAR:
        out.append('%.0f from a jump pad' % m['jump'])
    if not m['rooms']:
        out.append('in no room')
    return out


def source_spawns(layout, sources):
    out = {}
    for m in layout['maps']:
        nif = Nif.load(os.path.join(sources, 'map%03d' % m['id'], 'World.nif'))
        out[m['id']] = [('%s/%s' % (g, n), tuple(v + o for v, o in zip(p, m['offset']))) for g, n, p in spawn_nodes(nif)]
    return out


def arena_of(p, src):
    for mid, nodes in src.items():
        keys = [k for k, q in nodes if math.dist(p, q) <= SAME]
        if keys:
            return mid, keys
    return None, []


def draw(path, scene, spawns, size=2048):
    from PIL import Image, ImageDraw
    xs = [p[0] for _, t in scene.tris for p in t]
    ys = [p[1] for _, t in scene.tris for p in t]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    s = (size - 40) / max(x1 - x0, y1 - y0)

    def px(p):
        return 20 + (p[0] - x0) * s, size - 20 - (p[1] - y0) * s

    img = Image.new('RGB', (size, size), (24, 24, 28))
    d = ImageDraw.Draw(img)
    for _, t in sorted(scene.tris, key=lambda nt: min(p[2] for p in nt[1])):
        if abs(gu.normal(t)[2]) > 0.5:
            g = int(max(60, min(200, 120 + max(p[2] for p in t) / 15)))
            d.polygon([px(p) for p in t], fill=(g, g, g))
    for _, a, b in scene.mouths:
        d.line([px(a), px(b)], fill=(255, 220, 0), width=5)
        r = MOUTH_CLEAR * s
        q = px(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
        d.ellipse([q[0] - r, q[1] - r, q[0] + r, q[1] + r], outline=(255, 220, 0), width=1)
    for team, p, bad in spawns:
        q = px(p)
        col = (255, 60, 60) if team == 'REDTEAM' else (70, 160, 255)
        d.ellipse([q[0] - 9, q[1] - 9, q[0] + 9, q[1] + 9], fill=col, outline=(255, 255, 255) if not bad else (255, 0, 255),
                  width=2 if not bad else 4)
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('layout')
    ap.add_argument('sources', help='folder holding the extracted source mapNNN/ folders')
    ap.add_argument('built', help='the built mapNNN folder')
    ap.add_argument('--image', help='top view with the TDM spawns (png)')
    a = ap.parse_args()
    layout = layout_file.load(a.layout)
    team_of = {m['id']: m.get('tdm_team') for m in layout['maps']}
    names = {m['id']: 'm%03d' % m['id'] for m in layout['maps']}
    scene = Scene(layout, a.built)
    src = source_spawns(layout, a.sources)
    nodes = spawn_nodes(scene.world)
    tdm = [(g.split('/')[1], p) for g, _, p in nodes if g.startswith('TDM/')]
    bad = 0
    counts = collections.Counter()
    drawn = []
    for team, p in tdm:
        mid, keys = arena_of(p, src)
        enemy = min((math.dist(p, q) for t, q in tdm if t != team), default=math.inf)
        m = scene.measure(p)
        errs = problems(m, enemy)
        if mid is None:
            errs.append('from no source spawn')
        elif team_of[mid] != team:
            errs.append('in %s, an arena of the other team' % names[mid])
        counts[(team, mid)] += 1
        drawn.append((team, p, bool(errs)))
        line = '%s %s %s %s' % (team, names.get(mid, '?'), '/'.join(keys) or '-', tuple(round(v) for v in p))
        if m['floor'] is not None:
            line += ' gap %.0f head %.0f wall %.0f mouth %.0f enemy %.0f jump %.0f rooms %s' % (
                m['gap'], min(m['head'], 9999), min(m['wall'], 9999), m['mouth'], enemy, min(m['jump'], 9999),
                ','.join(m['rooms']))
        print(line + (' PROBLEM: ' + '; '.join(errs) if errs else ''))
        bad += bool(errs)
    totals = collections.Counter(t for t, _ in tdm)
    for team in ('REDTEAM', 'BLUETEAM'):
        print('%s %d: %s' % (team, totals[team], ', '.join('%s %d' % (names[mid], n) for (t, mid), n in sorted(counts.items(), key=str)
                                                          if t == team)))
    ffa = collections.Counter(arena_of(p, src)[0] for g, _, p in nodes if g == 'FFA')
    print('FFA %d: %s' % (sum(ffa.values()), ', '.join('%s %d' % (names.get(mid, '?'), n) for mid, n in sorted(ffa.items(), key=str))))
    if abs(totals['REDTEAM'] - totals['BLUETEAM']) > BALANCE:
        print('PROBLEM: red %d, blue %d' % (totals['REDTEAM'], totals['BLUETEAM']))
        bad += 1
    if a.image:
        draw(a.image, scene, drawn)
        print('image', a.image)
    print('problems: %d' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
