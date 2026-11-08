"""Domination zone models (party game 10, D85): party/DominationZone{A,B,C}{White,Red,Blue}.nif + their textures.

One mesh per model, built like the Hardpoint ring (make_hardpoint_ring.py: party/LavaWallX.nif's block structure,
NiAlphaProperty, NiMaterialProperty, depth test on / write off): the node "domination_zone" holds
  - a square of low walls (user 2026-10-03: "a square whose edges are red on the floor; the edges must not go very
    high"): side 2 x HALF (= server DominationMode::zoneHalfSize), WALL_HEIGHT high, double-sided, a glow brightest at
    the floor;
  - the zone's letter flat on the floor in the middle (LETTER_SIZE, just above the floor).
One texture per model (256 x 256): rows 0..63 the wall glow (the walls map to its middle column), rows 64..255 the
letter. White = neutral, red / blue = the owner (the DLL shows one of the three at the zone and blinks red / blue
while contested).

Usage: python make_domination_zones.py <LavaWallX.nif> <staging>   (writes <staging>/party/)
"""
import math
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
from make_hardpoint_ring import join, split, stream  # noqa: E402

HALF = 500.0          # server DominationMode::zoneHalfSize
WALL_HEIGHT = 60.0    # players are ~150-200 tall
LETTER_SIZE = 380.0
LETTER_LIFT = 3.0
TEX = 256
WALL_ROWS = 64        # texture rows 0..63: the wall glow; 64..255: the letter
COLOURS = {'White': (235, 235, 235), 'Red': (255, 50, 50), 'Blue': (40, 130, 255)}


def geometry():
    verts, idx = [], []

    def quad(a, b, c, d, normal, uvs, double):
        base = len(verts)
        for p, uv in zip((a, b, c, d), uvs):
            verts.append(p + normal + uv)
        idx.extend([base, base + 1, base + 2, base, base + 2, base + 3])
        if double:
            idx.extend([base, base + 2, base + 1, base, base + 3, base + 2])

    u = 0.5
    v_top, v_bottom = 1.0 / TEX, (WALL_ROWS - 1.0) / TEX  # the glow column: top of the wall .. the floor
    h, top = HALF, WALL_HEIGHT
    corners = [(-h, -h), (h, -h), (h, h), (-h, h)]
    for i in range(4):
        (x0, y0), (x1, y1) = corners[i], corners[(i + 1) % 4]
        nx, ny = (y1 - y0) / (2 * h), -(x1 - x0) / (2 * h)
        quad((x0, y0, 0.0), (x1, y1, 0.0), (x1, y1, top), (x0, y0, top), (nx, ny, 0.0),
             [(u, v_bottom), (u, v_bottom), (u, v_top), (u, v_top)], True)
    s = LETTER_SIZE / 2
    z = LETTER_LIFT
    v0, v1 = WALL_ROWS / TEX, 1.0
    u0, u1 = 32.0 / TEX, 224.0 / TEX
    quad((-s, -s, z), (s, -s, z), (s, s, z), (-s, s, z), (0.0, 0.0, 1.0), [(u0, v1), (u1, v1), (u1, v0), (u0, v0)], True)
    vdata = b''.join(struct.pack('<8f', *v) for v in verts)
    idata = struct.pack('<%dH' % len(idx), *idx)
    return vdata, len(verts), idata, len(idx)


