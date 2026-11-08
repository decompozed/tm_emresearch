"""PhysX 2.8 triangle meshes: NXS reader and cooking through cook/cook.exe + the game's PhysXCooking.dll.

NXS triangle mesh stream (as written by PhysXCooking.dll 2.8.4.4): 'NXS' 0x01 'MESH', u32 version (1),
u32 serial flags (0x08 8-bit indices, 0x10 16-bit indices, 0x02 face remap), f32 convex edge threshold,
u32 height field axis, f32 height field extent, u32 vertices, u32 triangles, vertices, indices,
[u32 max remap index, remap table in the smallest width], then the collision structures."""
import argparse
import collections
import os
import struct
import subprocess
import sys
import tempfile

from nifio import Nif

HERE = os.path.dirname(os.path.abspath(__file__))
COOK_EXE = os.path.join(HERE, 'cook', 'cook.exe')
WELD = 0.01


def _width(flags):
    return (2, 'H') if flags & 0x10 else (1, 'B') if flags & 0x08 else (4, 'I')


def _packed(blob, o, n, maxv):
    w, f = (1, 'B') if maxv < 0x100 else (2, 'H') if maxv < 0x10000 else (4, 'I')
    return list(struct.unpack_from('<%d%s' % (n, f), blob, o)), o + w * n


def parse(blob):
    assert blob[:8] == b'NXS\x01MESH', blob[:8]
    version, flags, edge, axis, extent, nv, nt = struct.unpack_from('<IIfIfII', blob, 8)
    o = 36
    verts = [struct.unpack_from('<3f', blob, o + 12 * i) for i in range(nv)]
    o += 12 * nv
    w, f = _width(flags)
    flat = struct.unpack_from('<%d%s' % (3 * nt, f), blob, o)
    o += 3 * w * nt
    tris = [flat[3 * i:3 * i + 3] for i in range(nt)]
    remap = None
    if flags & 0x02:
        maxv = struct.unpack_from('<I', blob, o)[0]
        remap, o = _packed(blob, o + 4, nt, maxv)
    return dict(version=version, flags=flags, edge=edge, axis=axis, extent=extent, verts=verts, tris=tris,
                remap=remap, rest=o)


def source_order(mesh):
    tris, remap = mesh['tris'], mesh['remap']
    if not remap or sorted(remap) != list(range(len(tris))):
        return list(tris)
    out = [None] * len(tris)
    for cooked, orig in enumerate(remap):
        out[orig] = tris[cooked]
    return out


def cook(verts, tris, dll, mesh_flags=0, skin_width=0.025, hint_speed=False, exe=COOK_EXE):
    data = struct.pack('<IIIfI', len(verts), len(tris), mesh_flags, skin_width, int(hint_speed))
    data += b''.join(struct.pack('<3f', *v) for v in verts)
    data += b''.join(struct.pack('<3I', *t) for t in tris)
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = os.path.join(tmp, 'in.bin'), os.path.join(tmp, 'out.nxs')
        with open(src, 'wb') as f:
            f.write(data)
        r = subprocess.run([exe, dll, src, dst], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError('cook failed (%d): %s' % (r.returncode, r.stderr.strip()))
        with open(dst, 'rb') as f:
            return f.read()


def triangle_set(m):
    out = []
    for t in m['tris']:
        p = [m['verts'][i] for i in t]
        k = p.index(min(p))
        out.append(tuple(p[k:] + p[:k]))
    return sorted(out)


def compare(orig_blob, new_blob):
    if orig_blob == new_blob:
        return 'identical'
    a, b = parse(orig_blob), parse(new_blob)
    if (a['verts'], a['tris'], a['remap']) == (b['verts'], b['tris'], b['remap']):
        tree_a, tree_b = (x[x.find(b'OPC'):x.find(b'HBM')] for x in (orig_blob, new_blob))
        return 'same arrays' if tree_a == tree_b else 'same arrays, other tree'
    ta, tb = collections.Counter(triangle_set(a)), collections.Counter(triangle_set(b))
    if ta == tb:
        return 'same triangles'
    only_a, only_b = list((ta - tb).elements()), list((tb - ta).elements())
    for t in only_a:
        j = next((k for k, u in enumerate(only_b) if near(t, u)), None)
        if j is None:
            return 'different'
        only_b.pop(j)
    return 'same triangles, %d welded' % len(only_a) if not only_b else 'different'


def near(t, u, eps=WELD):
    return any(all(abs(x - y) <= eps for p, q in zip(t, u[k:] + u[:k]) for x, y in zip(p, q)) for k in range(3))


def recook_test(nif_paths, dll):
    results = {}
    for path in nif_paths:
        nif = Nif.load(path)
        for b in nif.blocks:
            if b.base != 'NiPhysXMeshDesc':
                continue
            blob = b.data['Mesh Data']['Data']
            if blob[4:8] != b'MESH':
                results['not a triangle mesh'] = results.get('not a triangle mesh', 0) + 1
                print('%s %s: %r, skipped' % (path, nif.string(b.data['Mesh Name']), blob[4:8]))
                continue
            mesh = parse(blob)
            out = cook(mesh['verts'], source_order(mesh), dll)
            status = compare(blob, out)
            kind = 'same triangles after welding' if status.endswith('welded') else status
            results[kind] = results.get(kind, 0) + 1
            unique = len(set(mesh['verts']))
            used = len({i for t in mesh['tris'] for i in t})
            print('%s %s: %d verts (%d distinct, %d used), %d tris: %s, %d -> %d bytes'
                  % (path, nif.string(b.data['Mesh Name']), len(mesh['verts']), unique, used, len(mesh['tris']),
                     status, len(blob), len(out)))
    print('meshes %d: %s' % (sum(results.values()), ', '.join('%s %d' % kv for kv in sorted(results.items()))))
    return results.get('different', 0)


def read_obj(path):
    verts, tris = [], []
    with open(path) as f:
        for line in f:
            p = line.split()
            if p and p[0] == 'v':
                verts.append(tuple(float(x) for x in p[1:4]))
            elif p and p[0] == 'f':
                idx = [int(x.split('/')[0]) - 1 for x in p[1:]]
                tris += [(idx[0], idx[k], idx[k + 1]) for k in range(1, len(idx) - 1)]
    return verts, tris


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    t = sub.add_parser('test', help='re-cook the NiPhysXMeshDesc meshes of NIFs and compare with the originals')
    t.add_argument('dll', help='PhysXCooking.dll 2.8.4.4 (a copy)')
    t.add_argument('nifs', nargs='+')
    o = sub.add_parser('obj', help='cook a Wavefront OBJ into an NXS triangle mesh')
    o.add_argument('dll')
    o.add_argument('obj')
    o.add_argument('out')
    a = ap.parse_args()
    if a.cmd == 'test':
        return 1 if recook_test(a.nifs, a.dll) else 0
    verts, tris = read_obj(a.obj)
    blob = cook(verts, tris, a.dll)
    with open(a.out, 'wb') as f:
        f.write(blob)
    m = parse(blob)
    print('%d vertices, %d triangles -> %d bytes' % (len(m['verts']), len(m['tris']), len(blob)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
