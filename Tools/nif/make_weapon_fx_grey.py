"""Weapon effects the DLL recolours (D75: muzzle flashes, rockets in flight) take their colour from coloured textures
(fire orange, machine gun flash blue): a material tint over them only darkens. This tool makes those effects
recolourable, as make_glow_effects.py does for the level 9 glows: each coloured texture gets a grey copy (its
brightness, same alpha) named with a 'g' as first letter (same length: only the string table's bytes change), and the
materials of the meshes using it are multiplied by the texture's average colour, so the game's own look stays close
(grey x colour). The DLL then tints those materials (white included) with the weapon's colour.

Effects: fx.dat weapon/MuzzleFlame_*.nif, MuzzleEffect_*.nif; weapon.dat Bullet/Rocket_*.nif (the textures are in fx.dat).
Writes the staged copies of fx.dat and weapon.dat.

Usage: python make_weapon_fx_grey.py <ToyBattles folder> <staging folder>
"""
import io
import os
import struct
import sys
import zipfile

from PIL import Image

import nif
from make_glow_effects import blocks_with_offsets
import shutil

from make_item_lock_ui import original_dat
from nif_extract import effective_properties, texturing_refs

TEXTURE_FOLDERS = ('texture/ENG/', 'item/')
KEEP = {'rocket_body.dds', 'rocket_re.dds'}  # the rocket's body keeps its texture
SATURATED = 0.12                              # a texture whose average colour is less saturated stays as it is


def grey_name(name):
    return 'g' + name[1:]


def texture_image(fx, name):
    for folder in TEXTURE_FOLDERS:
        path = folder + name
        if path in fx.namelist():
            return Image.open(io.BytesIO(fx.read(path))).convert('RGBA')
    lower = {n.lower(): n for n in fx.namelist()}
    for folder in TEXTURE_FOLDERS:
        if (folder + name).lower() in lower:
            return Image.open(io.BytesIO(fx.read(lower[(folder + name).lower()]))).convert('RGBA')
    return None


def average_colour(image):
    """Average colour where the texture shows (weighted by brightness x alpha), its brightest channel = 1."""
    total, weight = [0.0, 0.0, 0.0], 0.0
    for r, g, b, a in image.getdata():
        light = max(r, g, b)
        if light == 0:
            continue
        w = light / 255.0 * a / 255.0
        for i, c in enumerate((r, g, b)):
            total[i] += w * c / light
        weight += w
    if weight == 0:
        return (1.0, 1.0, 1.0)
    colour = [c / weight for c in total]
    top = max(colour)
    return tuple(c / top for c in colour)


def grey(image):
    out = Image.new('RGBA', image.size)
    out.putdata([(max(r, g, b),) * 3 + (a,) for r, g, b, a in image.getdata()])
    return out


def parents_of(blocks):
    parents = {}
    for index, (kind, data) in enumerate(blocks):
        if kind not in ('NiNode', 'NiBillboardNode', 'NiSwitchNode', 'NiLODNode', 'NiSortAdjustNode'):
            continue
        try:
            r = nif.avobject(data)[4]
            for child in nif.node_children(r):
                if child >= 0:
                    parents[child] = index
        except Exception:
            pass
    return parents


def base_texture(blocks, strings, texturing):
    offsets = texturing_refs(blocks[texturing][1])
    if not offsets:
        return None
    source = struct.unpack_from('<i', blocks[texturing][1], offsets[0])[0]
    if not 0 <= source < len(blocks) or blocks[source][0] != 'NiSourceTexture':
        return None
    body = blocks[source][1]
    end = 12 + 4 * struct.unpack_from('<I', body, 4)[0]
    if body[end] != 1:
        return None
    index = struct.unpack_from('<i', body, end + 1)[0]
    return strings[index] if 0 <= index < len(strings) else None


