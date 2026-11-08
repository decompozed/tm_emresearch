"""Domination zones per map -> Common/include/HaveApi/DominationZones.h (party game 10, D85 / D87).

The teams respawn at their official bases (team_spawns.py: TDM, else SAB, REDTEAM / BLUETEAM; user 2026-10-03), so the
zones are laid out along the line from the red base to the blue one (centroids of the team spawns):
  - A on the red side, B on the blue side, C in the middle (user 2026-10-03: the zones were "all on one side of the
    map" on Academy, Battle Mine, House Top, Magic Paper Land (L), Neighborhood, The Studio, Toy Garden: there the Bomb
    Battle sites / battery bases both lie on one half);
  - A / B: the map's Bomb Battle sites (bombinfo.cdb: bi_id, map, ?, A x / y / z, B x / y / z, ranges) or Capture the
    Battery bases (kitdropinfo.cdb ki_mod 3, ki_type 1, team 1 / 2, ki_x / y / z floats) when they lie one on each
    side (SITE_SIDE along the base line, as far from their base as each other within SITE_BALANCE); else reachable
    flat floor near AB_POSITION and 1 - AB_POSITION of the line, as near the line as possible, the two as symmetric as
    possible;
  - C: reachable flat floor as far from A as from B and from both bases (BALANCE), near the middle;
  - every zone KEEP_OUT from every team spawn (no capturing by respawning);
  - floor: the levels of mapNNN/World_colp.nif with HEADROOM free above them (hardpoint_zones.floor_levels counts the
    underside of a floor slab as a level too: a zone there was hidden under the floor, user 2026-10-03: zone C "works
    but cannot be seen" on Bitmap, Beach, Football, Rumpus Room (2), Toy Garden 2).
Maps the user left out (EXCLUDED_MAPS) and maps without team spawns or zones are left out. Units: world units (the
positions of order 281); z = the floor height of the zone.

Usage: python domination_zones.py <unpacked map.dat dir> <cgd ENG dir (bombinfo.cdb, kitdropinfo.cdb)> <RoomEnums.h>
       <output .h> [<DLL header: the map ids, for the room's map lists (party/state/DominationMaps.h)>]
"""
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from hardpoint_zones import CELL, STEP, floor_levels, reachable, triangles  # noqa: E402
from map_spawns import spawns as read_spawns  # noqa: E402
from party_spawns import SPAWNS  # noqa: E402
from team_spawns import centroid, spread, team_spawns  # noqa: E402

HALF = 500.0           # half the side of a zone square (server DominationMode::zoneHalfSize)
KEEP_OUT = 1200.0      # every zone centre at least this far from every team spawn
C_KEEP_OUT = 800.0     # C's last resort
MIN_SPACING = 1500.0   # C at least this far from A and B
MIN_BASE_DISTANCE = 4000.0  # bases closer than this: no room for three zones
FLAT_SHARE = 0.7       # this share of the square's cells must be floor within STEP of the zone level
EXCLUDED = re.compile(r'^PVE|GHOST|DEV_|INVIS')  # spawn groups that are not players' spawns
# user 2026-10-03: Chess, RockBand, RockBandS, TheAftermath; then Invasion (Easy / Hard), Junk Yard, Town Square
EXCLUDED_MAPS = {12, 28, 29, 38, 19, 20, 21, 40}

