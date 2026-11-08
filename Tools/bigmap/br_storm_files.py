"""Battle Royale (party game 11) client files, made on top of a client's INSTALLED archives: only the records below
are added or replaced, every other record (the local serverinfo, the Big Map's minimap, kit drops, corridor covers,
...) is copied byte for byte (ziprecords.py), so it can run again after other changes to the same archives.
  data/map.dat  party/BrStormWallX.nif, party/BrStormWallY.nif, party/br_storm_wall.dds: the storm walls (DLL
                party/modes/BattleRoyaleMode.cpp), built from party/LavaWallX.nif as the Hardpoint ring
                (Tools/nif/make_hardpoint_ring.py): a double-sided plane 1000 long, 3000 high above the root, X on
                x = 0 along y, Y on y = 0 along x, depth test without depth write;
  data/ui.dat   minimap/bat_minimap.dds: 256 x 512, + zone cells 12 / 13 (solid white / blue: the storm's lines on the
                TAB map, hooks/ui/MinimapHooks.cpp); icon/ModesRibbons.dds: + tile 11 (Tools/nif/make_hardpoint_icon.py);
                icon/ENG/room_mode_title_party2.dds: tile 0 = BATTLE ROYALE (style of Tools/nif/make_party_titles.py);
                setup/ENG/room_partygames_6.dds: the announcements (DLL protocol/messages/AnnouncementMessages.cpp,
                events 61..69 = images 2080..2088), banners of the game's own art (Tools/nif/make_hardpoint_ui.py);
  data/cgd.dip  iconsinfo.cdb: + 91399021 (room title), 91399031 (ribbon), sorted by id; iconstag.cdb:
                + ICONTAG_ROOM_MOD_PARTY_11, ICONTAG_MODE_RIBBON_12;
  data/ui/scene/SCENE_BATTLE.xml (loose file): + texture 5086632897 (room_partygames_6.dds) and the image components
                2080..2088 (copies of component 620), the lines of an earlier run replaced.

Usage: python br_storm_files.py <client> <out> --password-file <pw> [--install]
  writes <out>/data/{map.dat,ui.dat,cgd.dip,ui/scene/SCENE_BATTLE.xml}; --install then copies them into <client>/data,
  keeping the originals of the first install in <client>/data/_br_storm_backup/.
"""
import argparse
import io
import math
import os
import re
import shutil
import struct
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'nif'))
sys.path.insert(0, os.path.join(HERE, '..', 'CgdManager'))
import ziprecords  # noqa: E402
import make_hardpoint_ui as hpui  # noqa: E402
from make_hardpoint_icon import make_ribbon  # noqa: E402
from make_hardpoint_ring import join, split, stream  # noqa: E402
from CdbParser import Cdb  # noqa: E402
from Utils import EntryDataType  # noqa: E402

TEMPLATE = 'party/LavaWallX.nif'
WALLS = {'party/BrStormWallX.nif': True, 'party/BrStormWallY.nif': False}
WALL_TEXTURE = 'br_storm_wall.dds'
LENGTH = 1000.0
HEIGHT = 3000.0
U_REPEAT = 2.0
V_REPEAT = 3.0
STORM = (150, 70, 230)

MINIMAP = 'minimap/bat_minimap.dds'
CELL, CELLS_TOP, PER_ROW = 64, 64, 4
BARS = {12: (245, 245, 245), 13: (60, 150, 255)}

RIBBONS = 'icon/ModesRibbons.dds'
RIBBON_TILE, RIBBON_ICON, RIBBON_TAG = 11, 91399031, 'ICONTAG_MODE_RIBBON_12'
TITLE_SHEET = 'room_mode_title_party2.dds'
TITLE_ICON, TITLE_TAG, TITLE = 91399021, 'ICONTAG_ROOM_MOD_PARTY_11', 'BATTLE ROYALE'
TITLE_W, TITLE_H, TITLE_COLOUR = 340, 50, (8, 47, 74, 255)
BACKUP = '_br_storm_backup'

