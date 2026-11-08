"""Cut objects out of a map's World.nif 1:1: the object's meshes with their original vertex data (every component:
positions, normals, vertex colours, both texture coordinate sets) and their original properties (texturing with the base
texture and the lightmap, material, vertex colour, alpha, stencil, z-buffer; the ones inherited from the parent nodes
included), written as a new .nif built on a template (party/LavaWallX.nif: header, root, the node the meshes hang on).

NIF 20.6.0.0 layouts used (checked on every NiMesh of ToyBattles' maps 1, 6, 12, 22, 40: 711 meshes parsed exactly):
  NiObjectNET   name (string), extra data (count + refs), controller (ref)
  NiAVObject    + flags (u16), translation (3f), rotation (9f), scale (f), properties (count + refs), collision (ref)
  NiRenderObject+ materials (count + (name string, extra u32)), active material (i32), material needs update (u8)
  NiMesh        + primitive type (u32), submeshes (u16), instancing (u8), bound (4f), streams (count + (ref,
                  per-instance u8, submesh count u16 + map u16s, components count + (semantic string, index u32))),
                  modifiers (count + refs)
  NiDataStream  bytes (u32), cloning (u32), regions (count + (start, count)), component formats (count + u32s), data,
                  streamable (u8)
  NiSourceTexture   NiObjectNET + use external (u8) + file name (string) + link (ref) + the rest
  NiTexturingProperty  NiObjectNET + flags (u16) + texture count (u32) + slots (has u8 + TexDesc: source ref, flags u16,
                  max anisotropy u16, has transform u8 [+ 2f 2f f u32 2f])
"""
import math
import struct

import nif
from make_hardpoint_ring import join, split

SLOTS = ('base', 'dark', 'detail', 'gloss', 'glow', 'bump', 'normal', 'parallax')


# ---------------------------------------------------------------- reading

def parse_mesh(data):
    r = nif.R(data)
    m = {'name': r.i32()}
    n = r.u32(); m['extras'] = [r.i32() for _ in range(n)]
    m['controller'] = r.i32()
    m['flags'] = r.u16()
    m['transform'] = r.f(13)
    n = r.u32(); m['props'] = [r.i32() for _ in range(n)]
    m['collision'] = r.i32()
    n = r.u32(); m['materials'] = [(r.i32(), r.u32()) for _ in range(n)]
    m['active_material'] = r.i32()
    m['material_update'] = r.u8()
    m['primitive'] = r.u32()
    m['submeshes'] = r.u16()
    m['instancing'] = r.u8()
    m['bound'] = r.f(4)
    m['streams'] = []
    for _ in range(r.u32()):
        ref = r.i32(); per = r.u8(); nsub = r.u16(); submap = [r.u16() for _ in range(nsub)]
        comps = [(r.i32(), r.u32()) for _ in range(r.u32())]
        m['streams'].append(dict(ref=ref, per=per, submap=submap, comps=comps))
    m['modifiers'] = [r.i32() for _ in range(r.u32())]
    if r.o != len(data):
        raise ValueError('NiMesh layout: %d of %d bytes' % (r.o, len(data)))
    return m


def parse_stream(data):
    r = nif.R(data)
    nbytes = r.u32(); cloning = r.u32()
    regions = [(r.u32(), r.u32()) for _ in range(r.u32())]
    formats = [r.u32() for _ in range(r.u32())]
    raw = r.raw(nbytes)
    return dict(cloning=cloning, regions=regions, formats=formats, raw=raw, trailer=bytes(data[r.o:]),
                count=sum(c for _, c in regions))


def component_bytes(fmt):
    """count (bits 16..23) x element size (low byte: 0x36 / 0x37 float, 0x15 u16, 0x13 u32?, the byte formats 1)."""
    count, kind = (fmt >> 16) & 0xFF, fmt & 0xFF
    return count * (4 if kind in (0x36, 0x37, 0x13, 0x17) else 2 if kind in (0x15, 0x16) else 1)


def object_net_end(data):
    """Offset after the NiObjectNET header."""
    n = struct.unpack_from('<I', data, 4)[0]
    return 12 + 4 * n


