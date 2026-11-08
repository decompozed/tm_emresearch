"""Client files of the inventory item lock (have_api D72, user 2026-10-02: "every item can be locked and unlocked from
the UI; it must be clear which items are locked").

  setup/ENG/item_lock.dds (ui.dat, texture 5086632899): the padlock drawn over a locked item's slot and the item
                           window's Lock / Unlock buttons
  data/ui/scene/SCENE_COMMON.xml    + the texture, components 69701..69704 and the buttons E_DLG_ITEM_POPUP_BTN_ITEM_LOCK
                                      (148043, open padlock: click to lock) / _UNLOCK (148044, closed padlock) at the
                                      top left of the item window (where the close button is on the right)
  data/ui/scene/SCENE_INVEN_2ND.xml + the texture and component 2050 (the slot padlock; the DLL draws it over the
                                      cells of the inventory's slot lists, ui/inventory/ItemLock.cpp)
  Upgrade Ticket Lv8 / Lv9 (item window, 148063..148073): a bottom row icon and a panel over the stats (the DLL's
  ui/inventory/UpgradeTicket.cpp)

The DLL shows one of the two buttons (the item's state) and draws the padlocks; the server keeps the locks.

  python Tools/nif/make_item_lock_ui.py "C:/Program Files (x86)/ToyBattles" client_files/toybattles/staging

writes staging/data/ui/scene/SCENE_COMMON.xml, SCENE_INVEN_2ND.xml, staging/ui/setup/ENG/item_lock.dds and adds the
image to staging/data/ui.dat (made from the game's ui.dat when missing). Files of the game folder are only read.
"""
import os
import re
import shutil
import sys
import zipfile

from PIL import Image, ImageDraw

TEXTURE_ID = b'5086632899'
TEXTURE_NAME = b'item_lock'
SIZE = 512   # sheet height (rows 256..: the option swap buttons)
WIDTH = 512  # sheet width (the weapon glow palette's swatches at x 256..)
# name: (x, y, w, h, component id)
LAYOUT_COMMON = {
    'item_option_tank': (0, 256, 78, 78, 69810),
    'item_option_tank_on': (80, 256, 78, 78, 69811),
    'item_option_speed': (160, 256, 78, 78, 69812),
    'item_option_speed_on': (240, 256, 78, 78, 69813),
    'item_lock_btn_lock': (196, 0, 26, 25, 69701),
    'item_lock_btn_lock_over': (196, 32, 26, 25, 69702),
    'item_lock_btn_unlock': (226, 0, 26, 25, 69703),
    'item_lock_btn_unlock_over': (226, 32, 26, 25, 69704),
}
# slot overlays, the size of the slots they cover (the DLL stretches them over the slot: 1:1, so the padlock is the
# item window's 26 x 25, user 2026-10-02): the inventory list's 116 x 113 slots (2050 locked / 2051 unlocked) and the
# equipment's 74 x 62 slots (2052 / 2053); the padlock in the top right corner
SLOT_TILES = {
    'item_lock_slot': (0, 0, 116, 113, 2050, True),
    'item_lock_slot_open': (0, 120, 116, 113, 2051, False),
    'item_lock_slot_small': (120, 0, 74, 62, 2052, True),
    'item_lock_slot_small_open': (120, 70, 74, 62, 2053, False),
}
LAYOUT_INVEN = {name: (x, y, w, h, cid) for name, (x, y, w, h, cid, _) in SLOT_TILES.items()}
BUTTONS = [  # node name suffix, id, normal component, over component
    (b'ITEM_LOCK', 148043, 69701, 69702),
    (b'ITEM_UNLOCK', 148044, 69703, 69704),
]
# Weapon glow palette (D75): 15 swatches in the item window (148045 = the effect's own colours, 148046.. = GLOW_PALETTE),
# shown by the DLL for the player's level 8-9 weapons; the current colour is shown "disabled" (a white frame).
# Same colours as Common/include/HaveApi/WeaponGlow.h and the DLL's ui/inventory/WeaponGlow.h.
GLOW_PALETTE = [0xFF2A2A, 0xFF8C1A, 0xFFE135, 0xFFD700, 0x9BFF2A, 0x2AD94A, 0x1EE6E6, 0x6EC8FF, 0x2A5BFF, 0x8C3CFF,
                0xC88CFF, 0xFF6EC8, 0xFF2AD4, 0xFFFFFF]
GLOW_FIRST_ID = 148045
GLOW_SWATCHES = len(GLOW_PALETTE) + 2  # + the last one (148060): no glow at all (a red X)
GLOW_SWATCH = 16
GLOW_X, GLOW_Y, GLOW_STEP = 22, -58, 17  # in the item window's header, above its title
GLOW_COMPONENT = 69710                   # + 3 x swatch: normal, mouse over, selected
GLOW_STATES = ('normal', 'over', 'selected')
for _i in range(GLOW_SWATCHES):
    for _s, _state in enumerate(GLOW_STATES):
        LAYOUT_COMMON['item_lock_glow_%02d_%s' % (_i, _state)] = (256 + _i * GLOW_SWATCH, _s * GLOW_SWATCH, GLOW_SWATCH, GLOW_SWATCH,
                                                                 GLOW_COMPONENT + 3 * _i + _s)


# Chat colour palette (D74): a toggle button in the chat box's tab row opens / closes a row of swatches above the chat box
# (E_DLG_CHAT 107101 toggle, 107102 DEFAULT = the game's colour, 107103.. = CHAT_PALETTE; the DLL shows / hides them and
# sends the choice). Same colours and order as Common/include/HaveApi/ChatColors.h.
CHAT_PALETTE = [0xE63C3C, 0xFF8C1A, 0x3C78FF, 0x78BEFF, 0x1ED7E6, 0xA05AE6, 0xC88CFF, 0xB4783C, 0xAAAAAA, 0xFFFFFF,
                0x3CC83C, 0xFFDC32, 0xFF82C8]
