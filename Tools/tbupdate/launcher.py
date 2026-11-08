from __future__ import annotations

import json
import os
import struct
import subprocess
import tempfile
import zlib
from dataclasses import dataclass, asdict
from pathlib import Path

from . import brand

BUNDLE_SIGNATURE = bytes.fromhex('8b1202b96a612038727b930214d7a03213f5b9e6efae3318ee3b2dce24b36aae')
CDN_AUTHORITY = 'cdn.toybattles.net'
API_AUTHORITY = '57.129.76.24:8000'
CDN_LITERALS = ('http://cdn.toybattles.net/ENG', 'http://cdn.toybattles.net/',
                'http://cdn.toybattles.net/ENG/microvolts/patch.ini')
API_LITERALS = ('http://57.129.76.24:8000/api/playercount', 'http://57.129.76.24:8000/api/top-players',
                'http://57.129.76.24:8000/api/top-clans')
MUTEX = ('ToyBattlesLauncherInstance', 'ToyBattlesHQLaunchInstance')
STATE_DIR = ('ToyBattlesLauncher', 'ToyBattlesHQLaunch')
ASSEMBLY_ROW_1_0_20 = bytes.fromhex('04800000' '0100' '0000' '1400' '0000')
ASSEMBLY_ROW_1_0_0 = bytes.fromhex('04800000' '0100' '0000' '0000' '0000')
ALIGNMENT = 4096

BRAND_NAME = 'NostalgiaVolts'
BRAND_MUTEX = 'NostalgiaVoltsLauncherInstance'
BRAND_STATE_DIR = 'NostalgiaVoltsLauncher'
REBRAND_PROJECT = Path(__file__).resolve().parent / 'rebrand' / 'LauncherRebrand.csproj'
# every "ToyBattles" in code strings except the temp-file pattern and the GitHub repository name
BRAND_LDSTR = [{'pattern': 'ToyBattles(?![_-])', 'replace': BRAND_NAME}]
BRAND_BAML = {
    'views/mainwindow.baml': {'ToyBattles Launcher': 'NostalgiaVolts Launcher', 'ToyBattles': BRAND_NAME,
                              'T': 'N', 'B': 'V'},
    'views/splashwindow.baml': {'ToyBattles Launcher': 'NostalgiaVolts Launcher',
                                'TOYBATTLES LAUNCHER': 'NOSTALGIAVOLTS LAUNCHER'},
    'views/homeview.baml': {'ToyBattles': BRAND_NAME, 'TOY': 'NOSTALGIA', 'BATTLES': 'VOLTS'},
}
BRAND_BAML_COUNTS = {
    'views/mainwindow.baml': {'ToyBattles Launcher': 1, 'ToyBattles': 2, 'T': 1, 'B': 1},
    'views/splashwindow.baml': {'ToyBattles Launcher': 1, 'TOYBATTLES LAUNCHER': 1},
    'views/homeview.baml': {'ToyBattles': 1, 'TOY': 1, 'BATTLES': 1},
}
BRAND_RESOURCES = {'Assets/banner.png': 'banner.png', 'Assets/banner_valentine.png': 'banner.png',
                   'Assets/logo.png': 'logo.png', 'Assets/logo_valentine.png': 'logo.png',
                   'Assets/logo.ico': 'logo.ico'}
BRAND_AUDIT = 'toy ?battles'
BRAND_KEPT = {'ToyBattles_FullInstall_*', 'ToyBattles_FullInstall_', 'https://toybattles.net/',
              'https://discord.gg/toybattles',
              'https://api.github.com/repos/kixrababyy/ToyBattles-Launcher/releases/latest',
              'https://github.com/kixrababyy/ToyBattles-Launcher/releases/latest/download/Launcher.exe',
              'https://github.com/kixrababyy/ToyBattles-Launcher/releases/latest'}
ICON_HEADER = struct.pack('<IiiHHI', 40, 32, 64, 1, 32, 0)
ICON_BODY = 4286 - 22


