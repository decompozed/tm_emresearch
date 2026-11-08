"""Minimal Gamebryo 20.6.0.0 reader: blocks, node transforms/children, NiMesh position streams -> world bounds."""
import struct
import sys


class R:
    def __init__(self, d, o=0):
        self.d, self.o = d, o

    def u8(self):
        v = self.d[self.o]; self.o += 1; return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.o)[0]; self.o += 2; return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.o)[0]; self.o += 4; return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.o)[0]; self.o += 4; return v

    def f(self, n=1):
        v = struct.unpack_from('<%df' % n, self.d, self.o); self.o += 4 * n; return v if n > 1 else v[0]

    def raw(self, n):
        v = self.d[self.o:self.o + n]; self.o += n; return v


def load(path):
    d = open(path, 'rb').read()
    nl = d.index(b'\n')
    r = R(d, nl + 1)
    version = r.u32(); endian = r.u8(); user = r.u32(); nblocks = r.u32()
    ntypes = r.u16()
    types = []
    for _ in range(ntypes):
        n = r.u32(); types.append(r.raw(n).decode())
    tindex = [r.u16() & 0x7FFF for _ in range(nblocks)]
    sizes = [r.u32() for _ in range(nblocks)]
    nstrings = r.u32(); r.u32()
    strings = []
    for _ in range(nstrings):
        n = r.u32(); strings.append(r.raw(n).decode('latin1'))
    ngroups = r.u32()
    r.o += 4 * ngroups
    blocks = []
    for i in range(nblocks):
        blocks.append((types[tindex[i]], d[r.o:r.o + sizes[i]]))
        r.o += sizes[i]
    return blocks, strings


def avobject(data):
    """NiObjectNET + NiAVObject start (20.6): name, extra data, controller, flags, transform."""
    r = R(data)
    name = r.i32()
    nextra = r.u32(); r.o += 4 * nextra
    r.i32()  # controller
    r.u16()  # flags (16 bit in these files)
    t = r.f(3); rot = r.f(9); s = r.f()
    nprops = r.u32(); r.o += 4 * nprops
    r.i32()  # collision object
    return name, t, rot, s, r


def node_children(r):
    # after transform: num properties? (removed in 20.2.0.7+ for NiAVObject? keep: collision ref), children
    nchildren = r.u32()
    children = [r.i32() for _ in range(nchildren)]
    return children


def transform_point(p, t, rot, s):
    # Gamebryo: world = R * (s * p) + t, rotation row-major
    x, y, z = p[0] * s, p[1] * s, p[2] * s
    return (rot[0] * x + rot[1] * y + rot[2] * z + t[0],
            rot[3] * x + rot[4] * y + rot[5] * z + t[1],
            rot[6] * x + rot[7] * y + rot[8] * z + t[2])


def compose(parent, child):
    pt, pr, ps = parent
    ct, cr, cs = child
    # world rot = pr * cr
    rot = [sum(pr[i * 3 + k] * cr[k * 3 + j] for k in range(3)) for i in range(3) for j in range(3)]
    t = transform_point(ct, pt, pr, ps)
    return (t, rot, ps * cs)


def datastream(data):
    r = R(data)
    nbytes = r.u32(); r.u32()  # cloning
    nreg = r.u32(); regions = [(r.u32(), r.u32()) for _ in range(nreg)]
    ncomp = r.u32(); comps = [r.u32() for _ in range(ncomp)]
    raw = r.raw(nbytes)
    nverts = sum(c for _, c in regions)
    return nverts, comps, raw


def mesh_streams(data, blocks, strings):
    out = []
    pos = strings.index('POSITION') if 'POSITION' in strings else -1
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
        if pos in names:
            out.append((ref, names.index(pos)))
    return out


def _old_mesh_streams(data, blocks, strings):
    """NiMesh (20.6): after NiRenderObject (material data) ... find stream refs with POSITION semantic."""
    # Robust approach: look for (stream ref, isPerInstance u8, numSubmeshes u16, submesh map u16s, numComponents u32,
    # component names (string idx), component indices u32) patterns naming POSITION.
    pos_idx = [i for i, s in enumerate(strings) if s == 'POSITION']
    refs = []
    if not pos_idx:
        return refs
    pi = pos_idx[0]
    for o in range(0, len(data) - 8):
        if struct.unpack_from('<i', data, o)[0] == pi and struct.unpack_from('<I', data, o + 4)[0] in (0, 1):
            # walk back to the stream ref: ref(4) perInstance(1) nsub(2) submap(2*nsub) ncomp(4)
            for nsub in range(1, 16):
                start = o - 4 - 2 * nsub - 2 - 1 - 4
                if start < 0:
                    break
                ref = struct.unpack_from('<i', data, start)[0]
                n2 = struct.unpack_from('<H', data, start + 5)[0]
                ncomp = struct.unpack_from('<I', data, o - 4)[0]
                if n2 == nsub and 1 <= ncomp <= 4 and 0 <= ref < len(blocks) and blocks[ref][0] == 'NiDataStream':
                    refs.append(ref)
                    break
    return refs


def world_bounds(path, verbose=False):
    blocks, strings = load(path)
    local = {}
    children = {}
    names = {}
    for i, (typ, data) in enumerate(blocks):
        if typ in ('NiNode', 'NiMesh', 'NiSwitchNode', 'NiBillboardNode', 'NiLODNode', 'NiSortAdjustNode') or typ.endswith('Node') or typ.startswith('NiMesh'):
            try:
                name, t, rot, s, r = avobject(data)
                local[i] = (t, rot, s)
                names[i] = strings[name] if 0 <= name < len(strings) else ''
                if not typ.startswith('NiMesh'):
                    children[i] = node_children(r)
            except Exception:
                pass
    world = {0: local.get(0, ((0, 0, 0), [1, 0, 0, 0, 1, 0, 0, 0, 1], 1))}
    stack = [0]
    while stack:
        n = stack.pop()
        for c in children.get(n, []):
            if c in local and c not in world:
                world[c] = compose(world[n], local[c])
                stack.append(c)
    lo = [1e30] * 3; hi = [-1e30] * 3
    count = 0
    for i, (typ, data) in enumerate(blocks):
        if not typ.startswith('NiMesh') or i not in world:
            continue
        for ref, comp in mesh_streams(data, blocks, strings):
            nverts, comps, raw = datastream(blocks[ref][1])
            if not nverts or len(raw) % nverts:
                continue
            stride = len(raw) // nverts
            offset = sum(((c >> 16) & 0xFF) * 4 for c in comps[:comp])
            for k in range(0, len(raw), stride):
                p = struct.unpack_from('<3f', raw, k + offset)
                w = transform_point(p, *world[i])
                for a in range(3):
                    lo[a] = min(lo[a], w[a]); hi[a] = max(hi[a], w[a])
                count += 1
            if verbose:
                print('  mesh', names.get(i), 'world t', [round(v) for v in world[i][0]], 'comps', [hex(c) for c in comps], nverts, 'verts')
    return count, lo, hi, names


if __name__ == '__main__':
    for p in sys.argv[1:]:
        c, lo, hi, _ = world_bounds(p, verbose=len(sys.argv) == 2)
        print(p, c, 'verts', [round(v) for v in lo], [round(v) for v in hi])
