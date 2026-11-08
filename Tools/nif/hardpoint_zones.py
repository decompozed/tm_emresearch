"""Hardpoint zone spots and spawn points per map -> Common/include/HaveApi/HardpointZones.h.

Reachable floor: the walkable triangles (normal z >= 0.85) of mapNNN/World_colp.nif (player collision) rasterised on
a 100-unit grid with EVERY level of each column (not just the lowest: under many maps lies a floor players never
reach, where falling kills). Floor levels of neighbour cells within STEP are connected; the reachable floor is what
is connected to the map's spawn points (mapNNN/World.nif, node "Respawn", Tools/nif/map_spawns.py).
Zone centres: reachable floor with (almost) the whole zone disc on the same level, inside the area the spawns span
(the play area), at least BASE_DISTANCE from the team bases, spread out by farthest-point sampling from the middle.
Maps without spawn points: the previous method (lowest walkable level, central 70 %).

Usage: python hardpoint_zones.py <unpacked map.dat dir> <RoomEnums.h> <output .h>
"""
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import nif  # noqa: E402
from map_spawns import spawns as read_spawns  # noqa: E402

CELL = 100.0
UP = 0.85
STEP = 110.0            # height difference between neighbour cells still walkable (steps, ramps)
LEVEL_MERGE = 60.0      # levels of one column closer than this are the same floor
ZONE_RADIUS = 900.0     # the zone spots were chosen with 900; the zones are 1100 since 2026-10-02 (spots kept)
DISC_CHECK = 600.0      # this much of the radius must be floor on the zone's level ...
DISC_SHARE = 0.75       # ... for this share of the disc cells
BASE_DISTANCE = 1500.0  # zone centres away from the team spawn bases
SPACING = 1800.0        # minimum distance between two zone centres


def semantic_streams(data, blocks, strings, name):
    out = []
    if name not in strings:
        return out
    target = strings.index(name)
    for o in range(0, len(data) - 12):
        ref = struct.unpack_from('<i', data, o)[0]
        if not (0 <= ref < len(blocks) and blocks[ref][0].startswith('NiDataStream')):
            continue
        per = data[o + 4]
        nsub = struct.unpack_from('<H', data, o + 5)[0]
        if per > 1 or not (1 <= nsub <= 64):
            continue
        q = o + 7 + 2 * nsub
        if q + 4 > len(data):
            continue
        ncomp = struct.unpack_from('<I', data, q)[0]
        if not (1 <= ncomp <= 8) or q + 4 + 8 * ncomp > len(data):
            continue
        names = [struct.unpack_from('<I', data, q + 4 + 8 * k)[0] for k in range(ncomp)]
        if target in names:
            out.append((ref, names.index(target)))
    return out


def triangles(path):
    blocks, strings = nif.load(path)
    local, children = {}, {}
    for i, (typ, data) in enumerate(blocks):
        if typ.endswith('Node') or typ.startswith('NiMesh'):
            try:
                name, t, rot, s, r = nif.avobject(data)
                local[i] = (t, rot, s)
                if not typ.startswith('NiMesh'):
                    children[i] = nif.node_children(r)
            except Exception:
                pass
    world = {0: local.get(0)}
    stack = [0]
    while stack:
        n = stack.pop()
        for c in children.get(n, []):
            if c in local and c not in world:
                world[c] = nif.compose(world[n], local[c])
                stack.append(c)
    tris = []
    for i, (typ, data) in enumerate(blocks):
        if not typ.startswith('NiMesh') or i not in world:
            continue
        pos = semantic_streams(data, blocks, strings, 'POSITION')
        idx = semantic_streams(data, blocks, strings, 'INDEX')
        if not pos or not idx:
            continue
        nverts, comps, raw = nif.datastream(blocks[pos[0][0]][1])
        if not nverts:
            continue
        stride = len(raw) // nverts
        offset = sum(((c >> 16) & 0xFF) * 4 for c in comps[:pos[0][1]])
        verts = [nif.transform_point(struct.unpack_from('<3f', raw, k * stride + offset), *world[i]) for k in range(nverts)]
        nidx, _, iraw = nif.datastream(blocks[idx[0][0]][1])
        if not nidx:
            continue
        isize = len(iraw) // nidx
        fmt = '<H' if isize == 2 else '<I'
        ids = [struct.unpack_from(fmt, iraw, k * isize)[0] for k in range(nidx)]
        for k in range(0, len(ids) - 2, 3):
            a, b, c = ids[k], ids[k + 1], ids[k + 2]
            if max(a, b, c) < len(verts):
                tris.append((verts[a], verts[b], verts[c]))
    return tris


