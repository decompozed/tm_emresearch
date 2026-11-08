"""Software renderer for checking NiMesh objects without the game (like a NifSkope look): per-pixel texturing with the
base texture (texture coordinates set 0) multiplied by the lightmap (detail slot, set 1) when present, z-buffered,
seen from the front-left. Reads the meshes' own streams and properties (nif_extract layouts).

Usage (library): render_meshes(blocks, strings, [(mesh index, world transform)], texture_dir, size) -> PIL image
"""
import math
import os
import struct

from PIL import Image

import nif
from nif_extract import component_bytes, object_net_end, parse_mesh, parse_stream, texturing_refs

_CACHE = {}


def load_texture(texture_dir, name):
    key = (texture_dir, (name or '').lower())
    if key not in _CACHE:
        image = None
        if name:
            files = {f.lower(): f for f in os.listdir(texture_dir)}
            path = files.get(name.lower())
            if path:
                try:
                    image = Image.open(os.path.join(texture_dir, path)).convert('RGB')
                except Exception:
                    image = None
        _CACHE[key] = image
    return _CACHE[key]


def mesh_textures(blocks, strings, mesh):
    """{slot index: (file name, uv set)} of the mesh's texturing property (slot 0 = base, 2 = detail: the lightmap)."""
    out = {}
    for p in mesh['props']:
        if not (0 <= p < len(blocks)) or blocks[p][0] != 'NiTexturingProperty':
            continue
        data = blocks[p][1]
        r = nif.R(data)
        r.o = object_net_end(data)
        r.u16(); r.u32()
        for slot in range(8):
            if not r.u8():
                continue
            source = r.i32(); flags = r.u16(); r.u16()
            if r.u8():
                r.f(2); r.f(2); r.f(); r.u32(); r.f(2)
            if slot == 5:
                r.f(2); r.f(4)
            elif slot == 7:
                r.f()
            if 0 <= source < len(blocks) and blocks[source][0] == 'NiSourceTexture':
                sd = blocks[source][1]
                e = object_net_end(sd)
                name_index = struct.unpack_from('<i', sd, e + 1)[0]
                out[slot] = (strings[name_index] if 0 <= name_index < len(strings) else None, flags & 0xFF)
    return out


def mesh_vertices(blocks, strings, mesh, world):
    """(positions in world space, uv sets {index: [uv]}, colours or None, triangles)."""
    t, rot, scale = world
    positions, uvs, colours, triangles = [], {}, None, []
    for entry in mesh['streams']:
        stream = parse_stream(blocks[entry['ref']][1])
        semantics = [(strings[s], i) for s, i in entry['comps']]
        if semantics[0][0] == 'INDEX':
            width = component_bytes(stream['formats'][0])
            fmt = '<H' if width == 2 else '<I'
            ids = [struct.unpack_from(fmt, stream['raw'], k * width)[0] for k in range(len(stream['raw']) // width)]
            triangles = [tuple(ids[k:k + 3]) for k in range(0, len(ids) - 2, 3)]
            continue
        stride = len(stream['raw']) // stream['count']
        offset = 0
        for (semantic, index), fmt in zip(semantics, stream['formats']):
            size = component_bytes(fmt)
            values = [stream['raw'][v * stride + offset:v * stride + offset + size] for v in range(stream['count'])]
            if semantic == 'POSITION':
                positions = [nif.transform_point(struct.unpack('<3f', b), t, rot, scale) for b in values]
            elif semantic == 'TEXCOORD':
                uvs[index] = [struct.unpack('<2f', b) for b in values]
            elif semantic == 'COLOR' and size == 4:
                colours = [tuple(b[:3]) for b in values]
            offset += size
    return positions, uvs, colours, triangles


def render_meshes(blocks, strings, meshes, texture_dir, size=256, use_colours=False):
    """meshes: [(mesh index, world transform)]; the image of all of them together."""
    items = []
    for index, world in meshes:
        mesh = parse_mesh(blocks[index][1])
        positions, uvs, colours, triangles = mesh_vertices(blocks, strings, mesh, world)
        textures = mesh_textures(blocks, strings, mesh)
        base = textures.get(0)
        light = textures.get(2)
        items.append((positions, uvs, colours, triangles,
                      (load_texture(texture_dir, base[0]), base[1]) if base else (None, 0),
                      (load_texture(texture_dir, light[0]), light[1]) if light else (None, 1)))
    points = [p for item in items for p in item[0]]
    if not points:
        return Image.new('RGB', (size, size), (40, 44, 52))
    lo = [min(p[a] for p in points) for a in range(3)]
    hi = [max(p[a] for p in points) for a in range(3)]
    centre = [(lo[a] + hi[a]) / 2 for a in range(3)]
    extent = max(hi[a] - lo[a] for a in range(3)) or 1.0
    ca, sa = math.cos(math.radians(35)), math.sin(math.radians(35))

    def project(p):
        x, y, z = p[0] - centre[0], p[1] - centre[1], p[2] - centre[2]
        u = x * ca - y * sa
        depth = x * sa + y * ca
        v = z * 0.9 + depth * 0.3
        return (size / 2 + u / extent * size * 0.8, size / 2 - v / extent * size * 0.8, depth)

    image = Image.new('RGB', (size, size), (40, 44, 52))
    pixels = image.load()
    zbuf = [[-1e30] * size for _ in range(size)]
    for positions, uvs, colours, triangles, (base, base_set), (light, light_set) in items:
        projected = [project(p) for p in positions]
        base_uv = uvs.get(base_set) or uvs.get(0)
        light_uv = uvs.get(light_set)
        for a, b, c in triangles:
            (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = projected[a], projected[b], projected[c]
            det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(det) < 1e-9:
                continue
            for py in range(max(0, int(min(y0, y1, y2))), min(size, int(max(y0, y1, y2)) + 1)):
                for px in range(max(0, int(min(x0, x1, x2))), min(size, int(max(x0, x1, x2)) + 1)):
                    l0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / det
                    l1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / det
                    l2 = 1 - l0 - l1
                    if l0 < 0 or l1 < 0 or l2 < 0:
                        continue
                    depth = l0 * z0 + l1 * z1 + l2 * z2
                    if depth <= zbuf[py][px]:
                        continue
                    zbuf[py][px] = depth
                    colour = (200, 200, 200)
                    if base is not None and base_uv:
                        u = (l0 * base_uv[a][0] + l1 * base_uv[b][0] + l2 * base_uv[c][0]) % 1.0
                        v = (l0 * base_uv[a][1] + l1 * base_uv[b][1] + l2 * base_uv[c][1]) % 1.0
                        colour = base.getpixel((min(base.width - 1, int(u * base.width)), min(base.height - 1, int(v * base.height))))
                    if light is not None and light_uv:
                        u = (l0 * light_uv[a][0] + l1 * light_uv[b][0] + l2 * light_uv[c][0]) % 1.0
                        v = (l0 * light_uv[a][1] + l1 * light_uv[b][1] + l2 * light_uv[c][1]) % 1.0
                        lm = light.getpixel((min(light.width - 1, int(u * light.width)), min(light.height - 1, int(v * light.height))))
                        colour = tuple(min(255, colour[k] * lm[k] * 2 // 255) for k in range(3))
                    if use_colours and colours:
                        vc = [l0 * colours[a][k] + l1 * colours[b][k] + l2 * colours[c][k] for k in range(3)]
                        colour = tuple(int(colour[k] * vc[k] / 255) for k in range(3))
                    pixels[px, py] = colour
    return image
