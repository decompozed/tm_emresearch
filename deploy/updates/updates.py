import argparse
import hashlib
import json
import posixpath
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / 'Tools'))

from tbupdate.versions import GameVersion, parse_patch_ini  # noqa: E402

CONTAINER = 'tb-updates'
IMAGE = 'nginx:1.27-alpine'
API_STUBS = {'playercount': '{"total":0,"servers":[]}', 'top-players': '[]', 'top-clans': '[]'}


BACKUP = """hist="$1"; prod="$2"; ver="$3"; shift 3
mkdir -p "$hist"
for f in "$@"; do
  if [ -f "$prod/$f" ]; then mkdir -p "$hist/$(dirname "$f")"; cp -p "$prod/$f" "$hist/$f"; fi
done
echo "$ver" > "$hist/.published"
"""
PROMOTE = """src="$1"; dst="$2"; shift 2
for f in "$@"; do mkdir -p "$dst/$(dirname "$f")"; mv -f "$src/$f" "$dst/$f"; done
rm -rf "$src"
"""
RESTORE = """src="$1"; tmp="$2"; dst="$3"; shift 3
rm -rf "$tmp"
for f in "$@"; do mkdir -p "$tmp/$(dirname "$f")"; cp -p "$src/$f" "$tmp/$f"; done
for f in "$@"; do mkdir -p "$dst/$(dirname "$f")"; mv -f "$tmp/$f" "$dst/$f"; done
rm -rf "$tmp"
"""
SHELVE = """dst="$1"; shelf="$2"; shift 2
mkdir -p "$shelf"
for v in "$@"; do mv "$dst/$v" "$shelf/$v"; done
"""
LIST_DIRS = """for d in "$1"/*/; do [ -d "$d" ] || continue; d=${d%/}; echo "${d##*/}"; done
"""
LIST_HISTORY = """for d in "$1"/*/; do [ -d "$d" ] || continue; d=${d%/}; echo "${d##*/}|$(cat "$d/.published" 2>/dev/null || true)"; done
"""
LIST_SNAPSHOT = """cd "$1"; find . -type f ! -name .published ! -path './removed/*' | sed 's|^\\./||' | sort
"""


class Fail(RuntimeError):
    pass


class Transport:
    def __init__(self, root: str):
        self.root = root.rstrip('/')

    def sh(self, script: str, *args: str, check: bool = True) -> str:
        result = subprocess.run(self.shell_cmd(args), input=('set -eu\n' + script).encode(), capture_output=True)
        if check and result.returncode != 0:
            raise Fail(f'remote command failed ({result.returncode}): {result.stderr.decode(errors="replace").strip()}')
        return result.stdout.decode(errors='replace')

    def shell_cmd(self, args) -> list[str]:
        raise NotImplementedError

    def put(self, files: list[tuple[Path, str]]) -> None:
        raise NotImplementedError


class Ssh(Transport):
    def __init__(self, target: str, root: str):
        super().__init__(root)
        self.target = target

    def shell_cmd(self, args):
        return ['ssh', '-o', 'BatchMode=yes', self.target, 'sh', '-s', '--', *args]

    def put(self, files):
        by_dir = defaultdict(list)
        for local, remote in files:
            by_dir[posixpath.dirname(remote)].append(str(local))
        self.sh('for d in "$@"; do mkdir -p "$d"; done', *by_dir)
        for directory, sources in by_dir.items():
            result = subprocess.run(['scp', '-q', '-B', *sources, f'{self.target}:{directory}/'])
            if result.returncode != 0:
                raise Fail(f'scp to {directory} failed')


class Wsl(Transport):
    def __init__(self, windows_root: str):
        self.windows_root = Path(windows_root).resolve()
        self.windows_root.mkdir(parents=True, exist_ok=True)
        converted = subprocess.run(['wsl', 'wslpath', '-u', self.windows_root.as_posix()], capture_output=True, text=True,
                                   check=True).stdout.strip()
        super().__init__(converted)

    def shell_cmd(self, args):
        return ['wsl', 'sh', '-s', '--', *args]

    def put(self, files):
        for local, remote in files:
            if not remote.startswith(self.root + '/'):
                raise Fail(f'{remote} is outside {self.root}')
            target = self.windows_root / remote[len(self.root) + 1:]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(local, target)


