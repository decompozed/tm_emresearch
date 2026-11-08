"""Corridors between the maps of a merged folder: openings cut into render meshes and PhysX meshes, and a closed
corridor (floor, ramps, walls, ceiling, end caps) added as render meshes cloned from existing materials and as PhysX
triangle meshes cooked with the game's PhysXCooking.dll.

Corridor spec (layout "corridors"), lengths in world units:
  axis       "x" or "y": direction of travel; "across": [b0, b1] inner side walls
  profile    [[a, floor z, ceiling z], ...] piecewise linear along the axis, constant beyond the ends
  render / colp / colb   surfaces of each kind: "floor": [a0, a1], "walls": [[a0, a1], [a0, a1]] (side b0, side b1),
             "ceiling": [a0, a1], "ceiling_end": [[a, b], ...] (replaces the a1 edge of the ceiling, at the end height),
             "margin" (collision walls extend below the floor and above the ceiling), "shell" (render: outer faces)
  caps       [{"at": a, "facing": 1 | -1, "top": z, "hole": [[b, z], ...] (from the floor up and back), "colb": bool}]
  rooms      [{"name": "XX_NDLRM", "box": [[x, y, z], [x, y, z]], "effects_from": room}] rooms covering the corridor
  room_walls [{"room", "normal": [x, y, z], "world": c}] moves a wall of an existing room (n.p >= c inside)
  portals    [{"between": [room, room], "polygon": [[x, y, z], ...]}] a portal each way
  walk       {"from": a, "to": a, "lanes": [offsets from the middle], "inside": [a0, a1]} (verify_corridor.py)
  materials  [{"until": a | null, "room": room | absent, "outside_rooms": bool, "wall" / "floor" / "ceiling":
             {"texture", "near": [x, y, z]}}]; with "room" the meshes go under a new LMG_ node, fixture of that room, with
             "outside_rooms" under the map's LMG_ node outside the rooms; cap: wall material
  cuts       [{"file", "mode": "remove" (whole triangles inside the box) | "clip" (parts inside the box; render and PhysX),
               "box": [[x, y, z], [x, y, z]], "vertical": bool, "map": "mNNN", "names": [...], "expect": n}]
  covers     [{"at": a, "side": 0 | 1, "length": along, "depth": across, "boxes": [height, ...], "inset": d}] stacked
             boxes against side wall b0 (0) or b1 (1), the lowest sunk below the floor, each next one inset by d on its
             open sides; render meshes with the layout's "cover_material" ({"texture", "near", "faces": {"top" | "side" |
             "end": [u0, v0, u1, v1]}}: properties of that mesh, one atlas cell per face) lit like the corridor wall at
             "at", closed boxes in the corridor's colp and colb meshes"""
import copy
import math
import struct

import geomutil as gu
import meshclip
import nifedit as ne
import physx_cook as pc
from nifio import Block, Nif

VERTICAL = 0.3
COLP_NAME = 'coc_sand'  # surface class of the floor (0x7AB720 classifies colp meshes by name)
COLB_NAME = 'cob_sand'  # bullet collision class (0x7AD780, compared without case)
COVER_SINK = 20.0


def fmt_size(c):
    return ((c >> 16) & 0xFF) * ((c >> 8) & 0xFF)


def streams(nif, mesh):
    out = {}
    for ds in mesh.data['Datastreams']:
        s = ds['Stream']
        fmts = nif.blocks[s].data['Component Formats']
        stride = sum(fmt_size(c) for c in fmts)
        off = 0
        for sem, f in zip(ds['Component Semantics'], fmts):
            out['%s%d' % (nif.string(sem['Name']), sem['Index'])] = (s, off, stride, f)
            off += fmt_size(f)
    return out


def read_component(nif, mesh, key):
    s, off, stride, f = streams(nif, mesh)[key]
    raw = nif.blocks[s].data['Data']
    n = len(raw) // stride
    k = (f >> 16) & 0xFF
    if (f >> 8) & 0xFF == 1:
        return [tuple(raw[j * stride + off:j * stride + off + k]) for j in range(n)]
    return [struct.unpack_from('<%df' % k, raw, j * stride + off) for j in range(n)]


