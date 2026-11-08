"""Map images of a merged map, built from the source maps' own images and the layout: the battle map of the TAB
dialog (MAP_BATTLE / MAP_BATTLE_F), the loading screen map (LOADING_MINIMAP / _F), the room thumbnail (MAP_MINIVIEW),
the loading picture (LOADING) and the two name images (ROOM_MAP_TITLE, LOADING_MAPNAME).

Client facts used:
- 0x5AB680 picks ICONTAG_MAP_BATTLE_F_%d for modes 1, 7, 8, 12 (FFA, ZSM, GRM, NOD), else ICONTAG_MAP_BATTLE_%d; the
  map's mi_size goes to +0x36C.
- 0x5AE400 draws texels (0, 0)-(512, 512) of that texture over the dialog's E_DLG_STATUS_PIC_MINIMAP rectangle;
  0x5ABC80, 0x5AE5F0 and 0x5AE6B0 place a world point (x, y) at texel (256 + 512 x / size, 256 - 512 y / size).
- 0x62B3B0 picks ICONTAG_LOADING_MINIMAP_F_%d for modes 1, 7, 8, 12, 15.
- 0x5A71D0: an icon is cell ii_offset of a grid of ii_width x ii_height cells, ii_filesize / ii_width per row, in
  icon\\<file> (ii_common 1) or icon\\<language>\\<file>.
- Icon ids and tags are looked up in maps (0x6CCBE0, 0x5E62F0): row order does not matter, the first row of a tag wins.

Each source image goes to its map's offset with the same formula. Some source images are not drawn to their map's
scale (with mi_size alone a player is drawn off the walls); a scale and shift of the image is used when it puts clearly
more of the map's upright PhysX edges on the image's lines and no fewer TDM spawns on its team zones.
"""
import argparse
import copy
import io
import math
import os
import subprocess
import sys
import tempfile
import zipfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

import layout as layout_file
import maptables
import nifedit as ne
import ziprecords
from nifio import Nif

N = 2048
TEX = 512
RED = (189, 52, 57)
BLUE = (57, 138, 206)
TEAM_COLOR = {'REDTEAM': RED, 'BLUETEAM': BLUE}
WALL_PX = 8
DASH_ALPHA = 136
LOADING_FILL_ALPHA = 68
MINIVIEW_KEEP = (0, 28, 125, 102)  # the room thumbnails carry a size label in their top right corner


