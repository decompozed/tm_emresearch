"""Battle Royale storm regions of the Big Map (map 58) -> Common/include/HaveApi/BattleRoyaleMap.h.

The storm's safe area is a union of axis-aligned boxes: the four arenas (each one to three boxes) and the four
corridors. The boxes must not overlap across regions, otherwise one region closing would leave another one's floor
safe (Magic Paper Land and The Studio overlap in x 5800..13190, y 3393..4050 by their bounding boxes).
  - floor: the standable levels (Tools/nif/domination_zones.standable) of the built map058/World_colp.nif reachable
    from its spawns (Tools/nif/hardpoint_zones), on a 100-unit grid;
  - a floor cell belongs to the arena whose own World_colp.nif (shifted by the layout offset) has a level at the same
    height in that cell, kept only inside the arena's render crop (outside it the merged collision has floor that is
    never drawn);
  - an arena's boxes: the bounding boxes of its cells split by PARTS (rules on the cell centre, first match wins);
  - a corridor's box: its `across` band between the two arena boxes holding (or within END_SNAP of) its mouths;
  - exclusion: the box of an arena that keeps both mouths at least EXCLUDE outside, one side per mouth (the side
    removing the fewest floor cells);
  - final points: standable floor of the arena inside the exclusion box (FINAL_MARGIN in), at least FINAL_DISTANCE
    from both mouths, flat around (FLAT_RADIUS), at the height of the arena's FFA spawns (SPAWN_LEVEL), spread out.
The tool fails when a box of one region holds a floor cell of another region or two regions' boxes overlap by more
than OVERLAP_TOLERANCE.

Usage: python br_regions.py <layout.json> <sources dir: mapNNN folders of the arenas> <map058 folder> <output .h>
"""
import itertools
import json
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'nif'))
import hardpoint_zones as hz  # noqa: E402
from domination_zones import standable  # noqa: E402
from map_spawns import spawns as read_spawns  # noqa: E402

CELL = 100.0
ARENAS = [('Castle', 10), ('Paper', 22), ('Pvc', 26), ('Studio', 39)]
PARTS = {
    10: [('main', lambda x, y: True)],
    22: [('south-west', lambda x, y: y < 5100 and x < 6338), ('south-east', lambda x, y: y < 5100 and x > 6662),
         ('main', lambda x, y: True)],
    26: [('main', lambda x, y: y < -1200), ('north-west', lambda x, y: x < -2000), ('tower', lambda x, y: True)],
    39: [('south', lambda x, y: y < -300), ('north', lambda x, y: True)],
}
END_SNAP = 300.0
EXCLUDE = 1200.0
FINAL_DISTANCE = 3000.0
FINAL_MARGIN = 1250.0
SPAWN_LEVEL = 250.0
FLAT_RADIUS = 500.0
FLAT_SHARE = 0.7
FINAL_POINTS = 6
FINAL_SPACING = 1200.0
OVERLAP_TOLERANCE = 50.0
MAX_BOXES = 16

LICENSE = """/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

"""


def centre(cell):
    return (cell[0] + 0.5) * CELL, (cell[1] + 0.5) * CELL


def bbox(cells):
    xs = [c[0][0] for c in cells]
    ys = [c[0][1] for c in cells]
    zs = sorted(c[1] for c in cells)
    return [min(xs) * CELL, (max(xs) + 1) * CELL, min(ys) * CELL, (max(ys) + 1) * CELL, zs[0]]


def inside(box, x, y):
    return box[0] <= x <= box[1] and box[2] <= y <= box[3]


def overlap(a, b):
    return min(a[1], b[1]) - max(a[0], b[0]), min(a[3], b[3]) - max(a[2], b[2])


