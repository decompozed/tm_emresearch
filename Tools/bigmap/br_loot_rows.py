"""Battle royale loot of a merged map: kitdropinfo rows ki_mod 12 (SquareMode, the party room mode), ki_type 1 (fixed
point), ki_map_id = the map, written into a copy of cgd.dip (ENG/kitdropinfo.cdb only).

Client (kitdropinfo.cdb loader 0x892340; per-frame item list 0xB972B0, host only; spawn order 260 0xE11F90):
  rows with ki_mod < 20 go to the list of mode game+4+4*ki_mod; at the start of a round the host creates every row of
  the current map at ki_x/y/z (floats), hidden; times are in 1/100 s (round end at 6000 * round minutes, 0xA87AA0);
  ki_left_over_time > 0: first spawn at round start + round length - ki_left_over_time, else ki_respawn_time > 0: first
  spawn at round start + ki_respawn_time, else never; at each spawn the item is visible when rand() * 1000 / 32768 <
  ki_prob (hidden bit of the 260); picked up (ii_type 17 applied at once, 18 into the item slot when it is empty), it
  spawns again ki_respawn_time later (never with 0), with a new ki_prob draw; a hidden item stays hidden until the
  next round; the spawn clears the removal time that ki_removal_time sets at round start (0xB972B0), written 0;
  ki_team 0 = anyone; the list holds 100 objects (0xE0DF60, full at 99, 0xE0E7C0).
Rows of type 0 (dropped by killed players, gated by mode byte +116, 0x523050 / 0xE0FB90) are never written.

Points: the LOOT/<mode>/mNNN_<item> nodes of the built World.nif (build_bigmap.py --kitdrop) or a JSON list
[{"x", "y", "z", "arena"}]; dropped: points in a corridor, near a corridor mouth, without floor (World_colp.nif) under
them, duplicates; the rest is thinned per arena (farthest point first) and given the items in ITEM_CYCLE order."""
import argparse
import copy
import json
import math
import os
import struct
import sys
import zipfile

import corridor as cr
import geomutil as gu
import layout as layout_file
import maptables
import nifedit as ne
import ziprecords
from nifio import Nif

TABLE = 'ENG/kitdropinfo.cdb'
SQUARE_MODE = 12
FIXED_POINT = 1
LIST_CAPACITY = 100
MAX_ROWS = 90

# item id, ki_prob (per mille), ki_respawn_time (1/100 s); effects in effectinfo, texts at 0x6C81D0
ITEMS = {
    'hp': (4001000, 800, 3000),       # ii_type 17, effect 10100: key 1 (UIMSG_BATTLE_DEATH_RECOVER) +800
    'ammo': (4028100, 800, 3000),     # ii_type 17, effect 10906: key 146 (UIMSG_SKILL_EK_BULLET_RELOAD), all weapons
    'speed': (4017000, 500, 6000),    # ii_type 18, effect 10412: key 13 (UIMSG_DETAILSTAT_MAINSTAT_1) +100, 20 s
    'shield': (4002000, 500, 6000),   # ii_type 18, effect 10200: key 4 (UIMSG_F1HELP_MOD_ITM_SHIELD), 4 s
    'radar': (4026000, 400, 9000),    # ii_type 17, effect 10420: key 129 (UIMSG_SKILL_EK_SCANNER)
}
ITEM_CYCLE = ('hp', 'ammo', 'hp', 'ammo', 'speed', 'hp', 'ammo', 'shield', 'hp', 'ammo',
              'hp', 'radar', 'ammo', 'hp', 'speed', 'ammo', 'hp', 'shield', 'ammo', 'hp')
MOUTH_CLEAR = 600.0
CORRIDOR_PAD = 100.0
SAME_POINT = 80.0
FLOOR_BELOW = (-5.0, 150.0)


