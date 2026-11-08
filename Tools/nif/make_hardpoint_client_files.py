"""Hardpoint and Headquarters client files, written to a staging folder (the game folder is never touched):
  data/ui/scene/SCENE_ROOM.xml   + settings combo boxes / captions of party game 6 (ids 110039..110044) and
                                   party game 7 (ids 110045..110052)
  data/ui/scene/SCENE_BATTLE.xml + texture room_partygames_3.dds and image components 2010..2025, the Hardpoint
                                   score box 137056..137058, the Headquarters bars / texts 137059..137062
  data/ui.dat                    + setup/ENG/room_partygames_3.dds (make_hardpoint_ui.py)
  data/map.dat                   + party/HardpointRing*.nif / hardpoint_ring_*.dds (make_hardpoint_ring.py)

The XML files are edited as bytes (they contain Korean font names in a legacy code page).
Usage: python make_hardpoint_client_files.py <game dir> <staging dir> [--xml <scene dir>] [--ui-files <images dir>]
  ToyBattles: --xml = port_party_ui.py's output (ToyBattles has no party UI of its own), --ui-files = the party images
  ToyBattles' ui.dat lacks (copied from ToyHeroes' ui.dat).
"""
import os
import re
import shutil
import sys
import zipfile

from make_hardpoint_ui import KEY_ROW_HEIGHT, KEY_TITLE_HEIGHT, LAYOUT, LAYOUT4, LAYOUT5

TEXTURE_ID = b'5086632894'
TEXTURE4_ID = b'5086632895'  # room_partygames_4.dds (Prop Hunt banners 2040..2045, scoreboard prop icon 2046)
TEXTURE5_ID = b'5086632896'  # room_partygames_5.dds (Domination banners 2060..2069, round box 2070; D85)
SETTINGS = [
    # new name suffix, caption, lava row to copy (position), combo id[, rows below that row (30 px each)]
    (b'HARDPOINT_ZONETIME', b'Zone Time', b'LAVA_SPEED', 110039),
    (b'HARDPOINT_POINTS', b'Points Limit', b'LAVA_GRACEPERIOD', 110041),
    (b'HARDPOINT_TIMELIMIT', b'Time Limit', b'LAVA_WALLS', 110043),
    (b'HEADQUARTERS_CAPTURETIME', b'Capture Time', b'LAVA_SPEED', 110045),
    (b'HEADQUARTERS_LIFETIME', b'HQ Lifetime', b'LAVA_GRACEPERIOD', 110047),
    (b'HEADQUARTERS_POINTS', b'Points Limit', b'LAVA_WALLS', 110049),
    (b'HEADQUARTERS_TIMELIMIT', b'Time Limit', b'LAVA_HOTPHASE', 110051),
    (b'PROPHUNT_HIDETIME', b'Hide Time', b'LAVA_SPEED', 110053),
    (b'PROPHUNT_ROUNDTIME', b'Round Time', b'LAVA_GRACEPERIOD', 110055),
    (b'PROPHUNT_WHISTLE', b'Radar', b'LAVA_WALLS', 110057),  # the whistle setting became the radar (2026-10-02)
    (b'PROPHUNT_ROUNDS', b'Rounds', b'LAVA_HOTPHASE', 110059),
    (b'PROPHUNT_CHANGES', b'Object Changes', b'LAVA_SLEEPPHASE', 110061),
    (b'PROPHUNT_DECOYS', b'Decoys', b'LAVA_SLEEPPHASE', 110063, 1),
    (b'ELIMSHUFFLE_ROUNDTIME', b'Round Time', b'LAVA_SPEED', 110065),  # Elimination Shuffle (party game 9, D81)
    (b'ELIMSHUFFLE_ROUNDS', b'Rounds', b'LAVA_GRACEPERIOD', 110067),
    (b'DOMINATION_POINTS', b'Points Limit', b'LAVA_SPEED', 110069),  # Domination (party game 10, D85)
    (b'DOMINATION_TIMELIMIT', b'Time Limit', b'LAVA_GRACEPERIOD', 110071),
]
ROW_STEP = 30