class LauncherPatchError(RuntimeError):
    pass


@dataclass
class BundleEntry:
    name: str
    offset: int
    size: int
    compressed: int
    field_pos: int


@dataclass
class PatchInfo:
    update_base_url: str
    update_base_path: str
    api_base_path: str
    status_url: str
    state_dir: str
    mutex: str
    brand: str | None = None


def read_bundle(data: bytes) -> tuple[int, list[BundleEntry]]:
    sig = data.find(BUNDLE_SIGNATURE)
    if sig < 8:
        raise LauncherPatchError('not a .NET single-file bundle')
    header = struct.unpack_from('<q', data, sig - 8)[0]
    major, _minor, count = struct.unpack_from('<IIi', data, header)
    if major < 6:
        raise LauncherPatchError(f'bundle format {major} not supported')
    pos = header + 12
    _, pos = _read_string(data, pos)
    pos += 8 * 4 + 8
    entries = []
    for _ in range(count):
        field_pos = pos
        offset, size, compressed = struct.unpack_from('<qqq', data, pos)
        pos += 25
        name, pos = _read_string(data, pos)
        entries.append(BundleEntry(name, offset, size, compressed, field_pos))
    return header, entries


def _read_string(data: bytes, pos: int) -> tuple[str, int]:
    length, shift = 0, 0
    while True:
        b = data[pos]
        pos += 1
        length |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            break
    return data[pos:pos + length].decode('utf-8'), pos + length


def padded_url(literal: str, old_authority: str, authority: str) -> str:
    prefix = 'http://' + old_authority
    if not literal.startswith(prefix):
        raise LauncherPatchError(f'{literal} does not start with {prefix}')
    missing = len(old_authority) - len(authority)
    if missing < 0:
        raise LauncherPatchError(f"'{authority}' is longer than '{old_authority}' ({len(old_authority)} chars max)")
    filler = '' if missing == 0 else '/' + '_' * (missing - 1)
    return 'http://' + authority + filler + literal[len(prefix):]


def _user_string_offsets(blob: bytes, text: str) -> list[int]:
    needle = text.encode('utf-16-le')
    length = 2 * len(text) + 1
    prefix = bytes([length]) if length < 0x80 else bytes([0x80 | (length >> 8), length & 0xFF])
    found, start = [], 0
    while (i := blob.find(prefix + needle, start)) >= 0:
        if blob[i + len(prefix) + len(needle)] in (0, 1):
            found.append(i + len(prefix))
        start = i + 1
    return found


def replace_user_string(blob: bytearray, old: str, new: str) -> None:
    if len(old) != len(new) or not new.isascii():
        raise LauncherPatchError(f"replacement for '{old}' must be ASCII with the same length")
    offsets = _user_string_offsets(bytes(blob), old)
    if len(offsets) != 1:
        raise LauncherPatchError(f"'{old}' found {len(offsets)} times, expected once (unknown launcher build?)")
    blob[offsets[0]:offsets[0] + 2 * len(old)] = new.encode('utf-16-le')


def _replace_unique(blob: bytearray, old: bytes, new: bytes, what: str) -> None:
    if blob.count(old) != 1:
        raise LauncherPatchError(f'{what}: pattern found {blob.count(old)} times, expected once')
    i = blob.find(old)
    blob[i:i + len(old)] = new


def _entry(entries: list[BundleEntry], name: str) -> BundleEntry:
    for e in entries:
        if e.name == name:
            return e
    raise LauncherPatchError(f'{name} not found in the bundle')


