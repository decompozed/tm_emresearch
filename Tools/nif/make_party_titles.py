"""Room title of every party game (the big "SQUARE MODE" title of the room screen, control 101033), written to the
staging folder on top of make_hardpoint_icon.py's output:
  data/ui.dat   icon/ENG/room_mode_title_party.dds: 340 x 50 tiles 0..7 = party game 1..8, in the style of
                room_mode_title02.dds (Arial Narrow Bold, colour 8,47,74, centred)
  data/cgd.dip  iconsinfo.cdb: + icons 91399011..91399018; iconstag.cdb: + ICONTAG_ROOM_MOD_PARTY_01..08;
                ENG/UImsg.cdb: the texts of UI_TEXTS replaced (ug_text of the entry named by ug_name)
                ENG/msg.cdb: the texts of MSG_TEXTS replaced (mg_text of the message box named by mg_name)

The DLL shows icon 91399010 + game id in a party room (RoomHooks.cpp, updateRoomMainScreen).
Usage: python make_party_titles.py <game dir> <staging dir> <cgd password as hex>
"""
import io
import os
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont

from make_hardpoint_icon import Cdb, EntryDataType, rebuild_zip, write_encrypted_zip

TITLES = {1: 'CHICKEN RUN', 2: 'CHASER', 3: 'ARENA', 4: 'TEAM CLASH', 5: 'LAVA ESCAPE', 6: 'HARDPOINT', 7: 'HEADQUARTERS', 8: 'PROP HUNT', 9: 'ELIMINATION SHUFFLE', 10: 'DOMINATION'}  # game id: title
FIRST_ICON = 91399010  # + game id
SHEET = 'room_mode_title_party.dds'
TILE_W, TILE_H = 340, 50
COLOUR = (8, 47, 74, 255)
# UI texts of the client (ENG/UImsg.cdb: ug_name -> ug_text), user 2026-10-02: the profile's second line is the
# player's grade (GameMaster, Moderator...), the first one ("Title") the title the player chose
UI_TEXTS = {'UIMSG_USERINFO_BASIC_RANKING': 'Grade'}
# Message boxes (ENG/msg.cdb: mg_name -> mg_text). ID_QUIT_BUSY is the box of the disconnect packet 73 with code 5
# (0xACFB09), which the server never sent: it now says the DLL is missing (D73: MainServer disconnects a client whose
# have_api.dll does not answer message 0x2F; the box needs no DLL).
MSG_TEXTS = {'ID_QUIT_BUSY': "have_api.dll is missing in your game's Bin folder!"}


def make_sheet():
    font = ImageFont.truetype('ARIALNB.TTF', 30)
    sheet = Image.new('RGBA', (512, 512), COLOUR[:3] + (0,))
    draw = ImageDraw.Draw(sheet)
    for game, title in TITLES.items():
        left, top, right, bottom = draw.textbbox((0, 0), title, font=font)
        x = (TILE_W - (right - left)) // 2 - left
        y = (game - 1) * TILE_H + 16 - top  # the original titles' caps span y 16..38 of their tile
        draw.text((x, y), title, font=font, fill=COLOUR)
    return sheet


def patch_ui_texts(data, texts=None, name_key='ug_name', text_key='ug_text'):
    """`texts` (default UI_TEXTS) written into a text cdb in place (only those bytes: the other texts stay byte for
    byte). cdb: key count, 30-byte key names, their sizes, then fixed-size records (UImsg: ug_id 4, ug_name 100,
    ug_text 250, ...; msg: mg_id 4, mg_name 40, mg_text 200, ...)."""
    import struct
    texts = UI_TEXTS if texts is None else texts
    data = bytearray(data)
    count = struct.unpack_from('<I', data, 0)[0]
    names = [bytes(data[4 + 30 * k:4 + 30 * (k + 1)]).rstrip(b'\0').decode() for k in range(count)]
    sizes = list(struct.unpack_from('<%dI' % count, data, 4 + 30 * count))
    offsets = {n: sum(sizes[:k]) for k, n in enumerate(names)}
    width = dict(zip(names, sizes))
    start, record = 4 + 34 * count, sum(sizes)
    done = set()
    for at in range(start, len(data) - record + 1, record):
        name = bytes(data[at + offsets[name_key]:at + offsets[name_key] + width[name_key]]).split(b'\0')[0].decode('latin1')
        if name in texts:
            text = texts[name].encode('latin1')
            assert len(text) < width[text_key]
            data[at + offsets[text_key]:at + offsets[text_key] + width[text_key]] = text.ljust(width[text_key], b'\0')
            done.add(name)
    assert done == set(texts), set(texts) - done
    return bytes(data)