def line_with(data, needle):
    start = data.index(needle)
    begin = data.rindex(b'\n', 0, start) + 1
    end = data.index(b'\n', start) + 1
    return begin, end


def strip_previous(data):
    """Removes the lines a previous run added (the game folder may already hold patched files)."""
    markers = (b'HARDPOINT', b'hardpoint', b'HEADQUARTERS', b'_HQ_', b'PROPHUNT', b'_PH_', b'ELIMSHUFFLE', b'DOMINATION',
               b'notice_dom_', b'topdisplay_domination', b'5086632894', b'5086632895', b'5086632896', b'<Font id="808" ',
               b'<Component id="809" ')
    lines = data.split(b'\n')
    kept = [line for line in lines if not any(m in line for m in markers)]
    return b'\n'.join(kept)


# The "Limit Explosives" check boxes (the DLL's room option) in ToyBattles' layout (user 2026-10-03: "not aligned,
# behind other options"):
#  - room settings (E_DLG_ROOM_SET_CBX_EXPLOSIVES, 103038): on "Balance Random" (331, 458) and over "Intrude": under
#    "Observer" (75, 438), left-aligned with it, above the Apply / Cancel row (y 504, x 305..);
#  - room screen (E_DLG_ROOM_MAIN_CBX_EXPLOSIVES_CLAN, 101073, "Limit Expl."): it lay under the "All Settings" button
#    (906, 477); user 2026-10-03: "put it where Team Balance was" (CKBX_ITEM_BALANCE_RANDOM, 662 x 477, "useless": hidden
#    off the screen as ToyBattles hides CKBX_ITEM_BALANCE). The only free room on the room screen is that row, x 662 up
#    to the "All Settings" button (906): right of the Number / Rule / Time column lie SCENE_COMMON_AGORA's E_DLG_BTN_ROOM
#    Observer / Start buttons (860..1009, 517..595), under it the room buttons' snow picture and the battery. The
#    normal rooms' Health / Weapon Balance combos (the DLL's room options; the party rooms show the Party Games button
#    at 757 instead) share it, narrower (user's choice 2026-10-03): Limit Expl. 82, Health 79, Weapon Balance 79.
EXPLOSIVES_PLACES = {  # node: (x, y, width or None)
    b'E_DLG_ROOM_SET_CBX_EXPLOSIVES': (75, 470, None),
    b'E_DLG_ROOM_MAIN_CBX_EXPLOSIVES_CLAN': (662, 477, 82),
    b'E_DLG_ROOM_MAIN_CKBX_ITEM_BALANCE_RANDOM': (-1612, 477, None),
    b'E_DLG_ROOM_MAIN_CBX_DIFFICULTY_HEALTH': (745, 483, 79),
    b'E_DLG_ROOM_MAIN_CBX_DIFFICULTY_HEALTH_PIC': (747, 486, None),
    b'E_DLG_ROOM_MAIN_CBX_DIFFICULTY_AMMO': (826, 483, 79),
    b'E_DLG_ROOM_MAIN_CBX_DIFFICULTY_AMMO_PIC': (828, 486, None),
}


def place_explosives_check_box(data):
    for node, (x, y, width) in EXPLOSIVES_PLACES.items():
        if b'nodename="' + node + b'"' not in data:
            continue
        b, e = line_with(data, b'nodename="' + node + b'"')
        line = re.sub(rb' x="-?\d+" y="-?\d+"', b' x="%d" y="%d"' % (x, y), data[b:e], count=1)
        if width is not None:
            line = re.sub(rb' w="\d+"', b' w="%d"' % width, line, count=1)
        data = data[:b] + line + data[e:]
    return data