ANNOUNCE_SHEET = 'setup/ENG/room_partygames_6.dds'
ANNOUNCE_TEXTURE = b'5086632897'
SCENE = 'ui/scene/SCENE_BATTLE.xml'
TEXT_LEFT, TEXT_WIDTH = 104, 240
# component id: (node name, panel, icon, lines); a line is [(text, colour)]. NAMED: the text sits under the player name
# that the DLL writes in E_DLG_BATTLE_PARTYGAME_STC_NOTICE_NEWCHICKEN_NICKNAME (5..33 px from the top of the picture)
ARENAS = ('CASTLE', 'PAPER LAND', 'PVC FACTORY', 'THE STUDIO')
ANNOUNCEMENTS = {
    2080: ('notice_br_zoneclosing', 'red', 'swords_clock', [[('THE ZONE', hpui.WHITE)], [('IS CLOSING!', hpui.GOLD)]]),
    **{2081 + i: ('notice_br_closes_%d' % i, 'red', 'swords_clock', [[(name, hpui.GOLD)], [('CLOSES IN 30 SECONDS', hpui.WHITE)]])
       for i, name in enumerate(ARENAS)},
    2085: ('notice_br_closes_paperpvc', 'red', 'swords_clock', [[('PAPER LAND + PVC FACTORY', hpui.GOLD)], [('CLOSE IN 30 SECONDS', hpui.WHITE)]]),
    2086: ('notice_br_closes_castlestudio', 'red', 'swords_clock', [[('CASTLE + THE STUDIO', hpui.GOLD)], [('CLOSE IN 30 SECONDS', hpui.WHITE)]]),
    2087: ('notice_br_instorm', 'red', 'swords_x', [[('YOU ARE IN', hpui.WHITE)], [('THE STORM!', hpui.GOLD)]]),
    2088: ('notice_br_lasttoy', 'navy', 'swords_trophy', [[('LAST TOY STANDING!', hpui.WHITE)]]),
}
NAMED = {2088}
BANNER_W, BANNER_H, BANNER_STEP = 360, 81, 90


def wall_geometry(along_y):
    h = LENGTH / 2
    if along_y:
        corners = [(0.0, -h), (0.0, h)]
        normal = (1.0, 0.0, 0.0)
    else:
        corners = [(-h, 0.0), (h, 0.0)]
        normal = (0.0, 1.0, 0.0)
    (x0, y0), (x1, y1) = corners
    verts, idx = [], []
    for sign in (1.0, -1.0):
        base = len(verts)
        n = tuple(sign * c for c in normal)
        for (x, y, z, u, v) in ((x0, y0, 0.0, 0.0, V_REPEAT), (x1, y1, 0.0, U_REPEAT, V_REPEAT),
                                (x1, y1, HEIGHT, U_REPEAT, 0.0), (x0, y0, HEIGHT, 0.0, 0.0)):
            verts.append((x, y, z) + n + (u, v))
        idx += [base, base + 1, base + 2, base, base + 2, base + 3] if sign > 0 else \
            [base, base + 2, base + 1, base, base + 3, base + 2]
    vdata = b''.join(struct.pack('<8f', *v) for v in verts)
    return vdata, len(verts), struct.pack('<%dH' % len(idx), *idx), len(idx)