def clean_path(path: str) -> str:
    return '/' + '/'.join(p for p in path.split('/') if p)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as f:
        while chunk := f.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def local_inventory(product: Path) -> dict[str, tuple[Path, str]]:
    return {p.relative_to(product).as_posix(): (p, sha256(p)) for p in sorted(product.rglob('*')) if p.is_file()}


def remote_inventory(t: Transport, product: str) -> dict[str, str]:
    out = t.sh('cd "$1" 2>/dev/null || exit 0; find . -type f -exec sha256sum {} +', product)
    inventory = {}
    for line in out.splitlines():
        digest, _, name = line.partition('  ')
        inventory[name[2:] if name.startswith('./') else name] = digest
    return inventory


def upload_verified(t: Transport, items: dict[str, tuple[Path, str]], staging: str) -> None:
    t.sh('rm -rf "$1"; mkdir -p "$1"', staging)
    t.put([(path, f'{staging}/{rel}') for rel, (path, _) in items.items()])
    listing = ''.join(f'{digest}  {rel}\n' for rel, (_, digest) in items.items())
    t.sh(f'cd "$1"\nsha256sum -c --quiet <<"EOF"\n{listing}EOF', staging)


def load_info(path: str) -> dict:
    info = json.loads(Path(path).read_text(encoding='utf-8'))
    info['update_base_path'] = clean_path(info['update_base_path'])
    info['api_base_path'] = clean_path(info['api_base_path'])
    return info


def transport(args) -> Transport:
    if args.local_root:
        return Wsl(args.local_root)
    if not args.ssh:
        raise Fail('pass --ssh user@host (or --local-root for a local test)')
    return Ssh(args.ssh, args.root)


def product_dir(t: Transport, info: dict) -> str:
    return f'{t.root}/www{info["update_base_path"]}/microvolts'


def docker_command(t: Transport, port: int) -> str:
    root = t.root if t.root.startswith('/') else f'$HOME/{t.root}'
    return (f'docker run -d --name {CONTAINER} --restart unless-stopped -p {port}:80 '
            f'--read-only --tmpfs /tmp --tmpfs /var/cache/nginx --tmpfs /var/run '
            f'--memory 256m --cpus 1 --log-opt max-size=50m --log-opt max-file=5 '
            f'-v "{root}/www:/srv/www:ro" -v "{root}/nginx.conf:/etc/nginx/nginx.conf:ro" '
            f'--entrypoint nginx {IMAGE} -g "daemon off;"')


def cmd_setup(args) -> None:
    t, info = transport(args), load_info(args.info)
    api = f'{t.root}/www{info["api_base_path"]}'
    t.sh('mkdir -p "$1/www" "$1/incoming" "$1/history" "$2"', t.root, api)
    stubs = HERE / '.api_stubs'
    stubs.mkdir(exist_ok=True)
    conf = stubs / 'nginx.conf'
    conf.write_text((HERE / 'nginx.conf').read_text(encoding='utf-8').replace('@UPDATE_BASE@', info['update_base_path']), encoding='utf-8')
    files = [(conf, f'{t.root}/nginx.conf')]
    for name, body in API_STUBS.items():
        (stubs / name).write_text(body, encoding='ascii')
        files.append((stubs / name, f'{api}/{name}'))
    t.put(files)
    shutil.rmtree(stubs, ignore_errors=True)
    print(f'(OK) {t.root}/ ready (www, incoming, history, nginx.conf, api stubs at {info["api_base_path"]})')
    command = docker_command(t, args.port)
    if args.start:
        t.sh(f'docker rm -f {CONTAINER} >/dev/null 2>&1 || true\n{command}\n')
        print(f'(OK) container {CONTAINER} started on port {args.port}')
    else:
        print('(*) start the server on the VPS with:\n    ' + command)