def patch_room(src):
    data = place_explosives_check_box(strip_previous(open(src, 'rb').read()))
    new_lines = b''
    for suffix, caption, lava, combo_id, *below in SETTINGS:
        for kind, cid in ((b'CBX', combo_id), (b'STC', combo_id + 1)):
            b, e = line_with(data, b'nodename="E_DLG_ROOM_PARTYGAMESSET_' + kind + b'_SETTING_' + lava + b'"')
            line = data[b:e]
            line = line.replace(b'_SETTING_' + lava + b'"', b'_SETTING_' + suffix + b'"')
            line = re.sub(rb' id="\d+"', b' id="' + str(cid).encode() + b'"', line, count=1)
            if kind == b'STC':
                line = re.sub(rb' text="[^"]*"', b' text="' + caption + b'"', line, count=1)
            if below:
                line = re.sub(rb' y="(\d+)"', lambda m: b' y="%d"' % (int(m.group(1)) + ROW_STEP * below[0]), line, count=1)
            new_lines += line
    b, e = line_with(data, b'nodename="E_DLG_ROOM_PARTYGAMESSET_STC_SETTING_LAVA_SLEEPPHASE"')
    return data[:e] + new_lines + data[e:]


def patch_battle(src):
    data = strip_previous(open(src, 'rb').read())
    b, e = line_with(data, b'<Texture id="5086632893"')
    texture = data[b:e].replace(b'5086632893', TEXTURE_ID).replace(b'room_partygames_2.dds', b'room_partygames_3.dds')
    texture4 = texture.replace(TEXTURE_ID, TEXTURE4_ID).replace(b'room_partygames_3.dds', b'room_partygames_4.dds')
    texture4 = re.sub(rb'height="\d+" width="\d+"', b'height="1024" width="512"', texture4, count=1)
    texture5 = texture.replace(TEXTURE_ID, TEXTURE5_ID).replace(b'room_partygames_3.dds', b'room_partygames_5.dds')
    data = data[:e] + texture + texture4 + texture5 + data[e:]
    b, e = line_with(data, b'<Component id="620"')
    template = data[b:e]
    components = b''
    for name, (x, y, w, h, cid) in LAYOUT.items():
        line = template.replace(b'id="620"', b'id="%d"' % cid).replace(b'5086632893', TEXTURE_ID)
        line = line.replace(b'nodename="notice_sleepphase"', b'nodename="' + name.encode() + b'"')
        line = re.sub(rb'texcoord="[^"]*"', b'texcoord="%d.%d.%d.%d"' % (x, y, x + w, y + h), line, count=1)
        components += line
    for name, (x, y, w, h, cid) in LAYOUT5.items():
        line = template.replace(b'id="620"', b'id="%d"' % cid).replace(b'5086632893', TEXTURE5_ID)
        line = line.replace(b'nodename="notice_sleepphase"', b'nodename="' + name.encode() + b'"')
        line = re.sub(rb'texcoord="[^"]*"', b'texcoord="%d.%d.%d.%d"' % (x, y, x + w, y + h), line, count=1)
        components += line
    for name, (x, y, w, h, cid) in LAYOUT4.items():
        line = template.replace(b'id="620"', b'id="%d"' % cid).replace(b'5086632893', TEXTURE4_ID)
        line = line.replace(b'nodename="notice_sleepphase"', b'nodename="' + name.encode() + b'"')
        line = re.sub(rb'texcoord="[^"]*"', b'texcoord="%d.%d.%d.%d"' % (x, y, x + w, y + h), line, count=1)
        components += line
    data = data[:e] + components + data[e:]
    # a big font for the score (the 24 pt of the ZSM round labels) + its font component
    b, e = line_with(data, b'<Font id="6" ')
    big_font = data[b:e].replace(b'<Font id="6" ', b'<Font id="808" ')
    b, e = line_with(data, b'<Component id="805" ')
    big_component = data[b:e].replace(b'<Component id="805" ', b'<Component id="809" ').replace(b'font="804"', b'font="808"')
    big_component = big_component.replace(b'nodename="basic_b3"', b'nodename="hardpoint_score"')
    data = data[:e] + big_font + big_component + data[e:]
    # score box + the two team scores, centred at the top of E_DLG_BATTLE_PARTYGAME
    b, e = line_with(data, b'nodename="E_DLG_BATTLE_PARTYGAME_PIC_LAVA_SLEEPPHASEACTIVE"')
    box = data[b:e].replace(b'E_DLG_BATTLE_PARTYGAME_PIC_LAVA_SLEEPPHASEACTIVE', b'E_DLG_BATTLE_PARTYGAME_PIC_HARDPOINT_SCOREBOX')
    box = re.sub(rb' id="\d+"', b' id="137056"', box, count=1)
    box = re.sub(rb'ctrlAlignType="\d+" alignOffset="-?\d+"', b'ctrlAlignType="3" alignOffset="0"', box, count=1)
    box = re.sub(rb' x="-?\d+" y="-?\d+" w="\d+" h="\d+"', b' x="0" y="8" w="360" h="70"', box, count=1)
    box = re.sub(rb'Element_0="\d+"', b'Element_0="2017"', box, count=1)
    b2, e2 = line_with(data, b'nodename="E_DLG_BATTLE_PARTYGAME_STC_TEAMCLASH_TEAMSCORE1"')
    scores = b''
    for name, cid, offset in ((b'REDSCORE', 137057, b'-88'), (b'BLUESCORE', 137058, b'88')):
        line = data[b2:e2].replace(b'E_DLG_BATTLE_PARTYGAME_STC_TEAMCLASH_TEAMSCORE1', b'E_DLG_BATTLE_PARTYGAME_STC_HARDPOINT_' + name)
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        line = re.sub(rb'alignOffset="-?\d+"', b'alignOffset="' + offset + b'"', line, count=1)
        line = re.sub(rb' x="-?\d+" y="-?\d+" w="\d+" h="\d+"', b' x="0" y="26" w="150" h="44"', line, count=1)
        line = re.sub(rb'display_font="\d+"', b'display_font="809"', line, count=1)
        line = re.sub(rb'fontColorNormal="-?\d+"', b'fontColorNormal="16777215"', line, count=1)
        line = re.sub(rb' text="[^"]*"', b' text="0"', line, count=1)
        scores += line
    letters = b''
    for letter, cid, offset in ((b'A', 137081, b'-46'), (b'B', 137082, b'0'), (b'C', 137083, b'46')):
        # Domination: the zones' letters under the team panel, in their owner's colour (the DLL shows them)
        line = data[b2:e2].replace(b'E_DLG_BATTLE_PARTYGAME_STC_TEAMCLASH_TEAMSCORE1', b'E_DLG_BATTLE_PARTYGAME_STC_DOMINATION_' + letter)
        line = re.sub(rb' invisible="\d"', b'', line)
        line = re.sub(rb'nodename="([^"]*)"', rb'nodename="\1" invisible="1"', line, count=1)
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        line = re.sub(rb'ctrlAlignType="\d+" alignOffset="-?\d+"', b'ctrlAlignType="3" alignOffset="' + offset + b'"', line, count=1)
        line = re.sub(rb' x="-?\d+" y="-?\d+" w="\d+" h="\d+"', b' x="0" y="118" w="44" h="44"', line, count=1)
        line = re.sub(rb'display_font="\d+"', b'display_font="809"', line, count=1)
        line = re.sub(rb'fontColorNormal="-?\d+"', b'fontColorNormal="16777215"', line, count=1)
        line = re.sub(rb' text="[^"]*"', b' text="' + letter + b'"', line, count=1)
        letters += line
    return (data[:e] + box + scores + letters + headquarters_controls(data, data[b2:e2]) + prop_hunt_controls(data, data[b2:e2]) +
            data[e:])


