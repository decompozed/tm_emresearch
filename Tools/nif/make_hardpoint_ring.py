"""Hardpoint ring models: party/HardpointRing{White,Red,Blue}.nif + party/hardpoint_ring_{white,red,blue}.dds.

Built from party/LavaWallX.nif (same block structure, NiAlphaProperty, NiMaterialProperty): the index / vertex streams
are replaced by an open, double-sided cylinder of radius RADIUS around the node, from BOTTOM to TOP (node space);
the node is renamed "hardpoint_ring" and the texture string points to the ring texture. Everything else is kept.
"""
import math
import os
import struct
import sys

RADIUS = 1100.0     # server HardpointMode::zoneRadius (user 2026-10-02: a bit bigger, was 900)
BOTTOM = -20000.0
TOP = 40000.0
SEGMENTS = 64
U_REPEAT = 24.0
V_TILE = 420.0      # the texture repeats every V_TILE world units upwards (bands at player height)


def split(path):
    d = open(path, 'rb').read()
    nl = d.index(b'\n') + 1
    o = nl
    version, endian, user, nblocks = struct.unpack_from('<IBII', d, o); o += 13
    ntypes = struct.unpack_from('<H', d, o)[0]; o += 2
    types = []
    for _ in range(ntypes):
        n = struct.unpack_from('<I', d, o)[0]; o += 4
        types.append(d[o:o + n]); o += n
    tindex = list(struct.unpack_from('<%dH' % nblocks, d, o)); o += 2 * nblocks
    sizes = list(struct.unpack_from('<%dI' % nblocks, d, o)); o += 4 * nblocks
    nstrings, maxlen = struct.unpack_from('<II', d, o); o += 8
    strings = []
    for _ in range(nstrings):
        n = struct.unpack_from('<I', d, o)[0]; o += 4
        strings.append(d[o:o + n]); o += n
    ngroups = struct.unpack_from('<I', d, o)[0]; o += 4
    groups = d[o:o + 4 * ngroups]; o += 4 * ngroups
    blocks = []
    for size in sizes:
        blocks.append(bytearray(d[o:o + size])); o += size
    footer = d[o:]
    return dict(text=d[:nl], version=version, endian=endian, user=user, types=types, tindex=tindex, strings=strings,
                ngroups=ngroups, groups=groups, blocks=blocks, footer=footer)


def join(n):
    out = bytearray(n['text'])
    out += struct.pack('<IBII', n['version'], n['endian'], n['user'], len(n['blocks']))
    out += struct.pack('<H', len(n['types']))
    for t in n['types']:
        out += struct.pack('<I', len(t)) + t
    out += struct.pack('<%dH' % len(n['tindex']), *n['tindex'])
    out += struct.pack('<%dI' % len(n['blocks']), *[len(b) for b in n['blocks']])
    out += struct.pack('<II', len(n['strings']), max(len(s) for s in n['strings']))
    for s in n['strings']:
        out += struct.pack('<I', len(s)) + s
    out += struct.pack('<I', n['ngroups']) + n['groups']
    for b in n['blocks']:
        out += b
    out += n['footer']
    return bytes(out)


def stream(nbytes_data, count, comps, trailer):
    head = struct.pack('<IIIII', len(nbytes_data), 0, 1, 0, count)
    head += struct.pack('<I', len(comps)) + b''.join(struct.pack('<I', c) for c in comps)
    return bytearray(head + nbytes_data + trailer)


def ring_geometry():
    verts, idx = [], []
    for side, sign in ((0, 1.0), (1, -1.0)):   # outside faces, then inside faces
        base = len(verts)
        for i in range(SEGMENTS + 1):
            a = 2 * math.pi * i / SEGMENTS
            x, y = RADIUS * math.cos(a), RADIUS * math.sin(a)
            nx, ny = sign * math.cos(a), sign * math.sin(a)
            u = U_REPEAT * i / SEGMENTS
            verts.append((x, y, BOTTOM, nx, ny, 0.0, u, 0.0))
            verts.append((x, y, TOP, nx, ny, 0.0, u, (TOP - BOTTOM) / V_TILE))
        for i in range(SEGMENTS):
            b0, t0, b1, t1 = base + 2 * i, base + 2 * i + 1, base + 2 * i + 2, base + 2 * i + 3
            if side == 0:
                idx += [b0, b1, t1, b0, t1, t0]
            else:
                idx += [b0, t1, b1, b0, t0, t1]
    vdata = b''.join(struct.pack('<8f', *v) for v in verts)
    idata = struct.pack('<%dH' % len(idx), *idx)
    return vdata, len(verts), idata, len(idx)


