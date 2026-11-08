"""Prop Hunt objects: as many small and medium objects of every map as possible (user 2026-10-01: "the most possible
props for each map, small / medium ones, not big ones"), cut out of the map's own World.nif 1:1 and written as models
into the map's folder of map.dat:

  mapNNN/prop_00.nif .. prop_NN.nif   (at most MAX_PROPS)

How an object is found:
  - the map's meshes are batched by material (one NiMesh holds every coffee cup of the map, another one every cup's
    logo), so every mesh is split into its connected pieces (vertices at the same place joined);
  - a piece touching a clearly bigger piece (ATTACHMENT_SHARE of its size), of any mesh, is part of it: a cup and its
    handle, a radio and its speaker grille; equal neighbours (a row of books) stay apart. One object = one group;
  - only objects INSIDE the playable map: the object stands over the walkable floor of the player collision
    (World_colp.nif) connected to the map's spawn nodes and Hardpoint zone spots (the flood fill of
    hardpoint_zones.py) and not below it (desks and shelves count); decorations outside the walls and over the kill
    floor are left out;
  - the automatic choice: whole objects (compact, not flat or thin; helper meshes skipped; every part textured;
    resting on a surface of the player collision, not hanging) of SMALL[0]..SMALL[1] units, copies of one object
    listed once (same textures, triangle count and surface area, whatever the rotation; with the same textures broken
    pieces go and similar shapes count once: whole_and_distinct); of those the CLOSED ones (user 2026-10-02: "fully closed objects that aren't invisible in any spot":
    solidity(), rays from 16 views around and above the object must hit front faces, open backs / sides / insides
    fail), the most detailed first, up to MAX_PROPS; at least FEWEST_GOOD (10) per map, the most solid others fill up.

Each model is the object 1:1 (nif_extract.py): every part keeps its source mesh's vertex data (positions / normals moved
to the object's origin, the bottom centre; vertex colours and both texture coordinate sets as they are) and its
original properties (texturing with the base texture and the lightmap, material, vertex colour, alpha, stencil,
z-buffer, the ones inherited from its parent nodes included), so it looks as it does in the map. Textures keep the
map's own file names: the model sits in the map's folder and the game finds them there as for World.nif (no texture is
copied). Built on party/LavaWallX.nif (header, root, the node "prop" the meshes hang on).

Choosing by hand: props_preview/mapNNN_candidates_1.png, _2.png... show every candidate of the map (inside the map,
20..800 units, no shape rules), textured as in the game, with its number (green: it passes the rules of the automatic
choice); listing numbers in Tools/nif/prop_choices.json ({"22": [3, 7, 12, 15, 20]}) uses exactly those for that map;
maps not listed keep the automatic choice. Numbers change when the candidate rules change.

Also writes props_preview/mapNNN.png / .txt (what was chosen) and, with --table, Common/include/HaveApi/PropModels.h
(the number of objects of every map, for the server).

Usage: python make_prop_models.py <unpacked map.dat dir> <staging dir> [--table Common/include/HaveApi/PropModels.h] [map ids...]
"""
import math

import numpy as np
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import nif  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402
from hardpoint_zones import CELL, floor_levels, reachable, semantic_streams, triangles  # noqa: E402
from map_spawns import spawns as read_spawns  # noqa: E402
from make_hardpoint_ring import join, split, stream  # noqa: E402
from nif_extract import effective_properties, parse_mesh, texturing_refs, write_object, write_objects  # noqa: E402

SMALL = (25.0, 220.0)    # the automatic choice: small and medium objects (the players are ~200 tall)
MAX_PROPS = 50
WHOLE_SHARE = 0.6        # see whole_and_distinct()
SAME_SHAPE = 0.25
FEWEST_GOOD = 10         # user 2026-10-02: at least 10 different objects per map
BIG = (160.0, 300.0)     # not used by the automatic choice any more (user 2026-10-01: no big props)
FALLBACK_MAX = 400.0     # maps with too few small objects: the next smallest up to this
FEWEST = 5               # objects of a small map ...
SMALL_MAP_CELLS = 1500   # ... (walkable 100 x 100 cells up to this)
BIG_MAP_CELLS = 5000     # 10 objects from this walkable area
ATTACHMENT_SHARE = 0.7   # a piece at most this size of the piece it touches is part of it (a cup's handle)
LOOSE_SIZE = (20.0, 800.0)  # the candidate list (user 2026-10-01: more candidates, fewer filters)
LOOSE_BASE_TOLERANCE = 150.0
BASE_TOLERANCE = 60.0    # the object's lowest point at most this below the walkable floor
MIN_THICKNESS = 15.0     # flat pieces (paper, decals) are no props
MIN_TRIANGLES = 12
MIN_COMPACTNESS = 0.25   # smallest / largest side: thin rods, slabs, doors and panels are pieces of something bigger
REST_BELOW = 25.0        # the object rests on a surface of the player collision: one at most this below its lowest
REST_ABOVE = 30.0        # point or this above it (sunk into the floor a little), under its footprint; hanging lamps,
                         # wall-mounted boxes and objects floating in decorations do not