def make_wall(template, along_y):
    n = split(template)
    strings = n['strings']
    strings[strings.index(b'lava_wall_x')] = b'br_storm_wall'
    strings[strings.index(b'wall_texture.dds')] = WALL_TEXTURE.encode()
    strings[strings.index(b'Box')] = b'Wall'
    blocks = n['blocks']
    types = [n['types'][i & 0x7FFF] for i in n['tindex']]
    index_block = next(i for i, t in enumerate(types) if t.startswith(b'NiDataStream') and t[13:14] == b'0')
    vertex_block = next(i for i, t in enumerate(types) if t.startswith(b'NiDataStream') and t[13:14] == b'1')
    mesh_block = types.index(b'NiMesh')

    def comps_and_trailer(block):
        nreg = struct.unpack_from('<I', block, 8)[0]
        o = 12 + 8 * nreg
        ncomp = struct.unpack_from('<I', block, o)[0]
        comps = list(struct.unpack_from('<%dI' % ncomp, block, o + 4))
        nbytes = struct.unpack_from('<I', block, 0)[0]
        return comps, bytes(block[o + 4 + 4 * ncomp + nbytes:])

    vdata, nverts, idata, nidx = wall_geometry(along_y)
    ic, it = comps_and_trailer(blocks[index_block])
    vc, vt = comps_and_trailer(blocks[vertex_block])
    blocks[index_block] = stream(idata, nidx, ic, it)
    blocks[vertex_block] = stream(vdata, nverts, vc, vt)
    mesh = blocks[mesh_block]
    at = bytes(mesh).find(struct.pack('<f', 25000.0))
    assert at >= 8, 'bound not found'
    struct.pack_into('<fff', mesh, at - 8, 0.0, 0.0, HEIGHT / 2)
    struct.pack_into('<f', mesh, at + 4, math.hypot(LENGTH / 2, HEIGHT / 2))
    zbuffer = types.index(b'NiZBufferProperty')
    assert struct.unpack_from('<H', blocks[zbuffer], 12)[0] == 0x000F, 'z buffer flags'
    struct.pack_into('<H', blocks[zbuffer], 12, 0x000D)
    return join(n)