def make_model(template, texture_name, out_path):
    n = split(template)
    strings = n['strings']
    strings[strings.index(b'lava_wall_x')] = b'domination_zone'
    strings[strings.index(b'wall_texture.dds')] = texture_name.encode()
    strings[strings.index(b'Box')] = b'Zone'
    blocks = n['blocks']
    types = [n['types'][i & 0x7FFF] for i in n['tindex']]
    index_block = next(i for i, t in enumerate(types) if t.startswith(b'NiDataStream') and t[13:14] == b'0')
    vertex_block = next(i for i, t in enumerate(types) if t.startswith(b'NiDataStream') and t[13:14] == b'1')
    mesh_block = types.index(b'NiMesh')
    vdata, nverts, idata, nidx = geometry()

    def comps_and_trailer(block):
        nreg = struct.unpack_from('<I', block, 8)[0]
        o = 12 + 8 * nreg
        ncomp = struct.unpack_from('<I', block, o)[0]
        comps = list(struct.unpack_from('<%dI' % ncomp, block, o + 4))
        nbytes = struct.unpack_from('<I', block, 0)[0]
        return comps, bytes(block[o + 4 + 4 * ncomp + nbytes:])

    ic, it = comps_and_trailer(blocks[index_block])
    vc, vt = comps_and_trailer(blocks[vertex_block])
    blocks[index_block] = stream(idata, nidx, ic, it)
    blocks[vertex_block] = stream(vdata, nverts, vc, vt)
    mesh = blocks[mesh_block]
    at = bytes(mesh).find(struct.pack('<f', 25000.0))
    assert at >= 8, 'bound not found'
    struct.pack_into('<fff', mesh, at - 8, 0.0, 0.0, WALL_HEIGHT / 2)
    struct.pack_into('<f', mesh, at + 4, math.sqrt(2 * HALF * HALF + (WALL_HEIGHT / 2) ** 2))
    zbuffer = types.index(b'NiZBufferProperty')
    assert struct.unpack_from('<H', blocks[zbuffer], 12)[0] == 0x000F, 'z buffer flags'
    struct.pack_into('<H', blocks[zbuffer], 12, 0x000D)  # depth test, no depth write (see make_hardpoint_ring.py)
    open(out_path, 'wb').write(join(n))


def letter_font(size):
    for name in ('BRLNSDB.TTF', 'arialbd.ttf', 'Arial Bold.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def make_texture(letter, rgb, out_path):
    img = Image.new('RGBA', (TEX, TEX), (0, 0, 0, 0))
    px = img.load()
    for y in range(WALL_ROWS):  # 0 = top of the wall, 63 = the floor: brightest and opaque at the floor
        t = y / (WALL_ROWS - 1)
        a = int(255 * (0.25 + 0.75 * t ** 1.5))
        white = max(0.0, (t - 0.85) / 0.15) * 0.6
        c = tuple(min(255, int(v * (0.7 + 0.3 * t) + 255 * white)) for v in rgb)
        for x in range(TEX):
            px[x, y] = c + (a,)
    # the letter: the colour with a dark outline and a soft glow, in rows 64..255 (192 x 192 at x 32..223)
    layer = Image.new('RGBA', (192, 192), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    font = letter_font(170)
    box = draw.textbbox((0, 0), letter, font=font, stroke_width=8)
    pos = ((192 - (box[2] - box[0])) // 2 - box[0], (192 - (box[3] - box[1])) // 2 - box[1])
    glow = Image.new('RGBA', (192, 192), (0, 0, 0, 0))
    ImageDraw.Draw(glow).text(pos, letter, font=font, fill=rgb + (200,), stroke_width=14, stroke_fill=rgb + (200,))
    glow = glow.filter(ImageFilter.GaussianBlur(8))
    draw.text(pos, letter, font=font, fill=rgb + (255,), stroke_width=8, stroke_fill=(20, 20, 30, 255))
    img.alpha_composite(Image.alpha_composite(glow, layer), (32, WALL_ROWS))
    img.save(out_path, 'DDS')
    return img


if __name__ == '__main__':
    template, staging = sys.argv[1], sys.argv[2]
    out = os.path.join(staging, 'party')
    os.makedirs(out, exist_ok=True)
    previews = []
    for letter in 'ABC':
        for name, rgb in COLOURS.items():
            texture = 'domination_zone_%s_%s.dds' % (letter.lower(), name.lower())
            make_model(template, texture, os.path.join(out, 'DominationZone%s%s.nif' % (letter, name)))
            previews.append(make_texture(letter, rgb, os.path.join(out, texture)))
    sheet = Image.new('RGBA', (TEX * 3, TEX * 3), (40, 40, 40, 255))
    for i, img in enumerate(previews):
        sheet.alpha_composite(img, ((i % 3) * TEX, (i // 3) * TEX))
    sheet.save(os.path.join(staging, 'domination_zones_preview.png'))
    print('ok')
