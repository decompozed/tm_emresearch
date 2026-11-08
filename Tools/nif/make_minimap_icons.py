"""TAB map icons with the party games' zones (D86): <staging>/ui/minimap/bat_minimap.dds.

The game draws the TAB map's markers from minimap/bat_minimap.dds of ui.dat (256 x 64, 32 x 32 cells; loaded by
0x5AB980, cells set by 0x5AAB00). This keeps those 64 rows and adds, below them, 64 x 64 cells, four a row, that the
DLL draws at the zones (hooks/ui/MinimapHooks.cpp, party::MinimapZone::Icon):
  cells 0..8   Domination: a square with its letter, 3 * letter (A B C) + colour (white, red, blue);
  cells 9..11  Hardpoint / Headquarters: a circle, white / red / blue.
The DLL scales a cell to the zone's size on the map and draws nothing when the texture is the game's (height 64).

Usage: python make_minimap_icons.py <game dir> <staging>   (reads data/ui_orig.dat when present, else data/ui.dat)
"""
import io
import os
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont

ENTRY = 'minimap/bat_minimap.dds'
CELL = 64
TOP = 64
PER_ROW = 4
COLOURS = [(240, 240, 240), (255, 60, 60), (60, 150, 255)]  # white, red, blue (the zones' models)
OUTLINE = (15, 15, 25)


def font(size):
    for name in ('BRLNSDB.TTF', 'arialbd.ttf', 'Arial Bold.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def square(letter, rgb):
    # drawn 4x and reduced: smooth edges
    big = CELL * 4
    img = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle((6, 6, big - 7, big - 7), fill=rgb + (80,), outline=OUTLINE + (230,), width=6)
    d.rectangle((12, 12, big - 13, big - 13), outline=rgb + (255,), width=20)
    f = font(int(big * 0.62))
    box = d.textbbox((0, 0), letter, font=f, stroke_width=12)
    pos = ((big - (box[2] - box[0])) // 2 - box[0], (big - (box[3] - box[1])) // 2 - box[1])
    d.text(pos, letter, font=f, fill=(255, 255, 255, 255), stroke_width=12, stroke_fill=OUTLINE + (255,))
    return img.resize((CELL, CELL), Image.LANCZOS)


def circle(rgb):
    big = CELL * 4
    img = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((6, 6, big - 7, big - 7), fill=rgb + (80,), outline=OUTLINE + (230,), width=6)
    d.ellipse((12, 12, big - 13, big - 13), outline=rgb + (255,), width=20)
    return img.resize((CELL, CELL), Image.LANCZOS)


def main(game, staging):
    data = os.path.join(game, 'data')
    source = os.path.join(data, 'ui_orig.dat')
    if not os.path.exists(source):
        source = os.path.join(data, 'ui.dat')
    with zipfile.ZipFile(source) as z:
        icons = Image.open(io.BytesIO(z.read(ENTRY))).convert('RGBA')
    assert icons.size == (256, 64), icons.size
    sheet = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    sheet.paste(icons, (0, 0))
    cells = [square(letter, rgb) for letter in 'ABC' for rgb in COLOURS] + [circle(rgb) for rgb in COLOURS]
    for i, cell in enumerate(cells):
        sheet.alpha_composite(cell, ((i % PER_ROW) * CELL, TOP + (i // PER_ROW) * CELL))
    out = os.path.join(staging, 'ui', 'minimap')
    os.makedirs(out, exist_ok=True)
    sheet.save(os.path.join(out, 'bat_minimap.dds'), 'DDS')
    preview = Image.new('RGBA', sheet.size, (60, 60, 60, 255))
    preview.alpha_composite(sheet)
    preview.save(os.path.join(staging, 'minimap_icons_preview.png'))
    print('ok')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
