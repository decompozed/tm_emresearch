"""Editing helpers on top of nifio: copying blocks between files, scene-graph transforms, garbage collection."""
import copy
import math

from nifio import Block, Nif

IDENTITY = ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0), 1.0)
ROT_KEYS = ('m11', 'm21', 'm31', 'm12', 'm22', 'm32', 'm13', 'm23', 'm33')


class Builder:

    def __init__(self, template):
        self.nif = Nif()
        for k in ('header_string', 'version', 'user', 'endian', 'schema'):
            setattr(self.nif, k, getattr(template, k))
        self.nif.type_order = []
        self._index = {}

    def intern(self, s):
        if s is None:
            return -1
        if s not in self._index:
            self._index[s] = len(self.nif.strings)
            self.nif.strings.append(s)
        return self._index[s]

    def add(self, src, rename=None):
        base = len(self.nif.blocks)
        for t in getattr(src, 'type_order', []):
            if t not in self.nif.type_order:
                self.nif.type_order.append(t)

        def fn(kind, v):
            if kind == 'str':
                if v < 0:
                    return v
                s = src.strings[v]
                return self.intern(rename(s) if rename else s)
            return v + base if v >= 0 else v

        for b in src.blocks:
            nb = Block(b.type, copy.deepcopy(b.data), b.flag)
            self.nif.links(nb, fn)
            self.nif.blocks.append(nb)
        return {i: base + i for i in range(len(src.blocks))}

    def new(self, type_name, data, flag=0):
        self.nif.blocks.append(Block(type_name, data, flag))
        if type_name not in self.nif.type_order:
            self.nif.type_order.append(type_name)
        return len(self.nif.blocks) - 1

    def string(self, i):
        return self.nif.string(i)

    def name(self, i):
        return self.nif.name(self.nif.blocks[i])

    def set_name(self, i, s):
        self.nif.blocks[i].data['Name'] = {'Index': self.intern(s)}


def ref_edges(nif):
    out = []
    for b in nif.blocks:
        e = []

        def fn(kind, v, e=e):
            if kind != 'str' and v >= 0:
                e.append((kind, v))
            return v
        nif.links(b, fn)
        out.append(e)
    return out


def collect(nif):
    edges = ref_edges(nif)
    keep, stack = set(), [r for r in nif.roots if r >= 0]
    while stack:
        k = stack.pop()
        if k in keep:
            continue
        keep.add(k)
        stack += [t for kind, t in edges[k] if kind == 'ref']
    first = list(dict.fromkeys(r for r in nif.roots if r >= 0))[:1]
    order = first + sorted(keep - set(first))
    remap = {old: new for new, old in enumerate(order)}
    cleared = [0]

    def fn(kind, v):
        if kind == 'str' or v < 0:
            return v
        if v in remap:
            return remap[v]
        if kind == 'ptr':
            cleared[0] += 1
            return -1
        raise ValueError('ref to a dropped block %d' % v)

    removed = len(nif.blocks) - len(order)
    nif.blocks = [nif.blocks[i] for i in order]
    for b in nif.blocks:
        nif.links(b, fn)
    nif.roots = [remap[r] for r in nif.roots]
    _compact_strings(nif)
    return removed, cleared[0]


def _compact_strings(nif):
    used = []
    seen = {}

    def fn(kind, v):
        if kind != 'str' or v < 0:
            return v
        if v not in seen:
            seen[v] = len(used)
            used.append(nif.strings[v])
        return seen[v]
    for b in nif.blocks:
        nif.links(b, fn)
    nif.strings = used


def local(block):
    d = block.data
    t = d['Translation']
    r = d['Rotation']
    return (t['x'], t['y'], t['z']), tuple(r[k] for k in ROT_KEYS), d['Scale']


def set_local(block, xf):
    t, r, s = xf
    block.data['Translation'] = {'x': t[0], 'y': t[1], 'z': t[2]}
    block.data['Rotation'] = dict(zip(ROT_KEYS, r))
    block.data['Scale'] = s


def apply(xf, p):
    """Gamebryo NiTransform: R * (s * p) + t; the 9 rotation floats are the rows of R in file order."""
    t, r, s = xf
    x, y, z = p[0] * s, p[1] * s, p[2] * s
    return (r[0] * x + r[1] * y + r[2] * z + t[0],
            r[3] * x + r[4] * y + r[5] * z + t[1],
            r[6] * x + r[7] * y + r[8] * z + t[2])


def compose(parent, child):
    pt, pr, ps = parent
    ct, cr, cs = child
    rot = tuple(sum(pr[i * 3 + k] * cr[k * 3 + j] for k in range(3)) for i in range(3) for j in range(3))
    return apply(parent, ct), rot, ps * cs


def inverse(xf):
    t, r, s = xf
    rt = (r[0], r[3], r[6], r[1], r[4], r[7], r[2], r[5], r[8])
    inv_s = 1.0 / s
    it = tuple(-inv_s * v for v in apply(((0, 0, 0), rt, 1.0), t))
    return it, rt, inv_s


def translation(o):
    return (float(o[0]), float(o[1]), float(o[2])), IDENTITY[1], 1.0


def is_identity_rotation(xf, eps=1e-6):
    return all(abs(a - b) <= eps for a, b in zip(xf[1], IDENTITY[1])) and abs(xf[2] - 1.0) <= eps


def parents(nif):
    out = {}
    for i, b in enumerate(nif.blocks):
        for c in b.data.get('Children') or []:
            if c >= 0:
                out[c] = i
    return out


def world(nif, i, par=None):
    par = parents(nif) if par is None else par
    chain = []
    while i is not None:
        chain.append(i)
        i = par.get(i)
    xf = IDENTITY
    for k in reversed(chain):
        xf = compose(xf, local(nif.blocks[k]))
    return xf


def children(nif, i):
    return [c for c in nif.blocks[i].data.get('Children') or [] if c >= 0]


def set_children(nif, i, kids):
    nif.blocks[i].data['Children'] = list(kids)
    nif.blocks[i].data['Num Children'] = len(kids)


def set_list(block, key, count_key, values):
    block.data[key] = list(values)
    block.data[count_key] = len(values)


def find(nif, name, start=None, base=None):
    stack = [start] if start is not None else list(nif.roots)
    while stack:
        i = stack.pop(0)
        b = nif.blocks[i]
        if nif.name(b) == name and (base is None or b.base == base):
            return i
        stack[0:0] = children(nif, i)
    return None


def find_all(nif, pred, start=None):
    out, stack = [], [start] if start is not None else list(nif.roots)
    while stack:
        i = stack.pop()
        if pred(i, nif.blocks[i]):
            out.append(i)
        stack += children(nif, i)
    return out


def finite(v):
    return not (math.isnan(v) or math.isinf(v)) and abs(v) < 3.0e38
