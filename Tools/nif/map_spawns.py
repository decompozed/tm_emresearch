"""Spawn points of every map, from mapNNN/World.nif: the node "Respawn" (any case) holds one child per game mode (TDM, FFA, SAB,
ZSM, ...), each with spawn nodes directly or grouped by team (REDTEAM / BLUETEAM). World positions = node transforms
composed from the scene root.
Usage: python map_spawns.py <unpacked map.dat dir>   (prints one line per map: map id, mode/team, positions)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import nif  # noqa: E402


def spawns(path):
    blocks, strings = nif.load(path)
    local, children, names = {}, {}, {}
    for i, (typ, data) in enumerate(blocks):
        if typ.endswith('Node'):
            try:
                name, t, rot, s, r = nif.avobject(data)
                local[i] = (t, rot, s)
                names[i] = strings[name] if 0 <= name < len(strings) else ''
                children[i] = nif.node_children(r)
            except Exception:
                pass
    world = {0: local.get(0)}
    parent = {}
    stack = [0]
    while stack:
        n = stack.pop()
        for c in children.get(n, []):
            if c in local and c not in world:
                world[c] = nif.compose(world[n], local[c])
                parent[c] = n
                stack.append(c)
    out = {}
    roots = {node for node, name in names.items() if name.lower() == 'respawn'}
    if not roots:
        # a few maps (RockBand, RockBandW) have the mode groups without a "Respawn" parent: their parent is the root
        roots = {parent[node] for node, name in names.items()
                 if name.upper() in ('TDM', 'FFA', 'SAB', 'CTF', 'ZSM') and node in parent}
    for node, name in names.items():
        if node not in world:
            continue
        if name.upper().startswith('RESPN_'):   # a few maps (Chess): single spawn nodes RESPN_R1 / RESPN_B1
            team = 'REDTEAM' if name.upper().startswith('RESPN_R') else 'BLUETEAM'
            out.setdefault('TDM/' + team, []).append(tuple(round(v) for v in world[node][0]))
            continue
        if node not in roots:
            continue

        def walk(n, path):
            kids = [c for c in children.get(n, []) if c in world]
            if not kids:
                out.setdefault('/'.join(path[:-1]) or path[0], []).append(tuple(round(v) for v in world[n][0]))
                return
            for c in kids:
                walk(c, path + [names.get(c, '?')])
        for mode in children.get(node, []):
            if mode in world:
                walk(mode, [names.get(mode, '?')])
    return out


if __name__ == '__main__':
    root = sys.argv[1]
    for d in sorted(os.listdir(root)):
        if not d.startswith('map') or d == 'mapObjects':
            continue
        path = next((os.path.join(root, d, f) for f in os.listdir(os.path.join(root, d)) if f.lower() == 'world.nif'), None)
        if not path:
            continue
        try:
            result = spawns(path)
        except Exception as e:  # noqa: BLE001
            print(d, 'error', e)
            continue
        print(int(d[3:]), {k: len(v) for k, v in result.items()})