def rebuild_bundle(data: bytes, replace: dict[str, bytes]) -> bytes:
    """Rewrite the bundle with some files replaced; entries keep their order and compression, the manifest stays last."""
    sig = data.find(BUNDLE_SIGNATURE)
    header, entries = read_bundle(data)
    _, meta = _read_string(data, header + 12)
    deps_offset, deps_size, config_offset, config_size = struct.unpack_from('<qqqq', data, meta)
    entries.sort(key=lambda e: e.offset)
    out = bytearray(data[:entries[0].offset])
    manifest = bytearray(data[header:])
    moved = {}
    for e in entries:
        if e.name in replace:
            blob, size, compressed = replace[e.name], len(replace[e.name]), 0
            if e.compressed:
                deflate = zlib.compressobj(9, zlib.DEFLATED, -15)
                blob = deflate.compress(blob) + deflate.flush()
                compressed = len(blob)
        else:
            blob, size, compressed = data[e.offset:e.offset + (e.compressed or e.size)], e.size, e.compressed
        if e.name.endswith('.dll') and not compressed:
            out += b'\0' * (-len(out) % ALIGNMENT)
        moved[e.offset] = len(out)
        struct.pack_into('<qqq', manifest, e.field_pos - header, len(out), size, compressed)
        out += blob
    struct.pack_into('<qqqq', manifest, meta - header, moved.get(deps_offset, 0), deps_size,
                     moved.get(config_offset, 0), config_size)
    struct.pack_into('<q', out, sig - 8, len(out))
    out += manifest
    return bytes(out)


def _launcher_icon(app: bytes) -> bytes:
    found, i = set(), app.find(ICON_HEADER)
    while i >= 0:
        found.add(app[i:i + ICON_BODY])
        i = app.find(ICON_HEADER, i + 1)
    if len(found) != 1:
        raise LauncherPatchError(f'Launcher.dll: {len(found)} different 32x32 icons, expected 1')
    return found.pop()


def build_rebrand_tool(work: Path) -> Path:
    env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_NOLOGO='1')
    r = subprocess.run(['dotnet', 'build', str(REBRAND_PROJECT), '-c', 'Release', '-o', str(work / 'tool')],
                       capture_output=True, text=True, env=env)
    if r.returncode:
        raise LauncherPatchError('building the rebrand tool failed (needs the .NET 8 SDK):\n' + r.stdout + r.stderr)
    return work / 'tool' / 'LauncherRebrand.dll'


def _rebrand_assembly(tool: Path, work: Path, name: str, blob: bytes, rules: dict) -> tuple[bytes, dict]:
    src, dst, spec = work / name, work / ('new_' + name), work / (name + '.json')
    src.write_bytes(blob)
    spec.write_text(json.dumps(rules), encoding='utf-8')
    r = subprocess.run(['dotnet', str(tool), str(src), str(dst), str(spec)], capture_output=True, text=True,
                       encoding='utf-8')
    if r.returncode:
        raise LauncherPatchError(f'rebrand of {name} failed:\n{r.stdout}{r.stderr}')
    return dst.read_bytes(), json.loads(r.stdout)


def _check_ldstr(name: str, report: dict, required: set[str]) -> None:
    changed = {c['old'] for c in report['ldstr']}
    if not required <= changed:
        raise LauncherPatchError(f'{name}: {sorted(required - changed)} not found (unknown launcher build?)')
    left = [s for s in report['unchanged'] if s not in BRAND_KEPT]
    if left:
        raise LauncherPatchError(f'{name}: strings still naming ToyBattles: {left}')


def rebrand_assemblies(app: bytes, core: bytes, assets: dict[str, Path], work: Path
                       ) -> tuple[bytes, bytes, list[tuple[str, str]]]:
    tool = build_rebrand_tool(work)
    app = app.replace(_launcher_icon(app), assets['logo.ico'].read_bytes()[22:])
    app_rules = {'ldstr': BRAND_LDSTR, 'baml': BRAND_BAML, 'audit': BRAND_AUDIT,
                 'resources': {k: str(assets[v]) for k, v in BRAND_RESOURCES.items()}}
    app, app_report = _rebrand_assembly(tool, work, 'Launcher.dll', app, app_rules)
    _check_ldstr('Launcher.dll', app_report, {MUTEX[0]})
    if app_report['baml'] != BRAND_BAML_COUNTS:
        raise LauncherPatchError(f'Launcher.dll: unexpected XAML replacements {app_report["baml"]}')
    core, core_report = _rebrand_assembly(tool, work, 'Launcher.Core.dll', core,
                                          {'ldstr': BRAND_LDSTR, 'audit': BRAND_AUDIT})
    _check_ldstr('Launcher.Core.dll', core_report, {STATE_DIR[0]})
    changes = [(c['old'], c['new']) for c in app_report['ldstr'] + core_report['ldstr']]
    changes += [(f'{f}: {old}', BRAND_BAML[f][old]) for f, counts in app_report['baml'].items() for old in counts]
    changes += [(f'resource {r}', BRAND_RESOURCES[r]) for r in app_report['resources']]
    return app, core, changes