CHAT_TOGGLE_ID = 107101
CHAT_X, CHAT_Y = 2, 50                   # the swatch row, over the chat list (above the box it covered the lobby's character buttons; the tabs' click areas reach y 45)
CHAT_TOGGLE_X, CHAT_TOGGLE_Y = 252, 6    # in the tab row, left of the option button
CHAT_COMPONENT = 69760                   # + 3 x swatch: normal, mouse over, selected; toggle: 69805 normal, 69806 over
CHAT_SHEET_Y = 3 * GLOW_SWATCH           # swatches drawn under the glow swatches
for _i in range(len(CHAT_PALETTE) + 1):
    for _s, _state in enumerate(GLOW_STATES):
        LAYOUT_COMMON['item_lock_chat_%02d_%s' % (_i, _state)] = (256 + _i * GLOW_SWATCH, CHAT_SHEET_Y + _s * GLOW_SWATCH,
                                                                 GLOW_SWATCH, GLOW_SWATCH, CHAT_COMPONENT + 3 * _i + _s)
LAYOUT_COMMON['item_lock_chat_toggle_normal'] = (256 + 15 * GLOW_SWATCH, CHAT_SHEET_Y, GLOW_SWATCH, GLOW_SWATCH, 69805)
LAYOUT_COMMON['item_lock_chat_toggle_over'] = (256 + 15 * GLOW_SWATCH, CHAT_SHEET_Y + GLOW_SWATCH, GLOW_SWATCH, GLOW_SWATCH, 69806)