class Icons:

    def __init__(self, tag_cdb, info_cdb, ui):
        self.tag_cdb, self.info_cdb, self.ui = tag_cdb, info_cdb, ui
        self.names = {n.lower(): n for n in ui.namelist()}
        self.tags = {}
        for e in tag_cdb.entries:
            self.tags.setdefault(maptables._get(e, 'it_icontag'), maptables._get(e, 'it_iconid'))
        self.info = {}
        for e in info_cdb.entries:
            self.info.setdefault(maptables._get(e, 'ii_id'), {f.key: f.value for f in e})

    def row(self, tag):
        icon = self.tags.get(tag)
        return self.info.get(icon) if icon is not None else None

    def path(self, row):
        return ('icon/' if row['ii_common'] else 'icon/ENG/') + row['ii_filename']

    def image(self, tag, cell=False):
        row = self.row(tag)
        if row is None:
            return None
        name = self.names.get(self.path(row).lower())
        if name is None:
            return None
        im = Image.open(io.BytesIO(self.ui.read(name)))
        im.load()
        im = im.convert('RGBA')
        if cell:
            w, h = row['ii_width'], row['ii_height']
            cols = row['ii_filesize'] // w
            x, y = w * (row['ii_offset'] % cols), h * (row['ii_offset'] // cols)
            im = im.crop((x, y, x + w, y + h))
        return im


def to_world(size):
    return lambda x, y: (N / 2 + N * x / size, N / 2 - N * y / size)


def premul(im):
    a = np.asarray(im, dtype=np.float32) / 255.0
    a[..., :3] *= a[..., 3:4]
    return a


def unpremul(a):
    out = a.copy()
    alpha = out[..., 3:4]
    out[..., :3] = np.where(alpha > 1e-4, out[..., :3] / np.maximum(alpha, 1e-4), 1.0)
    return Image.fromarray(np.clip(out * 255.0 + 0.5, 0, 255).astype(np.uint8), 'RGBA')


def place(src, map_size, offset, size, fit=(1.0, 1.0, 0.0, 0.0)):
    sx, sy, tx, ty = fit
    k = TEX / map_size
    coeffs = (k * sx * size / N, 0, TEX / 2 + k * (tx - sx * (size / 2 + offset[0])),
              0, k * sy * size / N, TEX / 2 + k * (sy * (offset[1] - size / 2) - ty))
    pm = premul(src)
    out = np.zeros((N, N, 4), np.float32)
    for c in range(4):
        ch = Image.fromarray(pm[..., c], 'F').transform((N, N), Image.AFFINE, coeffs, resample=Image.BILINEAR)
        out[..., c] = np.asarray(ch)
    return out


def wall_points(colp, zlo, zhi, spacing=40.0):
    import corridor as cr
    pts = []
    for i, name, pose, md in cr.physx_actors(colp):
        if name.lower().startswith('fence'):
            continue
        for t in cr.actor_world_triangles(colp, md, pose):
            u = [t[1][k] - t[0][k] for k in range(3)]
            v = [t[2][k] - t[0][k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            ln = math.sqrt(sum(c * c for c in n)) or 1.0
            if abs(n[2]) / ln > 0.2 or not zlo <= sum(p[2] for p in t) / 3 <= zhi:
                continue
            for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
                steps = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / spacing))
                pts += [(a[0] + (b[0] - a[0]) * s / steps, a[1] + (b[1] - a[1]) * s / steps) for s in range(steps + 1)]
    return np.array(pts)


def fit_image(f, map_size, pts):
    dist = ndimage.distance_transform_edt(np.asarray(f)[..., 3] <= 100)

    def score(sx, sy, tx, ty):
        x = TEX / 2 + TEX * (pts[:, 0] * sx + tx) / map_size
        y = TEX / 2 - TEX * (pts[:, 1] * sy + ty) / map_size
        ok = (x >= 0) & (x < TEX) & (y >= 0) & (y < TEX)
        return float((dist[y[ok].astype(int), x[ok].astype(int)] <= 1.5).sum()) / len(pts)

    grid = np.arange(0.9, 1.3001, 0.02)
    best = max((score(a, b, 0.0, 0.0), a, b, 0.0, 0.0) for a in grid for b in grid)
    for it in range(3):
        _, a0, b0, x0, y0 = best
        ds, dt = 0.02 / 2 ** it, 200.0 / 2 ** it
        best = max((score(a, b, x, y), a, b, x, y)
                   for a in a0 + ds * np.arange(-2, 3) for b in b0 + ds * np.arange(-2, 3)
                   for x in x0 + dt * np.arange(-3, 4) for y in y0 + dt * np.arange(-3, 4))
    return score(1.0, 1.0, 0.0, 0.0), best[0], tuple(float(v) for v in best[1:])


def spawns_in_zones(tdm, f, map_size, pts, fit):
    zone = ndimage.binary_dilation(np.abs(np.asarray(tdm).astype(int) - np.asarray(f).astype(int)).max(-1) > 8,
                                   iterations=2)
    sx, sy, tx, ty = fit
    n = 0
    for x, y in pts:
        i = int(TEX / 2 - TEX * (y * sy + ty) / map_size)
        j = int(TEX / 2 + TEX * (x * sx + tx) / map_size)
        n += bool(0 <= i < TEX and 0 <= j < TEX and zone[i, j])
    return n


def dilate(a, r=1):
    return ndimage.maximum_filter(a, size=(2 * r + 1, 2 * r + 1, 1))


def over(dst, src):
    return src + dst * (1.0 - src[..., 3:4])


def recolor(px, team):
    rgb = px[..., :3].astype(np.float32)
    red = rgb[..., 0] > rgb[..., 2]
    k = np.where(red, (255 - rgb[..., 1]) / (255 - RED[1]), (255 - rgb[..., 0]) / (255 - BLUE[0]))
    k = np.clip(k, 0, 1)[..., None]
    out = px.copy()
    out[..., :3] = np.clip(255 * (1 - k) + np.array(TEAM_COLOR[team], np.float32) * k + 0.5, 0, 255)
    return out


def spawns(nif):
    """[(group, name, (x, y, z))] of RESPAWN/TDM/REDTEAM, TDM/BLUETEAM and FFA (world position, 0x52CDC0)."""
    par = ne.parents(nif)
    out = []
    stack = [(r, ne.IDENTITY) for r in nif.roots]
    while stack:
        i, parent = stack.pop()
        b = nif.blocks[i]
        if 'Translation' not in b.data:
            continue
        w = ne.compose(parent, ne.local(b))
        stack += [(c, w) for c in ne.children(nif, i)]
        pn = nif.name(nif.blocks[par[i]]) if i in par else None
        if pn in ('REDTEAM', 'BLUETEAM', 'FFA') and b.data.get('Collision Object', -1) >= 0:
            group = pn if pn == 'FFA' else '%s/%s' % (nif.name(nif.blocks[par[par[i]]]), pn)
            out.append((group, nif.name(b), w[0]))
    return out


def map_images(icons, mid):
    tdm = icons.image('ICONTAG_MAP_BATTLE_%d' % mid)
    f = icons.image('ICONTAG_MAP_BATTLE_F_%d' % mid)
    if f is None:
        same = icons.row('ICONTAG_MAP_BATTLE_%d' % mid)['ii_filename'].lower()
        for tag, icon in sorted(icons.tags.items()):
            if not tag.startswith('ICONTAG_MAP_BATTLE_F_'):
                continue
            other = icons.row('ICONTAG_MAP_BATTLE_%s' % tag.rsplit('_', 1)[1])
            if other and other['ii_filename'].lower() == same:
                f = icons.image(tag)
                if f is not None:
                    break
    assert tdm is not None and f is not None, 'map %d: no battle map image' % mid
    return tdm, f


def tdm_image(tdm, f, team, kept, dropped, map_size, fit):
    t, fa = np.asarray(tdm).astype(np.int32), np.asarray(f).astype(np.int32)
    zone = np.abs(t - fa).max(-1) > 8
    labels, n = ndimage.label(zone, structure=np.ones((3, 3)))
    out = fa.astype(np.uint8).copy()
    removed = 0

    def texel(p):
        return (TEX / 2 + TEX * (p[0] * fit[0] + fit[2]) / map_size,
                TEX / 2 - TEX * (p[1] * fit[1] + fit[3]) / map_size)

    for k in range(1, n + 1):
        ys, xs = np.nonzero(labels == k)

        def dist(pts):
            return min((float(np.min((xs + 0.5 - x) ** 2 + (ys + 0.5 - y) ** 2)) for x, y in map(texel, pts)),
                       default=math.inf)
        if dist(dropped) < dist(kept):
            removed += 1
            continue
        out[ys, xs] = recolor(np.asarray(tdm)[ys, xs], team)
    return Image.fromarray(out, 'RGBA'), n, removed


def corridor_extent(c):
    along = [p[0] for p in c['profile']]
    ax = 0 if c['axis'] == 'x' else 1
    for cut in c.get('cuts', []):
        along += [cut['box'][0][ax], cut['box'][1][ax]]
    return min(along), max(along)


def mouths(c, canvas, W, size, inward=600.0, outward=300.0):
    a0, a1 = corridor_extent(c)
    b0, b1 = c['across']
    step = size / N / 2
    white = (canvas[..., 3] > 0.6) & (canvas[..., :3].min(-1) > 0.8 * canvas[..., 3])

    def wall(u):
        for f in (0.3, 0.5, 0.7):
            b = b0 + (b1 - b0) * f
            x, y = W(*((u, b) if c['axis'] == 'x' else (b, u)))
            i, j = int(y), int(x)
            if 0 <= i < N and 0 <= j < N and white[i, j]:
                return True
        return False

    out = []
    for end, sign in ((a0, -1), (a1, 1)):
        u, found = end - sign * inward, False
        while sign * (u - end) <= outward:
            if wall(u):
                found = True
                break
            u += sign * step
        if not found:
            out.append(end)
            continue
        while wall(u):
            u += sign * step
        out.append(u + sign * step)
    return out


def corridor_rect(c, a0, a1, b0, b1):
    if c['axis'] == 'x':
        return (a0, b0), (a1, b1)
    return (b0, a0), (b1, a1)


def draw_corridors(canvas, layout, W, extents):
    img = unpremul(canvas)
    clear = Image.new('L', (N, N), 0)
    cd = ImageDraw.Draw(clear)
    lines = Image.new('RGBA', (N, N), (255, 255, 255, 0))
    ld = ImageDraw.Draw(lines)
    for c, (a0, a1) in zip(layout['corridors'], extents):
        b0, b1 = c['across']
        (x0, y0), (x1, y1) = corridor_rect(c, a0, a1, b0, b1)
        p0, p1 = W(x0, y0), W(x1, y1)
        inset = WALL_PX / 2 + 1
        ix, iy = (0, inset) if c['axis'] == 'x' else (inset, 0)
        cd.rectangle([min(p0[0], p1[0]) + ix, min(p0[1], p1[1]) + iy,
                      max(p0[0], p1[0]) - ix, max(p0[1], p1[1]) - iy], 255)
        shell = c.get('render', {}).get('shell', {}).get('thickness', 30) / 2
        for b in (b0 - shell, b1 + shell):
            (sx0, sy0), (sx1, sy1) = corridor_rect(c, a0, a1, b, b)
            ld.line([W(sx0, sy0), W(sx1, sy1)], fill=(255, 255, 255, 255), width=WALL_PX)
        prof = c['profile']
        for (u0, f0, _), (u1, f1, _) in zip(prof, prof[1:]):
            if abs(f1 - f0) < 150:
                continue
            for u in (u0, u1):
                (sx0, sy0), (sx1, sy1) = corridor_rect(c, u, u, b0, b1)
                q0, q1 = W(sx0, sy0), W(sx1, sy1)
                length = math.hypot(q1[0] - q0[0], q1[1] - q0[1])
                steps = max(1, int(length // (3 * WALL_PX)))
                for s in range(steps):
                    t0, t1 = (s + 0.25) / steps, (s + 0.75) / steps
                    ld.line([(q0[0] + (q1[0] - q0[0]) * t0, q0[1] + (q1[1] - q0[1]) * t0),
                             (q0[0] + (q1[0] - q0[0]) * t1, q0[1] + (q1[1] - q0[1]) * t1)],
                            fill=(255, 255, 255, DASH_ALPHA), width=WALL_PX // 2)
    a = np.asarray(img).copy()
    a[np.asarray(clear) > 0, 3] = 0
    img = Image.fromarray(a, 'RGBA')
    img.alpha_composite(lines)
    return premul(img), np.asarray(clear) > 0


def to_texture(canvas, size=TEX):
    pm = np.stack([np.asarray(Image.fromarray(canvas[..., c], 'F').resize((size, size), Image.BOX))
                   for c in range(4)], -1)
    im = unpremul(pm)
    a = np.asarray(im).copy()
    a[a[..., 3] == 0, :3] = 255
    return Image.fromarray(a, 'RGBA')


def loading_map(canvas, arenas, corridors, cell=256, margin=10):
    alpha = arenas[..., 3]
    walls = ndimage.binary_closing(alpha > 0.5, structure=np.ones((3, 3)), iterations=6)
    outside = ndimage.binary_propagation(np.pad(np.zeros_like(walls), 1, constant_values=True),
                                         mask=np.pad(~walls, 1, constant_values=True))[1:-1, 1:-1]
    inside = (~outside & ~walls) | corridors
    fill = np.zeros_like(canvas)
    fill[inside] = (LOADING_FILL_ALPHA / 255.0,) * 4
    out = over(fill, canvas)
    ys, xs = np.nonzero(out[..., 3] > 0.02)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    side = max(x1 - x0, y1 - y0)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    box = side * cell / (cell - 2 * margin)
    crop = (cx - box / 2, cy - box / 2, cx + box / 2, cy + box / 2)
    chans = []
    for c in range(4):
        ch = Image.fromarray(out[..., c], 'F').transform((cell * 4, cell * 4), Image.EXTENT, crop, Image.BILINEAR)
        chans.append(np.asarray(ch.resize((cell, cell), Image.BOX)))
    im = unpremul(np.stack(chans, -1))
    a = np.asarray(im).copy()
    a[a[..., 3] == 0, :3] = 255
    return Image.fromarray(a, 'RGBA')


def mosaic(cells, w, h, crop=None):
    out = Image.new('RGBA', (w, h), (0, 0, 0, 255))
    hw, hh = w // 2, h // 2
    for k, im in enumerate(cells):
        im = im.convert('RGBA')
        if crop:
            im = im.crop(crop(im))
        x, y = (k % 2) * hw, (k // 2) * hh
        out.paste(im.resize((w - hw if k % 2 else hw, h - hh if k // 2 else hh), Image.LANCZOS), (x, y))
    return out


def letterbox(im, dark=24):
    rows = np.nonzero(np.asarray(im.convert('L')).mean(1) > dark)[0]
    return 0, int(rows.min()), im.size[0], int(rows.max()) + 1


def loading_picture(cells, w, h):
    _, top, _, bottom = letterbox(cells[0])
    out = Image.new('RGBA', (w, h), (0, 0, 0, 255))
    out.paste(mosaic(cells, w, bottom - top, letterbox), (0, top))
    return out


def text_cell(w, h, text, font_path, height, fill, stroke, stroke_width):
    size = height
    while True:
        font = ImageFont.truetype(font_path, size)
        l, t, r, b = font.getbbox(text, stroke_width=stroke_width)
        if r - l <= w - 4 or size <= 6:
            break
        size -= 1
    im = Image.new('RGBA', (w, h), stroke + (0,))
    ImageDraw.Draw(im).text(((w - (r - l)) / 2 - l, (h - (b - t)) / 2 - t), text, font=font, fill=fill,
                            stroke_width=stroke_width, stroke_fill=stroke)
    return im


def sheet(cell, offset, w, h, size=TEX):
    """A size x size sheet with the image in cell `offset` of a w x h grid (0x5A71D0)."""
    out = Image.new('RGBA', (size, size), (255, 255, 255, 0))
    cols = size // w
    out.paste(cell, (w * (offset % cols), h * (offset // cols)))
    return out


def dds(im, texconv, fmt='DXT3'):
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'img.png')
        im.save(src)
        subprocess.run([texconv, '-nologo', '-y', '-dx9', '-f', fmt, '-m', '0', '-o', tmp, src], check=True,
                       stdout=subprocess.DEVNULL)
        with open(os.path.join(tmp, 'img.dds'), 'rb') as f:
            return f.read()


def jpeg(im):
    buf = io.BytesIO()
    im.convert('RGB').save(buf, 'JPEG', quality=92)
    return buf.getvalue()


def set_icons(info_cdb, tag_cdb, new, log):
    template = info_cdb.entries[0]
    ids = {maptables._get(e, 'ii_id') for e in info_cdb.entries}
    next_index = max(maptables._get(e, 'it_index') for e in tag_cdb.entries) + 1
    for tag, icon, filename, offset, w, h, filesize in new:
        if icon in ids:
            raise ValueError('iconsinfo already has %d' % icon)
        row = copy.deepcopy(template)
        for k, v in (('ii_id', icon), ('ii_filename', filename), ('ii_offset', offset), ('ii_width', w),
                     ('ii_height', h), ('ii_filesize', filesize), ('ii_common', 1)):
            maptables._set(row, k, v)
        info_cdb.addEntry(row)
        ids.add(icon)
        rows = [e for e in tag_cdb.entries if maptables._get(e, 'it_icontag') == tag]
        if rows:
            for e in rows:
                maptables._set(e, 'it_iconid', icon)
            log('iconstag %s: icon %d (row %d)' % (tag, icon, maptables._get(rows[0], 'it_index')))
        else:
            row = copy.deepcopy(tag_cdb.entries[0])
            for k, v in (('it_index', next_index), ('it_iconid', icon), ('it_icontag', tag)):
                maptables._set(row, k, v)
            tag_cdb.addEntry(row)
            log('iconstag %s: icon %d (new row %d)' % (tag, icon, next_index))
            next_index += 1
        log('iconsinfo %d: icon/%s cell %d (%dx%d of %d)' % (icon, filename, offset, w, h, filesize))


def map_size_of(cgd, password, mid):
    sizes = set()
    for n in ('mapinfo.cdb', 'ENG/mapinfo.cdb'):
        cdb = maptables.load(cgd.read(n, pwd=password), 'mapinfo')
        sizes |= {maptables._get(e, 'mi_size') for e in cdb.entries if maptables._get(e, 'mi_id') == mid}
    assert len(sizes) == 1, 'map %d: mi_size %s' % (mid, sizes)
    return sizes.pop()


def build(a, log):
    layout = layout_file.load(a.layout)
    mid = layout['id']
    with open(a.password_file, 'rb') as f:
        password = f.read().strip()
    cgd = zipfile.ZipFile(a.cgd)
    ui = zipfile.ZipFile(os.path.join(a.client, 'data', 'ui.dat'))
    mapdat = zipfile.ZipFile(os.path.join(a.client, 'data', 'map.dat'))
    tag_cdb = maptables.load(cgd.read('iconstag.cdb', pwd=password), 'iconstag')
    info_cdb = maptables.load(cgd.read('iconsinfo.cdb', pwd=password), 'iconsinfo')
    icons = Icons(tag_cdb, info_cdb, ui)
    size = map_size_of(cgd, password, mid)
    W = to_world(size)
    log('map %d: mi_size %d, 1 texel = %.1f units' % (mid, size, size / TEX))

    tdm_canvas = np.zeros((N, N, 4), np.float32)
    f_canvas = np.zeros((N, N, 4), np.float32)
    for m in layout['maps']:
        sid = m['id']
        msize = map_size_of(cgd, password, sid)
        tdm, f = map_images(icons, sid)
        nif = Nif.read(mapdat.read('map%03d/World.nif' % sid))
        drop = set(m.get('drop_spawns', []))
        sp = spawns(nif)
        tdm_spawns = [(g, n, p) for g, n, p in sp if g.startswith('TDM/')]
        kept = [p for g, n, p in tdm_spawns if '%s/%s' % (g, n) not in drop]
        dropped = [p for g, n, p in tdm_spawns if '%s/%s' % (g, n) in drop]
        zs = [p[2] for g, n, p in sp]
        pts = wall_points(Nif.read(mapdat.read('map%03d/World_colp.nif' % sid)), min(zs) - 150, max(zs) + 500)
        s0, s1, fit = fit_image(f, msize, pts)
        tdm_xy = [p[:2] for g, n, p in tdm_spawns]
        in0 = spawns_in_zones(tdm, f, msize, tdm_xy, (1.0, 1.0, 0.0, 0.0))
        in1 = spawns_in_zones(tdm, f, msize, tdm_xy, fit)
        use = s1 - s0 >= 0.1 and in1 >= in0
        log('map %d: image fit sx %.3f sy %.3f tx %.0f ty %.0f: wall points on lines %.2f -> %.2f, TDM spawns in '
            'zones %d -> %d of %d: %s'
            % ((sid,) + fit + (s0, s1, in0, in1, len(tdm_xy), 'used' if use else 'not used')))
        if not use:
            fit = (1.0, 1.0, 0.0, 0.0)
        team_img, zones, removed = tdm_image(tdm, f, m['tdm_team'], kept, dropped, msize, fit)
        log('map %d: mi_size %d, scale %.3f, %d team zones (%d removed: dropped spawns) -> %s'
            % (sid, msize, msize / size, zones, removed, m['tdm_team']))
        grow = 1 if msize / size < 0.45 else 0
        for canvas, im in ((tdm_canvas, team_img), (f_canvas, f)):
            layer = place(im, msize, m['offset'], size, fit)
            canvas[:] = over(canvas, dilate(layer, grow) if grow else layer)
    extents = [mouths(c, f_canvas, W, size) for c in layout['corridors']]
    for c, (e0, e1), (r0, r1) in zip(layout['corridors'], extents, map(corridor_extent, layout['corridors'])):
        log('corridor %s: drawn %.0f..%.0f (geometry %.0f..%.0f)' % (c['name'], e0, e1, r0, r1))
    arenas = f_canvas.copy()
    tdm_canvas, corridor_mask = draw_corridors(tdm_canvas, layout, W, extents)
    f_canvas, _ = draw_corridors(f_canvas, layout, W, extents)

    stem = 'minimap_map%03d' % mid
    images = {
        'battle': to_texture(tdm_canvas),
        'battle_f': to_texture(f_canvas),
        'loading': loading_map(tdm_canvas, arenas, corridor_mask),
        'loading_f': loading_map(f_canvas, arenas, corridor_mask),
    }
    order = sorted(layout['maps'], key=lambda m: (-m['offset'][1], m['offset'][0]))
    north, south = sorted(order[:2], key=lambda m: m['offset'][0]), sorted(order[2:], key=lambda m: m['offset'][0])
    quad = north + south
    images['miniview'] = mosaic([icons.image('ICONTAG_MAP_MINIVIEW_%d' % m['id'], cell=True) for m in quad], 170, 102,
                                lambda im: MINIVIEW_KEEP)
    images['loading_pic'] = loading_picture([icons.image('ICONTAG_LOADING_%d' % m['id'], cell=True) for m in quad],
                                            1024, 476)
    images['title'] = text_cell(114, 22, a.name, a.font, 13, (255, 255, 255, 255), (16, 16, 41), 1)
    images['mapname'] = text_cell(512, 40, a.name, a.font, 30, (255, 211, 24, 255), (0, 0, 0), 1)

    files = [
        ('ICONTAG_MAP_BATTLE_%d', 90001000, stem + '.dds', 'battle', 0, 512, 512, 512),
        ('ICONTAG_MAP_BATTLE_F_%d', 90006000, stem + '_f.dds', 'battle_f', 0, 512, 512, 512),
        ('ICONTAG_LOADING_MINIMAP_%d', 90005700, 'loading_' + stem + '.dds', 'loading', 0, 256, 256, 512),
        ('ICONTAG_LOADING_MINIMAP_F_%d', 90005900, 'loading_' + stem + '_f.dds', 'loading_f', 0, 256, 256, 512),
        ('ICONTAG_MAP_MINIVIEW_%d', 90000800, 'map_miniview_map%03d.dds' % mid, 'miniview', 0, 170, 102, 512),
        ('ICONTAG_ROOM_MAP_TITLE_%d', 90001900, 'map_miniview_title_map%03d.dds' % mid, 'title', 0, 114, 22, 512),
        ('ICONTAG_LOADING_MAPNAME_%d', 90003300, 'loading_mapname_map%03d.dds' % mid, 'mapname', 0, 512, 40, 512),
        ('ICONTAG_LOADING_%d', 90003200, 'loading_map%03d.jpg' % mid, 'loading_pic', 0, 1024, 476, 1024),
    ]
    add, new = [], []
    os.makedirs(os.path.join(a.out, 'preview'), exist_ok=True)
    for tag, base, filename, key, offset, w, h, filesize in files:
        im = images[key]
        if im.size != (filesize, filesize):
            im = sheet(im, offset, w, h, filesize)
            if key == 'loading_pic':
                bg = Image.new('RGBA', im.size, (0, 0, 0, 255))
                bg.alpha_composite(im)
                im = bg
        im.save(os.path.join(a.out, 'preview', filename.rsplit('.', 1)[0] + '.png'))
        data = jpeg(im) if filename.endswith('.jpg') else dds(im, a.texconv)
        add.append(('icon/' + filename, data))
        new.append((tag % mid, base + mid, filename, offset, w, h, filesize))

    out_data = os.path.join(a.out, 'data')
    os.makedirs(out_data, exist_ok=True)
    ziprecords.rewrite(os.path.join(a.client, 'data', 'ui.dat'), os.path.join(out_data, 'ui.dat'), add=add)
    for n, d in add:
        log('ui.dat: %s (%d bytes)' % (n, len(d)))
    set_icons(info_cdb, tag_cdb, new, log)
    ziprecords.rewrite(a.cgd, os.path.join(out_data, 'cgd.dip'), password=password,
                       replace={'iconsinfo.cdb': maptables.dump(info_cdb), 'iconstag.cdb': maptables.dump(tag_cdb)})
    log('cgd.dip: iconsinfo.cdb, iconstag.cdb replaced')
    return [n[0] for n in new]


def verify(a, new_names, log):
    import struct
    out_data = os.path.join(a.out, 'data')
    problems = []
    for src, dst, changed in ((os.path.join(a.client, 'data', 'ui.dat'), os.path.join(out_data, 'ui.dat'), set()),
                              (a.cgd, os.path.join(out_data, 'cgd.dip'), {'iconsinfo.cdb', 'iconstag.cdb'})):
        old, _ = ziprecords.records(src)
        new, _ = ziprecords.records(dst)
        same = sum(1 for (n, l, c), (n2, l2, c2) in zip(old, new)
                   if n == n2 and (n in changed or (l == l2 and c[:42] + c[46:] == c2[:42] + c2[46:])))
        if same != len(old):
            problems.append('%s: %d of %d records changed' % (dst, len(old) - same, len(old)))
        log('%s: %d records kept, %d added' % (os.path.basename(dst), same, len(new) - len(old)))
    with open(a.password_file, 'rb') as f:
        password = f.read().strip()
    old_cgd, new_cgd = zipfile.ZipFile(a.cgd), zipfile.ZipFile(os.path.join(out_data, 'cgd.dip'))
    rows = {}
    for n, key in (('iconstag.cdb', 'it_index'), ('iconsinfo.cdb', 'ii_id')):
        old = [{f.key: f.value for f in e} for e in maptables.load(old_cgd.read(n, pwd=password)).entries]
        new = [{f.key: f.value for f in e} for e in maptables.load(new_cgd.read(n, pwd=password)).entries]
        rows[n] = new
        diff = [(o, w) for o, w in zip(old, new) if o != w]
        log('%s: %d rows, %d added, %d changed' % (n, len(new), len(new) - len(old), len(diff)))
        for o, w in diff:
            if set(k for k in o if o[k] != w[k]) != {'it_iconid'}:
                problems.append('%s: row %s changed beyond it_iconid' % (n, o[key]))
    info = {r['ii_id']: r for r in rows['iconsinfo.cdb']}
    ui = zipfile.ZipFile(os.path.join(out_data, 'ui.dat'))
    for tag in new_names:
        icon = next(r['it_iconid'] for r in rows['iconstag.cdb'] if r['it_icontag'] == tag)
        r = info[icon]
        data = ui.read('icon/' + r['ii_filename'])
        im = Image.open(io.BytesIO(data))
        im.load()
        desc = '%dx%d' % im.size
        if data[:4] == b'DDS ':
            h, w, mips = struct.unpack_from('<II', data, 12) + struct.unpack_from('<I', data, 28)
            desc += ' %s, %d mip levels' % (data[84:88].decode(), mips)
            if data[84:88] != b'DXT3' or mips != int(math.log2(max(w, h))) + 1:
                problems.append('%s: %s' % (r['ii_filename'], desc))
        cols = r['ii_filesize'] // r['ii_width']
        x, y = r['ii_width'] * (r['ii_offset'] % cols), r['ii_height'] * (r['ii_offset'] // cols)
        if x + r['ii_width'] > im.size[0] or y + r['ii_height'] > im.size[1]:
            problems.append('%s: cell outside the image' % r['ii_filename'])
        log('%s -> %d icon/%s %s, cell %d,%d %dx%d' % (tag, icon, r['ii_filename'], desc, x, y, r['ii_width'],
                                                         r['ii_height']))
    for p in problems:
        log('PROBLEM ' + p)
    return not problems


def check(a, log):
    import corridor as cr
    layout = layout_file.load(a.layout)
    mid = layout['id']
    with open(a.password_file, 'rb') as f:
        password = f.read().strip()
    out_data = os.path.join(a.out, 'data')
    cgd = zipfile.ZipFile(os.path.join(out_data, 'cgd.dip'))
    ui = zipfile.ZipFile(os.path.join(out_data, 'ui.dat'))
    icons = Icons(maptables.load(cgd.read('iconstag.cdb', pwd=password), 'iconstag'),
                  maptables.load(cgd.read('iconsinfo.cdb', pwd=password), 'iconsinfo'), ui)
    size = map_size_of(cgd, password, mid)
    S = 2

    def T(x, y):
        return (TEX / 2 + TEX * x / size) * S, (TEX / 2 - TEX * y / size) * S

    world = Nif.load(os.path.join(a.stage, 'World.nif'))
    colp = Nif.load(os.path.join(a.stage, 'World_colp.nif'))
    pads = []
    floors = Image.new('RGBA', (TEX * S, TEX * S), (24, 26, 34, 255))
    fd = ImageDraw.Draw(floors)
    for i, name, pose, md in cr.physx_actors(colp):
        tris = cr.actor_world_triangles(colp, md, pose)
        if 'Jumper' in name:
            pts = [p for t in tris for p in t]
            pads.append((name, sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)))
            continue
        if name.lower().startswith('fence'):
            continue
        for t in tris:
            u = [t[1][k] - t[0][k] for k in range(3)]
            v = [t[2][k] - t[0][k] for k in range(3)]
            nz = u[0] * v[1] - u[1] * v[0]
            ln = math.sqrt(sum(x * x for x in (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], nz))) or 1
            if abs(nz) / ln > 0.7:
                fd.polygon([T(p[0], p[1]) for p in t], fill=(52, 92, 60, 255))
    for tag in ('ICONTAG_MAP_BATTLE_%d', 'ICONTAG_MAP_BATTLE_F_%d'):
        im = icons.image(tag % mid)
        assert im is not None and im.size == (TEX, TEX), tag
        img = floors.copy()
        img.alpha_composite(im.resize((TEX * S, TEX * S), Image.LANCZOS))
        d = ImageDraw.Draw(img)
        sp = spawns(world)
        for g, n, p in sp:
            x, y = T(p[0], p[1])
            if g == 'FFA':
                d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(255, 235, 60, 255), outline=(0, 0, 0, 255))
            else:
                col = (255, 70, 70, 255) if g.endswith('REDTEAM') else (80, 170, 255, 255)
                d.rectangle([x - 4, y - 4, x + 4, y + 4], outline=col, width=2)
        for name, x0, y0 in pads:
            x, y = T(x0, y0)
            d.polygon([(x, y - 6), (x + 6, y), (x, y + 6), (x - 6, y)], fill=(255, 60, 255, 255),
                      outline=(0, 0, 0, 255))
        alpha = np.asarray(im)[..., 3] > 100
        walls = ndimage.binary_closing(np.asarray(im)[..., 3] > 40, iterations=4)
        outside = ndimage.binary_propagation(np.pad(np.zeros_like(walls), 1, constant_values=True),
                                             mask=np.pad(~walls, 1, constant_values=True))[1:-1, 1:-1]
        rgb = np.asarray(im)[..., :3].astype(int)
        zone = {'REDTEAM': (rgb[..., 0] - rgb[..., 2] > 60) & alpha,
                'BLUETEAM': (rgb[..., 2] - rgb[..., 0] > 60) & alpha}
        zone = {k: ndimage.binary_dilation(v, iterations=2) for k, v in zone.items()}

        def texel(x, y):
            return int(TEX / 2 - TEX * y / size), int(TEX / 2 + TEX * x / size)
        inside = sum(not outside[texel(p[0], p[1])] for g, n, p in sp)
        pads_in = sum(not outside[texel(x, y)] for n, x, y in pads)
        tdm = [(g, p) for g, n, p in sp if g.startswith('TDM/')]
        on_team = sum(bool(zone[g[4:]][texel(p[0], p[1])]) for g, p in tdm)
        log('check %s: spawns inside the outline %d/%d, jump pads %d/%d, TDM spawns on their team colour %d/%d'
            % (tag % mid, inside, len(sp), pads_in, len(pads), on_team, len(tdm)))
        path = os.path.join(a.out, 'preview', 'check_%s.png' % (tag % mid).lower())
        img.save(path)
        log('check: %s' % path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('layout')
    ap.add_argument('client', help='client folder: data/ui.dat and data/map.dat are read (source images, spawns)')
    ap.add_argument('cgd', help='cgd.dip to patch (with the merged map rows)')
    ap.add_argument('out', help='writes <out>/data/ui.dat, <out>/data/cgd.dip and <out>/preview/*.png')
    ap.add_argument('--password-file', required=True)
    ap.add_argument('--texconv', required=True, help='texconv.exe (DirectXTex)')
    ap.add_argument('--name', default='Big Map')
    ap.add_argument('--font', default=r'C:\Windows\Fonts\segoeuib.ttf')
    ap.add_argument('--stage', help='merged mapNNN/ folder: also writes check images with its spawns and jump pads')
    a = ap.parse_args()

    def log(s):
        print(s)
    tags = build(a, log)
    ok = verify(a, tags, log)
    if a.stage:
        check(a, log)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