# Per-map corrections after the in-game test (user 2026-10-03: "change ONLY these maps, the others are tested and
# working"). The general layout runs first; then, per map:
#   'zones': the zones as given (Cargo / Cargo (O): the layout of the first version, the battery bases and the radar,
#            "much better");
#   'ab': 'battery' = A / B at the Capture the Battery bases ("put A and B at their bases": House Top, Magic Paper
#         Land (L), Wild West, Championship Castle ("Castle Siege": all three in the middle, touching));
#         ('base', t) = A / B at position t / 1 - t of the base line, on the bases' own level ("A and B not reachable,
#         all too near the middle": Craftwork; "A and B nearer their bases": City Hall Station);
#   'c': 'radar' = C at the radar's place (kitdropinfo item 4026000, item2_radar) on the floor under it ("C cannot be
#        seen" / "C is in a wall, put it higher, at the radar": Castle, Hobby Shop, Toy Fleet, Toy Garden, Wild West);
#        'open' = C on floor with nothing above it (Toy Garden 2: C was under the terrain, no radar on the map).
# The zones not named keep the general layout.
RADAR_ITEM = 4026000
OVERRIDES = {
    9: {'zones': [(-11.7, -4193.0, 1782.5), (-30.1, 4158.0, 1806.3), (-50.0, -50.0, 704.2)]},
    49: {'zones': [(-11.7, -4193.0, 1782.5), (-30.1, 4158.0, 1806.3), (-50.0, -50.0, 704.2)]},
    10: {'c': 'radar'},
    11: {'ab': 'battery'},
    13: {'ab': ('base', 0.15)},
    17: {'c': 'radar'},
    18: {'ab': 'battery'},
    22: {'ab': 'battery'},
    23: {'ab': 'battery'},
    41: {'c': 'radar'},
    42: {'c': 'radar'},
    43: {'c': 'open'},
    46: {'ab': 'battery', 'c': 'radar'},
    57: {'ab': ('base', 0.12)},
}
BASE_LEVEL = 200.0     # ('base', t): the zone's floor within this of the base spawns' floor
BASE_KEEP_OUT = 500.0  # ('base', t): from every team spawn
OPEN_ABOVE = 1500.0    # 'open': no other level within this above the zone
BALANCE = 0.15         # C: distances to A / B (and to the two bases) differ by at most this share
HEADROOM = 150.0       # a level with another one less than this above is not a floor to stand on
SITE_SIDE = (0.08, 0.42)    # A's position along the red -> blue line (B: 1 - this)
SITE_BALANCE = 0.25         # A / B: their distances to their own base differ by at most this share
SITE_SYMMETRY = 0.12        # A / B: their positions along the line mirror each other within this
AB_POSITION = 0.27          # A's target along the line when the sites do not fit (B: 1 - this)
AB_RANGE = (0.12, 0.40)


def standable(levels):
    out = {}
    for cell, zs in levels.items():
        kept = [z for i, z in enumerate(zs) if i == len(zs) - 1 or zs[i + 1] - z >= HEADROOM]
        if kept:
            out[cell] = kept
    return out