def prop_hunt_controls(data, party_line):
    """Prop Hunt: two whole-screen pictures (black: hunters while the props hide, white: flash; drawn first, render
    level 0) and a status line at the bottom centre (copy of Bomb Battle's message line). Hidden until the DLL shows
    them."""
    indent = party_line[:len(party_line) - len(party_line.lstrip())]
    out = b''
    b, e = line_with(data, b'nodename="E_DLG_BATTLE_PARTYGAME_PIC_LAVA_SLEEPPHASEACTIVE"')
    for name, cid, element in ((b'E_DLG_BATTLE_PARTYGAME_PIC_PH_BLACKOUT', 137063, b'2032'),
                               (b'E_DLG_BATTLE_PARTYGAME_PIC_PH_FLASH', 137064, b'2033')):
        line = indent + data[b:e].lstrip()
        line = re.sub(rb' invisible="\d"', b'', line)  # once only (a duplicate attribute breaks the game's parser)
        line = re.sub(rb'nodename="[^"]*"', b'nodename="' + name + b'" invisible="1"', line, count=1)
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        line = re.sub(rb'renderlevel="\d+"', b'renderlevel="0"', line, count=1)
        line = re.sub(rb'ctrlAlignType="\d+" alignOffset="-?\d+"', b'ctrlAlignType="3" alignOffset="0"', line, count=1)
        line = re.sub(rb' x="-?\d+" y="-?\d+" w="\d+" h="\d+"', b' x="0" y="0" w="4000" h="3000"', line, count=1)
        out += re.sub(rb'Element_0="\d+"', b'Element_0="' + element + b'"', line, count=1)
    b, e = line_with(data, b'nodename="E_DLG_BMB_STC_BMB_MSG"')
    line = indent + data[b:e].lstrip()
    line = re.sub(rb' invisible="\d"', b'', line)
    line = re.sub(rb'nodename="[^"]*"', b'nodename="E_DLG_BATTLE_PARTYGAME_STC_PH_STATUS" invisible="1"', line, count=1)
    line = re.sub(rb' id="\d+"', b' id="137065"', line, count=1)
    line = re.sub(rb' y="-?\d+"', b' y="630"', line, count=1)
    out += re.sub(rb' text="[^"]*"', b' text=""', line, count=1)
    # the props' key panel (user 2026-10-02: the status line at the bottom was hard to read): a picture top right
    # (image 2047, right-aligned 10 px from the edge, under the class icons) with a title static (phase / time) and the
    # uses left of the change / decoy / flash rows (statics on the right of rows 1, 3, 4)
    px, py, pw, ph = 1014 - 230, 160, 230, 196
    b, e = line_with(data, b'nodename="E_DLG_BATTLE_PARTYGAME_PIC_LAVA_SLEEPPHASEACTIVE"')
    line = indent + data[b:e].lstrip()
    line = re.sub(rb' invisible="\d"', b'', line)
    line = re.sub(rb'nodename="[^"]*"', b'nodename="E_DLG_BATTLE_PARTYGAME_PIC_PH_KEYS" invisible="1"', line, count=1)
    line = re.sub(rb' id="\d+"', b' id="137066"', line, count=1)
    line = re.sub(rb'ctrlAlignType="\d+" alignOffset="-?\d+"', b'ctrlAlignType="2" alignOffset="-10"', line, count=1)
    line = re.sub(rb' x="-?\d+" y="-?\d+" w="\d+" h="\d+"', b' x="%d" y="%d" w="%d" h="%d"' % (px, py, pw, ph), line, count=1)
    out += re.sub(rb'Element_0="\d+"', b'Element_0="2047"', line, count=1)
    b, e = line_with(data, b'nodename="E_DLG_BMB_STC_BMB_MSG"')
    statics = [(b'TITLE', 137067, px + 8, py + 6, pw - 16, KEY_TITLE_HEIGHT - 8)]
    for row, name, cid in ((1, b'CHANGES', 137068), (3, b'DECOYS', 137069), (4, b'FLASHES', 137070)):
        statics.append((name, cid, px + pw - 66, py + KEY_TITLE_HEIGHT + row * KEY_ROW_HEIGHT + 2, 56, 24))
    for name, cid, x, y, w, h in statics:
        line = indent + data[b:e].lstrip()
        line = re.sub(rb' invisible="\d"', b'', line)
        line = re.sub(rb'nodename="[^"]*"', b'nodename="E_DLG_BATTLE_PARTYGAME_STC_PH_KEYS_' + name + b'" invisible="1"', line, count=1)
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        line = re.sub(rb'ctrlAlignType="\d+" alignOffset="-?\d+"', b'ctrlAlignType="2" alignOffset="%d"' % -(1024 - (x + w)), line, count=1)
        line = re.sub(rb' x="-?\d+" y="-?\d+" w="\d+" h="\d+"', b' x="%d" y="%d" w="%d" h="%d"' % (x, y, w, h), line, count=1)
        line = re.sub(rb'renderlevel="\d+"', b'renderlevel="4"', line, count=1)
        out += re.sub(rb' text="[^"]*"', b' text=""', line, count=1)
    return out


