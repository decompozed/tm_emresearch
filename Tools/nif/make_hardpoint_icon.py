"""Room-list ribbons of the added party games, written to the staging folder:
  data/ui.dat   icon/ModesRibbons.dds (96 x 96 tiles, 5 per row): tile 6 = Hardpoint (gold ribbon + zone icon), tile 7 =
                Headquarters (purple ribbon + HQ icon), tile 8 = Prop Hunt (green ribbon + crate)
  data/cgd.dip  iconsinfo.cdb: + icons 91399006..08 (ModesRibbons.dds, offsets 6..8);
                iconstag.cdb: + ICONTAG_MODE_RIBBON_07..09 (ToyBattles: 01..06 are the game's)

cgd.dip is a ZIP with ZipCrypto (traditional PKWARE) encryption; its password is in the client executable at
0xBD0C70 (UPX-packed ToyHeroes.exe: unpack a COPY with upx -d), 128 hex chars, RC6-decrypted with CCrypt(0)
(Tools/Client/Crypt.h): the password is the first 26 decrypted bytes (control characters in the ToyHeroes build).
See refactor_toyheroes_to_toybattles/README.md.

Usage: python make_hardpoint_icon.py <game dir> <staging dir> <password as hex> [<base ModesRibbons sheet>]
"""
import binascii
import colorsys
import io
import os
import struct
import sys
import time
import zipfile
import zlib

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'CgdManager'))
from CdbParser import Cdb  # noqa: E402
from Utils import EntryDataType  # noqa: E402

TILE = 96
# (tile, icon id, tag, emblem)
# ToyBattles (2026-10-01): ICONTAG_MODE_RIBBON_01..06 exist (06 = tile 5, a mode of ToyBattles'): ours are 07..09 on
# tiles 6..8 (the DLL's catalog names them, party/settings/PartyGameCatalog.cpp)
RIBBONS = [
    (6, 91399006, 'ICONTAG_MODE_RIBBON_07', 'zone'),
    (7, 91399007, 'ICONTAG_MODE_RIBBON_08', 'hq'),
    (8, 91399008, 'ICONTAG_MODE_RIBBON_09', 'prop'),
    (9, 91399009, 'ICONTAG_MODE_RIBBON_10', 'shuffle'),  # Elimination Shuffle (D81): two crossing arrows
    (10, 91399010, 'ICONTAG_MODE_RIBBON_11', 'domination'),  # Domination (D85): three flags, red / white / blue
    (11, 91399031, 'ICONTAG_MODE_RIBBON_12', 'royale'),  # Battle Royale: the closing zone (91399021 is its room title)
]
GAME_TILES = 6  # tiles 0..5 belong to the game (ribbons 01..06)


# ---------------- ZipCrypto ----------------
def _crc_table():
    table = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        table.append(c)
    return table


CRC = _crc_table()


class ZipCrypto:
    def __init__(self, password):
        self.k = [0x12345678, 0x23456789, 0x34567890]
        for b in password:
            self._update(b)

    def _crc(self, c, b):
        return CRC[(c ^ b) & 0xFF] ^ (c >> 8)

    def _update(self, b):
        self.k[0] = self._crc(self.k[0], b)
        self.k[1] = (self.k[1] + (self.k[0] & 0xFF)) & 0xFFFFFFFF
        self.k[1] = (self.k[1] * 134775813 + 1) & 0xFFFFFFFF
        self.k[2] = self._crc(self.k[2], self.k[1] >> 24)

    def encrypt(self, data):
        out = bytearray()
        for b in data:
            t = (self.k[2] | 2) & 0xFFFF
            out.append(b ^ ((t * (t ^ 1)) >> 8) & 0xFF)
            self._update(b)
        return bytes(out)