def bomb_sites(path):
    d = open(path, 'rb').read()
    columns = struct.unpack_from('<I', d, 0)[0]
    body = d[4 + 34 * columns:]
    rows = {}
    for i in range(len(body) // (4 * columns)):
        r = struct.unpack_from('<%di' % columns, body, 4 * columns * i)
        rows[r[1]] = ((float(r[3]), float(r[4]), float(r[5])), (float(r[6]), float(r[7]), float(r[8])))
    return rows


def radar_sites(path):
    """map -> the radar's place (kitdropinfo item RADAR_ITEM)"""
    d = open(path, 'rb').read()
    columns = struct.unpack_from('<I', d, 0)[0]
    body = d[4 + 34 * columns:]
    radars = {}
    for i in range(len(body) // (4 * columns)):
        _, _, _, item, mid = struct.unpack_from('<5i', body, 4 * columns * i)
        if item == RADAR_ITEM:
            radars[mid] = struct.unpack_from('<3f', body, 4 * columns * i + 20)
    return radars


def on_level(levels, p):
    """p on the floor under it (the level nearest below p's height, within 300; else p's own height)"""
    zs = [z for z in levels.get((int(math.floor(p[0] / CELL)), int(math.floor(p[1] / CELL))), []) if z <= p[2] + 50]
    return (p[0], p[1], max(zs) if zs and p[2] - max(zs) <= 300 else p[2])


def near_bases(levels, floor, line, red, blue, t):
    """A / B at t / 1 - t of the line, on the level of their base's spawns, the two as symmetric as possible"""
    spawns = red + blue
    best = None
    sides = []
    for target, base in ((t, red), (1 - t, blue)):
        base_z = sum(p[2] for p in base) / len(base) - 100  # the nodes are ~100 above their floor
        pool = []
        for (cell, z) in floor:
            p = ((cell[0] + 0.5) * CELL, (cell[1] + 0.5) * CELL, z)
            if abs(z - base_z) <= BASE_LEVEL and clear_of(p, spawns, BASE_KEEP_OUT):
                pool.append((abs(line.t(p) - target) * line.length + 0.5 * abs(line.side(p)), p))
        pool.sort()
        sides.append([p for _, p in pool[:600] if flat_share(levels, p[0], p[1], p[2]) >= 0.6][:40])
    for a in sides[0]:
        for b in sides[1]:
            cost = (abs(line.t(a) - (1 - line.t(b))) * line.length
                    + min(abs(line.side(a) + line.side(b)), abs(line.side(a) - line.side(b)))
                    + 0.25 * (abs(line.side(a)) + abs(line.side(b))))
            if best is None or cost < best[0]:
                best = (cost, a, b)
    return [best[1], best[2]] if best else None


def open_c(levels, floor, a, b, red_base, blue_base, spawns):
    """pick_c on floor with nothing above it within OPEN_ABOVE"""
    open_floor = set()
    for (cell, z) in floor:
        above = [level for level in levels.get(cell, []) if z < level <= z + OPEN_ABOVE]
        if not above:
            open_floor.add((cell, z))
    return pick_c(levels, open_floor, a, b, red_base, blue_base, spawns)


def battery_sites(path):
    """map -> (team 1 base, team 2 base) of Capture the Battery (kitdropinfo.cdb: ki_mod, ki_type, ki_team, ki_item_id,
    ki_map_id, ki_x, ki_y, ki_z (floats), ki_prob, ...)"""
    d = open(path, 'rb').read()
    columns = struct.unpack_from('<I', d, 0)[0]
    body = d[4 + 34 * columns:]
    bases = {}
    for i in range(len(body) // (4 * columns)):
        mode, kind, team, _, mid = struct.unpack_from('<5i', body, 4 * columns * i)
        x, y, z = struct.unpack_from('<3f', body, 4 * columns * i + 20)
        if mode == 3 and kind == 1 and team in (1, 2):
            bases.setdefault(mid, {})[team] = (x, y, z)
    return {mid: (t[1], t[2]) for mid, t in bases.items() if 1 in t and 2 in t}


def level_near(levels, x, y, z):
    """the floor level of the cell under (x, y) nearest to z (None: no floor)"""
    zs = levels.get((int(math.floor(x / CELL)), int(math.floor(y / CELL))), [])
    return min(zs, key=lambda level: abs(level - z)) if zs else None


def flat_share(levels, x, y, z):
    cells = 0
    flat = 0
    steps = int(HALF // CELL)
    for i in range(-steps, steps + 1):
        for j in range(-steps, steps + 1):
            cells += 1
            level = level_near(levels, x + i * CELL, y + j * CELL, z)
            if level is not None and abs(level - z) <= STEP:
                flat += 1
    return flat / cells


def dist(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


class Line:
    """the red base -> blue base line: position t (0 red, 1 blue) and signed side distance"""

    def __init__(self, red, blue):
        self.r = red
        self.length = dist(red, blue)
        self.u = ((blue[0] - red[0]) / self.length, (blue[1] - red[1]) / self.length)

    def t(self, p):
        return ((p[0] - self.r[0]) * self.u[0] + (p[1] - self.r[1]) * self.u[1]) / self.length

    def side(self, p):
        return (p[0] - self.r[0]) * -self.u[1] + (p[1] - self.r[1]) * self.u[0]


def clear_of(p, spawns, keep_out=KEEP_OUT):
    return all(dist(p, s) >= keep_out for s in spawns)


def flat_candidates(levels, floor, line, t_range, target, spawns, limit=40):
    """flat floor in t_range, KEEP_OUT from the spawns, nearest to (target, on the line) first"""
    pool = []
    for (cell, z) in floor:
        p = ((cell[0] + 0.5) * CELL, (cell[1] + 0.5) * CELL, z)
        t = line.t(p)
        if t_range[0] <= t <= t_range[1] and clear_of(p, spawns):
            pool.append((abs(t - target) * line.length + 0.5 * abs(line.side(p)), p))
    pool.sort()
    for flat_limit in (FLAT_SHARE, 0.6, 0.5):
        found = [p for _, p in pool[:600] if flat_share(levels, p[0], p[1], p[2]) >= flat_limit][:limit]
        if found:
            return found
    return []


def pick_ab(levels, floor, line, spawns, red_base, blue_base):
    """A (red side) and B (blue side) as symmetric as possible"""
    a_list = flat_candidates(levels, floor, line, AB_RANGE, AB_POSITION, spawns)
    b_list = flat_candidates(levels, floor, line, (1 - AB_RANGE[1], 1 - AB_RANGE[0]), 1 - AB_POSITION, spawns)
    best = None
    for a in a_list:
        for b in b_list:
            asymmetry = (abs(line.t(a) - (1 - line.t(b))) * line.length
                         + min(abs(line.side(a) + line.side(b)), abs(line.side(a) - line.side(b)))
                         + abs(dist(a, red_base) - dist(b, blue_base)))
            cost = asymmetry + 0.25 * (abs(line.side(a)) + abs(line.side(b)))
            if best is None or cost < best[0]:
                best = (cost, a, b)
    return (best[1], best[2]) if best else None


def sites_fit(a, b, line, spawns, red_base, blue_base):
    if not (SITE_SIDE[0] <= line.t(a) <= SITE_SIDE[1] and 1 - SITE_SIDE[1] <= line.t(b) <= 1 - SITE_SIDE[0]):
        return False
    if abs(line.t(a) - (1 - line.t(b))) > SITE_SYMMETRY:
        return False
    da, db = dist(a, red_base), dist(b, blue_base)
    return abs(da - db) / max(da, db) <= SITE_BALANCE and clear_of(a, spawns) and clear_of(b, spawns)


def pick_c(levels, floor, a, b, red_base, blue_base, spawns):
    """C: as far from A as from B and from both team bases (BALANCE), the flattest such floor, near the middle.
    Fairness first: the flatness required goes down (FLAT_SHARE .. 0.5) before the balance does (BALANCE .. 0.25);
    maps whose spawns cover the middle (Cargo): C nearer to them (C_KEEP_OUT) before giving up."""
    middle = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    for keep_out in (KEEP_OUT, C_KEEP_OUT):
        candidates = []
        for (cell, z) in floor:
            p = ((cell[0] + 0.5) * CELL, (cell[1] + 0.5) * CELL, z)
            da, db = dist(p, a), dist(p, b)
            if da < MIN_SPACING or db < MIN_SPACING or not clear_of(p, spawns, keep_out):
                continue
            ba, bb = dist(p, red_base), dist(p, blue_base)
            balance = max(abs(da - db) / max(da, db), abs(ba - bb) / max(ba, bb))
            candidates.append((balance, dist(p, middle), p))
        for balance_limit in (BALANCE, 0.2, 0.25):
            for flat_limit in (FLAT_SHARE, 0.6, 0.5):
                fitting = sorted((d, p) for balance, d, p in candidates if balance <= balance_limit)
                for _, p in fitting:
                    if flat_share(levels, p[0], p[1], p[2]) >= flat_limit:
                        return p
    return None


def header(rows, license_text):
    out = license_text + """#ifndef COMMON_HAVE_API_DOMINATION_ZONES_H
#define COMMON_HAVE_API_DOMINATION_ZONES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include "HaveApi/HardpointZones.h"

// Domination (party game 10, D85 / D87) per map, generated by Tools/nif/domination_zones.py: A on the red base's side,
// B on the blue base's side (the Bomb Battle sites / Capture the Battery bases when they lie one on each side, else
// flat reachable floor), C = flat reachable floor in the middle, as far from A as from B. The teams respawn at their
// official bases (PartySpawns.h). Units: world units (the positions of order 281); z = the floor height of the zone.
namespace Common::HaveApi
{
	struct DominationMap
	{
		std::uint16_t map;
		std::array<ZoneSpot, 3> zones; // A (red side), B (blue side), C
	};

	inline constexpr auto dominationMaps = std::to_array<DominationMap>({
"""
    for mid, name, zones in rows:
        out += '\t\t{ %d, { { %s } } }, // %s\n' % (mid, ', '.join('{ %.1ff, %.1ff, %.1ff }' % z for z in zones), name)
    out += """	});

	[[nodiscard]] inline const DominationMap* findDominationMap(std::uint32_t map)
	{
		const auto found = std::ranges::find(dominationMaps, map, &DominationMap::map);
		return found != dominationMaps.end() ? &*found : nullptr;
	}
}

#endif
"""
    return out


def map_zones(folder, sites, override=None, battery=None, radar=None):
    files = {f.lower(): os.path.join(folder, f) for f in os.listdir(folder)}
    if 'world.nif' not in files or 'world_colp.nif' not in files:
        return None, 'no World.nif / World_colp.nif'
    raw = floor_levels(triangles(files['world_colp.nif']))
    groups = read_spawns(files['world.nif'])
    teams = team_spawns(groups, raw)
    if teams is None:
        return None, 'no team spawns'
    _, red, blue = teams
    red, blue = spread(red, SPAWNS), spread(blue, SPAWNS)  # the points the server sends (PartySpawns.h)
    red_base, blue_base = centroid(red), centroid(blue)
    if dist(red_base, blue_base) < MIN_BASE_DISTANCE:
        return None, 'bases %d apart' % dist(red_base, blue_base)
    levels = standable(raw)
    every = [p for k, pts in groups.items() if not EXCLUDED.search(k) for p in pts]
    floor = reachable(levels, every)
    spawns = red + blue
    line = Line(red_base, blue_base)
    source = None
    if sites:
        a, b = sites[1]
        if line.t(a) > line.t(b):
            a, b = b, a
        zones = []
        for site in (a, b):
            level = level_near(levels, site[0], site[1], site[2])
            zones.append((site[0], site[1], level if level is not None and abs(level - site[2]) <= 300 else site[2]))
        if sites_fit(zones[0], zones[1], line, spawns, red_base, blue_base):
            source = sites[0]
    if source is None:
        ab = pick_ab(levels, floor, line, spawns, red_base, blue_base)
        if ab is None:
            return None, 'no floor for A / B'
        zones = list(ab)
        source = 'floor'
    c = pick_c(levels, floor, zones[0], zones[1], red_base, blue_base, spawns)
    if c is None:
        return None, 'no floor for C'
    zones.append(c)
    if override:
        if 'zones' in override:
            zones = list(override['zones'])
            source = 'user'
        if override.get('ab') == 'battery':
            ends = sorted(battery, key=line.t)  # A: the one on the red side
            zones[0], zones[1] = on_level(levels, ends[0]), on_level(levels, ends[1])
            source = 'battery bases (user)'
        elif isinstance(override.get('ab'), tuple):
            ab = near_bases(levels, floor, line, red, blue, override['ab'][1])
            if ab is None:
                return None, 'no floor for A / B near the bases'
            zones[0], zones[1] = ab
            source = 'near the bases (user)'
        if override.get('c') == 'radar':
            zones[2] = on_level(levels, radar)
        elif override.get('c') == 'open':
            c = open_c(levels, floor, zones[0], zones[1], red_base, blue_base, spawns)
            if c is None:
                return None, 'no open floor for C'
            zones[2] = c
    report = '%s A t %.2f B t %.2f C t %.2f | A-red %d B-blue %d | flat %s' % (
        source, line.t(zones[0]), line.t(zones[1]), line.t(zones[2]), dist(zones[0], red_base), dist(zones[1], blue_base),
        [round(flat_share(levels, z[0], z[1], z[2]), 2) for z in zones])
    return (source, zones), report


if __name__ == '__main__':
    root, cgd, enums, output = sys.argv[1:5]
    names = {int(n): v for n, v in re.findall(r'\{(\d+), "(\w+)"\}', open(enums).read())}
    bombs = bomb_sites(os.path.join(cgd, 'bombinfo.cdb'))
    batteries = battery_sites(os.path.join(cgd, 'kitdropinfo.cdb'))
    radars = radar_sites(os.path.join(cgd, 'kitdropinfo.cdb'))
    rows = []
    for d in sorted(os.listdir(root)):
        if not re.fullmatch(r'map\d{3}', d):
            continue
        mid = int(d[3:])
        name = names.get(mid, d)
        if mid in EXCLUDED_MAPS:
            print(mid, name, 'LEFT OUT: excluded by the user')
            continue
        sites = ('bomb sites', bombs[mid]) if mid in bombs else ('battery bases', batteries[mid]) if mid in batteries else None
        result, report = map_zones(os.path.join(root, d), sites, OVERRIDES.get(mid), batteries.get(mid), radars.get(mid))
        if result is None:
            print(mid, name, 'LEFT OUT:', report)
            continue
        rows.append((mid, name + ' (' + result[0] + ')', result[1]))
        print(mid, name, report)
    license_text = open(output, encoding='utf-8').read().replace('\r\n', '\n').split('#ifndef')[0] if os.path.exists(output) else ''
    open(output, 'w', encoding='utf-8', newline='').write(header(rows, license_text).replace('\n', '\r\n'))
    if len(sys.argv) > 5:
        ids = sorted(mid for mid, _, _ in rows)
        dll = ('#pragma once\n'
               "// The maps with Domination zones (party game 10): the room's map lists show only these while Domination is the\n"
               "// room's party game. Generated by Tools/nif/domination_zones.py (server repository) with\n"
               '// Common/include/HaveApi/DominationZones.h: run it again when the maps change.\n'
               '#include <algorithm>\n#include <array>\n\nnamespace party {\n\n'
               'inline constexpr std::array<int, %d> dominationMapIds = {%s};\n\n'
               '[[nodiscard]] inline bool isDominationMap(int mapId)\n{\n'
               '    return std::ranges::find(dominationMapIds, mapId) != dominationMapIds.end();\n}\n\n'
               '} // namespace party\n') % (len(ids), ', '.join(str(i) for i in ids))
        open(sys.argv[5], 'w', encoding='utf-8', newline='').write(dll)
    print('maps', len(rows))