def headquarters_controls(data, party_line):
    """Headquarters: two progress bars copied from Bomb Battle's plant gauge (E_DLG_BMB_PRG_BMB_SET_GAUGE: back 197,
    fill 191 red / 2024 blue), a caption under them and a status line above the bars, below the announcements (y 210: the battle timer is at y 91..113) (from E_DLG_BMB_STC_BMB_MSG).
    All hidden until the DLL shows them."""
    indent = party_line[:len(party_line) - len(party_line.lstrip())]

    def copy(anchor, name, cid):
        b, e = line_with(data, anchor)
        line = indent + data[b:e].lstrip()
        line = re.sub(rb' invisible="\d"', b'', line)
        line = re.sub(rb'nodename="[^"]*"', b'nodename="' + name + b'" invisible="1"', line, count=1)
        return re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)

    out = b''
    for name, cid, fill in ((b'E_DLG_BATTLE_PARTYGAME_PRG_HQ_RED', 137059, b'191'), (b'E_DLG_BATTLE_PARTYGAME_PRG_HQ_BLUE', 137060, b'2024')):
        line = copy(b'nodename="E_DLG_BMB_PRG_BMB_SET_GAUGE"', name, cid)
        line = re.sub(rb'Element_1="\d+"', b'Element_1="' + fill + b'"', line, count=1)
        out += re.sub(rb' value="\d+"', b' value="0"', line, count=1)
    for name, cid, y in ((b'E_DLG_BATTLE_PARTYGAME_STC_HQ_CAPTION', 137061, b'268'), (b'E_DLG_BATTLE_PARTYGAME_STC_HQ_STATUS', 137062, b'210')):
        line = copy(b'nodename="E_DLG_BMB_STC_BMB_MSG"', name, cid)
        line = re.sub(rb' y="-?\d+"', b' y="' + y + b'"', line, count=1)
        out += re.sub(rb' text="[^"]*"', b' text=""', line, count=1)
    return out