def wall_texture():
    w, h = 128, 256
    img = Image.new('RGBA', (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        band = math.exp(-((y - 200) / 10.0) ** 2)
        for x in range(w):
            streak = 0.5 + 0.5 * math.sin(2 * math.pi * (x / w * 6 + 0.35 * math.sin(2 * math.pi * y / h)))
            glow = 0.55 + 0.3 * streak + 0.6 * band
            a = int(255 * min(1.0, 0.40 + 0.25 * t + 0.2 * streak * t + 0.5 * band))
            c = tuple(min(255, int(v * glow + 200 * band)) for v in STORM)
            px[x, y] = c + (a,)
    buf = io.BytesIO()
    img.save(buf, 'DDS')
    return buf.getvalue()


def minimap_sheet(data):
    icons = Image.open(io.BytesIO(data)).convert('RGBA')
    assert icons.size[0] == 256 and icons.size[1] in (256, 512), icons.size
    sheet = Image.new('RGBA', (256, 512), (0, 0, 0, 0))
    sheet.paste(icons.crop((0, 0, 256, 256)), (0, 0))
    for cell, rgb in BARS.items():
        x, y = (cell % PER_ROW) * CELL, CELLS_TOP + (cell // PER_ROW) * CELL
        sheet.paste(rgb + (255,), (x, y, x + CELL, y + CELL))
    buf = io.BytesIO()
    sheet.save(buf, 'DDS')
    return buf.getvalue()


def ribbon_sheet(data):
    sheet = make_ribbon(Image.open(io.BytesIO(data)).convert('RGBA'), RIBBON_TILE, 'royale')
    buf = io.BytesIO()
    sheet.save(buf, 'DDS')
    return buf.getvalue()


def title_sheet():
    font = ImageFont.truetype('ARIALNB.TTF', 30)
    sheet = Image.new('RGBA', (512, 512), TITLE_COLOUR[:3] + (0,))
    draw = ImageDraw.Draw(sheet)
    left, top, right, _ = draw.textbbox((0, 0), TITLE, font=font)
    draw.text(((TITLE_W - (right - left)) // 2 - left, 16 - top), TITLE, font=font, fill=TITLE_COLOUR)
    buf = io.BytesIO()
    sheet.save(buf, 'DDS')
    return buf.getvalue()


def fitted_font(draw, lines, size):
    while size > 12:
        font = hpui.game_font(size)
        if all(sum(draw.textlength(t, font=font) for t, _ in line) <= TEXT_WIDTH for line in lines):
            return font
        size -= 1
    return hpui.game_font(size)


def banner(sheet, y, panel, icon_name, lines, named):
    sheet.paste(hpui.ART['panel_' + panel], (0, y))
    icon = hpui.ART[icon_name]
    sheet.alpha_composite(icon, (8 + max(0, (92 - icon.width) // 2), y + (BANNER_H - icon.height) // 2))
    draw = ImageDraw.Draw(sheet)
    font = fitted_font(draw, lines, 27 if len(lines) == 1 else 23)
    line_height = font.getbbox('AG')[3] + 3
    top = y + 40 if named else y + (BANNER_H - line_height * len(lines)) / 2
    for parts in lines:
        width = sum(draw.textlength(t, font=font) for t, _ in parts)
        x = TEXT_LEFT + (TEXT_WIDTH - width) / 2
        for text, colour in parts:
            if panel == 'red' and colour == hpui.RED:
                colour = hpui.GOLD
            draw.text((x, top), text, font=font, fill=colour, stroke_width=2, stroke_fill=(28, 30, 44, 255))
            x += draw.textlength(text, font=font)
        top += line_height


def announcement_sheet(ui_dat):
    hpui.load_art(ui_dat)
    sheet = Image.new('RGBA', (512, 1024), (0, 0, 0, 0))
    for index, (cid, (_, panel, icon, lines)) in enumerate(sorted(ANNOUNCEMENTS.items())):
        banner(sheet, index * BANNER_STEP, panel, icon, lines, cid in NAMED)
    buf = io.BytesIO()
    sheet.save(buf, 'DDS')
    return buf.getvalue()


def line_of(data, needle):
    start = data.index(needle)
    return data.rindex(b'\n', 0, start) + 1, data.index(b'\n', start) + 1


def battle_scene(data):
    lines = data.split(b'\n')
    data = b'\n'.join(line for line in lines if ANNOUNCE_TEXTURE not in line and b'nodename="notice_br_' not in line)
    b, e = line_of(data, b'<Texture id="5086632893"')
    texture = data[b:e].replace(b'5086632893', ANNOUNCE_TEXTURE).replace(b'room_partygames_2.dds', b'room_partygames_6.dds')
    texture = re.sub(rb'height="\d+" width="\d+"', b'height="1024" width="512"', texture, count=1)
    b, e = line_of(data, b'<Texture id="5086632896"')
    data = data[:e] + texture + data[e:]
    b, e = line_of(data, b'<Component id="620"')
    template = data[b:e]
    components = b''
    for index, (cid, (name, *_)) in enumerate(sorted(ANNOUNCEMENTS.items())):
        y = index * BANNER_STEP
        line = template.replace(b'id="620"', b'id="%d"' % cid).replace(b'5086632893', ANNOUNCE_TEXTURE)
        line = line.replace(b'nodename="notice_sleepphase"', b'nodename="' + name.encode() + b'"')
        components += re.sub(rb'texcoord="[^"]*"', b'texcoord="0.%d.%d.%d"' % (y, BANNER_W, y + BANNER_H), line, count=1)
    b, e = line_of(data, b'<Component id="2070"')
    return data[:e] + components + data[e:]


def icon_tables(cgd, password, work):
    out = {}
    with zipfile.ZipFile(cgd) as z:
        for name in ('iconsinfo.cdb', 'iconstag.cdb'):
            path = os.path.join(work, name)
            with open(path, 'wb') as f:
                f.write(z.read(name, pwd=password))
            cdb = Cdb(path)
            cdb.parseCdb()
            rows = [(TITLE_ICON, TITLE_SHEET, 0, TITLE_W, TITLE_H, 512, 0, TITLE_TAG),
                    (RIBBON_ICON, 'ModesRibbons.dds', RIBBON_TILE, 96, 96, 512, 1, RIBBON_TAG)]
            for icon, sheet, offset, w, h, size, common, tag in rows:
                if name == 'iconsinfo.cdb':
                    if any(e[0].value == icon for e in cdb.entries):
                        continue
                    values = [icon, sheet, offset, w, h, size, common]
                else:
                    if any(e[2].value == tag for e in cdb.entries):
                        continue
                    values = [max(e[0].value for e in cdb.entries) + 1, icon, tag]
                cdb.entries.append([EntryDataType(key=k, typeSize=ts, value=v) for k, ts, v in zip(cdb.keys, cdb.typeSizes, values)])
            if name == 'iconsinfo.cdb':
                cdb.entries.sort(key=lambda e: e[0].value)
            cdb.toCdb(work)
            with open(path, 'rb') as f:
                out[name] = f.read()
    return out


def rewrite(src, dst, files, password=None):
    with zipfile.ZipFile(src) as z:
        names = set(z.namelist())
    replace = {n: d for n, d in files.items() if n in names}
    add = [(n, d) for n, d in files.items() if n not in names]
    ziprecords.rewrite(src, dst, replace=replace, add=add, password=password)
    print('%s: %d replaced, %d added' % (os.path.basename(dst), len(replace), len(add)))


def stamp(path):
    info = os.stat(path)
    return info.st_size, info.st_mtime_ns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('client', help='a client folder (Bin/ and data/)')
    ap.add_argument('out', help='writes <out>/data/')
    ap.add_argument('--password-file', required=True, help="cgd.dip's zip password")
    ap.add_argument('--install', action='store_true')
    a = ap.parse_args()
    with open(a.password_file, 'rb') as f:
        password = f.read().strip()
    data = os.path.join(a.client, 'data')
    out = os.path.join(a.out, 'data')
    os.makedirs(out, exist_ok=True)
    names = ('map.dat', 'ui.dat', 'cgd.dip', SCENE)
    stamps = {name: stamp(os.path.join(data, name)) for name in names}

    with zipfile.ZipFile(os.path.join(data, 'map.dat')) as z:
        template = os.path.join(a.out, 'LavaWallX.nif')
        with open(template, 'wb') as f:
            f.write(z.read(TEMPLATE))
    map_files = {name: make_wall(template, along_y) for name, along_y in WALLS.items()}
    map_files['party/' + WALL_TEXTURE] = wall_texture()
    rewrite(os.path.join(data, 'map.dat'), os.path.join(out, 'map.dat'), map_files)

    with zipfile.ZipFile(os.path.join(data, 'ui.dat')) as z:
        ui_files = {MINIMAP: minimap_sheet(z.read(MINIMAP)), RIBBONS: ribbon_sheet(z.read(RIBBONS)),
                    'icon/ENG/' + TITLE_SHEET: title_sheet(), ANNOUNCE_SHEET: announcement_sheet(os.path.join(data, 'ui.dat'))}
    rewrite(os.path.join(data, 'ui.dat'), os.path.join(out, 'ui.dat'), ui_files)

    with open(os.path.join(data, SCENE), 'rb') as f:
        scene = battle_scene(f.read())
    os.makedirs(os.path.dirname(os.path.join(out, SCENE)), exist_ok=True)
    with open(os.path.join(out, SCENE), 'wb') as f:
        f.write(scene)
    print('%s: texture %s, components %d..%d' % (SCENE, ANNOUNCE_TEXTURE.decode(), min(ANNOUNCEMENTS), max(ANNOUNCEMENTS)))

    work = os.path.join(a.out, 'cgd_work')
    os.makedirs(work, exist_ok=True)
    tables = icon_tables(os.path.join(data, 'cgd.dip'), password, work)
    ziprecords.rewrite(os.path.join(data, 'cgd.dip'), os.path.join(out, 'cgd.dip'), replace=tables, password=password)
    with zipfile.ZipFile(os.path.join(out, 'cgd.dip')) as z:
        for info in z.infolist():
            if not info.is_dir():
                z.read(info.filename, pwd=password)
    print('cgd.dip: iconsinfo.cdb and iconstag.cdb replaced')

    if a.install:
        changed = [name for name, value in stamps.items() if stamp(os.path.join(data, name)) != value]
        if changed:
            sys.exit('changed while building, run again: %s' % ', '.join(changed))
        backup = os.path.join(data, BACKUP)
        os.makedirs(backup, exist_ok=True)
        for name in names:
            keep = os.path.join(backup, os.path.basename(name))
            if not os.path.exists(keep):
                shutil.copyfile(os.path.join(data, name), keep)
            shutil.copyfile(os.path.join(out, name), os.path.join(data, name))
            print('installed', name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