def patch_launcher(source: Path, target: Path, authority: str,
                   brand_dir: Path | None = None) -> tuple[PatchInfo, list[tuple[str, str]]]:
    data = bytearray(source.read_bytes())
    _, entries = read_bundle(bytes(data))
    app = _entry(entries, 'Launcher.dll')
    core = _entry(entries, 'Launcher.Core.dll')
    if app.compressed:
        raise LauncherPatchError('Launcher.dll is compressed in this bundle (unknown launcher build)')
    assets = brand.check_brand_dir(brand_dir) if brand_dir else None

    app_blob = bytearray(data[app.offset:app.offset + app.size])
    replacements = {lit: padded_url(lit, CDN_AUTHORITY, authority) for lit in CDN_LITERALS}
    replacements |= {lit: padded_url(lit, API_AUTHORITY, authority) for lit in API_LITERALS}
    if not assets:
        replacements[MUTEX[0]] = MUTEX[1]
    for old, new in replacements.items():
        replace_user_string(app_blob, old, new)
    _replace_unique(app_blob, ASSEMBLY_ROW_1_0_20, ASSEMBLY_ROW_1_0_0, 'Launcher.dll assembly version 1.0.20.0')

    raw = data[core.offset:core.offset + (core.compressed or core.size)]
    core_blob = bytearray(zlib.decompress(bytes(raw), -15) if core.compressed else raw)

    changes: list[tuple[str, str]] = []
    if assets:
        icon = _launcher_icon(bytes(app_blob))
        host = data[:min(e.offset for e in entries)]
        if host.count(icon) != 1:
            raise LauncherPatchError('Launcher.exe: the application icon is not in the host once')
        i = host.find(icon)
        data[i:i + len(icon)] = assets['logo.ico'].read_bytes()[22:]
        changes.append(('Launcher.exe icon', 'logo.ico'))
        with tempfile.TemporaryDirectory(prefix='tbrebrand_') as tmp:
            new_app, new_core, more = rebrand_assemblies(bytes(app_blob), bytes(core_blob), assets, Path(tmp))
        changes += more
        state_dir, mutex = BRAND_STATE_DIR, BRAND_MUTEX
    else:
        new_app = bytes(app_blob)
        replace_user_string(core_blob, *STATE_DIR)
        new_core = bytes(core_blob)
        state_dir, mutex = STATE_DIR[1], MUTEX[1]

    target.write_bytes(rebuild_bundle(bytes(data), {app.name: new_app, core.name: new_core}))
    base = replacements[CDN_LITERALS[0]]
    host_prefix = 'http://' + authority
    info = PatchInfo(update_base_url=base, update_base_path=base[len(host_prefix):],
                     api_base_path=replacements[API_LITERALS[0]][len(host_prefix):].rsplit('/', 1)[0],
                     status_url=replacements[CDN_LITERALS[1]], state_dir=state_dir, mutex=mutex,
                     brand=BRAND_NAME if assets else None)
    return info, changes


def updateinfo_ini(info: PatchInfo) -> str:
    return (f'[update]\r\naddr = {info.update_base_url}\r\n\r\n'
            f'[FullFile]\r\naddr = {info.update_base_url}/microvolts/Full/\r\n')


def write_patch_info(info: PatchInfo, path: Path) -> None:
    path.write_text(json.dumps(asdict(info), indent=2) + '\n', encoding='utf-8')
