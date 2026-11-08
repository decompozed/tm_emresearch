from __future__ import annotations

import io
import struct
import zipfile
from pathlib import Path

LOGO_SOURCE = 'setup/ENG/login_back.dds'
MASK_SOURCE = 'setup/ENG/collection_03.dds'
MASK_BOX = (532, 270, 743, 364)
# collection_03 logo scaled by MASK_SCALE lands at MASK_AT in login_back (found by a least-squares search)
MASK_SCALE = 3.42
MASK_AT = (600, 99)
PAD = 24
OUTLINE_RGB = (0, 31, 90)
# inside of the wordmark's "V": source of the yellow gradient and of the blue splashes
V_BOX = (60, 64, 240, 316)
MONO_TEXT = (('N', 4.0), ('V', -4.0))
MONO_FONTS = ('ariblk.ttf', 'seguibl.ttf', 'impact.ttf', 'DejaVuSans-Bold.ttf')
MONO_HEIGHT = 1200
MONO_STROKE = 0.11
MONO_OVERLAP = 0.10
MONO_SQUASH = 0.8

CANVAS = 3000
BANNER_BOX = (202, 920, 2819, 2116)
LOGO_BOX = (200, 200, 2800, 2800)
ICON_SIZE = 32

ASSET_FILES = ('banner.png', 'logo.png', 'logo.ico')


class BrandError(RuntimeError):
    pass


def _pil():
    try:
        import numpy
        from PIL import Image, ImageFilter
    except ImportError as e:
        raise BrandError('brand-assets needs Pillow and numpy (pip install pillow numpy)') from e
    return numpy, Image, ImageFilter


def _read_dds(archive: Path, name: str):
    _, Image, _ = _pil()
    with zipfile.ZipFile(archive) as z:
        names = {n.lower(): n for n in z.namelist()}
        if name.lower() not in names:
            raise BrandError(f'{name} not found in {archive}')
        return Image.open(io.BytesIO(z.read(names[name.lower()]))).convert('RGBA')


def extract_wordmark(client_root: Path):
    """NostalgiaVolts logo with transparency: pixels from login_back.dds (large, opaque background) cut with the
    alpha of the small transparent copy in collection_03.dds; the edge band is refined on the dark outline colour."""
    np, Image, ImageFilter = _pil()
    ui = client_root / 'data' / 'ui.dat'
    small = _read_dds(ui, MASK_SOURCE).crop(MASK_BOX)
    back = _read_dds(ui, LOGO_SOURCE).convert('RGB')
    w, h = round(small.width * MASK_SCALE), round(small.height * MASK_SCALE)
    x, y = MASK_AT
    region = back.crop((x - PAD, y - PAD, x + w + PAD, y + h + PAD))
    up = small.resize((w, h), Image.BICUBIC)
    rgb = np.asarray(region, dtype=np.float32)
    inside = np.asarray(up.getchannel('A'), dtype=np.float32) > 250
    error = np.abs(rgb[PAD:PAD + h, PAD:PAD + w] - np.asarray(up, dtype=np.float32)[..., :3])[inside].mean()
    if error > 30:
        raise BrandError(f'{LOGO_SOURCE} does not match {MASK_SOURCE} (mean error {error:.1f}): different client?')

    alpha = Image.new('L', region.size, 0)
    alpha.paste(up.getchannel('A'), (PAD, PAD))
    a = np.asarray(alpha, dtype=np.float32) / 255

    def eroded(n):
        return np.asarray(alpha.filter(ImageFilter.MinFilter(n)), dtype=np.float32) > 252

    core = eroded(9)
    band = (np.asarray(alpha.filter(ImageFilter.MaxFilter(9)), dtype=np.float32) > 2) & ~core
    dist = np.sqrt(((rgb - np.array(OUTLINE_RGB, dtype=np.float32)) ** 2).sum(-1))
    colour = np.clip((90.0 - dist) / 50.0, 0, 1)
    final = np.where(core, 1.0, np.where(band, colour * (a > 0.25) * np.clip(a * 2.5, 0, 1), 0.0))
    out = np.dstack([rgb, final * 255]).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def _font(size: int, font: str | None):
    from PIL import ImageFont
    for name in ((font,) if font else MONO_FONTS):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    raise BrandError(f'no bold font found ({font or ", ".join(MONO_FONTS)}): pass --font')


def _fill_from_wordmark(wordmark):
    """Vertical colour profile of the yellow fill and the blue splash mask/colour, taken from the wordmark's 'V'."""
    np, _, _ = _pil()
    px = np.asarray(wordmark.crop(V_BOX), dtype=np.float32)
    r, g, b, a = px[..., 0], px[..., 1], px[..., 2], px[..., 3]
    yellow = (a > 250) & (r > 190) & (g > 110) & (b < 110)
    blue = (a > 250) & (b > 170) & (r < 140) & (g > 110)
    rows = []
    for y in range(px.shape[0]):
        sel = yellow[y]
        rows.append(px[y, sel, :3].mean(0) if sel.sum() > 5 else rows[-1] if rows else np.array([250, 220, 0.0]))
    profile = np.array(rows)
    kernel = np.ones(9) / 9
    profile = np.stack([np.convolve(np.pad(profile[:, c], 4, mode='edge'), kernel, 'valid') for c in range(3)], 1)
    return profile, blue.astype(np.float32), np.median(px[blue][:, :3], axis=0)


def _outline(mask, width: float):
    np, Image, ImageFilter = _pil()
    blur = np.asarray(Image.fromarray(mask).filter(ImageFilter.GaussianBlur(width / 2)), dtype=np.float32) / 255
    return (np.clip((blur - 0.023) / 0.012, 0, 1) * 255).astype(np.uint8)