def texturing_refs(data):
    """Offsets of the texture references (TexDesc sources) of a NiTexturingProperty; the shader maps are not handled
    (None when the property has some: the caller skips the object)."""
    r = nif.R(data)
    r.o = object_net_end(data)
    r.u16(); r.u32()
    offsets = []
    for slot in SLOTS:
        if not r.u8():
            continue
        offsets.append(r.o)
        r.i32(); r.u16(); r.u16()
        if r.u8():
            r.f(2); r.f(2); r.f(); r.u32(); r.f(2)
        if slot == 'bump':
            r.f(2); r.f(4)
        elif slot == 'parallax':
            r.f()
    # decals (texture count > 8 ...), shader maps: anything left must be "no decal, no shader map"
    rest = data[r.o:]
    if any(rest):
        return None
    return offsets


# ---------------------------------------------------------------- writing

class Writer:
    """A .nif built from the template: blocks appended, strings and block types added on demand."""

    def __init__(self, template):
        self.n = split(template)
        self.types = self.n['types']
        self.strings = self.n['strings']
        self.copied = {}  # source block index -> new index (properties shared by parts)

    def string(self, text):
        if isinstance(text, str):
            text = text.encode('latin1')
        if text not in self.strings:
            self.strings.append(text)
        return self.strings.index(text)

    def add_block(self, type_name, data):
        if isinstance(type_name, str):
            type_name = type_name.encode('latin1')
        if type_name not in self.types:
            self.types.append(type_name)
        self.n['tindex'].append(self.types.index(type_name))
        self.n['blocks'].append(bytearray(data))
        return len(self.n['blocks']) - 1

    def save(self, path):
        open(path, 'wb').write(join(self.n))


def remapped_net_header(data, source_strings, writer):
    """NiObjectNET header with the name re-registered, no extra data, no controller."""
    name = struct.unpack_from('<i', data, 0)[0]
    new_name = writer.string(source_strings[name]) if 0 <= name < len(source_strings) else -1
    return struct.pack('<iIi', new_name, 0, -1), object_net_end(data)


def copy_property(index, blocks, strings, writer, texture_file):
    """A property block (and the source textures it references) copied once; texture_file(name) -> the file name to
    use (None: keep)."""
    if index in writer.copied:
        return writer.copied[index]
    type_name, data = blocks[index]
    header, end = remapped_net_header(data, strings, writer)
    body = bytearray(data[end:])
    if type_name == 'NiTexturingProperty':
        offsets = texturing_refs(data)
        if offsets is None:
            raise ValueError('texturing property with shader maps')
        for offset in offsets:
            source = struct.unpack_from('<i', data, offset)[0]
            new_source = copy_source(source, blocks, strings, writer, texture_file) if source >= 0 else -1
            struct.pack_into('<i', body, offset - end, new_source)
    new = writer.add_block(type_name, header + bytes(body))
    writer.copied[index] = new
    return new


def copy_source(index, blocks, strings, writer, texture_file):
    if index in writer.copied:
        return writer.copied[index]
    type_name, data = blocks[index]
    if type_name != 'NiSourceTexture':
        raise ValueError('texture reference to %s' % type_name)
    header, end = remapped_net_header(data, strings, writer)
    body = bytearray(data[end:])
    if body[0] != 1:
        raise ValueError('embedded texture')
    name_index = struct.unpack_from('<i', body, 1)[0]
    name = strings[name_index] if 0 <= name_index < len(strings) else ''
    struct.pack_into('<i', body, 1, writer.string(texture_file(name) or name))
    struct.pack_into('<i', body, 5, -1)  # link
    new = writer.add_block(type_name, header + bytes(body))
    writer.copied[index] = new
    return new


def rotate(rot, v):
    return (rot[0] * v[0] + rot[1] * v[1] + rot[2] * v[2],
            rot[3] * v[0] + rot[4] * v[1] + rot[5] * v[2],
            rot[6] * v[0] + rot[7] * v[1] + rot[8] * v[2])