def floor_levels(tris):
    """cell -> sorted list of floor heights (every walkable level of the column)."""
    grid = {}
    for a, b, c in tris:
        ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        if length < 1e-6 or abs(nz) / length < UP:
            continue
        det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(det) < 1e-9:
            continue
        for gx in range(int(math.floor(min(a[0], b[0], c[0]) / CELL)), int(math.floor(max(a[0], b[0], c[0]) / CELL)) + 1):
            for gy in range(int(math.floor(min(a[1], b[1], c[1]) / CELL)), int(math.floor(max(a[1], b[1], c[1]) / CELL)) + 1):
                px, py = (gx + 0.5) * CELL, (gy + 0.5) * CELL
                l1 = ((b[1] - c[1]) * (px - c[0]) + (c[0] - b[0]) * (py - c[1])) / det
                l2 = ((c[1] - a[1]) * (px - c[0]) + (a[0] - c[0]) * (py - c[1])) / det
                l3 = 1 - l1 - l2
                if min(l1, l2, l3) < -0.01:
                    continue
                grid.setdefault((gx, gy), []).append(l1 * a[2] + l2 * b[2] + l3 * c[2])
    levels = {}
    for cell, zs in grid.items():
        zs.sort()
        merged = [zs[0]]
        for z in zs[1:]:
            if z - merged[-1] > LEVEL_MERGE:
                merged.append(z)
            else:
                merged[-1] = max(merged[-1], z)
        levels[cell] = merged
    return levels


def reachable(levels, seeds):
    """flood fill over (cell, level index) from the floor under every seed position."""
    seen = set()
    stack = []
    for x, y, z in seeds:
        cell = (int(math.floor(x / CELL)), int(math.floor(y / CELL)))
        best = None
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                c = (cell[0] + dx, cell[1] + dy)
                for i, lz in enumerate(levels.get(c, [])):
                    if z - 400 <= lz <= z + 80 and (best is None or abs(lz - z) < best[0]):
                        best = (abs(lz - z), (c, i))
        if best and best[1] not in seen:
            seen.add(best[1])
            stack.append(best[1])
    while stack:
        cell, i = stack.pop()
        z = levels[cell][i]
        for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (cell[0] + d[0], cell[1] + d[1])
            for j, nz in enumerate(levels.get(n, [])):
                if abs(nz - z) <= STEP and (n, j) not in seen:
                    seen.add((n, j))
                    stack.append((n, j))
    return {(cell, levels[cell][i]) for cell, i in seen}


def pick_from_spawns(levels, spawn_sets, disc_check=DISC_CHECK, disc_share=DISC_SHARE, base_distance=BASE_DISTANCE):
    all_spawns = [p for pts in spawn_sets.values() for p in pts]
    bases = [p for k, pts in spawn_sets.items() if 'TEAM' in k for p in pts]
    floor = reachable(levels, all_spawns)
    by_cell = {}
    for cell, z in floor:
        by_cell.setdefault(cell, []).append(z)
    xs = [p[0] for p in all_spawns]
    ys = [p[1] for p in all_spawns]
    lo_x, hi_x, lo_y, hi_y = min(xs), max(xs), min(ys), max(ys)
    rad = int(math.ceil(disc_check / CELL))
    disc = [(dx, dy) for dx in range(-rad, rad + 1) for dy in range(-rad, rad + 1) if dx * dx + dy * dy <= rad * rad]
    candidates = []
    for cell, z in floor:
        x, y = (cell[0] + 0.5) * CELL, (cell[1] + 0.5) * CELL
        if not (lo_x <= x <= hi_x and lo_y <= y <= hi_y):
            continue
        if any(math.hypot(x - b[0], y - b[1]) < base_distance for b in bases):
            continue
        same = sum(1 for dx, dy in disc if any(abs(oz - z) <= 150 for oz in by_cell.get((cell[0] + dx, cell[1] + dy), [])))
        if same >= disc_share * len(disc):
            candidates.append((x, y, z))
    return spread(candidates, (sum(xs) / len(xs), sum(ys) / len(ys)), (hi_x - lo_x) * (hi_y - lo_y))