def plan(local: dict, remote: dict) -> tuple[dict, dict, list[str]]:
    versions, files, conflicts = defaultdict(dict), {}, []
    for rel, item in local.items():
        top = rel.split('/')[0]
        if rel in remote and remote[rel] == item[1]:
            continue
        if _is_version(top):
            if any(r.startswith(top + '/') for r in remote):
                conflicts.append(rel)
            else:
                versions[top][rel] = item
        else:
            files[rel] = item
    return versions, files, conflicts


def _is_version(name: str) -> bool:
    try:
        GameVersion.parse(name)
        return True
    except ValueError:
        return False


def with_signed_manifest(local: dict, version: str, key_path: Path, out_dir: Path) -> dict:
    from tbupdate import signing  # needs the 'cryptography' package, only when signing
    local = {r: i for r, i in local.items() if r not in (signing.MANIFEST_NAME, signing.SIGNATURE_NAME)}
    manifest = signing.build_signed_manifest({rel: digest for rel, (_, digest) in local.items()}, version)
    signature = signing.sign(manifest, signing.load_private_key(key_path))
    (out_dir / signing.MANIFEST_NAME).write_bytes(manifest)
    (out_dir / signing.SIGNATURE_NAME).write_bytes(signature)
    for name in (signing.MANIFEST_NAME, signing.SIGNATURE_NAME):
        local[name] = (out_dir / name, sha256(out_dir / name))
    return local


def cmd_publish(args) -> None:
    t, info = transport(args), load_info(args.info)
    tree = Path(args.tree) / 'microvolts'
    local = local_inventory(tree)
    if 'patch.ini' not in local:
        raise Fail(f'{tree / "patch.ini"} missing')
    latest = parse_patch_ini(local['patch.ini'][0].read_text(encoding='ascii')).latest
    if args.sign_key:
        # signed on this machine; the private key never goes to the server, only the manifest and its signature do
        local = with_signed_manifest(local, str(latest), Path(args.sign_key), Path(tempfile.mkdtemp(prefix='tb-sign-')))
    product = product_dir(t, info)
    remote = remote_inventory(t, product)
    versions, files, conflicts = plan(local, remote)
    if conflicts:
        raise Fail(f'published versions differ from the local tree (never rebuild a version): {conflicts}')
    print(f'(*) local latest {latest}; new versions: {sorted(versions) or "none"}; files: {sorted(files) or "none"}')
    if args.dry_run or (not versions and not files):
        return
    stamp = time.strftime('%Y%m%d-%H%M%S')
    staging = f'{t.root}/incoming/{stamp}'
    for version, items in sorted(versions.items(), key=lambda kv: GameVersion.parse(kv[0]).key()):
        upload_verified(t, items, staging)
        t.sh('test ! -e "$3/$2"; mkdir -p "$3"; mv "$1/$2" "$3/$2"; rm -rf "$1"', staging, version, product)
        print(f'(OK) {version}/ online')
    others = {r: i for r, i in files.items() if r != 'patch.ini'}
    ordered = sorted(others) + (['patch.ini'] if 'patch.ini' in files else [])
    history = f'{t.root}/history/{stamp}'
    t.sh(BACKUP, history, product, str(latest), *ordered)
    if ordered:
        upload_verified(t, {r: files[r] for r in ordered}, staging)
        t.sh(PROMOTE, staging, product, *ordered)
    print(f'(OK) published {latest} (backup: {history})')
    if args.public_url != 'none':
        check_public(args.public_url or info['update_base_url'], local, versions)


def check_public(base: str, local: dict, versions: dict) -> None:
    url = f'{base}/microvolts/patch.ini'
    with urllib.request.urlopen(url, timeout=15) as r:
        if r.read() != local['patch.ini'][0].read_bytes():
            raise Fail(f'{url} differs from the local patch.ini (cache or wrong path?)')
    for items in versions.values():
        for rel, (path, _) in items.items():
            request = urllib.request.Request(f'{base}/microvolts/{rel}', method='HEAD')
            with urllib.request.urlopen(request, timeout=15) as r:
                if int(r.headers['Content-Length']) != path.stat().st_size:
                    raise Fail(f'{rel}: public Content-Length differs')
    print(f'(OK) public check passed on {base}')