def append_to_zip(src, dst, files):
    """Adds `files`; entries a previous run added are replaced (the archive is then rebuilt, which takes a while)."""
    with zipfile.ZipFile(src) as z:
        have = {i.filename for i in z.infolist()}
    if not any(arcname in have for arcname, _ in files):
        shutil.copyfile(src, dst)
        with zipfile.ZipFile(dst, 'a', compression=zipfile.ZIP_DEFLATED) as z:
            for arcname, path in files:
                z.write(path, arcname)
    else:
        replace = {arcname: open(path, 'rb').read() for arcname, path in files}
        tmp = dst + '.new'
        with zipfile.ZipFile(src) as zin, zipfile.ZipFile(tmp, 'w', compression=zipfile.ZIP_DEFLATED) as zout:
            for info in zin.infolist():
                data = replace.pop(info.filename, None)
                if data is None:
                    data = zin.read(info.filename)
                out = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                out.compress_type = zipfile.ZIP_STORED if info.is_dir() else zipfile.ZIP_DEFLATED
                out.external_attr = info.external_attr
                zout.writestr(out, data)
            for arcname, data in replace.items():
                zout.writestr(arcname, data)
        os.replace(tmp, dst)
    with zipfile.ZipFile(dst) as z:
        assert z.testzip() is None


