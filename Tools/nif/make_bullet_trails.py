"""Bullet trail colours of the weapon glow palette (D75): the game draws hit-scan bullet trails (rifle, sniper, machine
gun, shotgun: CBulletTrailFx, 0x497D40 adds one) with fx.dat bullettrail.dds, 9 columns of 64 px (one colour each; the
weapon picks its column, weapon data field 4). This tool appends one column per palette colour (column 9 + palette
index), tinted from the game's column 2 (light blue: its brightness and alpha), and writes the texture into a staged
copy of fx.dat (both paths the game has: texture/ENG/ and item/). The DLL (ui/inventory/BulletTrails.cpp) sets the
texture coordinates of every new trail for this wider sheet: the game's column, or the shooter's colour.

Usage: python make_bullet_trails.py <ToyBattles folder> <staging folder>
Writes <staging>/data/fx.dat (from the game's fx.dat when not staged yet) and <staging>/ui/bullettrail_preview.png.
"""
import io
import os
import sys
import zipfile

from PIL import Image

from make_item_lock_ui import GLOW_PALETTE, add_to_ui_dat, original_dat

COLUMN = 64
GAME_COLUMNS = 9
SOURCE_COLUMN = 2   # light blue: a white core with a blue edge
PATHS = ('texture/ENG/bullettrail.dds', 'item/bullettrail.dds')


def tinted(column, rgb):
    """The column's brightness in `rgb`, its core going white (as the game's own columns)."""
    out = Image.new('RGBA', column.size)
    src = column.load()
    dst = out.load()
    for y in range(column.size[1]):
        for x in range(column.size[0]):
            r, g, b, a = src[x, y]
            light = max(r, g, b) / 255.0
            core = max(0.0, light - 0.92) / 0.08 * 0.6  # only the very core goes (partly) white
            dst[x, y] = tuple(int(min(255, c * min(1.0, light * 1.6) * (1 - core) + 255 * core)) for c in rgb) + (a,)
    return out


def build(game_fx_dat):
    with zipfile.ZipFile(game_fx_dat) as z:
        game = Image.open(io.BytesIO(z.read(PATHS[0]))).convert('RGBA')
    assert game.size == (GAME_COLUMNS * COLUMN, 256), game.size
    sheet = Image.new('RGBA', ((GAME_COLUMNS + len(GLOW_PALETTE)) * COLUMN, game.size[1]), (0, 0, 0, 0))
    sheet.paste(game, (0, 0))
    source = game.crop((SOURCE_COLUMN * COLUMN, 0, (SOURCE_COLUMN + 1) * COLUMN, game.size[1]))
    for i, color in enumerate(GLOW_PALETTE):
        rgb = ((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF)
        sheet.paste(tinted(source, rgb), ((GAME_COLUMNS + i) * COLUMN, 0))
    return sheet


def main(game, staging):
    game_fx_dat = original_dat(game, 'fx')
    staged_fx_dat = os.path.join(staging, 'data', 'fx.dat')
    os.makedirs(os.path.dirname(staged_fx_dat), exist_ok=True)
    os.makedirs(os.path.join(staging, 'ui'), exist_ok=True)
    sheet = build(game_fx_dat)
    dds = os.path.join(staging, 'ui', 'bullettrail.dds')
    sheet.save(dds)
    for path in PATHS:
        add_to_ui_dat(game_fx_dat, staged_fx_dat, path, dds)
    preview = Image.new('RGBA', sheet.size, (0, 0, 0, 255))
    preview.alpha_composite(sheet)
    preview.save(os.path.join(staging, 'ui', 'bullettrail_preview.png'))
    print('staged', staged_fx_dat, sheet.size)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