def cmd_rollback(args) -> None:
    t, info = transport(args), load_info(args.info)
    product = product_dir(t, info)
    lines = t.sh(LIST_HISTORY, f'{t.root}/history', check=False).splitlines()
    pairs = [tuple(line.split('|', 1)) for line in lines if '|' in line]
    if args.list or not args.to:
        for stamp, version in pairs:
            print(f'{stamp}  published {version} (rollback restores the state before it)')
        return
    if args.to not in dict(pairs):
        raise Fail(f'no backup {args.to}')
    snapshot = f'{t.root}/history/{args.to}'
    restored = t.sh(LIST_SNAPSHOT, snapshot).split()
    if 'patch.ini' not in restored:
        raise Fail(f'{snapshot} has no patch.ini')
    staging = f'{t.root}/incoming/rollback-{args.to}'
    ordered = [r for r in restored if r != 'patch.ini'] + ['patch.ini']
    t.sh(RESTORE, snapshot, staging, product, *ordered)
    print(f'(OK) restored patch.ini and {len(ordered) - 1} files from {args.to}')
    if args.purge:
        text = t.sh('cat "$1/patch.ini"', product)
        keep = parse_patch_ini(text).latest
        newer = [v for v in remote_versions(t, product) if GameVersion.parse(v).key() > keep.key()]
        t.sh(SHELVE, product, f'{snapshot}/removed', *newer)
        print(f'(OK) moved {newer or "nothing"} to {snapshot}/removed')


def remote_versions(t: Transport, product: str) -> list[str]:
    names = t.sh(LIST_DIRS, product, check=False).split()
    return [n for n in names if _is_version(n)]


def cmd_status(args) -> None:
    t, info = transport(args), load_info(args.info)
    product = product_dir(t, info)
    text = t.sh('cat "$1/patch.ini" 2>/dev/null || true', product)
    print(f'remote latest : {parse_patch_ini(text).latest if text.strip() else "none"}')
    print(f'versions      : {" ".join(sorted(remote_versions(t, product), key=lambda v: GameVersion.parse(v).key()))}')
    print(t.sh(f'docker ps --filter name=^{CONTAINER}$ --format "container     : {{{{.Status}}}}" 2>/dev/null || true',
               check=False).strip())


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description='Publish the ToyBattles update tree to the update server')
    p.add_argument('--ssh', help='user@host of the VPS')
    p.add_argument('--root', default='tb-updates', help='folder under the remote home (default tb-updates)')
    p.add_argument('--local-root', help='local test: Windows folder used through WSL instead of ssh')
    p.add_argument('--info', required=True, help='launcher.json written by "updater.py patch-launcher"')
    sub = p.add_subparsers(dest='command', required=True)

    s = sub.add_parser('setup', help='create the folders, nginx.conf and api stubs')
    s.add_argument('--port', type=int, default=80)
    s.add_argument('--start', action='store_true', help='also (re)start the nginx container')
    s.set_defaults(func=cmd_setup)

    s = sub.add_parser('publish', help='upload new versions, then switch patch.ini')
    s.add_argument('--tree', required=True)
    s.add_argument('--dry-run', action='store_true')
    s.add_argument('--sign-key', help='Ed25519 private key file (raw 32 bytes, kept off the server): also publish a signed '
                                      'update-manifest.json with the SHA-256 of every file')
    s.add_argument('--public-url', help='base url for the final HTTP check (default from launcher.json, "none" to skip)')
    s.set_defaults(func=cmd_publish)

    s = sub.add_parser('rollback', help='restore patch.ini and Full/ from a publish backup')
    s.add_argument('--list', action='store_true')
    s.add_argument('--to', help='backup timestamp printed by --list')
    s.add_argument('--purge', action='store_true', help='also move the versions newer than the restored one away')
    s.set_defaults(func=cmd_rollback)

    s = sub.add_parser('status')
    s.set_defaults(func=cmd_status)

    args = p.parse_args(argv)
    try:
        args.func(args)
        return 0
    except (Fail, OSError, ValueError, subprocess.SubprocessError) as e:
        print(f'(x) {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