MAX_OPEN_EDGES = 0.25    # share of edges used by one triangle only: torn-off fragments of a bigger object are open
# the mesh's or its parent node's name containing one of these: helpers, not objects
HELPER_NAMES = ('sound', 'ndlwl', 'decal', 'glow', 'light', 'dof', 'arrow', 'colp', 'colb', 'shadow', 'sky', 'water', 'effect')
OBJECTS = 10
MAX_VERTICES = 65535     # u16 indices
MAX_PARTS = 8            # materials of one object
SEED_WINDOW = (400.0, 80.0)
SPAWN_DROP = 800.0       # spawn nodes higher over the floor drop onto it (SummerChess: ~730 over the table)
# Foosball: the objects of the pitch are the table's figures: base texture + gloss map (gloss.tga), a body (463) bigger
# than FALLBACK_MAX carrying its foot and rod sleeve, blue and white copies of one shape (only the texture coordinates
# differ: every copy is listed)
FIGURE_MAPS = {14}
FIGURE_MAX = 500.0


def avobject_props(data):
    r = nif.R(data)
    r.i32()
    n = r.u32(); r.o += 4 * n
    r.i32(); r.u16(); r.f(13)
    nprops = r.u32()
    return [r.i32() for _ in range(nprops)]


def textures_of(blocks, strings, mesh):
    out = []
    for p in avobject_props(mesh):
        if not (0 <= p < len(blocks) and blocks[p][0] == 'NiTexturingProperty'):
            continue
        d = blocks[p][1]
        for o in range(0, len(d) - 3):
            ref = struct.unpack_from('<i', d, o)[0]
            if 0 <= ref < len(blocks) and blocks[ref][0] == 'NiSourceTexture':
                sd = blocks[ref][1]
                for q in range(0, len(sd) - 3):
                    si = struct.unpack_from('<i', sd, q)[0]
                    if 0 <= si < len(strings) and strings[si].lower().endswith(('.dds', '.tga', '.bmp')):
                        out.append(strings[si])
                        break
    return list(dict.fromkeys(out))


def material_colour(blocks, mesh):
    """Diffuse colour of the mesh's NiMaterialProperty (0..255), or None."""
    for p in avobject_props(mesh):
        if 0 <= p < len(blocks) and blocks[p][0] == 'NiMaterialProperty':
            r = nif.R(blocks[p][1])
            r.i32()
            n = r.u32(); r.o += 4 * n
            r.i32()
            r.f(3)  # ambient
            return tuple(max(0, min(255, int(c * 255))) for c in r.f(3))
    return None


def world_transforms(blocks, strings=None, names=None, parents=None):
    local, children = {}, {}
    for i, (typ, data) in enumerate(blocks):
        if typ.endswith('Node') or typ.startswith('NiMesh') or typ.startswith('NiRoom'):
            try:
                name, t, rot, s, r = nif.avobject(data)
                local[i] = (t, rot, s)
                if names is not None and strings is not None:
                    names[i] = strings[name].lower() if 0 <= name < len(strings) else ''
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
                if parents is not None:
                    parents[c] = n
                if names is not None:
                    names[c] = names.get(n, '') + '/' + names.get(c, '')
                stack.append(c)
    return world


def component_bytes(c):
    """Size of one vertex component: count (bits 16..23) x element size (format, low byte: 0x36 / 0x37 float, 0x15
    u16, 0x3C and the other byte formats 1)."""
    count, fmt = (c >> 16) & 0xFF, c & 0xFF
    return count * (4 if fmt in (0x36, 0x37) else 2 if fmt == 0x15 else 1)


def read_component(blocks, stream_ref, component):
    nverts, comps, raw = nif.datastream(blocks[stream_ref][1])
    if not nverts:
        return []
    stride = len(raw) // nverts
    offset = sum(component_bytes(c) for c in comps[:component])
    width = (comps[component] >> 16) & 0xFF
    return [struct.unpack_from('<%df' % width, raw, k * stride + offset) for k in range(nverts)]


