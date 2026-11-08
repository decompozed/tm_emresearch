"""The official team spawns of a map, as the DLL loads them in Hardpoint / Headquarters / Domination (D87): the mode
group TDM of mapNNN/World.nif "Respawn" when it has both REDTEAM and BLUETEAM children, else SAB (party/scene/
TeamSpawns.cpp asks the same groups in the same order). Only nodes with a walkable floor under them are kept (some
nodes hang over the void: hardpoint_zones.on_floor), and a node of one team closer than APART to a node of the other
team is dropped from both (user 2026-10-03: "make sure opposite teams don't spawn in the same places").
"""
import math

from hardpoint_zones import on_floor

GROUPS = ('TDM', 'SAB')
APART = 500.0  # nodes of the two teams closer than this: neither is used


def team_spawns(groups, levels):
    """(group, red points, blue points) or None (no team groups / a team without a usable node)"""
    group = next((g for g in GROUPS if g + '/REDTEAM' in groups and g + '/BLUETEAM' in groups), None)
    if group is None:
        return None
    red = [p for p in groups[group + '/REDTEAM'] if on_floor(levels, p)]
    blue = [p for p in groups[group + '/BLUETEAM'] if on_floor(levels, p)]

    def clear(points, others):
        return [p for p in points if all(math.hypot(p[0] - q[0], p[1] - q[1]) >= APART for q in others)]

    red, blue = clear(red, blue), clear(blue, red)
    if not red or not blue:
        return None
    return group, red, blue


def spread(points, count):
    """at most `count` of `points`, spread out (farthest point first), nodes one per 100 units"""
    unique = []
    for p in points:
        if all(math.hypot(p[0] - q[0], p[1] - q[1]) >= 100.0 for q in unique):
            unique.append(p)
    if len(unique) <= count:
        return unique
    chosen = [unique[0]]
    while len(chosen) < count:
        chosen.append(max((p for p in unique if p not in chosen),
                          key=lambda p: min(math.hypot(p[0] - q[0], p[1] - q[1]) for q in chosen)))
    return chosen


def centroid(points):
    return (sum(p[0] for p in points) / len(points), sum(p[1] for p in points) / len(points))
