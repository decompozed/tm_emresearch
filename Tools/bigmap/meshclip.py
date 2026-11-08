"""Clipping of render meshes (triangle NiMesh) against world boxes: parts inside a box removed (openings) or parts
outside a box removed (crop). New vertices interpolate every vertex component of the triangle they come from."""
import math
import struct

import geomutil as gu
import nifedit as ne

VERTICAL = 0.3
HIDDEN = ('Occluder', 'OUTSIDE_SOUND')


def fmt(c):
    return (c >> 16) & 0xFF, (c >> 8) & 0xFF


def _streams(nif, mesh):
    out = {}
    for ds in mesh.data['Datastreams']:
        s = ds['Stream']
        sem = ['%s%d' % (nif.string(x['Name']), x['Index']) for x in ds['Component Semantics']]
        out[s] = sem
    return out


class MeshData:

    def __init__(self, nif, i):
        self.nif, self.i = nif, i
        b = nif.blocks[i]
        assert b.data['Primitive Type'] == 0 and not b.data.get('Num Modifiers'), 'unsupported mesh'
        self.vstreams = []
        self.index = None
        for s, sem in _streams(nif, b).items():
            sd = nif.blocks[s].data
            assert sd['Num Regions'] == 1, 'stream with several regions'
            formats = sd['Component Formats']
            if sem == ['INDEX0']:
                n, size = fmt(formats[0])
                self.index = (s, 'H' if size == 2 else 'I')
                raw = sd['Data']
                self.tris = list(struct.unpack('<%d%s' % (len(raw) // size, self.index[1]), raw))
                continue
            stride = sum(a * b for a, b in map(fmt, formats))
            raw = sd['Data']
            nv = len(raw) // stride
            comps = []
            off = 0
            for c in formats:
                n, size = fmt(c)
                code = 'B' if size == 1 else 'f'
                assert size in (1, 4)
                comps.append([struct.unpack_from('<%d%s' % (n, code), raw, j * stride + off) for j in range(nv)])
                off += n * size
            self.vstreams.append(dict(stream=s, sem=sem, formats=formats, comps=comps, nv=nv))
        assert self.index, 'mesh without index stream'
        pos = next((v, k) for v in self.vstreams for k, s in enumerate(v['sem']) if s == 'POSITION0')
        self.pos = pos[0]['comps'][pos[1]]
        self.nv = pos[0]['nv']

    def add_vertex(self, weights):
        for v in self.vstreams:
            for k, comp in enumerate(v['comps']):
                n, size = fmt(v['formats'][k])
                val = [sum(w * comp[j][a] for j, w in weights.items()) for a in range(n)]
                if size == 1:
                    val = [max(0, min(255, int(round(x)))) for x in val]
                elif v['sem'][k].startswith('NORMAL') or v['sem'][k].startswith('TANGENT') or v['sem'][k].startswith('BINORMAL'):
                    ln = math.sqrt(sum(x * x for x in val)) or 1.0
                    val = [x / ln for x in val]
                comp.append(tuple(val))
            v['nv'] += 1
        self.nv += 1
        return self.nv - 1

    def write(self):
        nif = self.nif
        for v in self.vstreams:
            data = bytearray()
            for j in range(v['nv']):
                for k, c in enumerate(v['formats']):
                    n, size = fmt(c)
                    data += struct.pack('<%d%s' % (n, 'B' if size == 1 else 'f'), *v['comps'][k][j])
            sd = nif.blocks[v['stream']].data
            sd['Data'] = bytes(data)
            sd['Num Bytes'] = len(data)
            sd['Regions'] = [{'Start Index': 0, 'Num Indices': v['nv']}]
        s, code = self.index
        assert code == 'I' or self.nv < 65536, 'too many vertices for a 16-bit index stream'
        sd = nif.blocks[s].data
        sd['Data'] = struct.pack('<%d%s' % (len(self.tris), code), *self.tris)
        sd['Num Bytes'] = len(sd['Data'])
        sd['Regions'] = [{'Start Index': 0, 'Num Indices': len(self.tris)}]


def _wsplit(poly, axis, value, keep_below):
    out = []
    n = len(poly)
    for k in range(n):
        (pa, wa), (pb, wb) = poly[k], poly[(k + 1) % n]
        da = (value - pa[axis]) if keep_below else (pa[axis] - value)
        db = (value - pb[axis]) if keep_below else (pb[axis] - value)
        if da >= 0:
            out.append((pa, wa))
        if (da >= 0) != (db >= 0):
            f = da / (da - db)
            p = tuple(pa[a] + f * (pb[a] - pa[a]) for a in range(3))
            w = {}
            for j, x in wa.items():
                w[j] = w.get(j, 0.0) + x * (1 - f)
            for j, x in wb.items():
                w[j] = w.get(j, 0.0) + x * f
            out.append((p, w))
    return out


def _pieces(poly, box, keep):
    lo, hi = box
    rest = poly
    outside = []
    for axis in range(3):
        for value, below in ((lo[axis], True), (hi[axis], False)):
            if len(rest) < 3:
                break
            part = _wsplit(rest, axis, value, below)
            if len(part) >= 3:
                outside.append(part)
            rest = _wsplit(rest, axis, value, not below)
    if keep == 'outside':
        return outside
    return [rest] if len(rest) >= 3 else []


def clip_mesh(nif, i, box, keep, par, vertical=False):
    md = MeshData(nif, i)
    w = ne.world(nif, i, par)
    V = [ne.apply(w, p) for p in md.pos]
    old = md.tris
    new = []
    removed = 0.0
    for j in range(0, len(old) - 2, 3):
        tri = old[j:j + 3]
        t = tuple(V[k] for k in tri)
        a = gu.area(t)
        if vertical and abs(gu.normal(t)[2]) >= VERTICAL:
            new += tri
            continue
        inside_all = all(gu.inside_box(p, box, 0.0) for p in t)
        touches = gu.overlaps_box(t, box)
        if (keep == 'outside' and not touches) or (keep == 'inside' and inside_all):
            new += tri
            continue
        if (keep == 'outside' and inside_all) or (keep == 'inside' and not touches):
            removed += a
            continue
        polys = _pieces([(t[0], {tri[0]: 1.0}), (t[1], {tri[1]: 1.0}), (t[2], {tri[2]: 1.0})], box, keep)
        kept = 0.0
        for poly in polys:
            for f in range(1, len(poly) - 1):
                pts = (poly[0], poly[f], poly[f + 1])
                ar = gu.area(tuple(p for p, _ in pts))
                if ar < 0.01:
                    continue
                kept += ar
                for p, wt in pts:
                    if len(wt) == 1 or max(wt.values()) > 1 - 1e-9:
                        new.append(max(wt, key=wt.get))
                    else:
                        new.append(md.add_vertex(wt))
        removed += max(0.0, a - kept)
    if removed <= 0.01:
        return 0.0
    md.tris = new
    md.write()
    return removed


def render_meshes(nif, under=None, par=None):
    par = par or ne.parents(nif)
    out = []
    for i, b in enumerate(nif.blocks):
        if b.base != 'NiMesh' or b.data['Primitive Type'] != 0:
            continue
        chain = []
        j = i
        while j is not None:
            chain.append(j)
            j = par.get(j)
        names = [nif.name(nif.blocks[k]) or '' for k in chain]
        if any(n in HIDDEN or n.endswith('_NDLWL') for n in names):
            continue
        if under is not None and under not in chain:
            continue
        out.append(i)
    return out


def crop(nif, box, log=None, under=None):
    par = ne.parents(nif)
    total, emptied = 0.0, 0
    for i in render_meshes(nif, under, par):
        total += clip_mesh(nif, i, box, 'inside', par)
        md = MeshData(nif, i)
        if not md.tris:
            ne.set_children(nif, par[i], [c for c in ne.children(nif, par[i]) if c != i])
            emptied += 1
    if log:
        log('crop %s: %.0f area removed, %d meshes emptied' % (box, total, emptied))
    return total, emptied