def mesh_geometry(blocks, strings, i, transform):
    data = blocks[i][1]
    pos = semantic_streams(data, blocks, strings, 'POSITION')
    nrm = semantic_streams(data, blocks, strings, 'NORMAL')
    uv = semantic_streams(data, blocks, strings, 'TEXCOORD')
    idx = semantic_streams(data, blocks, strings, 'INDEX')
    col = semantic_streams(data, blocks, strings, 'COLOR')
    if not (pos and nrm and idx):
        return None
    positions = [nif.transform_point(p, *transform) for p in read_component(blocks, *pos[0])]
    t, rot, s = transform
    normals = []
    for n in read_component(blocks, *nrm[0]):
        x, y, z = (rot[0] * n[0] + rot[1] * n[1] + rot[2] * n[2], rot[3] * n[0] + rot[4] * n[1] + rot[5] * n[2],
                   rot[6] * n[0] + rot[7] * n[1] + rot[8] * n[2])
        length = math.sqrt(x * x + y * y + z * z) or 1.0
        normals.append((x / length, y / length, z / length))
    uvs = read_component(blocks, *uv[0]) if uv else [(0.5, 0.5)] * len(positions)
    colours = read_colours(blocks, *col[0]) if col else None
    nidx, _, iraw = nif.datastream(blocks[idx[0][0]][1])
    if not nidx or len(positions) != len(normals) or len(positions) != len(uvs):
        return None
    isize = len(iraw) // nidx
    fmt = '<H' if isize == 2 else '<I'
    ids = [struct.unpack_from(fmt, iraw, k * isize)[0] for k in range(nidx)]
    tris = [(ids[k], ids[k + 1], ids[k + 2]) for k in range(0, len(ids) - 2, 3)
            if max(ids[k], ids[k + 1], ids[k + 2]) < len(positions)]
    return positions, normals, uvs, tris, colours


def read_colours(blocks, stream_ref, component):
    nverts, comps, raw = nif.datastream(blocks[stream_ref][1])
    if not nverts or (comps[component] & 0xFF) in (0x36, 0x37):
        return None
    stride = len(raw) // nverts
    offset = sum(component_bytes(c) for c in comps[:component])
    return [tuple(raw[k * stride + offset:k * stride + offset + 3]) for k in range(nverts)]