def write_encrypted_zip(path, entries, password):
    """entries: [(name, bytes, is_dir)] -> deflate + ZipCrypto, no data descriptors."""
    dos_time = time.localtime()
    mtime = (dos_time.tm_hour << 11) | (dos_time.tm_min << 5) | (dos_time.tm_sec // 2)
    mdate = ((dos_time.tm_year - 1980) << 9) | (dos_time.tm_mon << 5) | dos_time.tm_mday
    central = bytearray()
    with open(path, 'wb') as f:
        for name, data, is_dir in entries:
            nb = name.encode('cp437', errors='replace')
            offset = f.tell()
            crc = binascii.crc32(data) & 0xFFFFFFFF
            if is_dir:
                flag, method, payload = 0, 0, b''
            else:
                c = zlib.compressobj(9, zlib.DEFLATED, -15)
                comp = c.compress(data) + c.flush()
                header = bytearray(os.urandom(11)) + bytes([crc >> 24])
                crypt = ZipCrypto(password)
                payload = crypt.encrypt(bytes(header)) + crypt.encrypt(comp)
                flag, method = 1, 8
            f.write(struct.pack('<IHHHHHIIIHH', 0x04034B50, 20, flag, method, mtime, mdate, crc, len(payload), len(data), len(nb), 0))
            f.write(nb)
            f.write(payload)
            central += struct.pack('<IHHHHHHIIIHHHHHII', 0x02014B50, 20, 20, flag, method, mtime, mdate, crc, len(payload), len(data),
                                   len(nb), 0, 0, 0, 0, 0x10 if is_dir else 0x20, offset)
            central += nb
        cd_offset = f.tell()
        f.write(central)
        f.write(struct.pack('<IHHHHIIH', 0x06054B50, 0, 0, len(entries), len(entries), len(central), cd_offset, 0))


# ---------------- ribbon ----------------
def make_ribbon(sheet, new_tile, emblem):
    """The ribbon shape (alpha) of Lava's tile, filled gold (Hardpoint) / purple (Headquarters) with a darker lower
    edge, + the game's emblem."""
    img = sheet.copy()
    lava = img.crop((4 * TILE, 0, 5 * TILE, TILE))
    src = lava.load()
    tile = Image.new('RGBA', (TILE, TILE), (0, 0, 0, 0))
    px = tile.load()
    for y in range(TILE):
        for x in range(TILE):
            a = src[x, y][3]
            if a == 0:
                continue
            # the band runs from the top edge to the right edge: shade across it (d = distance to the upper edge)
            d = (y - (x - 40)) / 56.0
            t = max(0.0, min(1.0, d))
            top, bottom = {'zone': ((246, 196, 62), (178, 116, 18)), 'hq': ((170, 110, 235), (96, 46, 160)),
                           'prop': ((90, 205, 120), (30, 125, 60)), 'shuffle': ((80, 150, 240), (30, 70, 160)),
                           'domination': ((235, 120, 40), (150, 60, 15)), 'royale': ((60, 200, 190), (15, 105, 110))}[emblem]
            colour = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
            px[x, y] = colour + (a,)
    d = ImageDraw.Draw(tile)
    white = (255, 255, 255, 255)
    if emblem == 'zone':
        d.ellipse((57, 23, 83, 31), outline=white, width=2)
        d.line((70, 27, 70, 7), fill=white, width=2)
        d.polygon([(70, 7), (81, 11), (70, 15)], fill=(225, 45, 45, 255))
    elif emblem == 'shuffle':  # two crossing arrows (red and white): the map changes every round
        d.line((58, 28, 82, 10), fill=white, width=3)
        d.polygon([(82, 10), (74, 10), (82, 18)], fill=white)
        d.line((58, 10, 82, 28), fill=(225, 45, 45, 255), width=3)
        d.polygon([(82, 28), (74, 28), (82, 20)], fill=(225, 45, 45, 255))
    elif emblem == 'domination':  # three flags (A B C): red, white, blue
        for pole, colour in ((60, (225, 45, 45, 255)), (70, white), (80, (50, 120, 240, 255))):
            d.line((pole, 30, pole, 8), fill=white, width=2)
            d.polygon([(pole, 8), (pole + 8, 11), (pole, 14)], fill=colour)
    elif emblem == 'royale':
        d.ellipse((57, 7, 83, 33), outline=white, width=2)
        d.ellipse((62, 12, 78, 28), outline=(255, 255, 255, 140), width=1)
        d.ellipse((66, 16, 74, 24), fill=(225, 45, 45, 255))
    elif emblem == 'prop':  # a crate with a question mark
        d.rectangle((60, 12, 80, 30), outline=white, width=2)
        d.line((60, 12, 80, 30), fill=white, width=1)
        d.line((60, 30, 80, 12), fill=white, width=1)
        d.rectangle((67, 15, 73, 19), fill=(225, 45, 45, 255))
    else:  # hq: a small building with a flag
        d.rectangle((61, 20, 79, 31), outline=white, width=2)
        d.line((58, 20, 70, 12, 82, 20), fill=white, width=2)
        d.rectangle((68, 25, 72, 31), fill=white)
        d.line((70, 12, 70, 4), fill=white, width=2)
        d.polygon([(70, 4), (78, 7), (70, 10)], fill=(225, 45, 45, 255))
    col, row = new_tile % 5, new_tile // 5
    img.paste(tile, (col * TILE, row * TILE))
    return img


def rebuild_zip(src, dst, replace):
    """Copies every entry (recompressed), with `replace` = {name: bytes} replacing or adding entries."""
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, 'w', compression=zipfile.ZIP_DEFLATED) as zout:
        names = set()
        for info in zin.infolist():
            names.add(info.filename)
            data = replace.get(info.filename, None)
            if data is None:
                data = zin.read(info.filename)
            out = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            out.compress_type = zipfile.ZIP_STORED if info.is_dir() else zipfile.ZIP_DEFLATED
            out.external_attr = info.external_attr
            zout.writestr(out, data)
        for name, data in replace.items():
            if name not in names:
                zout.writestr(name, data)