def arena_cells(layout, sources, built):
    raw = hz.floor_levels(hz.triangles(os.path.join(built, 'World_colp.nif')))
    levels = standable(raw)
    groups = read_spawns(os.path.join(built, 'World.nif'))
    floor = hz.reachable(levels, [p for pts in groups.values() for p in pts])
    own = {}
    crops = {}
    for m in layout['maps']:
        ox, oy, oz = m['offset']
        tris = hz.triangles(os.path.join(sources, 'map%03d' % m['id'], 'World_colp.nif'))
        own[m['id']] = hz.floor_levels([tuple((p[0] + ox, p[1] + oy, p[2] + oz) for p in t) for t in tris])
        if 'crop' in m:
            (x0, y0, _), (x1, y1, _) = m['crop']
            crops[m['id']] = (x0 + ox, x1 + ox, y0 + oy, y1 + oy)
    cells = {mid: [] for _, mid in ARENAS}
    for cell, z in floor:
        owners = [mid for mid, lv in own.items() if any(abs(level - z) <= 60 for level in lv.get(cell, []))]
        if len(owners) != 1:
            continue
        x, y = centre(cell)
        crop = crops.get(owners[0])
        if crop and not inside(crop, x, y):
            continue
        cells[owners[0]].append((cell, z))
    spawn_z = {}
    for x, y, z in groups.get('FFA', []):
        cell = (int(math.floor(x / CELL)), int(math.floor(y / CELL)))
        mid = next((m for m, c in cells.items() if any(cc == cell for cc, _ in c)), None)
        if mid is not None:
            lo, hi = spawn_z.get(mid, (z, z))
            spawn_z[mid] = (min(lo, z), max(hi, z))
    return cells, levels, spawn_z


def split_parts(mid, cells):
    parts = {name: [] for name, _ in PARTS[mid]}
    for cell, z in cells:
        x, y = centre(cell)
        name = next(n for n, rule in PARTS[mid] if rule(x, y))
        parts[name].append((cell, z))
    return [(name, bbox(c)) for name, c in parts.items() if c]


def corridor_box(corridor, arena_boxes):
    axis = 0 if corridor['axis'] == 'x' else 1
    a0, a1 = corridor['across']
    ends = [corridor['profile'][0], corridor['profile'][-1]]
    if ends[0][0] > ends[1][0]:
        ends.reverse()
    span = [ends[0][0], ends[1][0]]
    owners = []
    for k, (along, z, _) in enumerate(ends):
        point = (along, (a0 + a1) / 2) if axis == 0 else ((a0 + a1) / 2, along)
        best = None
        for arena, boxes in arena_boxes.items():
            for i, box in enumerate(boxes):
                lo, hi = (box[0], box[1]) if axis == 0 else (box[2], box[3])
                alo, ahi = (box[2], box[3]) if axis == 0 else (box[0], box[1])
                if min(ahi, a1) - max(alo, a0) <= 0:
                    continue
                face = hi if k == 0 else lo
                if inside(box, *point):
                    gap = 0.0
                else:
                    gap = abs(face - along)
                    beyond = along > face if k == 0 else along < face
                    if not beyond or gap > END_SNAP:
                        continue
                if best is None or gap < best[0]:
                    best = (gap, arena, face)
        if best is None:
            sys.exit('%s: no arena box at its end %d' % (corridor['name'], k))
        span[k] = best[2]
        owners.append(best[1])
    zfloor = min(p[1] for p in corridor['profile'])
    box = [span[0], span[1], a0, a1, zfloor] if axis == 0 else [a0, a1, span[0], span[1], zfloor]
    mouths = []
    for (along, z, _), arena in zip(ends, owners):
        mouths.append((arena, (along, (a0 + a1) / 2, z) if axis == 0 else ((a0 + a1) / 2, along, z)))
    return box, owners, mouths


def exclusion(cells, mouths):
    start = bbox(cells)[:4]
    sides = [[(0, mx + EXCLUDE), (1, mx - EXCLUDE), (2, my + EXCLUDE), (3, my - EXCLUDE)] for _, (mx, my, _) in mouths]
    best = None
    for choice in itertools.product(*sides):
        rect = list(start)
        for side, value in choice:
            rect[side] = max(rect[side], value) if side in (0, 2) else min(rect[side], value)
        if rect[0] >= rect[1] or rect[2] >= rect[3]:
            continue
        kept = sum(1 for c, _ in cells if inside(rect, *centre(c)))
        if best is None or kept > best[0]:
            best = (kept, rect)
    return best[1]