def convert(data, fx, greys, path):
    """The nif with its coloured textures renamed to the grey copies and its materials multiplied by their colour."""
    work = os.path.join(os.environ.get('TEMP', '.'), 'weapon_fx_grey.nif')
    open(work, 'wb').write(data)
    blocks, strings = nif.load(work)
    offsets = blocks_with_offsets(data)
    parents = parents_of(blocks)
    out = bytearray(data)
    colours = {}  # texture name -> average colour (None: kept)
    for name in strings:
        if not name.lower().endswith('.dds') or name.lower() in KEEP:
            continue
        image = texture_image(fx, name)
        if image is None:
            continue
        colour = average_colour(image)
        if max(colour) - min(colour) < SATURATED:
            colours[name] = None
            continue
        colours[name] = colour
        greys.setdefault(grey_name(name), grey(image))
    painted = set()
    for index, (kind, _) in enumerate(blocks):
        if kind not in ('NiMesh', 'NiPSParticleSystem'):
            continue
        properties = effective_properties(blocks, index, parents)
        material = next((p for p in properties if blocks[p][0] == 'NiMaterialProperty'), None)
        texturing = next((p for p in properties if blocks[p][0] == 'NiTexturingProperty'), None)
        if material is None or texturing is None or material in painted:
            continue
        colour = colours.get(base_texture(blocks, strings, texturing) or '')
        if colour is None:
            continue
        offset = offsets[material][1]
        for field in (12, 24, 48):  # ambient, diffuse, emissive
            values = struct.unpack_from('<3f', out, offset + field)
            struct.pack_into('<3f', out, offset + field, *(v * c for v, c in zip(values, colour)))
        painted.add(material)
    for name, colour in colours.items():
        if colour is None:
            continue
        encoded = name.encode('latin1')
        assert bytes(out).count(encoded) >= 1, (path, name)
        out = bytearray(bytes(out).replace(struct.pack('<I', len(encoded)) + encoded,
                                           struct.pack('<I', len(encoded)) + grey_name(name).encode('latin1')))
    return bytes(out), len(painted)


def main(game, staging):
    work = os.path.join(staging, 'ui', 'weapon_fx_grey')
    os.makedirs(work, exist_ok=True)
    staged_fx = os.path.join(staging, 'data', 'fx.dat')
    staged_weapon = os.path.join(staging, 'data', 'weapon.dat')
    greys = {}
    with zipfile.ZipFile(original_dat(game, 'fx')) as fx, zipfile.ZipFile(original_dat(game, 'weapon')) as weapon:
        jobs = [('fx.dat', n, fx) for n in fx.namelist()
                if n.startswith('weapon/') and n.split('/')[-1].startswith(('MuzzleFlame', 'MuzzleEffect'))]
        jobs += [('weapon.dat', n, weapon) for n in weapon.namelist()
                 if n.startswith('Bullet/') and n.split('/')[-1].startswith('Rocket_') and n.endswith('.nif')]
        outputs = []
        for dat, name, archive in jobs:
            converted, materials = convert(archive.read(name), fx, greys, name)
            path = os.path.join(work, name.replace('/', '_'))
            open(path, 'wb').write(converted)
            outputs.append((dat, name, path))
            print(dat, name, 'materials', materials)
    files = {'fx.dat': {}, 'weapon.dat': {}}
    for name, image in greys.items():
        path = os.path.join(work, name)
        image.save(path)
        for folder in TEXTURE_FOLDERS:
            files['fx.dat'][folder + name] = path
    for dat, name, path in outputs:
        files[dat][name] = path
    update_dat(original_dat(game, 'fx'), staged_fx, files['fx.dat'])
    update_dat(original_dat(game, 'weapon'), staged_weapon, files['weapon.dat'])
    print('grey textures', sorted(greys))


def update_dat(game_dat, staged_dat, files):
    """The staged archive (a copy of the game's when missing) with `files` (archive name -> path) replaced or added,
    in one rewrite."""
    if not os.path.exists(staged_dat):
        shutil.copyfile(game_dat, staged_dat)
    tmp = staged_dat + '.new'
    with zipfile.ZipFile(staged_dat) as zin, zipfile.ZipFile(tmp, 'w', compression=zipfile.ZIP_DEFLATED) as zout:
        present = set()
        for info in zin.infolist():
            present.add(info.filename)
            data = open(files[info.filename], 'rb').read() if info.filename in files else zin.read(info.filename)
            out = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            out.compress_type = zipfile.ZIP_STORED if info.is_dir() else zipfile.ZIP_DEFLATED
            out.external_attr = info.external_attr
            zout.writestr(out, data)
        for name, path in files.items():
            if name not in present:
                zout.write(path, name)
    os.replace(tmp, staged_dat)
    with zipfile.ZipFile(staged_dat) as z:
        assert z.testzip() is None


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
