"""The official team score / round panel of the game's Elimination (E_DLG_SAB) for the party games (D81), written to
the staging folder after make_hardpoint_client_files.py:
  data/ui/scene/SCENE_BATTLE.xml  + E_DLG_BATTLE_PARTYGAME_*_TR_* (137071..137080): copies of E_DLG_SAB's blue / red
                                  team backs (components 600 / 601), team points, the middle number (rounds / points
                                  limit), the ROUND picture (1349) + round number and the round timer (minutes, colon,
                                  seconds); hidden until the DLL shows them (Elimination Shuffle, Hardpoint,
                                  Headquarters)
  data/ui/scene/SCENE_LOADING.xml + the same panel in the loading screen (E_DLG_LOADING_MAIN_*_TR_*, 101009..101015):
                                  Elimination Shuffle shows the next round and the rounds won while the next map loads.
                                  The scene has its own tables: texture bat_01.dds, components 600 / 601 / 602 / 1349 /
                                  626 / 627 and font 8 copied from SCENE_BATTLE.
The XML files are edited as bytes (Korean font names in a legacy code page). Idempotent (removes what a previous run
added: lines with _TR_ and lines equal to the copied table rows).
Usage: python make_team_score_ui.py <game dir> <staging dir>
"""
import os
import re
import sys

MARKERS = (b'_TR_',)

# (E_DLG_SAB control, our suffix, battle id, loading id, loading y)
CONTROLS = [
    (b'PIC_BLUE_ROUNDTBACK', b'PIC_TR_BLUE_BACK', 137071, 101009, 140),
    (b'PIC_RED_ROUNDTBACK', b'PIC_TR_RED_BACK', 137072, 101010, 140),
    (b'STC_BLUE_POINT', b'STC_TR_BLUE_POINT', 137073, 101011, 165),
    (b'STC_RED_POINT', b'STC_TR_RED_POINT', 137074, 101012, 165),
    (b'STC_SAD_RULE', b'STC_TR_RULE', 137075, 101013, 169),
    (b'PIC_ROUND', b'PIC_TR_ROUND', 137076, 101014, 104),
    (b'STC_ROUNDNO', b'STC_TR_ROUNDNO', 137077, 101015, 104),
    (b'STC_SAD_TIME_M', b'STC_TR_TIME_M', 137078, None, None),
    (b'PIC_TIME_C', b'PIC_TR_TIME_C', 137079, None, None),
    (b'STC_SAD_TIME_S', b'STC_TR_TIME_S', 137080, None, None),
]
LOADING_TEXTURE = b'4086632892'  # bat_01.dds
LOADING_COMPONENTS = (600, 601, 602, 1349, 626, 627)
LOADING_FONTS = (8,)             # 626's font (Arial 14 bold); 627's font 3 is the same in both scenes


def line_with(data, needle):
    start = data.index(needle)
    begin = data.rindex(b'\n', 0, start) + 1
    end = data.index(b'\n', start) + 1
    return begin, end


def strip_previous(data):
    return b'\n'.join(line for line in data.split(b'\n') if not any(m in line for m in MARKERS))


def hidden_copy(line, name, cid, indent):
    line = indent + line.lstrip()
    line = re.sub(rb' invisible="\d"', b'', line)  # once only (a duplicate attribute stops the game's parser)
    line = re.sub(rb'nodename="[^"]*"', b'nodename="' + name + b'" invisible="1"', line, count=1)
    return re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)


def sab_line(battle, suffix):
    b, e = line_with(battle, b'nodename="E_DLG_SAB_' + suffix + b'"')
    return battle[b:e]


def patch_battle(path):
    data = strip_previous(open(path, 'rb').read())
    b, e = line_with(data, b'nodename="E_DLG_BATTLE_PARTYGAME_PIC_LAVA_SLEEPPHASEACTIVE"')
    indent = data[b:e][:len(data[b:e]) - len(data[b:e].lstrip())]
    out = b''
    for sab, suffix, cid, _, _ in CONTROLS:
        out += hidden_copy(sab_line(data, sab), b'E_DLG_BATTLE_PARTYGAME_' + suffix, cid, indent)
    return data[:e] + out + data[e:]


def table_line(battle, needle):
    b, e = line_with(battle, needle)
    return battle[b:e]


def patch_loading(src, battle):
    data = strip_previous(open(src, 'rb').read())
    # tables, copied as they are (the game may look components up by name): the texture after the last texture, the
    # components after the last component, the font after the last font. A previous run's copies (the game folder may
    # hold an installed file) are removed first: lines equal to them.
    rows = {
        b'<Texture ': table_line(battle, b'<Texture id="' + LOADING_TEXTURE + b'"'),
        b'<Component ': b''.join(table_line(battle, b'<Component id="%d"' % c) for c in LOADING_COMPONENTS),
        b'<Font ': b''.join(table_line(battle, b'<Font id="%d" ' % f) for f in LOADING_FONTS),
    }
    copied = {line.strip() for block in rows.values() for line in block.split(b'\n') if line.strip()}
    data = b'\n'.join(line for line in data.split(b'\n') if line.strip() not in copied)
    for tag in (b'<Font ', b'<Texture ', b'<Component '):
        last = data.rindex(tag)
        e = data.index(b'\n', last) + 1
        data = data[:e] + rows[tag] + data[e:]
    b, e = line_with(data, b'nodename="E_DLG_LOADING_MAIN_STC_PLAYTIP"')
    indent = data[b:e][:len(data[b:e]) - len(data[b:e].lstrip())]
    out = b''
    for sab, suffix, _, cid, y in CONTROLS:
        if cid is None:
            continue
        line = hidden_copy(sab_line(battle, sab), b'E_DLG_LOADING_MAIN_' + suffix, cid, indent)
        line = re.sub(rb' y="-?\d+"', b' y="%d"' % y, line, count=1)
        line = re.sub(rb'layerlevel="\d+"', b'layerlevel="4"', line, count=1)
        line = re.sub(rb'renderlevel="\d+"', b'renderlevel="4"', line, count=1)
        if suffix == b'STC_TR_ROUNDNO':  # the loading scene's font component 4 is a small one: Arial 14 bold
            line = re.sub(rb'display_font="\d+"', b'display_font="626"', line, count=1)
            line = re.sub(rb'fontColorNormal="-?\d+"', b'fontColorNormal="-198686"', line, count=1)
        out += line
    return data[:e] + out + data[e:]


def main(game, staging):
    scene = os.path.join(staging, 'data', 'ui', 'scene')
    battle_path = os.path.join(scene, 'SCENE_BATTLE.xml')  # make_hardpoint_client_files.py's output
    battle = patch_battle(battle_path)
    open(battle_path, 'wb').write(battle)
    loading = patch_loading(os.path.join(game, 'data', 'ui', 'scene', 'SCENE_LOADING.xml'), battle)
    open(os.path.join(scene, 'SCENE_LOADING.xml'), 'wb').write(loading)
    print('staged SCENE_BATTLE.xml, SCENE_LOADING.xml in', scene)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