def f32_bits(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def loot_nodes(world_nif, layout):
    nif = Nif.load(world_nif)
    root = ne.find(nif, 'LOOT')
    assert root is not None, 'no LOOT node: build with --kitdrop'
    par = ne.parents(nif)
    ids = {'m%03d' % m['id']: m['id'] for m in layout['maps']}
    out = []
    for group in ne.children(nif, root):
        for k in ne.children(nif, group):
            tag = nif.name(nif.blocks[k]).split('_')[0]
            x, y, z = ne.world(nif, k, par)[0]
            out.append(dict(x=x, y=y, z=z, arena=ids[tag], mode=nif.name(nif.blocks[group])))
    return out


def mouths(layout):
    out = []
    for c in layout.get('corridors', []):
        fr = cr.Frame(c)
        for a, floor, _ in (fr.profile[0], fr.profile[-1]):
            out.append(fr.world(a, 0.5 * (fr.b0 + fr.b1), floor))
    return out


def in_corridor(p, layout):
    for c in layout.get('corridors', []):
        fr = cr.Frame(c)
        a, b = (p[0], p[1]) if fr.axis == 'x' else (p[1], p[0])
        lo, hi = fr.profile[0][0], fr.profile[-1][0]
        if lo - CORRIDOR_PAD <= a <= hi + CORRIDOR_PAD and fr.b0 - CORRIDOR_PAD <= b <= fr.b1 + CORRIDOR_PAD:
            return c['name']
    return None


def colp_index(colp_nif):
    nif = Nif.load(colp_nif)
    return gu.TriIndex([(name, t) for _, name, pose, md in cr.physx_actors(nif)
                        for t in cr.actor_world_triangles(nif, md, pose)])


def candidates(points, layout, colp, log):
    ms = mouths(layout)
    kept, dropped = [], {}
    for p in points:
        pos = (p['x'], p['y'], p['z'])
        reason = None
        corridor = in_corridor(pos, layout)
        if corridor:
            reason = 'in corridor'
        elif min(math.hypot(pos[0] - m[0], pos[1] - m[1]) for m in ms) < MOUTH_CLEAR:
            reason = 'near a mouth'
        elif any(math.dist(pos, (q['x'], q['y'], q['z'])) < SAME_POINT for q in kept):
            reason = 'duplicate'
        else:
            floors = [z for z, _, nz in colp.column(pos[0], pos[1]) if nz > 0.5 and z <= pos[2] - FLOOR_BELOW[0]]
            if not floors or not FLOOR_BELOW[0] <= pos[2] - max(floors) <= FLOOR_BELOW[1]:
                reason = 'no floor under it'
            else:
                p = dict(p, floor=max(floors))
        if reason:
            dropped[reason] = dropped.get(reason, 0) + 1
        else:
            kept.append(p)
    log('points %d, kept %d, dropped %s' % (len(points), len(kept), dropped))
    return kept, ms


def thin(points, count, ms):
    if count >= len(points):
        count = len(points)
    first = max(range(len(points)), key=lambda i: min(math.hypot(points[i]['x'] - m[0], points[i]['y'] - m[1]) for m in ms))
    chosen = [first]
    near = [math.hypot(p['x'] - points[first]['x'], p['y'] - points[first]['y']) for p in points]
    while len(chosen) < count:
        i = max((j for j in range(len(points)) if j not in chosen), key=lambda j: near[j])
        chosen.append(i)
        near = [min(near[j], math.hypot(points[j]['x'] - points[i]['x'], points[j]['y'] - points[i]['y'])) for j in range(len(points))]
    return [points[i] for i in chosen]


def plan(points, total, ms):
    arenas = sorted({p['arena'] for p in points})
    shares = {a: sum(1 for p in points if p['arena'] == a) for a in arenas}
    quota = {a: int(round(total * shares[a] / len(points))) for a in arenas}
    out = []
    for a in arenas:
        for k, p in enumerate(thin([p for p in points if p['arena'] == a], quota[a], ms)):
            out.append(dict(p, kind=ITEM_CYCLE[k % len(ITEM_CYCLE)]))
    return out


def make_rows(cdb, chosen, map_id):
    template = next(e for e in cdb.entries if {f.key: f.value for f in e}['ki_type'] == FIXED_POINT)
    rows = []
    for p in chosen:
        item, prob, respawn = ITEMS[p['kind']]
        row = copy.deepcopy(template)
        values = dict(ki_mod=SQUARE_MODE, ki_type=FIXED_POINT, ki_team=0, ki_item_id=item, ki_map_id=map_id,
                      ki_x=f32_bits(p['x']), ki_y=f32_bits(p['y']), ki_z=f32_bits(p['z']), ki_prob=prob, ki_group=0,
                      ki_grade=0, ki_removal_time=0, ki_respawn_time=respawn, ki_left_over_time=0)
        assert set(values) == {f.key for f in row}, 'kitdropinfo columns changed'
        for f in row:
            f.value = values[f.key]
        rows.append(row)
    return rows


def patch(cdb, rows, map_id):
    keep = []
    for e in cdb.entries:
        d = {f.key: f.value for f in e}
        if d['ki_mod'] == SQUARE_MODE:
            assert d['ki_map_id'] == map_id and d['ki_type'] == FIXED_POINT, 'ki_mod 12 row of map %d type %d' % (d['ki_map_id'], d['ki_type'])
            continue
        keep.append(e)
    removed = len(cdb.entries) - len(keep)
    cdb.entries = keep
    for r in rows:
        cdb.addEntry(r)
    return removed


def check(data, before, map_id, count):
    cdb = maptables.load(data, 'kitdropinfo')
    rows = [{f.key: f.value for f in e} for e in cdb.entries]
    old = [{f.key: f.value for f in e} for e in before.entries]
    old = [r for r in old if r['ki_mod'] != SQUARE_MODE]
    assert rows[:len(old)] == old, 'existing kitdropinfo rows changed'
    new = rows[len(old):]
    assert len(new) == count and all(r['ki_mod'] == SQUARE_MODE and r['ki_type'] == FIXED_POINT and r['ki_map_id'] == map_id for r in new)
    return new


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('layout')
    ap.add_argument('stage', help='the built mapNNN folder (World.nif with LOOT, World_colp.nif)')
    ap.add_argument('cgd', help='cgd.dip to start from (read only)')
    ap.add_argument('out', help='cgd.dip to write')
    ap.add_argument('--password-file', required=True)
    ap.add_argument('--points', help='JSON [{"x", "y", "z", "arena"}] instead of the LOOT nodes')
    ap.add_argument('--count', type=int, default=84)
    ap.add_argument('--report', help='writes the chosen points and rows as JSON')
    a = ap.parse_args()
    layout = layout_file.load(a.layout)
    mid = layout['id']
    assert a.count <= MAX_ROWS < LIST_CAPACITY - 1
    with open(a.password_file, 'rb') as f:
        password = f.read().strip()
    if a.points:
        with open(a.points) as f:
            points = json.load(f)
    else:
        points = loot_nodes(os.path.join(a.stage, 'World.nif'), layout)
    kept, ms = candidates(points, layout, colp_index(os.path.join(a.stage, 'World_colp.nif')), print)
    chosen = plan(kept, a.count, ms)
    with zipfile.ZipFile(a.cgd) as z:
        raw = z.read(TABLE, pwd=password)
    before = maptables.load(raw, 'kitdropinfo')
    cdb = maptables.load(raw, 'kitdropinfo')
    removed = patch(cdb, make_rows(cdb, chosen, mid), mid)
    data = maptables.dump(cdb)
    ziprecords.rewrite(a.cgd, a.out, replace={TABLE: data}, password=password)
    with zipfile.ZipFile(a.out) as z:
        new = check(z.read(TABLE, pwd=password), before, mid, len(chosen))
    by_kind = {}
    for p in chosen:
        by_kind.setdefault(p['kind'], 0)
        by_kind[p['kind']] += 1
    by_arena = {}
    for p in chosen:
        by_arena[p['arena']] = by_arena.get(p['arena'], 0) + 1
    print('%s: %d rows ki_mod %d map %d (replaced %d), by item %s, by source map %s'
          % (TABLE, len(new), SQUARE_MODE, mid, removed, by_kind, by_arena))
    if a.report:
        with open(a.report, 'w') as f:
            json.dump([dict(x=round(p['x'], 1), y=round(p['y'], 1), z=round(p['z'], 1), floor=round(p['floor'], 1),
                            arena=p['arena'], kind=p['kind'], item=ITEMS[p['kind']][0], prob=ITEMS[p['kind']][1],
                            respawn=ITEMS[p['kind']][2]) for p in chosen], f, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