def monogram(wordmark, font: str | None = None):
    """'NV' in the style of the old 'TB': heavy tilted letters, the wordmark's yellow fill with blue splashes and its
    dark blue outline, one silhouette around both letters."""
    np, Image, _ = _pil()
    from PIL import ImageDraw
    profile, splash, splash_rgb = _fill_from_wordmark(wordmark)
    stroke = round(MONO_HEIGHT * MONO_STROKE)
    letters = []
    for i, (ch, angle) in enumerate(MONO_TEXT):
        f = _font(MONO_HEIGHT * 2, font)
        canvas = Image.new('L', (MONO_HEIGHT * 3, MONO_HEIGHT * 3), 0)
        ImageDraw.Draw(canvas).text((MONO_HEIGHT // 2, MONO_HEIGHT // 4), ch, font=f, fill=255)
        glyph = canvas.crop(canvas.getbbox())
        glyph = glyph.resize((round(glyph.width * MONO_SQUASH * MONO_HEIGHT / glyph.height), MONO_HEIGHT), Image.LANCZOS)
        h, w = glyph.height, glyph.width
        colour = profile[np.linspace(0, len(profile) - 1, h).astype(int)][:, None, :].repeat(w, 1)
        sp = Image.fromarray((splash * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
        sp = np.asarray(sp.transpose(Image.FLIP_LEFT_RIGHT) if i % 2 == 0 else sp, dtype=np.float32)[..., None] / 255
        rgb = colour * (1 - sp) + splash_rgb * sp
        fill = Image.fromarray(np.dstack([rgb, np.asarray(glyph)]).astype(np.uint8), 'RGBA')
        letters.append(fill.rotate(angle, Image.BICUBIC, expand=True))

    overlap = round(MONO_HEIGHT * MONO_OVERLAP)
    width = sum(l.width for l in letters) - overlap * (len(letters) - 1) + 2 * stroke
    height = max(l.height for l in letters) + 2 * stroke
    out = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    x, pos = stroke, []
    for l in letters:
        pos.append((x, (height - l.height) // 2))
        x += l.width - overlap
    silhouette = Image.new('L', out.size, 0)
    for l, p in zip(letters, pos):
        silhouette.paste(255, p, l.getchannel('A'))
    border = Image.new('RGBA', out.size, OUTLINE_RGB + (255,))
    border.putalpha(Image.fromarray(_outline(np.asarray(silhouette), stroke)))
    out.alpha_composite(border)
    for l, p in zip(letters, pos):
        rim = Image.new('RGBA', l.size, OUTLINE_RGB + (255,))
        rim.putalpha(Image.fromarray(_outline(np.asarray(l.getchannel('A')), stroke * 0.35)))
        out.alpha_composite(rim, p)
        out.alpha_composite(l, p)
    return _trim(out)


def _trim(img):
    return img.crop(img.getchannel('A').point(lambda v: 255 if v > 8 else 0).getbbox())


def _place(img, size: int, box: tuple[int, int, int, int]):
    _, Image, _ = _pil()
    img = _trim(img)
    bw, bh = box[2] - box[0], box[3] - box[1]
    scale = min(bw / img.width, bh / img.height)
    w, h = round(img.width * scale), round(img.height * scale)
    canvas = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(img.resize((w, h), Image.LANCZOS), (box[0] + (bw - w) // 2, box[1] + (bh - h) // 2))
    return canvas


def icon_bytes(img) -> bytes:
    """32x32 32-bit BMP icon (same layout and size as the launcher's own icon: 4286 bytes)."""
    np, _, _ = _pil()
    icon = _place(img, ICON_SIZE, (1, 1, ICON_SIZE - 1, ICON_SIZE - 1))
    px = np.asarray(icon, dtype=np.uint8)
    bgra = px[::-1][..., [2, 1, 0, 3]].tobytes()
    mask_rows = ((px[::-1][..., 3] == 0).astype(np.uint8))
    mask = np.packbits(mask_rows, axis=1).tobytes()
    header = struct.pack('<IiiHHIIiiII', 40, ICON_SIZE, ICON_SIZE * 2, 1, 32, 0, len(bgra) + len(mask), 0, 0, 0, 0)
    body = header + bgra + mask
    return struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', ICON_SIZE, ICON_SIZE, 0, 0, 1, 32, len(body), 22) + body


def write_assets(client_root: Path, out: Path, font: str | None = None) -> list[Path]:
    out.mkdir(parents=True, exist_ok=True)
    wordmark = extract_wordmark(client_root)
    mono = monogram(wordmark, font)
    _place(wordmark, CANVAS, BANNER_BOX).save(out / 'banner.png', optimize=True)
    _place(mono, CANVAS, LOGO_BOX).save(out / 'logo.png', optimize=True)
    (out / 'logo.ico').write_bytes(icon_bytes(mono))
    return [out / n for n in ASSET_FILES]


def check_brand_dir(path: Path) -> dict[str, Path]:
    files = {n: path / n for n in ASSET_FILES}
    missing = [n for n, p in files.items() if not p.is_file()]
    if missing:
        raise BrandError(f'{path}: missing {", ".join(missing)} (make them with brand-assets)')
    ico = files['logo.ico'].read_bytes()
    if len(ico) != 4286 or struct.unpack_from('<HHH', ico) != (0, 1, 1) or ico[6:8] != bytes([32, 32]):
        raise BrandError('logo.ico must be a single 32x32 32-bit BMP icon of 4286 bytes (as made by brand-assets)')
    return files