def main(game, staging, xml_dir=None, ui_files=None):
    """xml_dir: the scene XML to patch (default: the game's data/ui/scene; ToyBattles: the output of
    port_party_ui.py); ui_files: a folder of images added to ui.dat as setup/ENG/<name> (ToyBattles: the party images
    it lacks, room_partygames_2.dds and ui_extension.dds, copied from ToyHeroes' ui.dat)."""
    data = os.path.join(game, 'data')
    out = os.path.join(staging, 'data')
    scene = xml_dir or os.path.join(data, 'ui', 'scene')
    os.makedirs(os.path.join(out, 'ui', 'scene'), exist_ok=True)
    open(os.path.join(out, 'ui', 'scene', 'SCENE_ROOM.xml'), 'wb').write(patch_room(os.path.join(scene, 'SCENE_ROOM.xml')))
    open(os.path.join(out, 'ui', 'scene', 'SCENE_BATTLE.xml'), 'wb').write(patch_battle(os.path.join(scene, 'SCENE_BATTLE.xml')))
    images = [('setup/ENG/room_partygames_3.dds', os.path.join(staging, 'ui', 'setup', 'ENG', 'room_partygames_3.dds'))]
    four = os.path.join(staging, 'ui', 'setup', 'ENG', 'room_partygames_4.dds')
    if os.path.exists(four):
        images.append(('setup/ENG/room_partygames_4.dds', four))
    five = os.path.join(staging, 'ui', 'setup', 'ENG', 'room_partygames_5.dds')  # Domination (D85)
    if os.path.exists(five):
        images.append(('setup/ENG/room_partygames_5.dds', five))
    lock = os.path.join(staging, 'ui', 'setup', 'ENG', 'item_lock.dds')  # make_item_lock_ui.py (item lock, D72)
    if os.path.exists(lock):
        images.append(('setup/ENG/item_lock.dds', lock))
    icons = os.path.join(staging, 'ui', 'minimap', 'bat_minimap.dds')  # make_minimap_icons.py (zones on the TAB map, D86)
    if os.path.exists(icons):
        images.append(('minimap/bat_minimap.dds', icons))
    if ui_files:
        images += [('setup/ENG/' + f, os.path.join(ui_files, f)) for f in sorted(os.listdir(ui_files))]
    append_to_zip(os.path.join(data, 'ui.dat'), os.path.join(out, 'ui.dat'), images)
    party = os.path.join(staging, 'party')
    files = [('party/' + f, os.path.join(party, f)) for f in sorted(os.listdir(party))]
    # Prop Hunt objects in the maps' folders (make_prop_models.py)
    maps = os.path.join(staging, 'maps')
    if os.path.isdir(maps):
        for d in sorted(os.listdir(maps)):
            files += [(d + '/' + f, os.path.join(maps, d, f)) for f in sorted(os.listdir(os.path.join(maps, d)))]
    append_to_zip(os.path.join(data, 'map.dat'), os.path.join(out, 'map.dat'), files)
    print('staged in', out)


if __name__ == '__main__':
    # make_hardpoint_client_files.py <game> <staging> [--xml <scene dir>] [--ui-files <images dir>]
    options = dict(zip(sys.argv[3::2], sys.argv[4::2]))
    main(sys.argv[1], sys.argv[2], options.get('--xml'), options.get('--ui-files'))