def spread(candidates, centre, area):
    if not candidates:
        return []
    count = 8 if area > 60e6 else 7 if area > 25e6 else 6
    chosen = [min(candidates, key=lambda c: (c[0] - centre[0]) ** 2 + (c[1] - centre[1]) ** 2)]
    dist = {c: (c[0] - chosen[0][0]) ** 2 + (c[1] - chosen[0][1]) ** 2 for c in candidates}
    spacing = SPACING
    while len(chosen) < count:
        best = max(candidates, key=lambda c: dist[c])
        if dist[best] < spacing ** 2:
            if spacing <= ZONE_RADIUS * 1.2:
                break
            spacing *= 0.85   # small maps: accept closer zones rather than fewer
            continue
        chosen.append(best)
        for c in candidates:
            d = (c[0] - best[0]) ** 2 + (c[1] - best[1]) ** 2
            if d < dist[c]:
                dist[c] = d
    return chosen


def pick_lowest(levels):
    """maps without spawn points: the lowest level of every column, central 70 %."""
    floor = {cell: zs[0] for cell, zs in levels.items()}
    cells = list(floor)
    xs = sorted(c[0] for c in cells)
    ys = sorted(c[1] for c in cells)
    lo_x, hi_x = xs[int(len(xs) * 0.05)], xs[int(len(xs) * 0.95) - 1]
    lo_y, hi_y = ys[int(len(ys) * 0.05)], ys[int(len(ys) * 0.95) - 1]
    mx, my = 0.15 * (hi_x - lo_x), 0.15 * (hi_y - lo_y)
    candidates = [((c[0] + 0.5) * CELL, (c[1] + 0.5) * CELL, floor[c]) for c in cells
                  if lo_x + mx <= c[0] <= hi_x - mx and lo_y + my <= c[1] <= hi_y - my]
    centre = ((lo_x + hi_x) / 2 * CELL, (lo_y + hi_y) / 2 * CELL)
    return spread(candidates, centre, (hi_x - lo_x) * (hi_y - lo_y) * CELL * CELL)


SPAWN_MAX_DROP = 300.0  # a spawn node is kept when a walkable floor lies 0..300 under it


def on_floor(levels, point):
    """True when the player collision has a walkable floor just under the spawn node (0..SPAWN_MAX_DROP). Some maps'
    World.nif TDM nodes are not used by the game and hang over the void or sit under a floor (checked 2026-10-01:
    Academy's red base 3138 over the kill floor, Bitmap's red base): respawning there kills the player."""
    x, y, z = point
    below = [level for level in levels.get((int(math.floor(x / CELL)), int(math.floor(y / CELL))), []) if level <= z + 20.0]
    return bool(below) and z - max(below) <= SPAWN_MAX_DROP