def final_points(cells, levels, rect, mouths, spawn_z):
    inner = [rect[0] + FINAL_MARGIN, rect[1] - FINAL_MARGIN, rect[2] + FINAL_MARGIN, rect[3] - FINAL_MARGIN]
    rad = int(FLAT_RADIUS // CELL)
    disc = [(i, j) for i in range(-rad, rad + 1) for j in range(-rad, rad + 1) if i * i + j * j <= rad * rad]
    candidates = []
    for cell, z in cells:
        x, y = centre(cell)
        if not spawn_z[0] - SPAWN_LEVEL <= z <= spawn_z[1] + SPAWN_LEVEL:
            continue
        if not inside(inner, x, y) or any(math.hypot(x - m[0], y - m[1]) < FINAL_DISTANCE for _, m in mouths):
            continue
        flat = sum(1 for i, j in disc if any(abs(level - z) <= hz.STEP for level in levels.get((cell[0] + i, cell[1] + j), [])))
        if flat >= FLAT_SHARE * len(disc):
            candidates.append((x, y, z))
    if not candidates:
        return []
    cx = (inner[0] + inner[1]) / 2
    cy = (inner[2] + inner[3]) / 2
    chosen = [min(candidates, key=lambda c: (c[0] - cx) ** 2 + (c[1] - cy) ** 2)]
    while len(chosen) < FINAL_POINTS:
        best = max(candidates, key=lambda c: min((c[0] - p[0]) ** 2 + (c[1] - p[1]) ** 2 for p in chosen))
        if min(math.hypot(best[0] - p[0], best[1] - p[1]) for p in chosen) < FINAL_SPACING:
            break
        chosen.append(best)
    return chosen


def check(regions, cells):
    flat = [(name, box) for name, boxes in regions for box in boxes]
    for i, (na, a) in enumerate(flat):
        for nb, b in flat[i + 1:]:
            if na == nb:
                continue
            w, h = overlap(a, b)
            if w > OVERLAP_TOLERANCE and h > OVERLAP_TOLERANCE:
                sys.exit('boxes of %s and %s overlap by %d x %d' % (na, nb, w, h))
    for name, boxes in regions:
        for other, mid in ARENAS:
            if other == name:
                continue
            for box in boxes:
                stray = [c for c, _ in cells[mid] if box[0] < centre(c)[0] < box[1] and box[2] < centre(c)[1] < box[3]]
                if stray:
                    sys.exit('a box of %s holds %d floor cells of %s' % (name, len(stray), other))


def f(v):
    return '%.1ff' % v


def header(boxes, arenas, corridors):
    out = LICENSE + """#ifndef COMMON_HAVE_API_BATTLE_ROYALE_MAP_H
#define COMMON_HAVE_API_BATTLE_ROYALE_MAP_H

#include <array>
#include <cstdint>

// Generated by Tools/bigmap/br_regions.py from map058's World_colp.nif and Tools/bigmap/layouts/map058_f3.json.
namespace Common::HaveApi::BattleRoyaleMap
{
	enum class Arena : std::uint8_t { Castle = 0, Paper = 1, Pvc = 2, Studio = 3 };
	inline constexpr std::size_t arenaCount = 4;

	struct Box
	{
		float x1, x2, y1, y2, zFloor;
	};

	struct Point
	{
		float x, y, z;
	};

	struct ArenaRegion
	{
		const char* name;
		std::uint8_t firstBox;
		std::uint8_t boxCount;
		Box exclusion;
		std::uint8_t finalPointCount;
		std::array<Point, 6> finalPoints;
	};

	struct Corridor
	{
		const char* name;
		std::uint8_t box;
		bool alongX;
		Arena low;
		Arena high;
		Point lowMouth;
		Point highMouth;
	};

"""
    out += '\tinline constexpr std::array<Box, %d> boxes = { {\n' % len(boxes)
    for label, b in boxes:
        out += '\t\t{ %s, %s, %s, %s, %s }, // %s\n' % (f(b[0]), f(b[1]), f(b[2]), f(b[3]), f(b[4]), label)
    out += '\t} };\n\n\tinline constexpr std::array<ArenaRegion, arenaCount> arenas = { {\n'
    for a in arenas:
        pts = list(a['points']) + [(0.0, 0.0, 0.0)] * (6 - len(a['points']))
        e = a['exclusion']
        out += '\t\t{ "%s", %d, %d, { %s, %s, %s, %s, 0.0f }, %d, { { %s } } },\n' % (
            a['name'], a['first'], a['count'], f(e[0]), f(e[1]), f(e[2]), f(e[3]), len(a['points']),
            ', '.join('{ %s, %s, %s }' % (f(p[0]), f(p[1]), f(p[2])) for p in pts))
    out += '\t} };\n\n\tinline constexpr std::array<Corridor, %d> corridors = { {\n' % len(corridors)
    for c in corridors:
        lo, hi = c['mouths']
        out += '\t\t{ "%s", %d, %s, Arena::%s, Arena::%s, { %s, %s, %s }, { %s, %s, %s } },\n' % (
            c['name'], c['box'], 'true' if c['alongX'] else 'false', c['low'], c['high'],
            f(lo[0]), f(lo[1]), f(lo[2]), f(hi[0]), f(hi[1]), f(hi[2]))
    out += """\t} };

	static_assert(boxes.size() <= 16);
}

#endif
"""
    return out


def main():
    layout_path, sources, built, output = sys.argv[1:5]
    layout = json.load(open(layout_path))
    cells, levels, spawn_z = arena_cells(layout, sources, built)
    names = {mid: name for name, mid in ARENAS}
    arena_boxes = {}
    boxes = []
    arenas = []
    for name, mid in ARENAS:
        parts = split_parts(mid, cells[mid])
        arena_boxes[name] = [b for _, b in parts]
        arenas.append({'name': name, 'mid': mid, 'first': len(boxes), 'count': len(parts)})
        for part, b in parts:
            boxes.append(('%s %s' % (name, part), b))
            print('%-7s %-10s x %6d..%6d  y %6d..%6d  floor %5d' % (name, part, *b[:4], b[4]))
    corridors = []
    mouths = {name: [] for name, _ in ARENAS}
    for c in layout['corridors']:
        box, owners, ends = corridor_box(c, arena_boxes)
        corridors.append({'name': c['name'], 'box': len(boxes), 'alongX': c['axis'] == 'x', 'low': owners[0],
                          'high': owners[1], 'mouths': [p for _, p in ends]})
        boxes.append(('corridor %s' % c['name'], box))
        for arena, p in ends:
            mouths[arena].append((c['name'], p))
        print('%-18s x %6d..%6d  y %6d..%6d  floor %5d  %s - %s' % (c['name'], *box[:4], box[4], owners[0], owners[1]))
    assert len(boxes) <= MAX_BOXES
    check([(name, arena_boxes[name]) for name, _ in ARENAS] +
          [('corridor ' + c['name'], [boxes[c['box']][1]]) for c in corridors], cells)
    for a in arenas:
        a['exclusion'] = exclusion(cells[a['mid']], mouths[a['name']])
        a['points'] = final_points(cells[a['mid']], levels, a['exclusion'], mouths[a['name']], spawn_z[a['mid']])
        print('%-7s exclusion x %6d..%6d y %6d..%6d, final points %s' % (
            a['name'], *a['exclusion'], [(round(p[0]), round(p[1]), round(p[2])) for p in a['points']]))
        if not a['points']:
            sys.exit('%s: no final point' % a['name'])
    text = header(boxes, arenas, corridors)
    open(output, 'w', encoding='utf-8', newline='').write(text.replace('\n', '\r\n'))
    print('written', output, '(%s)' % ', '.join(names.values()))


if __name__ == '__main__':
    main()