def main(game, staging, password_hex, base_sheet=None):
    """base_sheet: a ModesRibbons.dds to start from when the game's ui.dat has none (ToyBattles: ToyHeroes' sheet, its
    ribbons 01..05 are the same games; tile 5 = ToyBattles' ribbon 06, unknown: cleared)."""
    password = bytes.fromhex(password_hex)
    data_dir = os.path.join(staging, 'data')
    os.makedirs(data_dir, exist_ok=True)

    # ui.dat: the staged one (with room_partygames_3.dds) if present, else the game's
    ui_src = os.path.join(data_dir, 'ui.dat')
    if not os.path.exists(ui_src):
        ui_src = os.path.join(game, 'data', 'ui.dat')
    with zipfile.ZipFile(ui_src) as z:
        entry = next((n for n in z.namelist() if n.lower() == 'icon/modesribbons.dds'), None)
        if entry is not None:
            sheet = Image.open(io.BytesIO(z.read(entry))).convert('RGBA')
        else:
            sheet = Image.open(base_sheet).convert('RGBA')
            for tile in range(5, GAME_TILES):  # tiles of the game's own ribbons we do not have the image of
                col, row = tile % 5, tile // 5
                sheet.paste((0, 0, 0, 0), (col * TILE, row * TILE, (col + 1) * TILE, (row + 1) * TILE))
    ribbons = sheet
    for tile, _, _, emblem in RIBBONS:
        ribbons = make_ribbon(ribbons, tile, emblem)
    buf = io.BytesIO()
    ribbons.save(buf, 'DDS')
    ribbons.save(os.path.join(staging, 'ModesRibbons_preview.png'))
    tmp = os.path.join(data_dir, 'ui.dat.new')
    rebuild_zip(ui_src, tmp, {'icon/ModesRibbons.dds': buf.getvalue()})
    os.replace(tmp, os.path.join(data_dir, 'ui.dat'))

    # cgd.dip: + the icon and its tag
    work = os.path.join(staging, 'cgd_work')
    os.makedirs(work, exist_ok=True)
    entries = []
    with zipfile.ZipFile(os.path.join(game, 'data', 'cgd.dip')) as z:
        for info in z.infolist():
            data = b'' if info.is_dir() else z.read(info.filename, pwd=password)
            entries.append([info.filename, data, info.is_dir()])
    for i, (name, data, is_dir) in enumerate(entries):
        if name not in ('iconsinfo.cdb', 'iconstag.cdb'):
            continue
        path = os.path.join(work, name)
        open(path, 'wb').write(data)
        cdb = Cdb(path)
        cdb.parseCdb()
        for tile, icon, tag, _ in RIBBONS:
            if name == 'iconsinfo.cdb':
                if any(e[0].value == icon for e in cdb.entries):
                    continue
                values = [icon, 'ModesRibbons.dds', tile, TILE, TILE, 512, 1]
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
    write_encrypted_zip(os.path.join(data_dir, 'cgd.dip'), entries, password)
    # check: every entry readable with the password
    with zipfile.ZipFile(os.path.join(data_dir, 'cgd.dip')) as z:
        for info in z.infolist():
            if not info.is_dir():
                z.read(info.filename, pwd=password)
    print('ok')


if __name__ == '__main__':
    # make_hardpoint_icon.py <game> <staging> <cgd password hex> [<base ModesRibbons sheet>]
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
