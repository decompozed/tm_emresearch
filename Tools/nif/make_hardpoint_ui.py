"""Hardpoint + Headquarters + Prop Hunt UI texture: setup/ENG/room_partygames_3.dds (1024 x 1024) for ui.dat, in the style of the party-game
banners of room_partygames_2.dds (360 x 81 panels, icon on the left, bold capital text).

Layout (x, y, w, h) = the texcoord of the SCENE_BATTLE.xml components (ids in the comments), see LAYOUT below;
LAYOUT4: setup/ENG/room_partygames_4.dds (the Prop Hunt banners added 2026-10-02 and the scoreboard's prop icon).

With the game's ui.dat (user 2026-10-02: the drawn banners were "ugly, not MicroVolts-like") every banner is made of
the game's own art (setup/ENG/room_partygames_2.dds): its navy or lava-red banner panel with the text wiped, its icons
(crossed swords with clock / trophy / red X, see GAME_ICONS) or the rendered red barrel of assets/prop_icon.png (the
props; map055 prop_03, nif_render.py), and outlined capitals in Berlin Sans FB Demi (the closest to the game's font).
Without it the old drawn style is used.

Usage: python make_hardpoint_ui.py <out dir> [<game ui.dat>]
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W = H = 1024
PANEL = (54, 64, 84, 255)
BORDER = (104, 118, 146, 255)
WHITE = (240, 240, 240, 255)
RED = (235, 64, 64, 255)
BLUE = (70, 140, 245, 255)
GOLD = (245, 200, 60, 255)

# name: (x, y, w, h, component id)
LAYOUT = {
    'notice_hardpoint_start': (0, 0, 360, 81, 2010),
    'notice_hardpoint_moved': (0, 90, 360, 81, 2011),
    'notice_hardpoint_moving': (0, 180, 360, 81, 2012),
    'notice_hardpoint_redholding': (0, 270, 360, 81, 2013),
    'notice_hardpoint_blueholding': (0, 360, 360, 81, 2014),
    'notice_hardpoint_contested': (0, 450, 360, 81, 2015),
    'topdisplay_hardpoint': (400, 0, 156, 101, 2016),
    'hardpoint_scorebox': (0, 540, 360, 70, 2017),
    # Headquarters (party game 7)
    'notice_hq_online': (400, 120, 360, 81, 2018),
    'notice_hq_captured': (400, 210, 360, 81, 2019),
    'notice_hq_enemycaptured': (400, 300, 360, 81, 2020),
    'notice_hq_destroyed': (400, 390, 360, 81, 2021),
    'notice_hq_expired': (400, 480, 360, 81, 2022),
    'notice_hq_contested': (400, 570, 360, 81, 2023),
    'hq_gauge_blue': (0, 620, 340, 20, 2024),  # same size as Bomb Battle's fills (bat_05.dds 171.384.511.404)
    'topdisplay_hq': (580, 0, 156, 101, 2025),
    # Prop Hunt (party game 8)
    'notice_ph_hide': (0, 660, 360, 81, 2026),
    'notice_ph_wait': (0, 750, 360, 81, 2027),
    'notice_ph_released': (0, 840, 360, 81, 2028),
    'notice_ph_hunterswin': (400, 660, 360, 81, 2029),
    'notice_ph_propswin': (400, 750, 360, 81, 2030),
    'topdisplay_prophunt': (760, 0, 156, 101, 2031),
    'ph_black': (800, 120, 32, 32, 2032),   # stretched over the whole screen (hunters while the props hide)
    'ph_white': (840, 120, 32, 32, 2033),   # flash
}


def font(size):
    for name in ('segoeuib.ttf', 'arialbd.ttf', 'Arial Bold.ttf'):
        try:
            return ImageFont.truetype(os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', name), size)
        except OSError:
            continue
    return ImageFont.load_default()


def panel(draw, x, y, w, h):
    draw.rounded_rectangle((x + 1, y + 1, x + w - 2, y + h - 2), radius=6, fill=PANEL, outline=BORDER, width=2)


def zone_icon(draw, cx, cy, r, colour):
    # the hardpoint: a ring on the ground with a flag
    draw.ellipse((cx - r, cy - r * 0.45, cx + r, cy + r * 0.45), outline=colour, width=4)
    draw.line((cx, cy, cx, cy - r * 1.1), fill=WHITE, width=3)
    draw.polygon([(cx, cy - r * 1.1), (cx + r * 0.7, cy - r * 0.9), (cx, cy - r * 0.7)], fill=colour)


def hq_icon(draw, cx, cy, r, colour):
    # the headquarters: a small building with a flag on its roof
    w, h = r * 1.3, r * 0.8
    draw.rectangle((cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2), fill=PANEL, outline=colour, width=3)
    draw.polygon([(cx - w / 2 - 4, cy - h / 2), (cx, cy - h / 2 - r * 0.55), (cx + w / 2 + 4, cy - h / 2)], outline=colour, fill=PANEL)
    draw.line((cx - w / 2 - 4, cy - h / 2, cx, cy - h / 2 - r * 0.55, cx + w / 2 + 4, cy - h / 2), fill=colour, width=3)
    draw.rectangle((cx - r * 0.15, cy + h / 2 - r * 0.45, cx + r * 0.15, cy + h / 2), fill=colour)
    top = cy - h / 2 - r * 0.55
    draw.line((cx, top, cx, top - r * 0.6), fill=WHITE, width=2)
    draw.polygon([(cx, top - r * 0.6), (cx + r * 0.5, top - r * 0.45), (cx, top - r * 0.3)], fill=colour)


def prop_icon(draw, cx, cy, r, colour):
    # a crate with a question mark: the hidden prop
    draw.rectangle((cx - r * 0.7, cy - r * 0.6, cx + r * 0.7, cy + r * 0.6), fill=PANEL, outline=colour, width=3)
    draw.line((cx - r * 0.7, cy - r * 0.6, cx + r * 0.7, cy + r * 0.6), fill=colour, width=2)
    draw.line((cx - r * 0.7, cy + r * 0.6, cx + r * 0.7, cy - r * 0.6), fill=colour, width=2)
    f = font(int(r * 0.9))
    draw.text((cx - draw.textlength('?', font=f) / 2, cy - r * 0.62), '?', font=f, fill=WHITE)


def eye_icon(draw, cx, cy, r, colour):
    # the hunter: an eye
    draw.ellipse((cx - r, cy - r * 0.5, cx + r, cy + r * 0.5), outline=colour, width=3)
    draw.ellipse((cx - r * 0.35, cy - r * 0.35, cx + r * 0.35, cy + r * 0.35), fill=colour)


def gauge(draw, x, y, w, h, light, dark):
    # Bomb Battle's gauge style: rounded bar, black outline, light-to-dark gradient
    for i in range(w):
        t = i / max(1, w - 1)
        colour = tuple(int(light[k] + (dark[k] - light[k]) * t) for k in range(3)) + (255,)
        draw.line((x + i, y + 2, x + i, y + h - 3), fill=colour)
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), radius=h // 2, fill=255)
    return mask


def swords_icon(draw, cx, cy, r):
    for sign in (1, -1):
        draw.line((cx - sign * r, cy - r, cx + sign * r, cy + r), fill=WHITE, width=5)
        draw.line((cx - sign * r * 0.5, cy + r * 0.2, cx + sign * r * 0.1, cy + r * 0.8), fill=GOLD, width=4)


def text_lines(draw, x, y, w, h, lines):
    """lines: [[(text, colour), ...], ...] centred in the box."""
    size = 26 if len(lines) == 1 else 22
    f = font(size)
    heights = [f.getbbox('Ag')[3] for _ in lines]
    total = sum(heights) + 4 * (len(lines) - 1)
    ty = y + (h - total) / 2 - 2
    for parts, lh in zip(lines, heights):
        width = sum(draw.textlength(t, font=f) for t, _ in parts)
        tx = x + (w - width) / 2
        for t, c in parts:
            draw.text((tx, ty), t, font=f, fill=c)
            tx += draw.textlength(t, font=f)
        ty += lh + 4


# ---- the game's art ----
GAME_PANELS = {'navy': (8, 309), 'red': (378, 770)}   # 360 x 81 banners of room_partygames_2.dds
GAME_ICONS = {                                         # their icon area (left 100 px)
    'swords_clock': (8, 309), 'swords_trophy': (7, 678), 'swords_x': (378, 193)}
ART = {}  # filled by load_art(): 'panel_navy', 'panel_red', icons by name


def load_art(ui_dat):
    import io
    import zipfile
    sheet = Image.open(io.BytesIO(zipfile.ZipFile(ui_dat).read('setup/ENG/room_partygames_2.dds'))).convert('RGBA')
    for name, (x, y) in GAME_PANELS.items():
        banner_image = sheet.crop((x, y, x + 360, y + 81))
        px = banner_image.load()
        # wipe icon and text: every row takes the panel colour of a column right of any text
        for row in range(6, 75):
            colour = px[350, row]
            for column in range(6, 352):
                px[column, row] = colour
        ART['panel_' + name] = banner_image
    for name, (x, y) in GAME_ICONS.items():
        icon = sheet.crop((x + 2, y + 2, x + 100, y + 79))
        # the panel behind the icon becomes transparent (each row: the colour of the banner's column 350)
        px = icon.load()
        for row in range(icon.height):
            pr, pg, pb, _ = sheet.getpixel((x + 350, y + 2 + row))
            for column in range(icon.width):
                r, g, b, a = px[column, row]
                if abs(r - pr) + abs(g - pg) + abs(b - pb) < 30:
                    px[column, row] = (r, g, b, 0)
        ART[name] = icon
    # the swords alone (left of the clock), the trophy and the red X alone (right of the swords)
    ART['swords'] = ART['swords_clock'].crop((0, 0, 58, 77))
    ART['trophy'] = ART['swords_trophy'].crop((62, 24, 98, 77))
    ART['x'] = ART['swords_x'].crop((64, 36, 98, 77))
    ART['prop'] = shaded_prop(64)
    ART['prop_trophy'] = overlay(ART['prop'], ART['trophy'])
    ART['prop_x'] = overlay(ART['prop'], ART['x'])


def shaded_prop(height):
    """assets/prop_icon.png (a red barrel rendered from map055) as a game icon: cylinder shading, dark outline."""
    from PIL import ImageFilter
    icon = Image.open(os.path.join(os.path.dirname(__file__), 'assets', 'prop_icon.png')).convert('RGBA')
    icon = icon.resize((round(icon.width * height / icon.height), height), Image.LANCZOS)
    px = icon.load()
    for x in range(icon.width):
        light = 0.65 + 0.55 * math.sin(math.pi * (x + 0.5) / icon.width) ** 0.6  # lit in the middle, darker sides
        light *= 1.15 if x < icon.width * 0.45 else 1.0
        for y in range(icon.height):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = (min(255, int(r * light)), min(255, int(g * light)), min(255, int(b * light)), a)
    outline = icon.split()[3].filter(ImageFilter.MaxFilter(5))
    out = Image.new('RGBA', (icon.width + 4, icon.height + 4), (0, 0, 0, 0))
    out.paste((24, 20, 28, 255), (0, 0), outline.resize(out.size))
    out.alpha_composite(icon, (2, 2))
    return out


def overlay(base, badge):
    out = Image.new('RGBA', (base.width + badge.width // 2, max(base.height, badge.height)), (0, 0, 0, 0))
    out.alpha_composite(base, (0, out.height - base.height))
    out.alpha_composite(badge, (out.width - badge.width, out.height - badge.height))
    return out


def game_font(size):
    for name in ('BRLNSDB.TTF', 'seguibl.ttf', 'ariblk.ttf'):
        try:
            return ImageFont.truetype(os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', name), size)
        except OSError:
            continue
    return font(size)


def game_banner(img, x, y, panel_name, icon_name, lines):
    img.paste(ART['panel_' + panel_name], (x, y))
    icon = ART[icon_name]
    img.alpha_composite(icon, (x + 8 + max(0, (92 - icon.width) // 2), y + (81 - icon.height) // 2))
    draw = ImageDraw.Draw(img)
    size = 27 if len(lines) == 1 else 23
    f = game_font(size)
    line_height = f.getbbox('AG')[3] + 3
    ty = y + (81 - line_height * len(lines)) / 2
    for parts in lines:
        width = sum(draw.textlength(t, font=f) for t, _ in parts)
        tx = x + 104 + (246 - width) / 2
        for t, c in parts:
            if panel_name == 'red' and c == RED:
                c = GOLD  # red on the lava-red panel cannot be read
            draw.text((tx, ty), t, font=f, fill=c, stroke_width=2, stroke_fill=(28, 30, 44, 255))
            tx += draw.textlength(t, font=f)
        ty += line_height


# key: (panel, icon) in the game's style
GAME_STYLE = {
    'notice_hardpoint_start': ('navy', 'swords_trophy'), 'notice_hardpoint_moved': ('navy', 'swords_clock'),
    'notice_hardpoint_moving': ('red', 'swords_clock'), 'notice_hardpoint_redholding': ('navy', 'swords_trophy'),
    'notice_hardpoint_blueholding': ('navy', 'swords_trophy'), 'notice_hardpoint_contested': ('red', 'swords_x'),
    'notice_hq_online': ('navy', 'swords_trophy'), 'notice_hq_captured': ('navy', 'swords_trophy'),
    'notice_hq_enemycaptured': ('red', 'swords_x'), 'notice_hq_destroyed': ('navy', 'swords_x'),
    'notice_hq_expired': ('navy', 'swords_clock'), 'notice_hq_contested': ('red', 'swords_x'),
    'notice_ph_hide': ('navy', 'prop'), 'notice_ph_wait': ('navy', 'swords_clock'),
    'notice_ph_released': ('red', 'swords_x'), 'notice_ph_hunterswin': ('navy', 'swords_trophy'),
    'notice_ph_propswin': ('navy', 'prop_trophy'),
}
TARGET = {'img': None}


def banner(draw, key, icon, lines):
    x, y, w, h, _ = LAYOUT[key]
    if ART and key in GAME_STYLE:
        game_banner(TARGET['img'], x, y, *GAME_STYLE[key], lines)
        return
    panel(draw, x, y, w, h)
    icon(x + 42, y + h / 2 + 4)
    text_lines(draw, x + 84, y, w - 92, h, lines)


# room_partygames_4.dds (texture 5086632895 of SCENE_BATTLE.xml)
LAYOUT4 = {
    'notice_ph_decoy': (0, 0, 360, 81, 2040),
    'notice_ph_flashhit': (0, 90, 360, 81, 2041),
    'notice_ph_flashmissed': (0, 180, 360, 81, 2042),
    'notice_ph_found': (0, 270, 360, 81, 2043),
    'notice_ph_radar': (0, 360, 360, 81, 2044),
    'notice_ph_radarprops': (0, 450, 360, 81, 2045),
    'ph_row_prop': (400, 0, 28, 28, 2046),   # scoreboard row marker of a prop (28 x 28 like the game's)
    'ph_keys_panel': (0, 560, 230, 196, 2047),  # the props' key panel, top right (user 2026-10-02)
}

# room_partygames_5.dds (texture 5086632896 of SCENE_BATTLE.xml): Domination (party game 10, D85)
LAYOUT5 = {
    'notice_dom_start': (0, 0, 360, 81, 2060),
    'notice_dom_captured_a': (0, 90, 360, 81, 2061),
    'notice_dom_captured_b': (0, 180, 360, 81, 2062),
    'notice_dom_captured_c': (0, 270, 360, 81, 2063),
    'notice_dom_enemy_a': (400, 0, 360, 81, 2064),
    'notice_dom_enemy_b': (400, 90, 360, 81, 2065),
    'notice_dom_enemy_c': (400, 180, 360, 81, 2066),
    'notice_dom_attack_a': (400, 270, 360, 81, 2067),
    'notice_dom_attack_b': (400, 360, 360, 81, 2068),
    'notice_dom_attack_c': (400, 450, 360, 81, 2069),
    'topdisplay_domination': (0, 380, 156, 101, 2070),
}


def sheet5(ui_dat):
    img = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
    banners = {'notice_dom_start': ('navy', 'swords_trophy', [[('CAPTURE', WHITE)], [('THE ZONES!', GOLD)]])}
    for letter in 'abc':
        big = letter.upper()
        banners['notice_dom_captured_' + letter] = ('navy', 'swords_trophy', [[('ZONE ', WHITE), (big, GOLD)], [('CAPTURED!', GOLD)]])
        banners['notice_dom_enemy_' + letter] = ('red', 'swords_x', [[('THE ENEMY', WHITE)], [('CAPTURED ', WHITE), (big, GOLD)]])
        banners['notice_dom_attack_' + letter] = ('red', 'swords_clock', [[('ZONE ', WHITE), (big, GOLD)], [('UNDER ATTACK!', WHITE)]])
    for key, (panel_name, icon_name, lines) in banners.items():
        x, y, _, _, _ = LAYOUT5[key]
        game_banner(img, x, y, panel_name, icon_name, lines)
    x, y, _, _, _ = LAYOUT5['topdisplay_domination']
    img.paste(game_frame(ui_dat, 0.08), (x, y))  # the game's round box, orange
    return img


# the props' key panel: rows of (key, action); the DLL writes the phase / time over the title row and the uses left
# (changes, decoys, flashes) at the right of their rows (make_hardpoint_client_files.py: KEY_PANEL)
KEY_ROWS = [('F4', 'WALL'), ('F6', 'CHANGE'), ('F7', 'LOCK'), ('F8', 'DECOY'), ('F9', 'FLASH')]
KEY_TITLE_HEIGHT = 40
KEY_ROW_HEIGHT = 30


def nine_slice(source, width, height, border=8):
    """source stretched to width x height keeping its border (the game's banner frame) intact."""
    out = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    sw, sh = source.size
    xs = [(0, border, 0, border), (border, sw - border, border, width - border), (sw - border, sw, width - border, width)]
    ys = [(0, border, 0, border), (border, sh - border, border, height - border), (sh - border, sh, height - border, height)]
    for sx0, sx1, dx0, dx1 in xs:
        for sy0, sy1, dy0, dy1 in ys:
            piece = source.crop((sx0, sy0, sx1, sy1)).resize((dx1 - dx0, dy1 - dy0), Image.BICUBIC)
            out.paste(piece, (dx0, dy0))
    return out


def key_panel(width, height):
    panel = nine_slice(ART['panel_navy'], width, height)
    draw = ImageDraw.Draw(panel)
    # title row: a darker band, the DLL writes the phase and the time on it
    draw.rounded_rectangle((6, 6, width - 7, KEY_TITLE_HEIGHT - 2), radius=5, fill=(30, 36, 54, 255))
    f = game_font(19)
    for row, (key, action) in enumerate(KEY_ROWS):
        y = KEY_TITLE_HEIGHT + row * KEY_ROW_HEIGHT + 3
        # the key cap: light, rounded, dark outline, with a darker lower edge (a pressed-in key look)
        draw.rounded_rectangle((12, y + 1, 52, y + 25), radius=5, fill=(70, 74, 92, 255))
        draw.rounded_rectangle((12, y, 52, y + 22), radius=5, fill=(232, 234, 240, 255), outline=(24, 26, 38, 255), width=2)
        tw = draw.textlength(key, font=f)
        draw.text((32 - tw / 2, y + 1), key, font=f, fill=(34, 40, 66, 255))
        draw.text((62, y + 1), action, font=f, fill=WHITE, stroke_width=2, stroke_fill=(28, 30, 44, 255))
    return panel


def sheet4(ui_dat):
    img = Image.new('RGBA', (512, 1024), (0, 0, 0, 0))  # 512 wide, 1024 high: six 81 px banners + room
    banners = {
        'notice_ph_decoy': ('red', 'prop_x', [[("THAT'S A DECOY!", GOLD)], [('-100 HP PER HIT', WHITE)]]),
        'notice_ph_flashhit': ('navy', 'swords_x', [[('FLASH HIT!', GOLD)], [('A HUNTER IS BLINDED', WHITE)]]),
        'notice_ph_flashmissed': ('red', 'swords_clock', [[('FLASH MISSED', WHITE)], [('NO HUNTER NEAR', GOLD)]]),
        'notice_ph_found': ('red', 'prop_x', [[("YOU'VE BEEN FOUND!", WHITE)], [('RUN!', GOLD)]]),
        'notice_ph_radar': ('navy', 'swords_clock', [[('RADAR ONLINE!', GOLD)], [('FIND THE PROPS', WHITE)]]),
        'notice_ph_radarprops': ('red', 'swords_clock', [[('THE HUNTERS', WHITE)], [('HAVE RADAR!', GOLD)]]),
    }
    for key, (panel_name, icon_name, lines) in banners.items():
        x, y, _, _, _ = LAYOUT4[key]
        game_banner(img, x, y, panel_name, icon_name, lines)
    x, y, w, h, _ = LAYOUT4['ph_keys_panel']
    img.alpha_composite(key_panel(w, h), (x, y))
    x, y, w, h, _ = LAYOUT4['ph_row_prop']
    small = shaded_prop(h)
    img.alpha_composite(small.resize((round(small.width * h / small.height), h), Image.LANCZOS), (x + (w - round(small.width * h / small.height)) // 2, y))
    return img


# The round box at the top of the screen ("ROUND N" is written over it): the game's own frame (room_partygames.dds of
# ui.dat, the yellow one at 859.782.1015.883, 156 x 101), its coloured bars turned to the mode's hue (user 2026-10-02:
# the icon drawn over it was "super ugly and not MicroVolts-like").
GAME_FRAME = (859, 782, 1015, 883)
FRAME_HUES = {'topdisplay_hardpoint': None, 'topdisplay_hq': 0.78, 'topdisplay_prophunt': 0.36}


def game_frame(ui_dat, hue):
    import colorsys
    import io
    import zipfile
    sheet = Image.open(io.BytesIO(zipfile.ZipFile(ui_dat).read('setup/ENG/room_partygames.dds'))).convert('RGBA')
    frame = sheet.crop(GAME_FRAME)
    if hue is None:
        return frame
    px = frame.load()
    for y in range(frame.height):
        for x in range(frame.width):
            r, g, b, a = px[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if s > 0.35 and v > 0.2:  # the coloured bars (the white balls and the dark middle stay)
                r2, g2, b2 = colorsys.hsv_to_rgb(hue, s, v)
                px[x, y] = (round(r2 * 255), round(g2 * 255), round(b2 * 255), a)
    return frame


def main(out_dir, ui_dat=None):
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    TARGET['img'] = img
    if ui_dat:
        load_art(ui_dat)
    d = ImageDraw.Draw(img)
    banner(d, 'notice_hardpoint_start', lambda cx, cy: zone_icon(d, cx, cy, 26, GOLD),
           [[('CAPTURE', WHITE)], [('THE ZONE!', GOLD)]])
    banner(d, 'notice_hardpoint_moved', lambda cx, cy: zone_icon(d, cx, cy, 26, GOLD),
           [[('ZONE', GOLD), (' MOVED!', WHITE)]])
    banner(d, 'notice_hardpoint_moving', lambda cx, cy: zone_icon(d, cx, cy, 26, WHITE),
           [[('ZONE', GOLD), (' MOVES', WHITE)], [('IN 10 SECONDS', WHITE)]])
    banner(d, 'notice_hardpoint_redholding', lambda cx, cy: zone_icon(d, cx, cy, 26, RED),
           [[('RED TEAM', RED)], [('IS HOLDING', WHITE)]])
    banner(d, 'notice_hardpoint_blueholding', lambda cx, cy: zone_icon(d, cx, cy, 26, BLUE),
           [[('BLUE TEAM', BLUE)], [('IS HOLDING', WHITE)]])
    banner(d, 'notice_hardpoint_contested', lambda cx, cy: swords_icon(d, cx, cy - 4, 20),
           [[('TEAMS ARE FIGHTING', WHITE)], [('IN THE ZONE', GOLD)]])
    x, y, w, h, _ = LAYOUT['topdisplay_hardpoint']
    panel(d, x, y, w, h)
    zone_icon(d, x + w / 2, y + h / 2 + 12, 34, GOLD)
    # score box: red half | blue half; the numbers are the SCENE_BATTLE.xml statics 137057 / 137058 on top
    x, y, w, h, _ = LAYOUT['hardpoint_scorebox']
    d.rounded_rectangle((x + 1, y + 1, x + w - 2, y + h - 2), radius=12, fill=PANEL, outline=BORDER, width=2)
    for i in range(w // 2 - 6):
        t = i / (w // 2)
        d.line((x + 6 + i, y + 6, x + 6 + i, y + h - 7), fill=(int(150 + 60 * t), 30, 36, 235))
        d.line((x + w - 7 - i, y + 6, x + w - 7 - i, y + h - 7), fill=(28, int(70 + 40 * t), int(170 + 60 * t), 235))
    d.rounded_rectangle((x + w // 2 - 3, y + 4, x + w // 2 + 2, y + h - 5), radius=2, fill=GOLD)
    f = font(13)
    d.text((x + 14, y + 6), 'RED', font=f, fill=WHITE)
    d.text((x + w - 14 - d.textlength('BLUE', font=f), y + 6), 'BLUE', font=f, fill=WHITE)

    # ---- Headquarters ----
    banner(d, 'notice_hq_online', lambda cx, cy: hq_icon(d, cx, cy, 26, GOLD),
           [[('NEW HQ', GOLD), (' ONLINE!', WHITE)], [('CAPTURE IT!', WHITE)]])
    banner(d, 'notice_hq_captured', lambda cx, cy: hq_icon(d, cx, cy, 26, GOLD),
           [[('HQ CAPTURED!', GOLD)], [('NO RESPAWNS!', WHITE)]])
    banner(d, 'notice_hq_enemycaptured', lambda cx, cy: hq_icon(d, cx, cy, 26, RED),
           [[('ENEMY HQ!', RED)], [('DESTROY IT!', WHITE)]])
    banner(d, 'notice_hq_destroyed', lambda cx, cy: hq_icon(d, cx, cy, 26, WHITE),
           [[('HQ', GOLD), (' DESTROYED!', WHITE)], [('NEW HQ IN 5 SECONDS', WHITE)]])
    banner(d, 'notice_hq_expired', lambda cx, cy: hq_icon(d, cx, cy, 26, WHITE),
           [[('HQ', GOLD), (' EXPIRED', WHITE)], [('NEW HQ IN 5 SECONDS', WHITE)]])
    banner(d, 'notice_hq_contested', lambda cx, cy: swords_icon(d, cx, cy - 4, 20),
           [[('TEAMS ARE FIGHTING', WHITE)], [('FOR THE HQ', GOLD)]])
    x, y, w, h, _ = LAYOUT['topdisplay_hq']
    panel(d, x, y, w, h)
    hq_icon(d, x + w / 2, y + h / 2 + 14, 40, GOLD)
    # blue gauge fill: drawn apart, then cut to the rounded bar shape with a black outline
    x, y, w, h, _ = LAYOUT['hq_gauge_blue']
    bar = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bar)
    mask = gauge(bd, 0, 0, w, h, (110, 170, 255), (30, 80, 210))
    clipped = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    clipped.paste(bar, (0, 0), mask)
    ImageDraw.Draw(clipped).rounded_rectangle((0, 0, w - 1, h - 1), radius=h // 2, outline=(0, 0, 0, 255), width=2)
    img.paste(clipped, (x, y))

    # ---- Prop Hunt ----
    banner(d, 'notice_ph_hide', lambda cx, cy: prop_icon(d, cx, cy, 26, GOLD),
           [[('HIDE!', GOLD)], [('BECOME AN OBJECT', WHITE)]])
    banner(d, 'notice_ph_wait', lambda cx, cy: eye_icon(d, cx, cy, 26, RED),
           [[('THE PROPS ARE HIDING', WHITE)], [('WAIT...', GOLD)]])
    banner(d, 'notice_ph_released', lambda cx, cy: eye_icon(d, cx, cy, 26, RED),
           [[('THE HUNTERS', GOLD)], [('ARE COMING!', WHITE)]])
    banner(d, 'notice_ph_hunterswin', lambda cx, cy: eye_icon(d, cx, cy, 26, RED),
           [[('HUNTERS WIN!', RED)], [('ALL PROPS FOUND', WHITE)]])
    banner(d, 'notice_ph_propswin', lambda cx, cy: prop_icon(d, cx, cy, 26, BLUE),
           [[('PROPS WIN!', BLUE)], [('TIME IS UP', WHITE)]])
    x, y, w, h, _ = LAYOUT['topdisplay_prophunt']
    panel(d, x, y, w, h)
    prop_icon(d, x + w / 2, y + h / 2 + 4, 38, GOLD)  # replaced by the game's frame below (ui.dat given)
    if ui_dat:
        for key, hue in FRAME_HUES.items():
            x, y, w, h, _ = LAYOUT[key]
            d.rectangle((x, y, x + w - 1, y + h - 1), fill=(0, 0, 0, 0))
            img.paste(game_frame(ui_dat, hue), (x, y))
    for key, colour in (('ph_black', (0, 0, 0, 255)), ('ph_white', (255, 255, 255, 255))):
        x, y, w, h, _ = LAYOUT[key]
        d.rectangle((x, y, x + w - 1, y + h - 1), fill=colour)

    out = os.path.join(out_dir, 'setup', 'ENG')
    os.makedirs(out, exist_ok=True)
    img.save(os.path.join(out, 'room_partygames_3.dds'), 'DDS')
    img.save(os.path.join(out_dir, 'room_partygames_3_preview.png'))
    if ui_dat:
        four = sheet4(ui_dat)
        four.save(os.path.join(out, 'room_partygames_4.dds'), 'DDS')
        four.save(os.path.join(out_dir, 'room_partygames_4_preview.png'))
        five = sheet5(ui_dat)
        five.save(os.path.join(out, 'room_partygames_5.dds'), 'DDS')
        five.save(os.path.join(out_dir, 'room_partygames_5_preview.png'))
    print('ok')


if __name__ == '__main__':
    # make_hardpoint_ui.py <staging>/ui [<game ui.dat>: the round boxes from the game's own frame]
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