def main(game, staging, password_hex):
    password = bytes.fromhex(password_hex)
    data_dir = os.path.join(staging, 'data')
    os.makedirs(data_dir, exist_ok=True)

    sheet = make_sheet()
    sheet.save(os.path.join(staging, 'room_mode_title_party_preview.png'))
    buf = io.BytesIO()
    sheet.save(buf, 'DDS')
    ui_src = os.path.join(data_dir, 'ui.dat')
    if not os.path.exists(ui_src):
        ui_src = os.path.join(game, 'data', 'ui.dat')
    tmp = os.path.join(data_dir, 'ui.dat.new')
    rebuild_zip(ui_src, tmp, {'icon/ENG/' + SHEET: buf.getvalue()})
    os.replace(tmp, os.path.join(data_dir, 'ui.dat'))

    cgd_src = os.path.join(data_dir, 'cgd.dip')
    if not os.path.exists(cgd_src):
        cgd_src = os.path.join(game, 'data', 'cgd.dip')
    work = os.path.join(staging, 'cgd_work')
    os.makedirs(work, exist_ok=True)
    entries = []
    with zipfile.ZipFile(cgd_src) as z:
        for info in z.infolist():
            data = b'' if info.is_dir() else z.read(info.filename, pwd=password)
            entries.append([info.filename, data, info.is_dir()])
    for i, (name, data, _) in enumerate(entries):
        if name == 'ENG/UImsg.cdb':
            entries[i][1] = patch_ui_texts(data)
            continue
        if name == 'ENG/msg.cdb':
            entries[i][1] = patch_ui_texts(data, MSG_TEXTS, 'mg_name', 'mg_text')
            continue
        if name not in ('iconsinfo.cdb', 'iconstag.cdb'):
            continue
        path = os.path.join(work, name)
        open(path, 'wb').write(data)
        cdb = Cdb(path)
        cdb.parseCdb()
        for game_id in TITLES:
            icon = FIRST_ICON + game_id
            tag = 'ICONTAG_ROOM_MOD_PARTY_%02d' % game_id
            if name == 'iconsinfo.cdb':
                if any(e[0].value == icon for e in cdb.entries):
                    continue
                values = [icon, SHEET, game_id - 1, TILE_W, TILE_H, 512, 0]
            else:
                if any(e[2].value == tag for e in cdb.entries):
                    continue
                values = [max(e[0].value for e in cdb.entries) + 1, icon, tag]
            cdb.entries.append([EntryDataType(key=k, typeSize=ts, value=v) for k, ts, v in zip(cdb.keys, cdb.typeSizes, values)])
        if name == 'iconsinfo.cdb':
            # the game looks icons up in a table sorted by id: rows appended at the end hid other icons (the
            # inventory's weapons and parts, 2026-10-02); a stable sort keeps the game's duplicate ids in their order
            cdb.entries.sort(key=lambda e: e[0].value)
        cdb.toCdb(work)
        entries[i][1] = open(os.path.join(work, name), 'rb').read()
    out = os.path.join(data_dir, 'cgd.dip.new')
    write_encrypted_zip(out, entries, password)
    with zipfile.ZipFile(out) as z:
        for info in z.infolist():
            if not info.is_dir():
                z.read(info.filename, pwd=password)
    os.replace(out, os.path.join(data_dir, 'cgd.dip'))
    print('ok')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