def add_part(writer, blocks, strings, mesh_index, world, effective_props, used, tris, origin, texture_file, name):
    """One mesh of the object: the triangles `tris` (vertex indices of the source mesh, `used` = its vertices) with the
    source mesh's vertex data, positions / normals moved to the object (`origin` = bottom centre), properties copied."""
    mesh = parse_mesh(blocks[mesh_index][1])
    t, rot, scale = world
    remap = {v: k for k, v in enumerate(used)}
    new_streams = []
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for entry in mesh['streams']:
        type_name, data = blocks[entry['ref']]
        stream = parse_stream(data)
        semantics = [strings[s] for s, _ in entry['comps']]
        if semantics == ['INDEX']:
            width = component_bytes(stream['formats'][0])
            fmt = '<H' if width == 2 else '<I'
            if fmt == '<H' and len(used) > 65535:
                raise ValueError('too many vertices')
            raw = b''.join(struct.pack(fmt, remap[v]) for tri in tris for v in tri)
            count = 3 * len(tris)
        else:
            stride = len(stream['raw']) // stream['count']
            offsets = []
            o = 0
            for f in stream['formats']:
                offsets.append(o)
                o += component_bytes(f)
            out = bytearray()
            for v in used:
                vertex = bytearray(stream['raw'][v * stride:(v + 1) * stride])
                for k, semantic in enumerate(semantics):
                    if semantic == 'POSITION':
                        p = struct.unpack_from('<3f', vertex, offsets[k])
                        w = nif.transform_point(p, t, rot, scale)
                        local = (w[0] - origin[0], w[1] - origin[1], w[2] - origin[2])
                        struct.pack_into('<3f', vertex, offsets[k], *local)
                        lo = [min(lo[a], local[a]) for a in range(3)]
                        hi = [max(hi[a], local[a]) for a in range(3)]
                    elif semantic == 'NORMAL' or semantic in ('BINORMAL', 'TANGENT'):
                        nx, ny, nz = rotate(rot, struct.unpack_from('<3f', vertex, offsets[k]))
                        length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                        struct.pack_into('<3f', vertex, offsets[k], nx / length, ny / length, nz / length)
                out += vertex
            raw = bytes(out)
            count = len(used)
        block = struct.pack('<III', len(raw), stream['cloning'], 1) + struct.pack('<II', 0, count)
        block += struct.pack('<I', len(stream['formats'])) + b''.join(struct.pack('<I', f) for f in stream['formats'])
        block += raw + stream['trailer']
        new_streams.append((writer.add_block(cpu_readable(type_name), block), entry))
    props = [copy_property(p, blocks, strings, writer, texture_file) for p in effective_props]
    centre = [(lo[a] + hi[a]) / 2 for a in range(3)]
    radius = math.sqrt(sum((hi[a] - lo[a]) ** 2 for a in range(3))) / 2
    out = struct.pack('<iIi', writer.string(name), 0, -1)
    out += struct.pack('<H', mesh['flags'])
    out += struct.pack('<13f', 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1)
    out += struct.pack('<I', len(props)) + b''.join(struct.pack('<i', p) for p in props)
    collision_at = len(out)
    out += struct.pack('<i', -1)  # the collision object, set below
    out += struct.pack('<I', len(mesh['materials'])) + b''.join(
        struct.pack('<iI', writer.string(strings[s]) if 0 <= s < len(strings) else -1, e) for s, e in mesh['materials'])
    out += struct.pack('<iB', mesh['active_material'], mesh['material_update'])
    out += struct.pack('<IHB', mesh['primitive'], 1, 0)
    out += struct.pack('<4f', centre[0], centre[1], centre[2], radius)
    out += struct.pack('<I', len(new_streams))
    for ref, entry in new_streams:
        out += struct.pack('<iBHH', ref, entry['per'], 1, 0)
        out += struct.pack('<I', len(entry['comps'])) + b''.join(
            struct.pack('<iI', writer.string(strings[s]), i) for s, i in entry['comps'])
    out += struct.pack('<I', 0)
    index = writer.add_block('NiMesh', out)
    # Prop Hunt hit test (2026-10-02): the game tests a player's hit parts in its NiPick's collision mode, which only
    # hits a node with a NiCollisionData (+0xA8; 0xC13D70: its world bounding volume); the characters' body parts
    # have one, so every part of the object gets a box of its exact bounds (the World.nif layout: target, propagation
    # 3 = never, collision mode 2 = use the bounding volume, has volume 1, volume type 1 = box: centre, axes, extents).
    half = [(hi[a] - lo[a]) / 2 for a in range(3)]
    box = struct.pack('<iIIBI', index, 3, 2, 1, 1) + struct.pack('<3f', *centre) +         struct.pack('<9f', 1, 0, 0, 0, 1, 0, 0, 0, 1) + struct.pack('<3f', *[max(h, 1.0) for h in half])
    collision = writer.add_block('NiCollisionData', box)
    struct.pack_into('<i', writer.n['blocks'][index], collision_at, collision)
    return index