def make_model(template, texture_name, out_path):
    n = split(template)
    strings = n['strings']
    strings[strings.index(b'lava_wall_x')] = b'hardpoint_ring'
    strings[strings.index(b'wall_texture.dds')] = texture_name.encode()
    strings[strings.index(b'Box')] = b'Ring'
    blocks = n['blocks']
    types = [n['types'][i & 0x7FFF] for i in n['tindex']]
    index_block = next(i for i, t in enumerate(types) if t.startswith(b'NiDataStream') and t[13:14] == b'0')
    vertex_block = next(i for i, t in enumerate(types) if t.startswith(b'NiDataStream') and t[13:14] == b'1')
    mesh_block = types.index(b'NiMesh')
    vdata, nverts, idata, nidx = ring_geometry()
    # keep each stream's component formats and trailing "streamable" byte
    def comps_and_trailer(block):
        o = 12
        nreg = struct.unpack_from('<I', block, 8)[0]
        o = 12 + 8 * nreg
        ncomp = struct.unpack_from('<I', block, o)[0]
        comps = list(struct.unpack_from('<%dI' % ncomp, block, o + 4))
        nbytes = struct.unpack_from('<I', block, 0)[0]
        trailer = bytes(block[o + 4 + 4 * ncomp + nbytes:])
        return comps, trailer
    ic, it = comps_and_trailer(blocks[index_block])
    vc, vt = comps_and_trailer(blocks[vertex_block])
    blocks[index_block] = stream(idata, nidx, ic, it)
    blocks[vertex_block] = stream(vdata, nverts, vc, vt)
    # bounding sphere of the mesh (centre z, radius): replace the template's (0, 0, 25000) / 35355
    mesh = blocks[mesh_block]
    old = struct.pack('<f', 25000.0)
    at = bytes(mesh).find(old)
    assert at >= 8, 'bound not found'
    centre = (BOTTOM + TOP) / 2
    radius = math.sqrt(RADIUS ** 2 + ((TOP - BOTTOM) / 2) ** 2)
    struct.pack_into('<fff', mesh, at - 8, 0.0, 0.0, centre)
    struct.pack_into('<f', mesh, at + 4, radius)
    # depth: test on, write OFF (user 2026-10-03: players inside the zone did not see those outside and the other way
    # round). The template's NiZBufferProperty is 0x000F (test + write, func <=): the transparent ring wrote its depth
    # and the characters behind it, drawn later, failed the depth test. 0x000D keeps the test (walls still hide it).
    zbuffer = types.index(b'NiZBufferProperty')
    assert struct.unpack_from('<H', blocks[zbuffer], 12)[0] == 0x000F, 'z buffer flags'
    struct.pack_into('<H', blocks[zbuffer], 12, 0x000D)
    open(out_path, 'wb').write(join(n))


def make_texture(rgb, out_path):
    """128 x 128, tiled (user 2026-10-02: more modern): an energy field - a fine diamond grid on a faint veil, one soft
    glowing band per tile with a white-hot core line, the colour saturated towards the band."""
    from PIL import Image
    w = h = 128
    img = Image.new('RGBA', (w, h))
    px = img.load()
    for y in range(h):
        band = math.exp(-((y - 18) / 9.0) ** 2)            # the soft glow of the band
        core = math.exp(-((y - 18) / 1.6) ** 2)            # its thin white-hot line
        fade = 0.55 + 0.45 * math.exp(-((y - 18) / 40.0) ** 2)  # the veil brighter near the band
        for x in range(w):
            d1, d2 = (x + y) % 32, (x - y) % 32           # diamond grid (seamless every 32 px)
            grid = 0.55 * (math.exp(-(min(d1, 32 - d1) / 0.9) ** 2) + math.exp(-(min(d2, 32 - d2) / 0.9) ** 2))
            glow = min(1.0, 0.18 * fade + 0.9 * band + 0.45 * grid)
            a = int(255 * min(1.0, 0.14 * fade + 0.75 * band + 0.6 * grid + core))
            white = min(1.0, 0.75 * core + 0.25 * band)
            c = tuple(min(255, int(v * glow + 255 * white)) for v in rgb)
            px[x, y] = c + (a,)
    img.save(out_path, 'DDS')

if __name__ == '__main__':
    template, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(os.path.join(out_dir, 'party'), exist_ok=True)
    for name, rgb in (('White', (255, 196, 32)), ('Red', (255, 40, 40)), ('Blue', (30, 120, 255))):  # free = gold
        tex = 'hardpoint_ring_%s.dds' % name.lower()
        make_model(template, tex, os.path.join(out_dir, 'party', 'HardpointRing%s.nif' % name))
        make_texture(rgb, os.path.join(out_dir, 'party', tex))
    print('ok')