def read_indices(nif, mesh):
    s, _, _, f = streams(nif, mesh)['INDEX0']
    raw = nif.blocks[s].data['Data']
    code = 'H' if fmt_size(f) == 2 else 'I'
    return list(struct.unpack('<%d%s' % (len(raw) // fmt_size(f), code), raw)), s, code


def intern(nif, s):
    if s in nif.strings:
        return nif.strings.index(s)
    nif.strings.append(s)
    return len(nif.strings) - 1


def add_block(nif, type_name, data):
    nif.blocks.append(Block(type_name, data))
    if type_name not in nif.type_order:
        nif.type_order.append(type_name)
    return len(nif.blocks) - 1


def under(nif, i, top_name, par):
    while i is not None:
        if nif.name(nif.blocks[i]) == top_name:
            return True
        i = par.get(i)
    return False


def render_triangles(nif, i, par):
    b = nif.blocks[i]
    w = ne.world(nif, i, par)
    pos = read_component(nif, b, 'POSITION0')
    idx, _, _ = read_indices(nif, b)
    return w, [ne.apply(w, p) for p in pos], idx


def clip_render(nif, cut, log):
    par = ne.parents(nif)
    box = (tuple(cut['box'][0]), tuple(cut['box'][1]))
    total = 0.0
    detail = []
    for i in meshclip.render_meshes(nif, None, par):
        if cut.get('map') and not under(nif, i, cut['map'], par):
            continue
        if 'INDEX0' not in streams(nif, nif.blocks[i]):
            continue
        a = meshclip.clip_mesh(nif, i, box, 'outside', par, cut.get('vertical', False))
        if a > 0:
            total += a
            detail.append('%s(%.0f)' % (nif.name(nif.blocks[i]), a))
    log('  %s %s: area %.0f clipped %s' % (cut['file'], cut.get('label', ''), total, ' '.join(detail)))
    assert total > 0, 'cut %s clipped nothing' % cut.get('label')
    return total


def cut_render(nif, cut, log):
    if cut.get('mode') == 'clip':
        return clip_render(nif, cut, log)
    par = ne.parents(nif)
    box = (tuple(cut['box'][0]), tuple(cut['box'][1]))
    removed = 0
    detail = []
    for i, b in enumerate(nif.blocks):
        if b.base != 'NiMesh' or b.data['Primitive Type'] != 0 or 'INDEX0' not in streams(nif, b):
            continue
        if cut.get('map') and not under(nif, i, cut['map'], par):
            continue
        _, V, idx = render_triangles(nif, i, par)
        keep, gone = [], 0
        for j in range(0, len(idx) - 2, 3):
            t = (V[idx[j]], V[idx[j + 1]], V[idx[j + 2]])
            hit = all(gu.inside_box(p, box) for p in t) and (not cut.get('vertical') or abs(gu.normal(t)[2]) < VERTICAL)
            if hit:
                gone += 1
            else:
                keep += idx[j:j + 3]
        if not gone:
            continue
        _, s, code = read_indices(nif, b)
        sd = nif.blocks[s].data
        assert sd['Num Regions'] == 1, 'index stream with several regions'
        sd['Data'] = struct.pack('<%d%s' % (len(keep), code), *keep)
        sd['Num Bytes'] = len(sd['Data'])
        sd['Regions'] = [{'Start Index': 0, 'Num Indices': len(keep)}]
        removed += gone
        detail.append('%s(%d)' % (nif.name(b), gone))
    log('  %s %s: %d triangles removed %s' % (cut['file'], cut.get('label', ''), removed, ' '.join(detail)))
    if 'expect' in cut:
        assert removed == cut['expect'], 'cut %s removed %d triangles, expected %d' % (cut.get('label'), removed, cut['expect'])
    return removed


def pose_apply(p, v):
    x, y, z = v
    return (p['m11'] * x + p['m21'] * y + p['m31'] * z + p['m14'],
            p['m12'] * x + p['m22'] * y + p['m32'] * z + p['m24'],
            p['m13'] * x + p['m23'] * y + p['m33'] * z + p['m34'])


def pose_inverse_apply(p, v):
    x, y, z = v[0] - p['m14'], v[1] - p['m24'], v[2] - p['m34']
    return (p['m11'] * x + p['m12'] * y + p['m13'] * z,
            p['m21'] * x + p['m22'] * y + p['m23'] * z,
            p['m31'] * x + p['m32'] * y + p['m33'] * z)


def physx_actors(nif):
    out = []
    for i, b in enumerate(nif.blocks):
        if b.base != 'NiPhysXActorDesc':
            continue
        shape = nif.blocks[b.data['Shape Descriptions'][0]]
        md = shape.data['Mesh']
        out.append((i, nif.string(nif.blocks[md].data['Mesh Name']), b.data['Poses'][0], md))
    return out


def actor_world_triangles(nif, md, pose):
    m = pc.parse(nif.blocks[md].data['Mesh Data']['Data'])
    V = [pose_apply(pose, v) for v in m['verts']]
    return [(V[a], V[b], V[c]) for a, b, c in pc.source_order(m)]


def set_mesh(nif, md, blob):
    nif.blocks[md].data['Mesh Data'] = {'Data Size': len(blob), 'Data': blob}


def cook_world(tris, pose, dll):
    verts, index, out = [], {}, []
    for t in tris:
        tri = []
        for p in t:
            q = tuple(round(c, 4) for c in pose_inverse_apply(pose, p))
            if q not in index:
                index[q] = len(verts)
                verts.append(q)
            tri.append(index[q])
        if len(set(tri)) == 3:
            out.append(tuple(tri))
    return pc.cook(verts, out, dll)


def cut_physx(nif, cut, dll, log):
    box = (tuple(cut['box'][0]), tuple(cut['box'][1]))
    changed = []
    for i, name, pose, md in physx_actors(nif):
        if cut.get('names') and name not in cut['names']:
            continue
        tris = actor_world_triangles(nif, md, pose)
        new, hits = [], 0
        for t in tris:
            vertical = abs(gu.normal(t)[2]) < VERTICAL
            if (cut.get('vertical') and not vertical) or not gu.overlaps_box(t, box):
                new.append(t)
                continue
            if cut['mode'] == 'remove':
                if all(gu.inside_box(p, box) for p in t):
                    hits += 1
                else:
                    new.append(t)
            else:
                rest, inside = gu.subtract_box(t, box)
                if inside <= 0.01:
                    new.append(t)
                    continue
                hits += 1
                new += rest
        if not hits:
            continue
        set_mesh(nif, md, cook_world(new, pose, dll))
        changed.append('%s#%d(%d -> %d tris, %d cut)' % (name, i, len(tris), len(new), hits))
    log('  %s %s: %s' % (cut['file'], cut.get('label', ''), ', '.join(changed) or 'nothing cut'))
    assert changed, 'cut %s touched no PhysX mesh' % cut.get('label')
    if 'expect' in cut:
        got = sum(int(c.split(', ')[-1].split()[0]) for c in changed)
        assert got == cut['expect'], 'cut %s cut %d triangles, expected %d' % (cut.get('label'), got, cut['expect'])
    return changed


def add_physx_actor(nif, name, tris, dll, template):
    prop_desc = next(i for i, b in enumerate(nif.blocks) if b.base == 'NiPhysXPropDesc')
    t_actor = nif.blocks[template]
    t_shape = nif.blocks[t_actor.data['Shape Descriptions'][0]]
    t_mesh = nif.blocks[t_shape.data['Mesh']]
    identity = {'m11': 1.0, 'm21': 0.0, 'm31': 0.0, 'm12': 0.0, 'm22': 1.0, 'm32': 0.0, 'm13': 0.0, 'm23': 0.0,
                'm33': 1.0, 'm14': 0.0, 'm24': 0.0, 'm34': 0.0}
    sname = intern(nif, name)
    mdata = copy.deepcopy(t_mesh.data)
    mdata['Mesh Name'] = sname
    m = add_block(nif, t_mesh.type, mdata)
    set_mesh(nif, m, cook_world(tris, identity, dll))
    sdata = copy.deepcopy(t_shape.data)
    sdata['Shape Name'] = sname
    sdata['Mesh'] = m
    s = add_block(nif, t_shape.type, sdata)
    adata = copy.deepcopy(t_actor.data)
    adata['Actor Name'] = sname
    adata['Poses'] = [dict(identity)]
    adata['Num Poses'] = 1
    adata['Shape Descriptions'] = [s]
    adata['Num Shape Descs'] = 1
    a = add_block(nif, t_actor.type, adata)
    actors = nif.blocks[prop_desc].data['Actors'] + [a]
    ne.set_list(nif.blocks[prop_desc], 'Actors', 'Num Actors', actors)
    return a


class Frame:
    def __init__(self, c):
        self.axis = c['axis']
        self.b0, self.b1 = c['across']
        self.profile = sorted(c['profile'])

    def world(self, a, b, z):
        return (a, b, z) if self.axis == 'x' else (b, a, z)

    def at(self, a):
        p = self.profile
        if a <= p[0][0]:
            return p[0][1], p[0][2]
        for (a0, f0, c0), (a1, f1, c1) in zip(p, p[1:]):
            if a0 <= a <= a1:
                f = (a - a0) / (a1 - a0) if a1 > a0 else 0.0
                return f0 + f * (f1 - f0), c0 + f * (c1 - c0)
        return p[-1][1], p[-1][2]

    def breaks(self, a0, a1, extra=()):
        pts = {a0, a1} | {p[0] for p in self.profile if a0 < p[0] < a1} | {x for x in extra if a0 < x < a1}
        return sorted(pts)

    def along_dir(self):
        return (1.0, 0.0, 0.0) if self.axis == 'x' else (0.0, 1.0, 0.0)

    def across_dir(self):
        return (0.0, 1.0, 0.0) if self.axis == 'x' else (1.0, 0.0, 0.0)


def oriented(t, want):
    return t if gu.dot(gu.normal(t), want) >= 0 else (t[0], t[2], t[1])


def quad(p00, p10, p11, p01, want):
    return [oriented((p00, p10, p11), want), oriented((p00, p11, p01), want)]


def scale(v, s):
    return (v[0] * s, v[1] * s, v[2] * s)


def wall_surface(fr, b, a0, a1, want, lo=0.0, hi=0.0, splits=()):
    out = []
    xs = fr.breaks(a0, a1, splits)
    for u, v in zip(xs, xs[1:]):
        fu, cu = fr.at(u)
        fv, cv = fr.at(v)
        out.append((0.5 * (u + v), quad(fr.world(u, b, fu - lo), fr.world(v, b, fv - lo), fr.world(v, b, cv + hi),
                                         fr.world(u, b, cu + hi), want)))
    return out


def flat_surface(fr, a0, a1, b0, b1, which, offset, want, splits=(), end=None):
    out = []
    xs = fr.breaks(a0, a1, splits)
    for u, v in zip(xs, xs[1:]):
        zu, zv = fr.at(u)[which] + offset, fr.at(v)[which] + offset
        out.append((0.5 * (u + v), quad(fr.world(u, b0, zu), fr.world(v, b0, zv), fr.world(v, b1, zv), fr.world(u, b1, zu), want)))
    if end:
        z = fr.at(a1)[which] + offset
        assert all(abs(fr.at(a)[which] + offset - z) < 1e-6 for a, _ in end), 'ceiling end not at constant height'
        poly = gu.ccw([(a1, b0)] + [tuple(p) for p in end] + [(a1, b1)])
        pts = [fr.world(a, b, z) for a, b in poly]
        tris = [oriented((pts[i], pts[j], pts[k]), want) for i, j, k in gu.triangulate(poly)]
        out.append((a1 + 1.0, tris))
    return out


def cap_surface(fr, cap):
    a = cap['at']
    f, _ = fr.at(a)
    hole = [tuple(p) for p in cap['hole']]
    poly = gu.ccw([(fr.b0, f)] + hole + [(fr.b1, f), (fr.b1, cap['top']), (fr.b0, cap['top'])])
    want = scale(fr.along_dir(), cap['facing'])
    pts = [fr.world(a, b, z) for b, z in poly]
    return [(a, [oriented((pts[i], pts[j], pts[k]), want) for i, j, k in gu.triangulate(poly)])]


def surfaces(c, kind):
    fr = Frame(c)
    spec = c[kind]
    up, down = (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)
    acr = fr.across_dir()
    splits = [m['until'] for m in c.get('materials', []) if m.get('until') is not None]
    margin = spec.get('margin', 0.0)
    out = {'floor': [], 'wall': [], 'ceiling': [], 'cap': []}
    if 'floor' in spec:
        a0, a1 = spec['floor']
        out['floor'] += flat_surface(fr, a0, a1, fr.b0, fr.b1, 0, 0.0, up, splits)
    if 'walls' in spec:
        (s0, s1), (n0, n1) = spec['walls']
        out['wall'] += wall_surface(fr, fr.b0, s0, s1, acr, margin, margin, splits)
        out['wall'] += wall_surface(fr, fr.b1, n0, n1, scale(acr, -1), margin, margin, splits)
    if 'ceiling' in spec:
        a0, a1 = spec['ceiling']
        out['ceiling'] += flat_surface(fr, a0, a1, fr.b0, fr.b1, 1, 0.0, down, splits, spec.get('ceiling_end'))
    shell = spec.get('shell')
    if shell:
        a0, a1 = shell['range']
        th = shell['thickness']
        out['wall'] += wall_surface(fr, fr.b0 - th, a0, a1, scale(acr, -1), th, th, splits)
        out['wall'] += wall_surface(fr, fr.b1 + th, a0, a1, acr, th, th, splits)
        out['floor'] += flat_surface(fr, a0, a1, fr.b0 - th, fr.b1 + th, 0, -th, down, splits)
        out['ceiling'] += flat_surface(fr, a0, a1, fr.b0 - th, fr.b1 + th, 1, th, up, splits)
    for cap in c.get('caps', []):
        if kind == 'render' or cap.get(kind):
            out['cap'] += cap_surface(fr, cap)
    return out


def cover_boxes(c, cover):
    fr = Frame(c)
    a0, a1 = cover['at'] - 0.5 * cover['length'], cover['at'] + 0.5 * cover['length']
    floors = [fr.at(a)[0] for a in fr.breaks(a0, a1)]
    z, top = min(floors) - COVER_SINK, max(floors)
    depth, inset = cover['depth'], cover.get('inset', 0.0)
    out = []
    for k, h in enumerate(cover['boxes']):
        if k:
            a0, a1, depth, z = a0 + inset, a1 - inset, depth - inset, top
        top += h
        b0, b1 = (fr.b0, fr.b0 + depth) if cover['side'] == 0 else (fr.b1 - depth, fr.b1)
        p, q = fr.world(a0, b0, z), fr.world(a1, b1, top)
        out.append((tuple(map(min, p, q)), tuple(map(max, p, q))))
    return out


def box_face(box, k, s):
    lo, hi = box
    u, v = [j for j in range(3) if j != k]
    corners = []
    for cu, cv in ((0, 0), (1, 0), (1, 1), (0, 1)):
        p = [0.0, 0.0, 0.0]
        p[k] = hi[k] if s > 0 else lo[k]
        p[u] = hi[u] if cu else lo[u]
        p[v] = hi[v] if cv else lo[v]
        corners.append(tuple(p))
    return quad(*corners, tuple(float(s) if j == k else 0.0 for j in range(3)))


def face_uv(box, k, s, p, cell):
    lo, hi = box
    if k == 2:
        u = (p[0] - lo[0]) / (hi[0] - lo[0])
        v = (p[1] - lo[1]) / (hi[1] - lo[1])
    else:
        r = 1 - k
        right = gu.cross((0.0, 0.0, 1.0), tuple(float(s) if j == k else 0.0 for j in range(3)))[r]
        u = (p[r] - lo[r]) / (hi[r] - lo[r])
        u = u if right > 0 else 1.0 - u
        v = (hi[2] - p[2]) / (hi[2] - lo[2])
    return cell[0] + u * (cell[2] - cell[0]), cell[1] + v * (cell[3] - cell[1])


def cover_render(c, cover, cells):
    along = 0 if c['axis'] == 'x' else 1
    across = 1 - along
    wall = -1 if cover['side'] == 0 else 1
    faces = ((2, 1, 'top'), (across, -wall, 'side'), (along, -1, 'end'), (along, 1, 'end'))
    tris, uvs = [], []
    for box in cover_boxes(c, cover):
        for k, s, kind in faces:
            for t in box_face(box, k, s):
                tris.append(t)
                uvs.append([face_uv(box, k, s, p, cells[kind]) for p in t])
    return tris, uvs


def cover_collision(c):
    across = 1 if c['axis'] == 'x' else 0
    out = []
    for cover in c.get('covers', []):
        wall = (across, -1 if cover['side'] == 0 else 1)
        for box in cover_boxes(c, cover):
            out += [t for k in range(3) for s in (-1, 1) if (k, s) != wall for t in box_face(box, k, s)]
    return out


class Material:

    def __init__(self, nif, spec, par):
        best = None
        near = tuple(spec['near'])
        for i, b in enumerate(nif.blocks):
            if b.base != 'NiMesh' or texture_of(nif, b) != spec['texture'].lower():
                continue
            if 'INDEX0' not in streams(nif, b) or 'TEXCOORD1' not in streams(nif, b):
                continue
            _, V, idx = render_triangles(nif, i, par)
            for j in range(0, len(idx) - 2, 3):
                t = (V[idx[j]], V[idx[j + 1]], V[idx[j + 2]])
                c = tuple(sum(p[k] for p in t) / 3 for k in range(3))
                d = sum((c[k] - near[k]) ** 2 for k in range(3))
                if gu.area(t) > 1.0 and (best is None or d < best[0]):
                    best = (d, i, idx[j:j + 3], t)
        assert best, 'no mesh with texture %s' % spec['texture']
        _, self.mesh, tri, t = best
        b = nif.blocks[self.mesh]
        uv0 = read_component(nif, b, 'TEXCOORD0')
        uv1 = read_component(nif, b, 'TEXCOORD1')
        col = read_component(nif, b, 'COLOR0')
        self.uv1 = tuple(sum(uv1[k][a] for k in tri) / 3 for a in range(2))
        self.color = bytes(int(round(sum(col[k][a] for k in tri) / 3)) for a in range(4))
        self.place(nif, par[self.mesh], par)
        self.lmg = next(nif.name(nif.blocks[j]) for j in ancestors(self.mesh, par) if (nif.name(nif.blocks[j]) or '').startswith('LMG_'))
        n = gu.normal(t)
        e1, e2 = self._basis(n)
        s = [(gu.dot(p, e1), gu.dot(p, e2)) for p in t]
        uv = [uv0[k] for k in tri]
        ds1, dt1 = s[1][0] - s[0][0], s[1][1] - s[0][1]
        ds2, dt2 = s[2][0] - s[0][0], s[2][1] - s[0][1]
        det = ds1 * dt2 - ds2 * dt1
        self.grad = []
        for a in range(2):
            du1, du2 = uv[1][a] - uv[0][a], uv[2][a] - uv[0][a]
            self.grad.append(((du1 * dt2 - du2 * dt1) / det, (ds1 * du2 - ds2 * du1) / det))
        self.origin = uv[0]
        self.s0 = s[0]

    def place(self, nif, parent, par):
        self.parent = parent
        self.parent_xf = ne.world(nif, parent, par)
        self.inv = ne.inverse(self.parent_xf)

    @staticmethod
    def _basis(n):
        if abs(n[2]) >= VERTICAL:
            return (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)
        h = gu.unit(gu.cross(n, (0.0, 0.0, 1.0)))
        return h, (0.0, 0.0, 1.0)

    def uv0(self, p, n):
        e1, e2 = self._basis(n)
        s, t = gu.dot(p, e1) - self.s0[0], gu.dot(p, e2) - self.s0[1]
        return tuple(self.origin[a] + self.grad[a][0] * s + self.grad[a][1] * t for a in range(2))


def texture_of(nif, mesh):
    for p in mesh.data.get('Properties', []):
        pb = nif.blocks[p]
        if pb.base == 'NiTexturingProperty' and pb.data.get('Has Base Texture'):
            src = pb.data['Base Texture']['Source']
            if src >= 0:
                return nif.string(nif.blocks[src].data['File Name']['Index']).lower()
    return None


def add_render_mesh(nif, mat, tris, name, uvs=None):
    t_mesh = nif.blocks[mat.mesh]
    st = streams(nif, t_mesh)
    keys = ('POSITION0', 'NORMAL0', 'COLOR0', 'TEXCOORD0', 'TEXCOORD1')
    vstream = st['POSITION0'][0]
    assert all(st[k][0] == vstream for k in keys) and st['POSITION0'][2] == 44, 'unexpected vertex layout'
    order = sorted(keys, key=lambda k: st[k][1])
    rot = mat.inv[1]
    data = bytearray()
    local = []
    for ti, t in enumerate(tris):
        n = gu.normal(t)
        ln = gu.unit((rot[0] * n[0] + rot[1] * n[1] + rot[2] * n[2], rot[3] * n[0] + rot[4] * n[1] + rot[5] * n[2],
                      rot[6] * n[0] + rot[7] * n[1] + rot[8] * n[2]))
        for vi, p in enumerate(t):
            lp = ne.apply(mat.inv, p)
            local.append(lp)
            uv = uvs[ti][vi] if uvs else mat.uv0(p, n)
            comp = {'POSITION0': struct.pack('<3f', *lp), 'NORMAL0': struct.pack('<3f', *ln), 'COLOR0': mat.color,
                    'TEXCOORD0': struct.pack('<2f', *uv), 'TEXCOORD1': struct.pack('<2f', *mat.uv1)}
            data += b''.join(comp[k] for k in order)
    nv = len(local)
    assert nv < 65536
    i_old, v_old = (nif.blocks[st['INDEX0'][0]], nif.blocks[vstream])
    idata = copy.deepcopy({k: v for k, v in i_old.data.items() if k != 'Data'})
    idata['Data'] = struct.pack('<%d%s' % (nv, 'H' if fmt_size(st['INDEX0'][3]) == 2 else 'I'), *range(nv))
    idata['Num Bytes'] = len(idata['Data'])
    idata['Regions'] = [{'Start Index': 0, 'Num Indices': nv}]
    vdata = copy.deepcopy({k: v for k, v in v_old.data.items() if k != 'Data'})
    vdata['Data'] = bytes(data)
    vdata['Num Bytes'] = len(data)
    vdata['Regions'] = [{'Start Index': 0, 'Num Indices': nv}]
    si = add_block(nif, i_old.type, idata)
    sv = add_block(nif, v_old.type, vdata)
    md = copy.deepcopy(t_mesh.data)
    md['Name'] = {'Index': intern(nif, name)}
    md['Translation'] = {'x': 0.0, 'y': 0.0, 'z': 0.0}
    md['Rotation'] = dict(zip(ne.ROT_KEYS, ne.IDENTITY[1]))
    md['Scale'] = 1.0
    lo = [min(p[k] for p in local) for k in range(3)]
    hi = [max(p[k] for p in local) for k in range(3)]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    md['Bounding Sphere'] = {'Center': dict(zip('xyz', c)),
                             'Radius': max(math.sqrt(sum((p[k] - c[k]) ** 2 for k in range(3))) for p in local)}
    for ds in md['Datastreams']:
        ds['Stream'] = si if ds['Stream'] == st['INDEX0'][0] else sv
    m = add_block(nif, t_mesh.type, md)
    ne.set_children(nif, mat.parent, ne.children(nif, mat.parent) + [m])
    return m


# The client keeps portal culling on: NiRoomGroup::OnVisible (0xD7D330) draws the camera's room and what its portals
# show, or only the last room when the camera is in none; characters are attached to the room holding their
# "Bip01 Spine" and stay in the last one outside every room (0x8E5A70). Room walls: n.p >= c inside, model space.
# Portals are children of the room they lead out of, wound so that their normal points back into that room.

def ancestors(i, par):
    while i is not None:
        yield i
        i = par.get(i)


def room_index(nif, name):
    grp = ne.find(nif, 'PORTAL_NDLRG')
    found = [r for r in nif.blocks[grp].data['Rooms'] if nif.name(nif.blocks[r]) == name]
    assert len(found) == 1, 'room %s: %d matches' % (name, len(found))
    return found[0]


def world_walls(nif, r, par):
    xf = ne.world(nif, r, par)
    assert ne.is_identity_rotation(xf), 'rotated room'
    out = []
    for p in nif.blocks[r].data['Wall Planes']:
        n = (p['Normal']['x'], p['Normal']['y'], p['Normal']['z'])
        out.append((n, p['Constant'] + gu.dot(n, xf[0])))
    return out


def inside(walls, p, tol=0.0):
    return all(gu.dot(n, p) - c >= -tol for n, c in walls)


def set_wall(nif, r, normal, world_c, par):
    xf = ne.world(nif, r, par)
    hits = [p for p in nif.blocks[r].data['Wall Planes']
            if all(abs(p['Normal'][k] - v) < 1e-3 for k, v in zip('xyz', normal))]
    assert len(hits) == 1, 'wall %s of %s' % (normal, nif.name(nif.blocks[r]))
    hits[0]['Constant'] = world_c - gu.dot(normal, xf[0])


def box_walls(box):
    (x0, y0, z0), (x1, y1, z1) = box
    return [((1.0, 0.0, 0.0), x0), ((-1.0, 0.0, 0.0), -x1), ((0.0, 1.0, 0.0), y0), ((0.0, -1.0, 0.0), -y1),
            ((0.0, 0.0, 1.0), z0), ((0.0, 0.0, -1.0), -z1)]


def add_room(nif, spec, template, log):
    grp = ne.find(nif, 'PORTAL_NDLRG')
    t = nif.blocks[template].data
    d = copy.deepcopy(t)
    d['Name'] = {'Index': intern(nif, spec['name'])}
    d['Translation'] = {'x': 0.0, 'y': 0.0, 'z': 0.0}
    d['Rotation'] = dict(zip(ne.ROT_KEYS, ne.IDENTITY[1]))
    d['Scale'] = 1.0
    d['Wall Planes'] = [{'Normal': dict(zip('xyz', n)), 'Constant': c} for n, c in box_walls(spec['box'])]
    d['Num Walls'] = 6
    for key, count in (('Children', 'Num Children'), ('In Portals', 'Num In Portals'), ('Out Portals', 'Num Out Portals'),
                       ('Fixtures', 'Num Fixtures')):
        d[key], d[count] = [], 0
    src = room_index(nif, spec['effects_from'])
    d['Effects'] = list(nif.blocks[src].data['Effects'])
    d['Num Effects'] = len(d['Effects'])
    r = add_block(nif, nif.blocks[template].type, d)
    g = nif.blocks[grp]
    ne.set_list(g, 'Rooms', 'Num Rooms', g.data['Rooms'] + [r])
    ne.set_children(nif, grp, ne.children(nif, grp) + [r])
    log('  room %s %s, lights of %s' % (spec['name'], spec['box'], spec['effects_from']))
    return r


def add_portal(nif, owner, target, polygon, template, par):
    walls = world_walls(nif, owner, par)
    pts = [tuple(p) for p in polygon]
    n = gu.normal((pts[0], pts[1], pts[2]))
    c = tuple(sum(p[k] for p in pts) / len(pts) for k in range(3))
    probe = tuple(c[k] + 5.0 * n[k] for k in range(3))
    if not inside(walls, probe, 1e-3):
        pts.reverse()
        n = gu.normal((pts[0], pts[1], pts[2]))
        probe = tuple(c[k] + 5.0 * n[k] for k in range(3))
    assert inside(walls, probe, 1e-3), 'portal not on the boundary of %s' % nif.name(nif.blocks[owner])
    inv = ne.inverse(ne.world(nif, owner, par))
    d = copy.deepcopy(nif.blocks[template].data)
    name = '%s-%s_NDLPR' % (nif.name(nif.blocks[owner])[:-6], nif.name(nif.blocks[target])[:-6])
    d['Name'] = {'Index': intern(nif, name)}
    d['Translation'] = {'x': 0.0, 'y': 0.0, 'z': 0.0}
    d['Rotation'] = dict(zip(ne.ROT_KEYS, ne.IDENTITY[1]))
    d['Scale'] = 1.0
    d['Vertices'] = [dict(zip('xyz', ne.apply(inv, p))) for p in pts]
    d['Num Vertices'] = len(pts)
    d['Adjoiner'] = target
    k = add_block(nif, nif.blocks[template].type, d)
    o, t = nif.blocks[owner], nif.blocks[target]
    ne.set_children(nif, owner, ne.children(nif, owner) + [k])
    ne.set_list(o, 'Out Portals', 'Num Out Portals', o.data['Out Portals'] + [k])
    ne.set_list(t, 'In Portals', 'Num In Portals', t.data['In Portals'] + [k])
    return k


def room_overlaps(nif, r, par, step=50.0):
    walls = world_walls(nif, r, par)
    pts = [p for p in box_samples(walls, step) if inside(walls, p, -1.0)]
    grp = ne.find(nif, 'PORTAL_NDLRG')
    out = set()
    for o in nif.blocks[grp].data['Rooms']:
        if o == r:
            continue
        ow = world_walls(nif, o, par)
        if any(inside(ow, p, -1.0) for p in pts):
            out.add(nif.name(nif.blocks[o]))
    return sorted(out)


def box_samples(walls, step):
    assert all(max(abs(v) for v in n) > 0.99 for n, _ in walls), 'room walls not axis aligned'
    lo = [next(c for n, c in walls if n[k] > 0.99) for k in range(3)]
    hi = [-next(c for n, c in walls if n[k] < -0.99) for k in range(3)]
    axes = [[lo[k] + (hi[k] - lo[k]) * i / max(1, int((hi[k] - lo[k]) / step)) for i in range(int((hi[k] - lo[k]) / step) + 1)]
            for k in range(3)]
    return [(x, y, z) for x in axes[0] for y in axes[1] for z in axes[2]]


def apply_rooms(nif, c, log):
    par = ne.parents(nif)
    for w in c.get('room_walls', []):
        set_wall(nif, room_index(nif, w['room']), tuple(w['normal']), w['world'], par)
        log('  room %s: wall %s moved to %s' % (w['room'], w['normal'], w['world']))
    template = room_index(nif, c['rooms'][0]['effects_from'])
    new = [add_room(nif, spec, template, log) for spec in c.get('rooms', [])]
    par = ne.parents(nif)
    portal_t = nif.blocks[template].data['Out Portals'][0]
    for p in c.get('portals', []):
        a, b = (room_index(nif, n) for n in p['between'])
        add_portal(nif, a, b, p['polygon'], portal_t, par)
        add_portal(nif, b, a, p['polygon'], portal_t, par)
        log('  portals %s <-> %s' % tuple(p['between']))
    par = ne.parents(nif)
    for r in new + [room_index(nif, w['room']) for w in c.get('room_walls', [])]:
        over = room_overlaps(nif, r, par)
        assert not over, 'room %s overlaps %s' % (nif.name(nif.blocks[r]), over)


def room_lmg(nif, room_name, lmg_name):
    r = room_index(nif, room_name)
    for k in ne.children(nif, r):
        if nif.name(nif.blocks[k]) == lmg_name:
            return k
    tmpl = next(i for i, b in enumerate(nif.blocks) if b.base == 'NiNode' and nif.name(b) == lmg_name)
    d = copy.deepcopy(nif.blocks[tmpl].data)
    for key, count in (('Children', 'Num Children'), ('Effects', 'Num Effects'), ('Properties', 'Num Properties'),
                       ('Extra Data List', 'Num Extra Data List')):
        d[key], d[count] = [], 0
    d['Controller'] = -1
    d['Translation'] = {'x': 0.0, 'y': 0.0, 'z': 0.0}
    d['Rotation'] = dict(zip(ne.ROT_KEYS, ne.IDENTITY[1]))
    d['Scale'] = 1.0
    k = add_block(nif, nif.blocks[tmpl].type, d)
    ne.set_children(nif, r, ne.children(nif, r) + [k])
    rb = nif.blocks[r]
    ne.set_list(rb, 'Fixtures', 'Num Fixtures', rb.data['Fixtures'] + [k])
    return k


def outside_lmg(nif, lmg_name):
    par = ne.parents(nif)
    for i, b in enumerate(nif.blocks):
        if b.base == 'NiNode' and nif.name(b) == lmg_name and not any(nif.blocks[j].base == 'NiRoom' for j in ancestors(i, par)):
            return i
    raise ValueError('no %s outside the rooms' % lmg_name)


def material_for(c, a, part):
    m = next((m for m in c['materials'] if m.get('until') is None or a < m['until']), c['materials'][-1])
    return dict(m[part], room=m.get('room'), outside=bool(m.get('outside_rooms')))


def material_key(m):
    return m['texture'], tuple(m['near']), m.get('room'), m['outside']


def add_covers(world, c, spec, mats, log):
    """Cover meshes: the cover material's properties, placed and lit like the corridor wall material at each cover
    (the client gives a mesh the lightmap of its LMG_ node and clones a texturing property already given another
    one, 0x52F260)."""
    crate = Material(world, spec, ne.parents(world))
    groups = {}
    for cover in c['covers']:
        tris, uvs = cover_render(c, cover, spec['faces'])
        g = groups.setdefault(material_key(material_for(c, cover['at'], 'wall')), ([], []))
        g[0].extend(tris)
        g[1].extend(uvs)
    for k, (key, (tris, uvs)) in enumerate(groups.items()):
        light = mats[key]
        mat = copy.copy(crate)
        mat.parent, mat.parent_xf, mat.inv = light.parent, light.parent_xf, light.inv
        mat.uv1, mat.color = light.uv1, light.color
        name = '%s_covers%s_%s' % (c['name'], '_%d' % k if k else '', spec['texture'].rsplit('.', 1)[0])
        add_render_mesh(world, mat, tris, name, uvs)
        log('  render %s: %d triangles under %r, lit as %s' % (name, len(tris), world.name(world.blocks[mat.parent]), key[0]))


def apply(folder, layout, dll, log=print):
    paths = {n: '%s/%s' % (folder, n) for n in ('World.nif', 'World_colp.nif', 'World_colb.nif')}
    nifs = {n: Nif.load(p) for n, p in paths.items()}
    for c in layout.get('corridors', []):
        log('corridor %s' % c['name'])
        for cut in c.get('cuts', []):
            if cut['file'] == 'World.nif':
                cut_render(nifs[cut['file']], cut, log)
            else:
                cut_physx(nifs[cut['file']], cut, dll, log)
        world = nifs['World.nif']
        if c.get('rooms') or c.get('room_walls'):
            apply_rooms(world, c, log)
        mats = {}
        for part, pieces in surfaces(c, 'render').items():
            groups = {}
            for a, tris in pieces:
                m = material_for(c, a, 'wall' if part == 'cap' else part)
                groups.setdefault(material_key(m), []).extend(tris)
            for key, tris in groups.items():
                tex, near, room, outside = key
                if key not in mats:
                    par = ne.parents(world)
                    mats[key] = mat = Material(world, {'texture': tex, 'near': near}, par)
                    if room:
                        node = room_lmg(world, room, mat.lmg)
                        mat.place(world, node, ne.parents(world))
                    elif outside:
                        mat.place(world, outside_lmg(world, mat.lmg), ne.parents(world))
                name = '%s_%s_%s' % (c['name'], part, tex.rsplit('.', 1)[0])
                add_render_mesh(world, mats[key], tris, name)
                log('  render %s: %d triangles under %r' % (name, len(tris), world.name(world.blocks[mats[key].parent])))
        if c.get('covers'):
            add_covers(world, c, layout['cover_material'], mats, log)
        for kind, fname, cname in (('colp', 'World_colp.nif', COLP_NAME), ('colb', 'World_colb.nif', COLB_NAME)):
            nif = nifs[fname]
            tris = [t for pieces in surfaces(c, kind).values() for _, ts in pieces for t in ts] + cover_collision(c)
            template = next(i for i, n, _, _ in physx_actors(nif) if n.lower() == cname)
            add_physx_actor(nif, cname, tris, dll, template)
            log('  %s: actor %s with %d triangles' % (fname, cname, len(tris)))
    for n, nif in nifs.items():
        nif.save(paths[n])