def pieces(positions, tris):
    """Connected pieces (vertices at the same place are joined: the seams of a texture)."""
    parent = list(range(len(positions)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    at = {}
    for v, p in enumerate(positions):
        key = (round(p[0], 1), round(p[1], 1), round(p[2], 1))
        if key in at:
            union(v, at[key])
        else:
            at[key] = v
    for a, b, c in tris:
        union(a, b)
        union(b, c)
    groups = {}
    for t in tris:
        groups.setdefault(find(t[0]), []).append(t)
    return list(groups.values())


def group_parts(parts, gap=2.0, structure=None):
    """Indices of parts forming one object: a part touching a clearly bigger part (of any mesh) belongs to it."""
    structure = FALLBACK_MAX if structure is None else structure
    boxes = [([p['lo'][a] - gap for a in range(3)], [p['hi'][a] + gap for a in range(3)]) for p in parts]
    parent = list(range(len(parts)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    order = sorted(range(len(parts)), key=lambda i: boxes[i][0][0])
    for k, i in enumerate(order):  # sweep along x
        for j in order[k + 1:]:
            if boxes[j][0][0] > boxes[i][1][0]:
                break
            if all(boxes[i][0][a] <= boxes[j][1][a] and boxes[j][0][a] <= boxes[i][1][a] for a in (1, 2)):
                # an attachment is clearly smaller than its object; equal neighbours (a row of books) stay apart
                si = max(boxes[i][1][a] - boxes[i][0][a] for a in range(3))
                sj = max(boxes[j][1][a] - boxes[j][0][a] for a in range(3))
                # structures (desks, walls, floors: bigger than any object) absorb nothing
                if min(si, sj) <= ATTACHMENT_SHARE * max(si, sj) and max(si, sj) <= structure:
                    parent[find(i)] = find(j)
    groups = {}
    for i in range(len(parts)):
        groups.setdefault(find(i), []).append(i)
    return list(groups.values())


def surface_area(parts):
    """Total triangle area: with the textures and the triangle count, the same object whatever its rotation."""
    total = 0.0
    for part in parts:
        pos = part['positions']
        for a, b, c in part['tris']:
            u = [pos[b][k] - pos[a][k] for k in range(3)]
            v = [pos[c][k] - pos[a][k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            total += math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) / 2
    return total


def open_edge_share(parts):
    """Edges (world positions, seams joined) used by one triangle only, over all edges of the object's parts."""
    uses = {}
    for part in parts:
        positions = part['positions']

        def key(v):
            p = positions[v]
            return (round(p[0], 1), round(p[1], 1), round(p[2], 1))

        for a, b, c in part['tris']:
            for e in ((a, b), (b, c), (c, a)):
                k = tuple(sorted((key(e[0]), key(e[1]))))
                uses[k] = uses.get(k, 0) + 1
    return sum(1 for n in uses.values() if n == 1) / max(1, len(uses))


VIEW_DIRECTIONS = 16      # closedness test: views around the object (8 level, 8 from 35 degrees above)
VIEW_GRID = 18            # rays per side of each view
MIN_SOLID = 0.98          # share of the rays hitting the object whose first hit is a front face (or a double-sided
                          # part): below it the object shows holes / its inside from some side (open backs and sides)


def double_sided(part):
    """The part's NiStencilProperty draws both faces (draw mode 3)."""
    blocks, strings, mesh_index, world, props = part['source']
    for p in props:
        if blocks[p][0] == 'NiStencilProperty':
            data = blocks[p][1]
            n = struct.unpack_from('<I', data, 4)[0]
            flags = struct.unpack_from('<H', data, 12 + 4 * n)[0]
            if (flags >> 10) & 3 == 3:
                return True
    return False


def solidity(parts, lo, hi):
    """Share of the rays that hit the object from VIEW_DIRECTIONS views (level and from above: nobody sees a prop from
    below) whose first hit is a front face: 1.0 for a closed object, less where it is open (the hole shows the inside's
    back faces, which the game does not draw)."""
    tri_a, tri_b, tri_c, normals, both = [], [], [], [], []
    for part in parts:
        pos, nrm, ds = part['positions'], part['normals'], double_sided(part)
        for a, b, c in part['tris']:
            tri_a.append(pos[a]); tri_b.append(pos[b]); tri_c.append(pos[c])
            normals.append([(nrm[a][k] + nrm[b][k] + nrm[c][k]) for k in range(3)])
            both.append(ds)
    if not tri_a:
        return 0.0
    A, B, C = np.array(tri_a), np.array(tri_b), np.array(tri_c)
    N, both = np.array(normals), np.array(both)
    E1, E2 = B - A, C - A
    centre = (np.array(lo) + np.array(hi)) / 2
    radius = np.linalg.norm(np.array(hi) - np.array(lo)) / 2 + 1.0
    hits = solid = 0
    for k in range(VIEW_DIRECTIONS):
        yaw = 2 * math.pi * (k % 8) / 8 + (math.pi / 8 if k >= 8 else 0)
        pitch = 0.0 if k < 8 else math.radians(35)
        d = np.array([-math.cos(pitch) * math.cos(yaw), -math.cos(pitch) * math.sin(yaw), -math.sin(pitch)])
        u = np.cross(d, [0, 0, 1.0]) if abs(d[2]) < 0.99 else np.array([1.0, 0, 0])
        u /= np.linalg.norm(u)
        v = np.cross(u, d)
        steps = (np.arange(VIEW_GRID) + 0.5) / VIEW_GRID * 2 - 1
        gu, gv = np.meshgrid(steps * radius, steps * radius)
        origins = centre - d * radius * 2 + gu.reshape(-1, 1) * u + gv.reshape(-1, 1) * v  # rays x 3
        # Moeller-Trumbore, rays x triangles
        p = np.cross(d, E2)                       # T x 3
        det = np.einsum('ij,ij->i', E1, p)        # T
        ok = np.abs(det) > 1e-9
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        s_ = origins[:, None, :] - A[None, :, :]  # R x T x 3
        bu = np.einsum('rtk,tk->rt', s_, p) * inv
        q = np.cross(s_, E1[None, :, :])
        bv = np.einsum('rtk,k->rt', q, d) * inv
        t = np.einsum('rtk,tk->rt', q, E2) * inv
        valid = ok & (bu >= 0) & (bv >= 0) & (bu + bv <= 1) & (t > 0)
        t = np.where(valid, t, np.inf)
        first = np.argmin(t, axis=1)
        hit = np.isfinite(t[np.arange(len(origins)), first])
        front = (N[first] @ d < 0) | both[first]
        hits += int(hit.sum())
        solid += int((hit & front).sum())
    return solid / hits if hits else 0.0


ZONES_HEADER = os.path.join(os.path.dirname(__file__), '..', '..', 'Common', 'include', 'HaveApi', 'HardpointZones.h')


def zone_spots(map_id):
    """The map's Hardpoint zone spots (HardpointZones.h, hardpoint_zones.py; tested as playable in game)."""
    if not os.path.exists(ZONES_HEADER):
        return []
    import re
    for line in open(ZONES_HEADER, encoding='utf-8'):
        m = re.match(r'\s*\{ (\d+), \d+, \{ \{(.*)\} \} \},', line)
        if m and int(m.group(1)) == map_id:
            return [tuple(float(v.rstrip('f')) for v in spot.split(','))
                    for spot in re.findall(r'\{ ([-\d.f]+, [-\d.f]+, [-\d.f]+) \}', line)]
    return []


def playable_floor(files, map_id=0):
    """(cells: {cell: [reachable floor heights]}, walkable cell count) from World_colp.nif: the floor connected to the
    map's spawn nodes and to its Hardpoint zone spots (maps without spawn nodes have zone spots too)."""
    if 'world_colp.nif' not in files:
        return None, 0
    levels = floor_levels(triangles(files['world_colp.nif']))
    seeds = list(zone_spots(map_id))
    for pts in read_spawns(files['world.nif']).values():
        seeds += pts  # reachable() only starts from a floor near the seed (nodes over the void start nothing)
    floors = {}
    for cell, z in reachable(levels, seeds + dropped(levels, seeds)):
        floors.setdefault(cell, []).append(z)
    return floors, len(floors)


def dropped(levels, seeds):
    out = []
    for x, y, z in seeds:
        cell = (int(math.floor(x / CELL)), int(math.floor(y / CELL)))
        near = [lz for dx in (-1, 0, 1) for dy in (-1, 0, 1) for lz in levels.get((cell[0] + dx, cell[1] + dy), [])]
        if any(z - SEED_WINDOW[0] <= lz <= z + SEED_WINDOW[1] for lz in near):
            continue
        below = [lz for lz in near if z - SPAWN_DROP <= lz < z - SEED_WINDOW[0]]
        if below:
            out.append((x, y, max(below)))
    return out


_LEVELS = {}


def collision_levels(files):
    """cell -> every upward surface height of the player collision (floors, desk tops, shelves), cached per map."""
    path = files.get('world_colp.nif')
    if path not in _LEVELS:
        _LEVELS[path] = floor_levels(triangles(path)) if path else None
    return _LEVELS[path]


def resting(levels, lo, hi):
    """The object stands on a surface (see REST_BELOW / REST_ABOVE) under the middle of its footprint."""
    if levels is None:
        return True
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    for gx in range(int(math.floor(min(cx, lo[0] + 10) / CELL)), int(math.floor(max(cx, hi[0] - 10) / CELL)) + 1):
        for gy in range(int(math.floor(min(cy, lo[1] + 10) / CELL)), int(math.floor(max(cy, hi[1] - 10) / CELL)) + 1):
            if any(lo[2] - REST_BELOW <= z <= lo[2] + REST_ABOVE for z in levels.get((gx, gy), [])):
                return True
    return False


def inside(floors, lo, hi, tolerance=None):
    """The object stands over the walkable area (a reachable floor under or next to its footprint) and not below its
    lowest floor there: objects on desks and shelves count, decorations outside the walls and the kill floor do not."""
    if floors is None:
        return True
    for gx in range(int(math.floor(lo[0] / CELL)) - 1, int(math.floor(hi[0] / CELL)) + 2):
        for gy in range(int(math.floor(lo[1] / CELL)) - 1, int(math.floor(hi[1] / CELL)) + 2):
            heights = floors.get((gx, gy))
            if heights and lo[2] >= min(heights) - (BASE_TOLERANCE if tolerance is None else tolerance):
                return True
    return False


def object_count(walkable_cells):
    if walkable_cells <= SMALL_MAP_CELLS:
        return FEWEST
    if walkable_cells >= BIG_MAP_CELLS:
        return OBJECTS
    share = (walkable_cells - SMALL_MAP_CELLS) / (BIG_MAP_CELLS - SMALL_MAP_CELLS)
    return FEWEST + round(share * (OBJECTS - FEWEST))


def candidates(map_dir):
    """(objects inside the playable map, number of objects for the map). An object: dict(parts, lo, hi, size)."""
    files = {f.lower(): os.path.join(map_dir, f) for f in os.listdir(map_dir)}
    if 'world.nif' not in files:
        return [], 0
    map_id = int(os.path.basename(os.path.normpath(map_dir))[3:])
    floors, walkable = playable_floor(files, map_id)
    levels = collision_levels(files)
    blocks, strings = nif.load(files['world.nif'])
    paths, parents = {}, {}
    world = world_transforms(blocks, strings, paths, parents)
    meshes = [i for i, (t, _) in enumerate(blocks) if t == 'NiMesh' and i in world
              and not any(h in part for part in paths.get(i, '').split('/')[-2:] for h in HELPER_NAMES)]
    texture_lists = {i: [t for t in textures_of(blocks, strings, blocks[i][1]) if not t.lower().startswith('lightmap')
                         and not (map_id in FIGURE_MAPS and t.lower() == 'gloss.tga')]
                     for i in meshes}
    # a texture listed by most meshes is not the object's own (a scene-wide map)
    counts = {}
    for texs in texture_lists.values():
        for t in set(texs):
            counts[t] = counts.get(t, 0) + 1
    parts = []
    for i in meshes:
        texs = [t for t in texture_lists[i] if counts[t] <= len(meshes) // 2] or texture_lists[i]
        if len(texs) > 1 or (texs and texs[0].lower() not in files):
            continue
        try:
            parse_mesh(blocks[i][1])
            texturing = [p for p in effective_properties(blocks, i, parents) if blocks[p][0] == 'NiTexturingProperty']
            if any(texturing_refs(blocks[p][1]) is None for p in texturing):
                continue
            geometry = mesh_geometry(blocks, strings, i, world[i])
        except (struct.error, IndexError, ValueError, ZeroDivisionError):
            geometry = None  # a stream layout this reader does not know: not a prop
        if geometry is None:
            continue
        positions, normals, uvs, tris, colours = geometry
        texture = texs[0] if texs else None
        base_colour = None if texture else material_colour(blocks, blocks[i][1])
        for piece in pieces(positions, tris):
            used = sorted({v for t in piece for v in t})
            if len(used) > MAX_VERTICES:
                continue
            colour = base_colour
            if texture is None and colours:
                colour = tuple(sum(colours[v][a] for v in used) // len(used) for a in range(3))
            if texture is None and colour is None:
                continue
            parts.append(dict(texture=texture, colour=colour, texture_file=files.get((texture or '').lower()),
                              source=(blocks, strings, i, world[i], effective_properties(blocks, i, parents)),
                              lo=[min(positions[v][a] for v in used) for a in range(3)],
                              hi=[max(positions[v][a] for v in used) for a in range(3)],
                              tris=piece, used=used, positions=positions, normals=normals, uvs=uvs))
    out = []
    groups = group_parts(parts, structure=FIGURE_MAX if map_id in FIGURE_MAPS else None)
    for members in ([parts[k] for k in group] for group in groups):
        if len(members) > MAX_PARTS or sum(len(m['tris']) for m in members) < 4:
            continue
        lo = [min(m['lo'][a] for m in members) for a in range(3)]
        hi = [max(m['hi'][a] for m in members) for a in range(3)]
        size = [hi[a] - lo[a] for a in range(3)]
        # the candidate list: inside the map (looser height tolerance), a broad size range, no shape rules
        if not (LOOSE_SIZE[0] <= max(size) <= LOOSE_SIZE[1]) or not inside(floors, lo, hi, LOOSE_BASE_TOLERANCE):
            continue
        # the automatic choice: the strict rules (whole, compact, closed objects)
        strict = (sum(len(m['tris']) for m in members) >= MIN_TRIANGLES and max(size) <= FALLBACK_MAX and
                  min(size) / max(size) >= MIN_COMPACTNESS and open_edge_share(members) <= MAX_OPEN_EDGES and
                  inside(floors, lo, hi) and resting(levels, lo, hi) and
                  all(m['texture'] and m['texture_file'] for m in members))
        fill = (resting(levels, lo, hi) and all(m['texture'] and m['texture_file'] for m in members) and
                sum(len(m['tris']) for m in members) >= MIN_TRIANGLES and inside(floors, lo, hi))
        out.append(dict(parts=members, lo=lo, hi=hi, size=size, tris=[t for m in members for t in m['tris']], strict=strict,
                        fill=fill,
                        key=tuple(sorted(str(m['texture'] or m['colour']) for m in members))))
    # a stable order: the candidate numbers of the sheets and of prop_choices.json do not change between runs
    out.sort(key=lambda c: (round(c['lo'][0]), round(c['lo'][1]), round(c['lo'][2])))
    # copies of one object (every key of a keyboard, every cup of the room) are listed once
    unique, seen = [], set()
    for c in out:
        signature = (c['key'], len(c['tris']), round(math.log(surface_area(c['parts']) + 1) / math.log(1.1)))
        if signature not in seen:
            seen.add(signature)
            unique.append(c)
    if map_id not in FIGURE_MAPS:
        out = unique
    return out, object_count(walkable)


def whole_and_distinct(found):
    """Objects with the same textures: the broken pieces go (fewer than WHOLE_SHARE of the triangles of the most
    detailed one: a chair's back, half a table) and of similar shapes (sorted sides within SAME_SHAPE) one stays."""
    by_key = {}
    for c in found:
        by_key.setdefault(c['key'], []).append(c)
    out = []
    for group in by_key.values():
        most = max(len(c['tris']) for c in group)
        kept = []
        for c in sorted(group, key=lambda c: -len(c['tris'])):
            if len(c['tris']) < WHOLE_SHARE * most:
                continue
            sides = sorted(c['size'])
            if any(all(abs(a - b) <= SAME_SHAPE * max(a, b) for a, b in zip(sides, sorted(k['size']))) for k in kept):
                continue
            kept.append(c)
        out += kept
    return out


def choose(found, count=MAX_PROPS):
    """The best whole objects of SMALL[0]..SMALL[1] units, up to `count`: closed from every side seen in the game
    (solidity >= MIN_SOLID) and passing the strict rules, the most detailed (triangles) and compact first. Maps with
    fewer than FEWEST_GOOD of them are filled up with the other objects of that size that rest on a surface and are
    textured (the shape rules aside), the most solid first."""
    def sized(c):
        return SMALL[0] <= max(c['size']) <= SMALL[1] and min(c['size']) >= MIN_THICKNESS

    def solid(c):
        if 'solid' not in c:
            c['solid'] = solidity(c['parts'], c['lo'], c['hi'])
        return c['solid']

    found = whole_and_distinct([c for c in found if sized(c) and (c['strict'] or c.get('fill'))])
    pool = [c for c in found if c['strict']]
    good = [c for c in pool if solid(c) >= MIN_SOLID]
    chosen = sorted(good, key=lambda c: (-min(len(c['tris']), 400), -min(c['size']) / max(c['size'])))[:count]
    if len(chosen) < FEWEST_GOOD:
        rest = [c for c in found if c not in chosen and sized(c) and (c['strict'] or c.get('fill'))]
        rest.sort(key=lambda c: (-round(solid(c), 2), -min(len(c['tris']), 400)))
        chosen += rest[:FEWEST_GOOD - len(chosen)]
    return sorted(chosen, key=lambda c: max(c['size']))

def model_parts(obj):
    lo, hi = obj['lo'], obj['hi']
    origin = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2])
    parts = []
    for part in obj['parts']:
        blocks, strings, mesh_index, world, props = part['source']
        parts.append((blocks, strings, mesh_index, world, props, part['used'], part['tris']))
    return parts, origin


def write_model(template, obj, out_path):
    """The object 1:1 (nif_extract.write_object): one mesh per part with its source mesh's data and properties."""
    lo, hi = obj['lo'], obj['hi']
    origin = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2])
    parts = []
    for part in obj['parts']:
        blocks, strings, mesh_index, world, props = part['source']
        parts.append((blocks, strings, mesh_index, world, props, part['used'], part['tris']))
    write_object(template, parts, origin, out_path)

_TEXTURES = {}


def texture_image(part):
    """The part's texture (RGB, cached), or None for a coloured part / an unreadable texture."""
    path = part['texture_file']
    if not path:
        return None
    if path not in _TEXTURES:
        try:
            _TEXTURES[path] = Image.open(path).convert('RGB')
        except Exception:
            _TEXTURES[path] = None
    return _TEXTURES[path]


def triangle_colour(part, tri, image):
    """The texture's colour at the triangle's centre (its texture coordinates, wrapped), else the part's colour."""
    if image is None:
        return tuple(part['colour']) if part['colour'] else (180, 180, 180)
    u = sum(part['uvs'][v][0] for v in tri) / 3 % 1.0
    v = sum(part['uvs'][v][1] for v in tri) / 3 % 1.0
    return image.getpixel((min(image.width - 1, int(u * image.width)), min(image.height - 1, int(v * image.height))))


def render(obj, size=160):
    """Preview of an object: its triangles seen from the front-left, flat shaded, each part coloured by its texture's
    mean (or its colour), with its size in units."""
    img = Image.new('RGB', (size, size), (40, 44, 52))
    d = ImageDraw.Draw(img)
    lo, hi = obj['lo'], obj['hi']
    c = [(lo[a] + hi[a]) / 2 for a in range(3)]
    extent = max(hi[a] - lo[a] for a in range(3)) or 1.0
    ca, sa = math.cos(math.radians(35)), math.sin(math.radians(35))

    def project(p):
        x, y, z = p[0] - c[0], p[1] - c[1], p[2] - c[2]
        u = x * ca - y * sa
        depth = x * sa + y * ca
        v = z * 0.9 + depth * 0.3
        return (size / 2 + u / extent * size * 0.7, size / 2 - v / extent * size * 0.7, depth)

    tris = []
    for part in obj['parts']:
        image = texture_image(part)
        for t in part['tris']:
            mean = triangle_colour(part, t, image)
            a, b, cc = (part['positions'][v] for v in t)
            nx = (b[1] - a[1]) * (cc[2] - a[2]) - (b[2] - a[2]) * (cc[1] - a[1])
            ny = (b[2] - a[2]) * (cc[0] - a[0]) - (b[0] - a[0]) * (cc[2] - a[2])
            nz = (b[0] - a[0]) * (cc[1] - a[1]) - (b[1] - a[1]) * (cc[0] - a[0])
            length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
            light = 0.45 + 0.55 * abs(0.3 * nx - 0.4 * ny + 0.85 * nz) / length
            pts = [project(p) for p in (a, b, cc)]
            tris.append((sum(p[2] for p in pts) / 3, [(p[0], p[1]) for p in pts], tuple(int(m * light) for m in mean)))
    for _, pts, colour in sorted(tris, key=lambda t: -t[0]):
        d.polygon(pts, fill=colour)
    d.text((4, size - 14), '%d x %d x %d' % tuple(round(hi[a] - lo[a]) for a in range(3)), fill=(230, 230, 230))
    return img


def label(obj):
    """The object's material names, shortened (e.g. coffee + logo)."""
    names = [os.path.splitext(str(m['texture'] or 'colour'))[0] for m in obj['parts']]
    return ' + '.join(dict.fromkeys(names))[:26]


def relaxed_candidates(map_dir):
    """Maps with fewer than FEWEST objects: shape and size rules relaxed (never the inside rule)."""
    global FALLBACK_MAX, MIN_COMPACTNESS, MAX_OPEN_EDGES
    kept = FALLBACK_MAX, MIN_COMPACTNESS, MAX_OPEN_EDGES
    FALLBACK_MAX, MIN_COMPACTNESS, MAX_OPEN_EDGES = 600.0, 0.1, 0.4
    try:
        return candidates(map_dir)
    finally:
        FALLBACK_MAX, MIN_COMPACTNESS, MAX_OPEN_EDGES = kept


def candidate_sheets(found, title, path_base, columns=6, rows=5, size=180):
    """Every candidate of a map with its number (for prop_choices.json), textured, 30 per sheet:
    mapNNN_candidates_1.png, _2.png, ... A green number: passes the strict rules (automatic choice)."""
    per_sheet = columns * rows
    for page in range(max(1, (len(found) + per_sheet - 1) // per_sheet)):
        items = found[page * per_sheet:(page + 1) * per_sheet]
        sheet = Image.new('RGB', (columns * size, rows * (size + 14) + 20), (20, 20, 24))
        d = ImageDraw.Draw(sheet)
        d.text((4, rows * (size + 14) + 4), '%s (sheet %d)' % (title, page + 1), fill=(240, 240, 240))
        for i, obj in enumerate(items):
            k = page * per_sheet + i
            x, y = (i % columns) * size, (i // columns) * (size + 14)
            sheet.paste(render(obj, size), (x, y))
            d.text((x + 4, y + 2), '#%d' % k, fill=(120, 230, 120) if obj['strict'] else (255, 220, 80))
            d.text((x + 4, y + size), label(obj), fill=(200, 200, 210))
        sheet.save('%s_%d.png' % (path_base, page + 1))


def load_choices():
    """Tools/nif/prop_choices.json: {"map id": [candidate numbers]} chosen by hand (the last one = the bigger one)."""
    path = os.path.join(os.path.dirname(__file__), 'prop_choices.json')
    if not os.path.exists(path):
        return {}
    import json
    return {int(k): v for k, v in json.load(open(path, encoding='utf-8')).items() if not k.startswith('_')}


def main(root, staging, only):
    template = os.path.join(root, 'party', 'LavaWallX.nif')
    preview = os.path.join(staging, 'props_preview')
    os.makedirs(preview, exist_ok=True)
    rows = []
    choices = load_choices()
    for d in sorted(os.listdir(root)):
        if not d.startswith('map') or d == 'mapObjects':
            continue
        mid = int(d[3:])
        if only and mid not in only:
            continue
        found, _ = candidates(os.path.join(root, d))
        count = MAX_PROPS
        candidate_sheets(found, '%s: %d candidates, prop_choices.json picks them by number; green = automatic rules' % (
                         d, len(found)), os.path.join(preview, d + '_candidates'))
        if mid in choices:
            chosen = [found[k] for k in choices[mid] if 0 <= k < len(found)]
        else:
            chosen = choose(found, count)
        out_dir = os.path.join(staging, 'maps', d)
        os.makedirs(out_dir, exist_ok=True)
        lines = []
        sheet = Image.new('RGB', (160 * 5, 160 * 2 + 20), (20, 20, 24))
        ImageDraw.Draw(sheet).text((4, 160 * 2 + 4), '%s: %d objects, the last one bigger' % (d, count), fill=(240, 240, 240))
        for k, piece in enumerate(chosen):
            sheet.paste(render(piece), ((k % 5) * 160, (k // 5) * 160))
            write_model(template, piece, os.path.join(out_dir, 'prop_%02d.nif' % k))
            lines.append('prop_%02d %s size %s triangles %d' % (k, ' + '.join(piece['key']), [round(s) for s in piece['size']],
                                                              len(piece['tris'])))
        if chosen:  # every object of the map in one file: the DLL reads it once (prop_all.nif, nodes prop_00..)
            write_objects(template, [model_parts(piece) for piece in chosen], os.path.join(out_dir, 'prop_all.nif'))
        open(os.path.join(preview, d + '.txt'), 'w').write('\n'.join(lines) + '\n')
        sheet.save(os.path.join(preview, d + '.png'))
        rows.append((mid, [c['size'][2] for c in chosen]))
        print(mid, len(chosen), [(len(c['parts']), round(max(c['size']))) for c in chosen])
    return rows


def write_table(rows, output):
    """Common/include/HaveApi/PropModels.h: number of prop models of every map (the server picks among them)."""
    license_text = open(output, encoding='utf-8').read().split('#ifndef')[0] if os.path.exists(output) else ''
    out = license_text + """#ifndef COMMON_HAVE_API_PROP_MODELS_H
#define COMMON_HAVE_API_PROP_MODELS_H

#include <algorithm>
#include <array>
#include <cstdint>

// Prop Hunt objects per map (mapNNN/prop_00.. in map.dat), generated by Tools/nif/make_prop_models.py. Do not edit.
namespace Common::HaveApi
{
	struct MapPropModels
	{
		std::uint32_t map;
		std::int32_t count;
	};

	inline constexpr auto propModels = std::to_array<MapPropModels>({
"""
    for mid, sizes in sorted(rows):
        out += '\t\t{ %d, %d },\n' % (mid, len(sizes))
    out += """	});

	// 0 for a map without objects
	[[nodiscard]] constexpr std::int32_t propModelCount(std::uint32_t map)
	{
		const auto found = std::ranges::find(propModels, map, &MapPropModels::map);
		return found != propModels.end() ? found->count : 0;
	}
}

#endif
"""
    open(output, 'w', encoding='utf-8', newline='').write(out.replace('\n', '\r\n'))


if __name__ == '__main__':
    # make_prop_models.py <unpacked map.dat> <staging> [--table <PropModels.h>] [map ids...]
    args = sys.argv[3:]
    table = None
    if args[:1] == ['--table']:
        table, args = args[1], args[2:]
    result = main(sys.argv[1], sys.argv[2], [int(a) for a in args])
    if table:
        write_table(result, table)