def cpu_readable(type_name):
    """NiDataStream type names carry the usage and the access mask: "NiDataStream<usage><access>". The prop
    must be pickable (the hit test, Prop Hunt: the game's NiPick reads the positions and indices on the CPU), so the
    access mask gets CPU_READ (1); some of the maps' streams are GPU only (access 18)."""
    parts = type_name.split('')
    if len(parts) == 3 and parts[0] == 'NiDataStream' and parts[2].isdigit():
        parts[2] = str(int(parts[2]) | 1)
        return ''.join(parts)
    return type_name


def effective_properties(blocks, mesh_index, parents):
    """The mesh's properties and the ones inherited from its parent nodes (the nearest of each type wins)."""
    chain = [mesh_index]
    k = parents.get(mesh_index)
    while k is not None:
        chain.append(k)
        k = parents.get(k)
    chosen = {}
    for node in chain:
        r = nif.R(blocks[node][1])
        r.i32(); n = r.u32(); r.o += 4 * n; r.i32(); r.u16(); r.f(13)
        for p in [r.i32() for _ in range(r.u32())]:
            if 0 <= p < len(blocks) and blocks[p][0] not in chosen:
                chosen[blocks[p][0]] = p
    return list(chosen.values())


def write_object(template, parts, origin, out_path, texture_file=lambda name: None, node_name=b'prop'):
    """parts: [(blocks, strings, mesh index, world transform, effective props, used vertices, triangles)]. The template's
    node holding its mesh gets the parts' meshes instead (the template's own mesh blocks stay unused in the file)."""
    writer = Writer(template)
    strings = writer.strings
    strings[strings.index(b'lava_wall_x')] = node_name
    meshes = [add_part(writer, blocks, s, i, world, props, used, tris, origin, texture_file, 'PropMesh%d' % k)
              for k, (blocks, s, i, world, props, used, tris) in enumerate(parts)]
    blocks = writer.n['blocks']
    types = [writer.types[i & 0x7FFF] for i in writer.n['tindex']]
    template_mesh = types.index(b'NiMesh')
    node = next(i for i, t in enumerate(types) if t == b'NiNode' and struct.pack('<ii', 1, template_mesh) in bytes(blocks[i]))
    data = bytes(blocks[node])
    at = data.find(struct.pack('<ii', 1, template_mesh))
    blocks[node] = bytearray(data[:at] + struct.pack('<I', len(meshes)) + b''.join(struct.pack('<i', m) for m in meshes)
                             + data[at + 8:])
    writer.save(out_path)


def write_objects(template, objects, out_path):
    """Every object of a map in one file (2026-10-02: the DLL reads it once per map; loading the objects one file each
    read the shared textures - atlases, lightmaps - once per object and froze the game for seconds). objects: [(parts,
    origin)] as write_object's; object k is the node "prop_KK" (a copy of the template's node) holding its meshes, all
    of them children of the template's node, renamed "prop". One writer: the properties and textures are shared."""
    writer = Writer(template)
    strings = writer.strings
    strings[strings.index(b'lava_wall_x')] = b'prop'
    blocks = writer.n['blocks']
    types = [writer.types[i & 0x7FFF] for i in writer.n['tindex']]
    template_mesh = types.index(b'NiMesh')
    node = next(i for i, t in enumerate(types) if t == b'NiNode' and struct.pack('<ii', 1, template_mesh) in bytes(blocks[i]))
    node_data = bytes(blocks[node])
    children_at = node_data.find(struct.pack('<ii', 1, template_mesh))
    object_nodes = []
    for k, (parts, origin) in enumerate(objects):
        meshes = [add_part(writer, b, s, i, world, props, used, tris, origin, lambda name: None, 'PropMesh%d' % n)
                  for n, (b, s, i, world, props, used, tris) in enumerate(parts)]
        data = bytearray(node_data[:children_at] + struct.pack('<I', len(meshes)) +
                         b''.join(struct.pack('<i', m) for m in meshes) + node_data[children_at + 8:])
        struct.pack_into('<i', data, 0, writer.string('prop_%02d' % k))
        object_nodes.append(writer.add_block('NiNode', bytes(data)))
    blocks = writer.n['blocks']
    blocks[node] = bytearray(node_data[:children_at] + struct.pack('<I', len(object_nodes)) +
                             b''.join(struct.pack('<i', m) for m in object_nodes) + node_data[children_at + 8:])
    writer.save(out_path)
