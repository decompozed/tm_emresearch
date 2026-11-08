"""Weapon glow effects that the DLL can recolour (D75). The DLL tints the effect's materials and particle colour keys, but
two level 9 effects take their colour from a coloured texture, which a tint only darkens:
  - level 9 point (MP) glows: heka_a.dds (blue)    -> heka_e.dds (the grey glow of level 8; the material stays blue)
  - level 9 cash (RT) stars:  attack_red.dds (red) -> attack_gry.dds (new: its brightness in grey), the stars'
    material (emissive 1, 0.27, 0.87) set to the rays' red so the game's own look stays close
heka_a.dds and attack_red.dds themselves are not changed (other effects use heka_a). Writes into a staged copy of fx.dat.

Usage: python make_glow_effects.py <ToyBattles folder> <staging folder>
"""
import io
import os
import struct
import sys
import zipfile

from PIL import Image

from make_item_lock_ui import add_to_ui_dat, original_dat

STAR_MATERIAL_EMISSIVE = (1.0, 0.27, 0.87)
GREY_STAR = 'attack_gry.dds'
TEXTURE_FOLDERS = ('texture/ENG/', 'item/')


def blocks_with_offsets(data):
    """(type, offset, size) of every block (Gamebryo 20.6 header, as Tools/nif/nif.py reads it)."""
    o = data.index(b'\n') + 1
    o += 4 + 1 + 4  # version, endian, user version
    nblocks = struct.unpack_from('<I', data, o)[0]; o += 4
    ntypes = struct.unpack_from('<H', data, o)[0]; o += 2
    types = []
    for _ in range(ntypes):
        n = struct.unpack_from('<I', data, o)[0]; o += 4
        types.append(data[o:o + n].decode()); o += n
    tindex = struct.unpack_from('<%dH' % nblocks, data, o); o += 2 * nblocks
    sizes = struct.unpack_from('<%dI' % nblocks, data, o); o += 4 * nblocks
    nstrings = struct.unpack_from('<I', data, o)[0]; o += 8
    for _ in range(nstrings):
        o += 4 + struct.unpack_from('<I', data, o)[0]
    ngroups = struct.unpack_from('<I', data, o)[0]; o += 4 + 4 * ngroups
    out = []
    for i in range(nblocks):
        out.append((types[tindex[i] & 0x7FFF], o, sizes[i]))
        o += sizes[i]
    return out


def star_texture(game_fx):
    """attack_red.dds in grey (its brightness, normalised) and its average colour (where it is bright)."""
    red = Image.open(io.BytesIO(game_fx.read('texture/ENG/attack_red.dds'))).convert('RGBA')
    grey = Image.new('RGBA', red.size)
    src, dst = red.load(), grey.load()
    total, weight = [0.0, 0.0, 0.0], 0.0
    for y in range(red.size[1]):
        for x in range(red.size[0]):
            r, g, b, a = src[x, y]
            light = max(r, g, b)
            dst[x, y] = (light, light, light, a)
            w = (light / 255.0) * (a / 255.0)
            if light > 0:
                for i, c in enumerate((r, g, b)):
                    total[i] += w * c / light
                weight += w
    average = tuple(round(c / weight, 3) for c in total) if weight else (1.0, 0.0, 0.0)
    # the rays are red, the core white x the pink material: a red a little toward pink keeps the game's look closest
    return grey, (1.0, round(min(average[1], 0.3), 3), round(min(average[2], 0.35), 3))


def patch_star_material(data, color):
    """The material whose emissive is STAR_MATERIAL_EMISSIVE: ambient, diffuse, emissive = the star's colour."""
    data = bytearray(data)
    count = 0
    for kind, offset, size in blocks_with_offsets(bytes(data)):
        if kind != 'NiMaterialProperty':
            continue
        emissive = struct.unpack_from('<3f', data, offset + 48)
        if all(abs(a - b) < 0.01 for a, b in zip(emissive, STAR_MATERIAL_EMISSIVE)):
            for field in (12, 24, 48):  # ambient, diffuse, emissive (specular +36 kept)
                struct.pack_into('<3f', data, offset + field, *color)
            count += 1
    return bytes(data), count


def main(game, staging):
    game_fx_dat = original_dat(game, 'fx')
    staged_fx_dat = os.path.join(staging, 'data', 'fx.dat')
    work = os.path.join(staging, 'ui', 'glow_effects')
    os.makedirs(work, exist_ok=True)
    with zipfile.ZipFile(game_fx_dat) as game_fx:
        grey, star_color = star_texture(game_fx)
        grey_path = os.path.join(work, GREY_STAR)
        grey.save(grey_path)
        for folder in TEXTURE_FOLDERS:
            add_to_ui_dat(game_fx_dat, staged_fx_dat, folder + GREY_STAR, grey_path)
        print('star colour', star_color)
        for variant in 'abcdefgh':
            point = 'weapon/wpn_up_lv9_%s_point.nif' % variant
            data = game_fx.read(point)
            assert data.count(b'heka_a.dds') == 1, point
            path = os.path.join(work, os.path.basename(point))
            open(path, 'wb').write(data.replace(b'heka_a.dds', b'heka_e.dds'))
            add_to_ui_dat(game_fx_dat, staged_fx_dat, point, path)

            cash = 'weapon/wpn_up_lv9_%s_cash.nif' % variant
            data = game_fx.read(cash)
            assert data.count(b'attack_red.dds') == 1, cash
            data, materials = patch_star_material(data.replace(b'attack_red.dds', GREY_STAR.encode()), star_color)
            assert materials >= 1, cash
            path = os.path.join(work, os.path.basename(cash))
            open(path, 'wb').write(data)
            add_to_ui_dat(game_fx_dat, staged_fx_dat, cash, path)
    print('staged', staged_fx_dat)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