# Zoom sensitivities (2026-10-03): two rows in the option dialog's Controls tab, under the mouse box's "invert" row:
# title (left column), left arrow, slider 10..300 (% of the game's zoomed look), right arrow, value; ids 103400 + 6 x row
# (+0 title, +1 left, +2 slider, +3 right, +4 value back, +5 value) and a separator line 103412. The DLL
# (ui/options/ZoomSensitivity) sets them, shows them with the Controls tab only and saves them in option.ini. The
# key list section below moves down by ZOOM_SHIFT (absolute positions: the tool can run on its own output).
ZOOM_FIRST_ID = 103400
ZOOM_ROW_Y = (278, 306)
ZOOM_SHIFT = 56
ZOOM_MOVED = {  # element: attributes in the patched dialog (the game's: OUTLINE02 h 132, OUTLINE03 y 272, ...)
    b'E_DLG_OPTION_PIC_CON_OUTLINE02': {b'h': 132 + ZOOM_SHIFT},
    b'E_DLG_OPTION_PIC_CON_OUTLINE03': {b'y': 272 + ZOOM_SHIFT},
    b'E_DLG_OPTION_PIC_CON_OUTLINE04': {b'y': 282 + ZOOM_SHIFT, b'h': 179 - ZOOM_SHIFT},
    b'E_DLG_OPTION_STC_CON_OP_PAD': {b'y': 286 + ZOOM_SHIFT},
    b'E_DLG_OPTION_STC_CON_OP_PAD_USE': {b'y': 286 + ZOOM_SHIFT},
    b'E_DLG_OPTION_RADIO_PAD_ON': {b'y': 285 + ZOOM_SHIFT},
    b'E_DLG_OPTION_RADIO_PAD_OFF': {b'y': 285 + ZOOM_SHIFT},
    b'E_DLG_OPTION_0_LBX_KEYMAP': {b'y': 308 + ZOOM_SHIFT, b'h': 163 - ZOOM_SHIFT},
    b'E_DLG_OPTION_PIC_KEYMAP_BACKIMG': {b'y': 305 + ZOOM_SHIFT},
    b'E_DLG_OPTION_BTN_KEYMAP_OPEN': {b'y': 339},  # the key prompt (120 high) inside the moved key box (338..461)
    b'E_DLG_OPTION_STC_CON_TITLE01': {b'y': 195 + ZOOM_SHIFT // 2},
    b'E_DLG_OPTION_STC_CON_TITLE02': {b'y': 357 + ZOOM_SHIFT // 2},
}
# the key rows (the list box places them while scrolling; their own positions moved too): the game's y values
ZOOM_KEY_ROWS_Y = ([310 + 23 * i for i in range(7)] + [494 + 23 * i for i in range(19)] + [954 + 23 * i for i in range(4)]
                   + [678] * 13)
for _i, _y in enumerate(ZOOM_KEY_ROWS_Y):
    ZOOM_MOVED[b'E_DLG_OPTION_BTN_KEYMAP%02d' % (_i + 1)] = {b'y': _y + ZOOM_SHIFT}

# Enemy outline (2026-10-03): a row of the Graphics tab's quality list after the game's 14 (title 103413, ON 103414,
# OFF 103415, radio group 119001), copies of the "enemy mark" row (OP18); the DLL binds it to the list (0x780AF0, as the
# game's 0x6352C0 does), sets and saves it ([Video_Adv] EnemyOutline) and switches the game's halo option.
OUTLINE_IDS = (103413, 103414, 103415)
OUTLINE_GROUP = 119001
OUTLINE_Y = 644

# Set part option swap (D80 / D91): two icon buttons like the item window's bottom row (copies of its "Gamble" button
# E_DLG_ITEM_POPUP_BTN_SEAL: 78 x 78, layer 3; user 2026-10-03: a click anywhere else in the window closes it, the
# bottom row's buttons get the click). 148061 "Tank" (the item is the Speed one), 148062 "Speed" (the HP one); the DLL
# shows the one for the item (only for the player's items the server says can swap) in a free place of the bottom row,
# the one next to Gamble first.
OPTION_SWAP_TANK_ID = 148061
OPTION_SWAP_SPEED_ID = 148062
OPTION_SWAP_TILE = 78
OPTION_SWAP_SHEET_Y = 256
# name: (x, y, component id) in the sheet; normal / mouse over (as Gamble: blue, red)
OPTION_SWAP_TILES = {
    'item_option_tank': (0, OPTION_SWAP_SHEET_Y, 69810),
    'item_option_tank_on': (80, OPTION_SWAP_SHEET_Y, 69811),
    'item_option_speed': (160, OPTION_SWAP_SHEET_Y, 69812),
    'item_option_speed_on': (240, OPTION_SWAP_SHEET_Y, 69813),
}
OPTION_SWAP_BUTTONS = [  # node name suffix, id, normal component, over component
    (b'OPTION_SWAP_TANK', OPTION_SWAP_TANK_ID, 69810, 69811),
    (b'OPTION_SWAP_SPEED', OPTION_SWAP_SPEED_ID, 69812, 69813),
]
GAMBLE_BLUE = (66, 69, 156, 221)   # the bottom row's icons (item_popup.dds btn_seal_de / _on)
GAMBLE_RED = (232, 34, 34, 230)
CAPTION = (41, 48, 66, 255)

# Upgrade Ticket: the icon (a copy of Gamble like the option swap; the DLL puts it in a free place of the bottom row) opens
# a panel over the stats: PIC_TICKET_PANEL (the window's own two stat boxes, item_popup.dds 15,125 .. 281,381) over
# x 14..280 / y 59..315, the title, three branch buttons, the Lv8 / Lv9 ticket buttons, two text lines, Confirm. The
# DLL writes the texts; its "disabled" look (Element_3) shows the choice.
TICKET_TOGGLE_ID = 148063
TICKET_PANEL = (148064, 14, 59, 266, 256)
TICKET_PANEL_TEXCOORD = b'15.125.281.381'
TICKET_PANEL_COMPONENT = 69822
TICKET_LAYER = 4
TICKET_TEXT_FONT = 685                  # the stat lines' font component
TICKET_TEXT_COLOR = -16764007           # the stat lines' colour
TICKET_BUTTON_TEXT = -1
TICKET_STATICS = [
    (b'STC_TICKET_TITLE', 148065, 22, 64, 250, 20),
    (b'STC_TICKET_INFO_1', 148071, 22, 216, 250, 20),
    (b'STC_TICKET_INFO_2', 148072, 22, 236, 250, 20),
]
TICKET_BUTTONS = [
    (b'BTN_TICKET_BRANCH_1', 148066, 27, 88, 'wide'),
    (b'BTN_TICKET_BRANCH_2', 148067, 27, 118, 'wide'),
    (b'BTN_TICKET_BRANCH_3', 148068, 27, 148, 'wide'),
    (b'BTN_TICKET_LV8', 148069, 27, 184, 'small'),
    (b'BTN_TICKET_LV9', 148070, 151, 184, 'small'),
    (b'BTN_TICKET_CONFIRM', 148073, 77, 268, 'confirm'),
]
TICKET_TILES = {
    'wide': (240, 26, 0, 336, (69823, 69824, 69825)),
    'small': (116, 26, 244, 336, (69826, 69827, 69828)),
    'confirm': (140, 30, 364, 336, (69829, 69830, 69829)),
}
TICKET_ICON = {'item_ticket_icon': (320, 256, 69820), 'item_ticket_icon_on': (400, 256, 69821)}
for _name, (_x, _y, _cid) in TICKET_ICON.items():
    LAYOUT_COMMON[_name] = (_x, _y, OPTION_SWAP_TILE, OPTION_SWAP_TILE, _cid)
for _kind, (_w, _h, _x, _y, _cids) in TICKET_TILES.items():
    for _s, _cid in enumerate(_cids[:2] if _kind == 'confirm' else _cids):
        LAYOUT_COMMON['item_ticket_%s_%d' % (_kind, _s)] = (_x, _y + _s * (_h + 2), _w, _h, _cid)
TICKET_FILL = {'wide': ((66, 69, 156, 235), (98, 104, 214, 245), (232, 34, 34, 235)),
               'small': ((66, 69, 156, 235), (98, 104, 214, 245), (232, 34, 34, 235)),
               'confirm': ((36, 140, 64, 240), (54, 186, 92, 250))}

MARKERS = (b'item_lock', b'item_option_', b'ITEM_LOCK', b'ITEM_UNLOCK', b'BTN_GLOW_', b'E_DLG_CHAT_BTN_COLOR', b'ZOOMSENS',
           b'ENEMYOUTLINE', b'BTN_OPTION_SWAP', b'_TICKET', b'item_ticket', TEXTURE_ID)

GOLD = (255, 204, 64, 255)
WHITE = (245, 245, 250, 255)
OUTLINE = (20, 22, 34, 255)


SCALE = 4  # drawn 4x bigger, then reduced (smooth edges)


def padlock(x, y, w, h, closed, body, shackle, glow=False):
    """Padlock image of w x h (drawn at SCALE): body in the lower part, U shackle above; open = lifted, its left leg
    out of the body."""
    W, H = w * SCALE, h * SCALE
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if glow:
        d.ellipse((0, 0, W - 1, H - 1), fill=(255, 255, 255, 60))
    body_top = H * 0.46
    bw = W * 0.76
    bx = (W - bw) / 2
    sw = bw * 0.64                      # shackle outer width
    sx = (W - sw) / 2
    t = W * 0.14                        # shackle thickness
    top = H * 0.06 if closed else H * 0.0
    lift = 0 if closed else H * 0.10
    for colour, extra in ((OUTLINE, W * 0.06), (shackle, 0)):
        th = t + extra
        # arc: the top half of a ring
        d.arc((sx - extra / 2, top - extra / 2, sx + sw + extra / 2, top + sw + extra / 2), 180, 360, fill=colour, width=round(th))
        legs_from = top + sw / 2
        right_leg_x = sx + sw - t / 2
        left_leg_x = sx + t / 2
        d.line((right_leg_x, legs_from, right_leg_x, body_top + H * 0.05), fill=colour, width=round(th))
        left_end = body_top + H * 0.05 if closed else body_top - lift - H * 0.04
        d.line((left_leg_x, legs_from, left_leg_x, left_end), fill=colour, width=round(th))
    d.rounded_rectangle((bx, body_top, bx + bw, H - SCALE), radius=W * 0.14, fill=body, outline=OUTLINE, width=round(W * 0.07))
    # a light band on the body (shine) and the keyhole
    d.rounded_rectangle((bx + W * 0.1, body_top + H * 0.06, bx + bw - W * 0.1, body_top + H * 0.12), radius=W * 0.03,
                        fill=(255, 255, 255, 90))
    cx = W / 2
    kr = W * 0.075
    ky = body_top + (H - body_top) * 0.36
    d.ellipse((cx - kr, ky - kr, cx + kr, ky + kr), fill=OUTLINE)
    d.polygon([(cx - kr * 0.55, ky), (cx + kr * 0.55, ky), (cx + kr * 0.35, ky + (H - body_top) * 0.36), (cx - kr * 0.35, ky + (H - body_top) * 0.36)], fill=OUTLINE)
    return img.resize((w, h), Image.LANCZOS)


def option_swap_tile(kind, colour):
    """78 x 78 like the bottom row's icons: the symbol (shield = Tank, bolt = Speed) between two swap arrows, the
    caption under it"""
    from PIL import ImageFont
    big = OPTION_SWAP_TILE * SCALE
    img = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = SCALE
    cx, cy = big // 2, 30 * s
    if kind == 'tank':  # shield
        d.polygon([(cx - 13 * s, cy - 15 * s), (cx + 13 * s, cy - 15 * s), (cx + 13 * s, cy + 1 * s), (cx, cy + 16 * s),
                   (cx - 13 * s, cy + 1 * s)], fill=colour)
        d.polygon([(cx - 8 * s, cy - 10 * s), (cx + 8 * s, cy - 10 * s), (cx + 8 * s, cy), (cx, cy + 10 * s), (cx - 8 * s, cy)],
                  fill=(0, 0, 0, 0))
        d.rectangle((cx - 2 * s, cy - 8 * s, cx + 2 * s, cy + 6 * s), fill=colour)
        d.rectangle((cx - 6 * s, cy - 3 * s, cx + 6 * s, cy + 1 * s), fill=colour)
    else:  # lightning bolt
        d.polygon([(cx + 4 * s, cy - 16 * s), (cx - 10 * s, cy + 2 * s), (cx - 1 * s, cy + 2 * s), (cx - 5 * s, cy + 16 * s),
                   (cx + 10 * s, cy - 3 * s), (cx + 1 * s, cy - 3 * s)], fill=colour)
    for side in (-1, 1):  # swap arrows left / right of the symbol: up on the left, down on the right
        x = cx + side * 24 * s
        d.rectangle((x - 2 * s, cy - 9 * s, x + 2 * s, cy + 9 * s), fill=colour)
        tip = cy - 15 * s if side < 0 else cy + 15 * s
        base = cy - 8 * s if side < 0 else cy + 8 * s
        d.polygon([(x, tip), (x - 6 * s, base), (x + 6 * s, base)], fill=colour)
    font = None
    for name in ('arialbd.ttf', 'Arial Bold.ttf'):
        try:
            font = ImageFont.truetype(name, 14 * s)
            break
        except OSError:
            continue
    caption = 'Tank' if kind == 'tank' else 'Speed'
    box = d.textbbox((0, 0), caption, font=font)
    d.text(((big - (box[2] - box[0])) // 2 - box[0], 55 * s), caption, font=font, fill=CAPTION)
    return img.resize((OPTION_SWAP_TILE, OPTION_SWAP_TILE), Image.LANCZOS)


def ticket_icon(colour):
    from PIL import ImageFont
    big = OPTION_SWAP_TILE * SCALE
    img = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = SCALE
    cx, cy = big // 2, 30 * s
    left, right, top, bottom = cx - 26 * s, cx + 26 * s, cy - 15 * s, cy + 15 * s
    d.rounded_rectangle((left, top, right, bottom), radius=4 * s, fill=colour)
    for x in (left, right):
        d.ellipse((x - 6 * s, cy - 6 * s, x + 6 * s, cy + 6 * s), fill=(0, 0, 0, 0))
    d.line((cx + 12 * s, top + 4 * s, cx + 12 * s, bottom - 4 * s), fill=(0, 0, 0, 0), width=2 * s)
    import math
    points = []
    for i in range(10):
        r = (10 if i % 2 == 0 else 4.2) * s
        a = -math.pi / 2 + i * math.pi / 5
        points.append((cx - 7 * s + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(points, fill=(255, 255, 255, 255))
    font = None
    for name in ('arialbd.ttf', 'Arial Bold.ttf'):
        try:
            font = ImageFont.truetype(name, 14 * s)
            break
        except OSError:
            continue
    box = d.textbbox((0, 0), 'Ticket', font=font)
    d.text(((big - (box[2] - box[0])) // 2 - box[0], 55 * s), 'Ticket', font=font, fill=CAPTION)
    return img.resize((OPTION_SWAP_TILE, OPTION_SWAP_TILE), Image.LANCZOS)


def ticket_button(w, h, fill):
    big = Image.new('RGBA', (w * SCALE, h * SCALE), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    d.rounded_rectangle((0, 0, w * SCALE - 1, h * SCALE - 1), radius=6 * SCALE, fill=fill, outline=OUTLINE, width=SCALE)
    shine = Image.new('RGBA', big.size, (0, 0, 0, 0))
    ImageDraw.Draw(shine).rounded_rectangle((3 * SCALE, 2 * SCALE, w * SCALE - 3 * SCALE, h * SCALE // 2), radius=4 * SCALE,
                                            fill=(255, 255, 255, 45))
    big.alpha_composite(shine)
    return big.resize((w, h), Image.LANCZOS)


def sheet():
    img = Image.new('RGBA', (WIDTH, SIZE), (0, 0, 0, 0))
    # slot overlays: the buttons' padlocks (closed gold / open white, 26 x 25) in the top right corner of each tile
    for x, y, w, h, _, closed in SLOT_TILES.values():
        img.alpha_composite(padlock(0, 0, 26, 25, closed, GOLD if closed else WHITE, (214, 216, 226, 255)), (x + w - 26 - 4, y + 4))
    # buttons: open padlock (click to lock), closed padlock (click to unlock); "over": brighter, on a light disc
    for x, closed in ((196, False), (226, True)):
        for y, over in ((0, False), (32, True)):
            body = GOLD if closed else WHITE
            shackle = (255, 236, 150, 255) if over else (214, 216, 226, 255)
            img.alpha_composite(padlock(0, 0, 26, 25, closed, body, shackle, glow=over), (x, y))
    # weapon glow swatches: 0 = the original glow (the effect's green / pink halves), then the palette
    d = ImageDraw.Draw(img)
    for i in range(GLOW_SWATCHES):
        x = 256 + i * GLOW_SWATCH
        for s, state in enumerate(GLOW_STATES):
            y = s * GLOW_SWATCH
            box = (x + 1, y + 1, x + GLOW_SWATCH - 2, y + GLOW_SWATCH - 2)
            if i == GLOW_SWATCHES - 1:
                d.rectangle(box, fill=(45, 45, 55, 255))
                d.line((box[0] + 2, box[1] + 2, box[2] - 2, box[3] - 2), fill=(235, 40, 40, 255), width=3)
                d.line((box[0] + 2, box[3] - 2, box[2] - 2, box[1] + 2), fill=(235, 40, 40, 255), width=3)
            elif i == 0:
                d.polygon([(box[0], box[1]), (box[2], box[1]), (box[0], box[3])], fill=(0, 255, 70, 255))
                d.polygon([(box[2], box[1]), (box[2], box[3]), (box[0], box[3])], fill=(255, 70, 222, 255))
            else:
                c = GLOW_PALETTE[i - 1]
                d.rectangle(box, fill=((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255))
            outline = {'normal': (20, 22, 34, 255), 'over': (230, 230, 240, 255), 'selected': (255, 255, 255, 255)}[state]
            d.rectangle((x, y, x + GLOW_SWATCH - 1, y + GLOW_SWATCH - 1), outline=outline, width=2 if state == 'selected' else 1)
    # chat colour swatches: 0 = the game's colour (dark, crossed), then the palette; the toggle: four colour quarters
    for i in range(len(CHAT_PALETTE) + 1):
        x = 256 + i * GLOW_SWATCH
        for s, state in enumerate(GLOW_STATES):
            y = CHAT_SHEET_Y + s * GLOW_SWATCH
            box = (x + 1, y + 1, x + GLOW_SWATCH - 2, y + GLOW_SWATCH - 2)
            if i == 0:
                d.rectangle(box, fill=(60, 60, 70, 255))
                d.line((box[0] + 1, box[3] - 1, box[2] - 1, box[1] + 1), fill=(235, 235, 235, 255), width=2)
            else:
                c = CHAT_PALETTE[i - 1]
                d.rectangle(box, fill=((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255))
            outline = {'normal': (20, 22, 34, 255), 'over': (230, 230, 240, 255), 'selected': (255, 255, 255, 255)}[state]
            d.rectangle((x, y, x + GLOW_SWATCH - 1, y + GLOW_SWATCH - 1), outline=outline, width=2 if state == 'selected' else 1)
    x = 256 + 15 * GLOW_SWATCH
    for s in range(2):
        y = CHAT_SHEET_Y + s * GLOW_SWATCH
        quarters = [(0xE6, 0x3C, 0x3C), (0xFF, 0xDC, 0x32), (0x3C, 0x78, 0xFF), (0x3C, 0xC8, 0x3C)]
        for q, (qx, qy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
            d.rectangle((x + 2 + qx * 6, y + 2 + qy * 6, x + 7 + qx * 6, y + 7 + qy * 6), fill=quarters[q] + (255,))
        d.rectangle((x, y, x + GLOW_SWATCH - 1, y + GLOW_SWATCH - 1), outline=(20, 22, 34, 255) if s == 0 else (230, 230, 240, 255))
    for name, (x, y, _) in OPTION_SWAP_TILES.items():
        kind = 'tank' if 'tank' in name else 'speed'
        img.alpha_composite(option_swap_tile(kind, GAMBLE_RED if name.endswith('_on') else GAMBLE_BLUE), (x, y))
    for name, (x, y, _) in TICKET_ICON.items():
        img.alpha_composite(ticket_icon(GAMBLE_RED if name.endswith('_on') else GAMBLE_BLUE), (x, y))
    for kind, (w, h, x, y, _) in TICKET_TILES.items():
        for s, fill in enumerate(TICKET_FILL[kind]):
            img.alpha_composite(ticket_button(w, h, fill), (x, y + s * (h + 2)))
    return img


def line_with(data, needle):
    start = data.index(needle)
    begin = data.rindex(b'\n', 0, start) + 1
    end = data.index(b'\n', start) + 1
    return begin, end


def strip_previous(data):
    lines = data.split(b'\n')
    return b'\n'.join(line for line in lines if not any(m in line for m in MARKERS))


def add_texture_and_components(data, layout, texture_after, component_after):
    """The texture goes after the scene's last texture; its components get a textureNodeName of their own (it is a
    key of the texture, unique per texture: sharing the template's key broke the inventory slots, 2026-10-02)."""
    b, e = line_with(data, b'<Texture id="' + texture_after + b'"')
    texture = re.sub(rb'id="\d+"', b'id="' + TEXTURE_ID + b'"', data[b:e], count=1)
    texture = re.sub(rb'name="[^"]*"', b'name="' + TEXTURE_NAME + b'.dds"', texture, count=1)
    texture = re.sub(rb'height="\d+" width="\d+"', b'height="%d" width="%d"' % (SIZE, WIDTH), texture, count=1)
    texture = re.sub(rb'filename="[^"]*"', b'filename="' + TEXTURE_NAME + b'"', texture, count=1)
    last = [m.end() for m in re.finditer(rb'<Texture [^\n]*\n', data)][-1]
    data = data[:last] + texture + data[last:]
    node = max(int(n) for n in re.findall(rb'textureNodeName="(\d+)"', data)) + 1
    b, e = line_with(data, b'<Component id="' + component_after + b'"')
    template = data[b:e]
    components = b''
    for name, (x, y, w, h, cid) in layout.items():
        line = re.sub(rb'<Component id="\d+"', b'<Component id="%d"' % cid, template, count=1)
        line = re.sub(rb'texture="\d+"', b'texture="' + TEXTURE_ID + b'"', line, count=1)
        line = re.sub(rb'nodename="[^"]*"', b'nodename="' + name.encode() + b'"', line, count=1)
        line = re.sub(rb'texcoord="[^"]*"', b'texcoord="%d.%d.%d.%d"' % (x, y, x + w, y + h), line, count=1)
        line = re.sub(rb'alpa="\d+"', b'alpa="255"', line, count=1)
        line = re.sub(rb'enable="\d+"', b'enable="1"', line, count=1)
        line = re.sub(rb'textureNodeName="\d+"', b'textureNodeName="%d"' % node, line, count=1)
        components += line
    return data[:e] + components + data[e:]


# E_DLG_INFO: the title (110181) and grade (110182) lines are 0 x 0 in the game's layout (the game writes "-" there):
# the size of the lines below them, so the server's profile texts (message 0x05) show (D70).
PROFILE_LINES = (b'E_DLG_INFO_STC_INFO_BASIC_INFO02', b'E_DLG_INFO_STC_INFO_BASIC_INFO03')


# ToyBattles' own placeholders over the same two lines (their default text showed under the server's texts): hidden
PROFILE_PLACEHOLDERS = (b'E_DLG_INFO_STC_INFO_BASIC_INFO02_PLACEHOLDER', b'E_DLG_INFO_STC_INFO_BASIC_INFO03_PLACEHOLDER')


def resize(data, node, size):
    b, e = line_with(data, b'nodename="' + node + b'"')
    line = re.sub(rb' w="\d+" h="\d+"', size, data[b:e], count=1)
    return data[:b] + line + data[e:]


def show_profile_lines(data):
    for node in PROFILE_LINES:
        data = resize(data, node, b' w="260" h="24"')
    for node in PROFILE_PLACEHOLDERS:
        if b'nodename="' + node + b'"' in data:
            data = resize(data, node, b' w="0" h="0"')
    return data


def patch_common(src):
    data = strip_previous(open(src, 'rb').read())
    data = show_profile_lines(add_texture_and_components(data, LAYOUT_COMMON, b'2469069713', b'531'))
    b, e = line_with(data, b'nodename="E_DLG_ITEM_POPUP_BTN_ITEM_POPUP_CLOSE"')
    close = data[b:e]  # one line: <Button ... >Button</Button>
    buttons = b''
    for suffix, cid, normal, over in BUTTONS:
        line = close.replace(b'E_DLG_ITEM_POPUP_BTN_ITEM_POPUP_CLOSE', b'E_DLG_ITEM_POPUP_BTN_' + suffix)
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        line = re.sub(rb' x="-?\d+"', b' x="14"', line, count=1)
        for index in range(10):  # 1, 2: mouse over / pressed
            component = over if index in (1, 2) else normal
            line = re.sub(rb' Element_%d="\d+"' % index, b' Element_%d="%d"' % (index, component), line, count=1)
        buttons += line
    for i in range(GLOW_SWATCHES):
        line = close.replace(b'E_DLG_ITEM_POPUP_BTN_ITEM_POPUP_CLOSE', b'E_DLG_ITEM_POPUP_BTN_GLOW_%02d' % i)
        line = re.sub(rb' id="\d+"', b' id="%d"' % (GLOW_FIRST_ID + i), line, count=1)
        line = re.sub(rb' x="-?\d+" y="-?\d+" w="\d+" h="\d+"', b' x="%d" y="%d" w="%d" h="%d"' % (GLOW_X + i * GLOW_STEP, GLOW_Y, GLOW_SWATCH, GLOW_SWATCH), line, count=1)
        line = re.sub(rb' ClickAreaW="\d+" ClickAreaH="\d+"', b' ClickAreaW="%d" ClickAreaH="%d"' % (GLOW_SWATCH, GLOW_SWATCH), line, count=1)
        normal, over, selected = (GLOW_COMPONENT + 3 * i + s for s in range(3))
        for index in range(10):  # 1, 2: mouse over / pressed; 3: disabled = the current colour
            component = over if index in (1, 2) else selected if index == 3 else normal
            line = re.sub(rb' Element_%d="\d+"' % index, b' Element_%d="%d"' % (index, component), line, count=1)
        buttons += line
    gb, ge = line_with(data, b'nodename="E_DLG_ITEM_POPUP_BTN_SEAL"')
    for suffix, cid, normal, over in OPTION_SWAP_BUTTONS:  # copies of Gamble (bottom row: x 10 / 75 / 140 / 205, y 417)
        line = data[gb:ge].replace(b'E_DLG_ITEM_POPUP_BTN_SEAL', b'E_DLG_ITEM_POPUP_BTN_' + suffix)
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        for index in range(10):  # as Gamble: 1, 2, 6 the mouse over / pressed image
            component = over if index in (1, 2, 6) else normal
            line = re.sub(rb' Element_%d="\d+"' % index, b' Element_%d="%d"' % (index, component), line, count=1)
        buttons += line
    data = data[:e] + buttons + data[e:]
    return add_upgrade_ticket(add_enemy_outline(add_zoom_sensitivity(add_chat_palette(data))))


def add_upgrade_ticket(data):
    b, e = line_with(data, b'<Component id="698"')  # item_popup_bg_02: the window's texture and its key
    panel = re.sub(rb'<Component id="\d+"', b'<Component id="%d"' % TICKET_PANEL_COMPONENT, data[b:e], count=1)
    panel = re.sub(rb'nodename="[^"]*"', b'nodename="item_ticket_panel"', panel, count=1)
    panel = re.sub(rb'texcoord="[^"]*"', b'texcoord="' + TICKET_PANEL_TEXCOORD + b'"', panel, count=1)
    data = data[:e] + panel + data[e:]

    def text_attributes(line, color, align):
        line = with_attributes(line, {b'layerlevel': TICKET_LAYER, b'display_font': TICKET_TEXT_FONT, b'TextWAlign': align})
        for state in (b'Normal', b'Select', b'Over', b'Disable', b'Press'):
            line = with_attributes(line, {b'fontColor' + state: color})
        line, count = re.subn(rb' text="[^"]*"', b' text=""', line, count=1)
        assert count == 1
        return line

    lines = b''
    gb, ge = line_with(data, b'nodename="E_DLG_ITEM_POPUP_BTN_SEAL"')
    line = data[gb:ge].replace(b'E_DLG_ITEM_POPUP_BTN_SEAL', b'E_DLG_ITEM_POPUP_BTN_TICKET')
    line = with_attributes(line, {b'id': TICKET_TOGGLE_ID})
    normal, over = TICKET_ICON['item_ticket_icon'][2], TICKET_ICON['item_ticket_icon_on'][2]
    for index in range(10):  # as Gamble: 1, 2, 6 the mouse over / pressed image
        line = re.sub(rb' Element_%d="\d+"' % index, b' Element_%d="%d"' % (index, over if index in (1, 2, 6) else normal),
                      line, count=1)
    lines += line

    pid, px, py, pw, ph = TICKET_PANEL
    tb, te = line_with(data, b'nodename="E_DLG_ITEM_POPUP_PIC_ITEM_POPUP_BG_02"')
    line = data[tb:te].replace(b'E_DLG_ITEM_POPUP_PIC_ITEM_POPUP_BG_02', b'E_DLG_ITEM_POPUP_PIC_TICKET_PANEL')
    line = with_attributes(line, {b'id': pid, b'layerlevel': TICKET_LAYER - 1, b'x': px, b'y': py, b'w': pw, b'h': ph,
                                  b'Element_0': TICKET_PANEL_COMPONENT})
    lines += line

    sb, se = line_with(data, b'nodename="E_DLG_ITEM_POPUP_STC_ITEM_POPUP_STA_01"')
    for suffix, cid, x, y, w, h in TICKET_STATICS:
        line = data[sb:se].replace(b'E_DLG_ITEM_POPUP_STC_ITEM_POPUP_STA_01', b'E_DLG_ITEM_POPUP_' + suffix)
        line = with_attributes(line, {b'id': cid, b'x': x, b'y': y, b'w': w, b'h': h})
        lines += text_attributes(line, TICKET_TEXT_COLOR, 1)

    cb, ce = line_with(data, b'nodename="E_DLG_ITEM_POPUP_BTN_ITEM_POPUP_CLOSE"')
    for suffix, cid, x, y, kind in TICKET_BUTTONS:
        w, h, _, _, (normal, over, selected) = TICKET_TILES[kind]
        line = data[cb:ce].replace(b'E_DLG_ITEM_POPUP_BTN_ITEM_POPUP_CLOSE', b'E_DLG_ITEM_POPUP_' + suffix)
        line = with_attributes(line, {b'id': cid, b'x': x, b'y': y, b'w': w, b'h': h, b'ClickAreaW': w, b'ClickAreaH': h})
        for index in range(10):  # 1, 2, 6: mouse over / pressed; 3: disabled = the choice
            component = over if index in (1, 2, 6) else selected if index == 3 else normal
            line = re.sub(rb' Element_%d="\d+"' % index, b' Element_%d="%d"' % (index, component), line, count=1)
        lines += text_attributes(line, TICKET_BUTTON_TEXT, 1)

    b, e = line_with(data, b'nodename="E_DLG_ITEM_POPUP_BTN_OPTION_SWAP_SPEED"')
    return data[:e] + lines + data[e:]


def with_attributes(line, attributes):
    for name, value in attributes.items():
        line, count = re.subn(rb' ' + name + rb'="-?\d+"', b' ' + name + b'="%d"' % value, line, count=1)
        assert count == 1, (name, line[:80])
    return line


def add_zoom_sensitivity(data):
    for name, attributes in ZOOM_MOVED.items():
        b, e = line_with(data, b'nodename="' + name + b'"')
        data = data[:b] + with_attributes(data[b:e], attributes) + data[e:]
    b, e = line_with(data, b'nodename="E_DLG_OPTION_PIC_CON_LINE02"')
    insert_at = e
    lines = []

    def clone(template, name, cid, attributes):
        tb, te = line_with(data, b'nodename="' + template + b'"')
        line = data[tb:te].replace(b'nodename="' + template + b'"', b'nodename="' + name + b'"')
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        return with_attributes(line, attributes)

    lines.append(clone(b'E_DLG_OPTION_PIC_CON_LINE02', b'E_DLG_OPTION_PIC_CON_ZOOMSENS_LINE', ZOOM_FIRST_ID + 12,
                       {b'y': ZOOM_ROW_Y[0] - 5}))
    for row, y in enumerate(ZOOM_ROW_Y):
        first = ZOOM_FIRST_ID + 6 * row
        tag = b'E_DLG_OPTION_%s_CON_ZOOMSENS%d'
        lines.append(clone(b'E_DLG_OPTION_STC_CON_OP01_TITLE', tag % (b'STC', row) + b'_TITLE', first,
                           {b'x': 30, b'y': y + 3, b'w': 118}))
        lines.append(clone(b'E_DLG_OPTION_BTN_CON_OP01_LEFT', tag % (b'BTN', row) + b'_LEFT', first + 1, {b'y': y}))
        lines.append(clone(b'E_DLG_OPTION_SLI_CON_OP01', tag % (b'SLI', row), first + 2,
                           {b'y': y, b'min': 10, b'max': 300, b'value': 100}))
        lines.append(clone(b'E_DLG_OPTION_BTN_CON_OP01_RIGHT', tag % (b'BTN', row) + b'_RIGHT', first + 3, {b'y': y}))
        lines.append(clone(b'E_DLG_OPTION_PIC_CON_OP01_NOBACK', tag % (b'PIC', row) + b'_NOBACK', first + 4, {b'y': y + 2}))
        lines.append(clone(b'E_DLG_OPTION_STC_CON_OP01_SLIDE_NO', tag % (b'STC', row) + b'_SLIDE_NO', first + 5,
                           {b'y': y + 2}))
    return data[:insert_at] + b''.join(lines) + data[insert_at:]


def add_enemy_outline(data):
    b, e = line_with(data, b'nodename="E_DLG_OPTION_RADIO_GRA_OP18_OFF"')
    lines = b''
    parts = ((b'E_DLG_OPTION_STC_GRA_OP18_TITLE', b'E_DLG_OPTION_STC_GRA_ENEMYOUTLINE_TITLE'),
             (b'E_DLG_OPTION_RADIO_GRA_OP18_ON', b'E_DLG_OPTION_RADIO_GRA_ENEMYOUTLINE_ON'),
             (b'E_DLG_OPTION_RADIO_GRA_OP18_OFF', b'E_DLG_OPTION_RADIO_GRA_ENEMYOUTLINE_OFF'))
    for (template, name), cid in zip(parts, OUTLINE_IDS):
        tb, te = line_with(data, b'nodename="' + template + b'"')
        line = data[tb:te].replace(b'nodename="' + template + b'"', b'nodename="' + name + b'"')
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        line = with_attributes(line, {b'y': OUTLINE_Y})
        line = re.sub(rb' groupid="\d+"', b' groupid="%d"' % OUTLINE_GROUP, line, count=1)
        lines += line
    return data[:e] + lines + data[e:]


def add_chat_palette(data):
    # template: the option tab button (layer 0, always shown; the option panel's close button is on the panel's
    # layer and hidden with it)
    b, e = line_with(data, b'nodename="E_DLG_CHAT_BTN_TAP_CHAT_OPTION"')
    template = data[b:e]
    buttons = b''

    def button(name, cid, x, y, normal, over, selected):
        line = template.replace(b'E_DLG_CHAT_BTN_TAP_CHAT_OPTION', name)
        line = re.sub(rb' id="\d+"', b' id="%d"' % cid, line, count=1)
        line = re.sub(rb' x="-?\d+" y="-?\d+" w="\d+" h="\d+"', b' x="%d" y="%d" w="%d" h="%d"' % (x, y, GLOW_SWATCH, GLOW_SWATCH), line, count=1)
        line = re.sub(rb' ClickAreaW="\d+" ClickAreaH="\d+"', b' ClickAreaW="%d" ClickAreaH="%d"' % (GLOW_SWATCH, GLOW_SWATCH), line, count=1)
        for index in range(10):
            component = over if index in (1, 2) else selected if index == 3 else normal
            line = re.sub(rb' Element_%d="\d+"' % index, b' Element_%d="%d"' % (index, component), line, count=1)
        return line

    buttons += button(b'E_DLG_CHAT_BTN_COLOR_TOGGLE', CHAT_TOGGLE_ID, CHAT_TOGGLE_X, CHAT_TOGGLE_Y, 69805, 69806, 69805)
    for i in range(len(CHAT_PALETTE) + 1):
        normal, over, selected = (CHAT_COMPONENT + 3 * i + s for s in range(3))
        buttons += button(b'E_DLG_CHAT_BTN_COLOR_%02d' % i, CHAT_TOGGLE_ID + 1 + i, CHAT_X + i * (GLOW_SWATCH + 1), CHAT_Y,
                          normal, over, selected)
    return data[:e] + buttons + data[e:]


def patch_inven(src):
    data = strip_previous(open(src, 'rb').read())
    return add_texture_and_components(data, LAYOUT_INVEN, b'2463698531', b'357')


def original_dat(game, name):
    """The game's untouched archive: data/<name>_orig.dat when the installed one was replaced by a staged copy
    (fx_orig.dat, weapon_orig.dat, ...), else data/<name>.dat. The tools read only from it: run again, they never
    convert their own output."""
    data = os.path.join(game, 'data')
    backup = os.path.join(data, name + '_orig.dat')
    return backup if os.path.exists(backup) else os.path.join(data, name + '.dat')


def add_to_ui_dat(game_ui_dat, staged_ui_dat, arcname, path):
    if not os.path.exists(staged_ui_dat):
        shutil.copyfile(game_ui_dat, staged_ui_dat)
    with zipfile.ZipFile(staged_ui_dat) as z:
        present = arcname in z.namelist()
    if not present:
        with zipfile.ZipFile(staged_ui_dat, 'a', compression=zipfile.ZIP_DEFLATED) as z:
            z.write(path, arcname)
    else:
        tmp = staged_ui_dat + '.new'
        with zipfile.ZipFile(staged_ui_dat) as zin, zipfile.ZipFile(tmp, 'w', compression=zipfile.ZIP_DEFLATED) as zout:
            for info in zin.infolist():
                data = open(path, 'rb').read() if info.filename == arcname else zin.read(info.filename)
                out = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                out.compress_type = zipfile.ZIP_STORED if info.is_dir() else zipfile.ZIP_DEFLATED
                out.external_attr = info.external_attr
                zout.writestr(out, data)
        os.replace(tmp, staged_ui_dat)
    with zipfile.ZipFile(staged_ui_dat) as z:
        assert z.testzip() is None


def main(game, staging):
    scene = os.path.join(game, 'data', 'ui', 'scene')
    out_scene = os.path.join(staging, 'data', 'ui', 'scene')
    os.makedirs(out_scene, exist_ok=True)
    open(os.path.join(out_scene, 'SCENE_COMMON.xml'), 'wb').write(patch_common(os.path.join(scene, 'SCENE_COMMON.xml')))
    open(os.path.join(out_scene, 'SCENE_INVEN_2ND.xml'), 'wb').write(patch_inven(os.path.join(scene, 'SCENE_INVEN_2ND.xml')))
    out_setup = os.path.join(staging, 'ui', 'setup', 'ENG')
    os.makedirs(out_setup, exist_ok=True)
    img = sheet()
    dds = os.path.join(out_setup, 'item_lock.dds')
    img.save(dds, 'DDS')
    img.resize((WIDTH * 2, SIZE * 2), Image.NEAREST).save(os.path.join(staging, 'ui', 'item_lock_preview.png'))
    add_to_ui_dat(os.path.join(game, 'data', 'ui.dat'), os.path.join(staging, 'data', 'ui.dat'), 'setup/ENG/item_lock.dds', dds)
    print('staged in', os.path.join(staging, 'data'))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