def header(rows, license_text):
    out = license_text + """#ifndef COMMON_HAVE_API_HARDPOINT_ZONES_H
#define COMMON_HAVE_API_HARDPOINT_ZONES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

// Hardpoint zone centres and spawn points per map, generated by Tools/nif/hardpoint_zones.py from map.dat:
//  - spawns: mapNNN/World.nif, node "Respawn" (team bases: TDM REDTEAM / BLUETEAM; others: every other spawn);
//  - zones: floor of mapNNN/World_colp.nif (player collision) connected to the spawns, the whole zone on one level,
//    inside the area the spawns span, away from the team bases, spread out from the middle.
// Units: world units (the positions of order 281). z = the floor height (the zone itself is a vertical cylinder:
// only x / y count). Check the zones in game with /haveapi zone <n>.
namespace Common::HaveApi
{
	struct ZoneSpot
	{
		float x, y, z;
	};

	struct MapZones
	{
		std::uint16_t map;
		std::uint8_t count;
		std::array<ZoneSpot, 8> spots;
	};

	struct MapSpawn
	{
		std::uint16_t map;
		std::int8_t team; // 1 red base, 2 blue base, 0 any other spawn
		float x, y, z;
	};

	inline constexpr auto hardpointZones = std::to_array<MapZones>({
"""
    for mid, name, spots, _ in rows:
        sp = ', '.join('{ %.1ff, %.1ff, %.1ff }' % s for s in spots)
        out += '\t\t{ %d, %d, { { %s } } }, // %s\n' % (mid, len(spots), sp, name)
    out += """	});

	inline constexpr auto mapSpawns = std::to_array<MapSpawn>({
"""
    for mid, name, _, spawn_sets in rows:
        seen = set()
        for key, pts in spawn_sets.items():
            team = 1 if key == 'TDM/REDTEAM' else 2 if key == 'TDM/BLUETEAM' else 0
            for p in pts:
                if (team, p) in seen:
                    continue
                seen.add((team, p))
                out += '\t\t{ %d, %d, %.1ff, %.1ff, %.1ff }, // %s %s\n' % (mid, team, p[0], p[1], p[2], name, key)
    out += """	});

	[[nodiscard]] inline std::span<const ZoneSpot> findHardpointZones(std::uint32_t map)
	{
		const auto found = std::ranges::find(hardpointZones, map, &MapZones::map);
		if (found == hardpointZones.end())
		{
			return {};
		}
		return std::span<const ZoneSpot>{ found->spots.data(), found->count };
	}

	// The spawns of `map` (contiguous in the table).
	[[nodiscard]] inline std::span<const MapSpawn> findMapSpawns(std::uint32_t map)
	{
		const auto first = std::ranges::find(mapSpawns, map, &MapSpawn::map);
		const auto last = std::find_if(first, mapSpawns.end(), [&](const MapSpawn& s) { return s.map != map; });
		return std::span<const MapSpawn>{ first, last };
	}
}

#endif
"""
    return out


if __name__ == '__main__':
    root, enums, output = sys.argv[1], sys.argv[2], sys.argv[3]
    names = {int(n): v for n, v in re.findall(r'\{(\d+), "(\w+)"\}', open(enums).read())}
    rows = []
    for d in sorted(os.listdir(root)):
        if not d.startswith('map') or d == 'mapObjects':
            continue
        files = {f.lower(): os.path.join(root, d, f) for f in os.listdir(os.path.join(root, d))}
        if 'world_colp.nif' not in files:
            continue
        mid = int(d[3:])
        levels = floor_levels(triangles(files['world_colp.nif']))
        spawn_sets = read_spawns(files['world.nif']) if 'world.nif' in files else {}
        spawn_sets = {k: v for k, v in spawn_sets.items() if not k.startswith('PVE')}
        spots, method = [], 'spawns'
        # small or cramped maps: a smaller free disc and closer to the bases before giving up on the spawns
        for check, share, base in ((DISC_CHECK, DISC_SHARE, BASE_DISTANCE), (450.0, 0.6, 1000.0), (300.0, 0.5, 700.0)):
            if spawn_sets and len(spots) < 4:
                spots = pick_from_spawns(levels, spawn_sets, check, share, base)
                method = 'spawns (disc %d, %.0f %%, bases %d)' % (check, share * 100, base)
        if len(spots) < 4:
            spots = pick_lowest(levels)
            method = 'lowest floor (no spawns)' if not spawn_sets else 'lowest floor (spawn floor too small)'
        grounded = {key: [p for p in pts if on_floor(levels, p)] for key, pts in spawn_sets.items()}
        dropped = sum(len(pts) for pts in spawn_sets.values()) - sum(len(pts) for pts in grounded.values())
        if dropped:
            print(mid, names.get(mid, d), 'spawns without floor dropped:', dropped)
        rows.append((mid, names.get(mid, d), spots, grounded))
        print(mid, names.get(mid, d), method, len(spots), [(round(x), round(y), round(z)) for x, y, z in spots])
    license_text = open(output, encoding='utf-8').read().replace('\r\n', '\n').split('#ifndef')[0] if os.path.exists(output) else ''
    open(output, 'w', encoding='utf-8', newline='').write(header(rows, license_text).replace('\n', '\r\n'))
