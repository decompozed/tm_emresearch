import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from tbupdate import brand, cgd, launcher  # noqa: E402
from tbupdate.package import BuildError, BuildOptions, build_package, drop_latest, init_tree  # noqa: E402
from tbupdate.tree import verify_tree  # noqa: E402
from tbupdate.versions import GameVersion  # noqa: E402

PASSWORD_ENV = 'TB_CGD_PASSWORD'


def load_password(path: str | None) -> bytes | None:
    if path:
        return Path(path).read_bytes().rstrip(b'\r\n')
    value = os.environ.get(PASSWORD_ENV)
    return value.encode('latin-1') if value else None


def cmd_init(args) -> int:
    init_tree(Path(args.tree), Path(args.client_root))
    return 0


def cmd_build(args) -> int:
    options = BuildOptions(
        files_dir=Path(args.files), tree=Path(args.tree),
        version=GameVersion.parse(args.version) if args.version else None,
        client_root=Path(args.client_root) if args.client_root else None,
        cgd_password=load_password(args.password_file), skip_cgd=args.skip_cgd,
        require_existing=args.require_existing, compression=args.compression,
        legacy_patch_ini=args.legacy_launcher)
    build_package(options)
    if not args.baseline:
        return 0
    return cmd_verify(argparse.Namespace(tree=args.tree, baseline=args.baseline, password_file=args.password_file,
                                         shallow=False))


def cmd_drop(args) -> int:
    drop_latest(Path(args.tree), GameVersion.parse(args.version))
    return 0


def cmd_verify(args) -> int:
    result = verify_tree(Path(args.tree), GameVersion.parse(args.baseline), load_password(args.password_file),
                         deep=not args.shallow)
    for w in result.warnings:
        print(f'(!) {w}')
    for e in result.errors:
        print(f'(x) {e}')
    print('(OK) tree verified' if not result.errors else f'(x) {len(result.errors)} errors')
    return 1 if result.errors else 0


def cmd_cgd(args) -> int:
    password = load_password(args.password_file)
    if password is None:
        raise BuildError(f'pass --password-file or set {PASSWORD_ENV}')
    if args.set:
        versions = cgd.write_pi_version(Path(args.dip), Path(args.out), password, str(GameVersion.parse(args.set)))
    else:
        versions = cgd.read_pi_versions(Path(args.dip), password)
    for name, value in versions.items():
        print(f'{name}: pi_version = {value}')
    return 0


def cmd_patch_launcher(args) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    info, changes = launcher.patch_launcher(Path(args.launcher), out / 'Launcher.exe', args.host,
                                            Path(args.brand) if args.brand else None)
    (out / 'updateinfo.ini').write_bytes(launcher.updateinfo_ini(info).encode('ascii'))
    launcher.write_patch_info(info, out / 'launcher.json')
    print(f'(OK) {out / "Launcher.exe"}')
    print(f'     update base url : {info.update_base_url}')
    print(f'     server paths    : {info.update_base_path}/microvolts/...  {info.api_base_path}/...')
    print(f'     state folder    : %LOCALAPPDATA%\\{info.state_dir}')
    for old, new in changes:
        print(f'     {old!a} -> {new!a}')
    return 0


def cmd_brand_assets(args) -> int:
    for path in brand.write_assets(Path(args.client_root), Path(args.out), args.font):
        print(f'(OK) {path}')
    return 0


def cmd_gui(_args) -> int:
    from tbupdate.gui import run
    return run()


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description='ToyBattles update package generator')
    sub = p.add_subparsers(dest='command')

    s = sub.add_parser('init', help='create the server tree from the baseline client')
    s.add_argument('--tree', required=True)
    s.add_argument('--client-root', required=True)
    s.set_defaults(func=cmd_init)

    s = sub.add_parser('build', help='add the next version to the server tree')
    s.add_argument('--tree', required=True)
    s.add_argument('--files', required=True, help='new files, laid out like the client root (Bin/..., data/...)')
    s.add_argument('--version', help='explicit new version (default: last digit + 1)')
    s.add_argument('--client-root', help='installed client, used to flag paths that do not exist')
    s.add_argument('--password-file', help=f'cgd.dip password (or env {PASSWORD_ENV})')
    s.add_argument('--skip-cgd', action='store_true', help='do not touch cgd.dip pi_version')
    s.add_argument('--require-existing', action='store_true', help='refuse paths missing from --client-root')
    s.add_argument('--compression', choices=['MSZIP', 'LZX'], default='MSZIP')
    s.add_argument('--legacy-launcher', action='store_true',
                   help='also put patch.ini.new in the cab (2012 MicroVolts launcher only)')
    s.add_argument('--baseline', help='also verify the whole tree against this baseline version')
    s.set_defaults(func=cmd_build)

    s = sub.add_parser('drop', help='remove the latest version from the local tree (after a server rollback)')
    s.add_argument('--tree', required=True)
    s.add_argument('--version', required=True)
    s.set_defaults(func=cmd_drop)

    s = sub.add_parser('verify', help='check the version chain, cab, xml and checksums')
    s.add_argument('--tree', required=True)
    s.add_argument('--baseline', required=True, help='oldest client version that must be able to update')
    s.add_argument('--password-file')
    s.add_argument('--shallow', action='store_true', help='only check that the files exist')
    s.set_defaults(func=cmd_verify)

    s = sub.add_parser('cgd', help='read or set pi_version in a cgd.dip')
    s.add_argument('--dip', required=True)
    s.add_argument('--set', help='new pi_version')
    s.add_argument('--out', help='output cgd.dip (required with --set)')
    s.add_argument('--password-file')
    s.set_defaults(func=cmd_cgd)

    s = sub.add_parser('patch-launcher', help='point a copy of the ToyBattles .NET launcher (1.0.20) to our server')
    s.add_argument('--launcher', required=True)
    s.add_argument('--out', required=True)
    s.add_argument('--host', required=True, help='host[:port] of the update server, max 17 chars')
    s.add_argument('--brand', help='folder with banner.png, logo.png, logo.ico (from brand-assets): '
                                   'NostalgiaVolts name, logo, mutex and state folder (needs the .NET 8 SDK)')
    s.set_defaults(func=cmd_patch_launcher)

    s = sub.add_parser('brand-assets', help='cut the NostalgiaVolts logo from the client into launcher images')
    s.add_argument('--client-root', required=True, help='NostalgiaVolts client (reads data/ui.dat)')
    s.add_argument('--out', required=True)
    s.add_argument('--font', help='bold TrueType font for the NV monogram (default: Arial Black, Segoe UI Black, Impact)')
    s.set_defaults(func=cmd_brand_assets)

    s = sub.add_parser('gui', help='graphical front-end for build')
    s.set_defaults(func=cmd_gui)
    return p


def main(argv=None) -> int:
    p = parser()
    args = p.parse_args(argv)
    if args.command is None:
        return cmd_gui(args)
    if args.command == 'cgd' and args.set and not args.out:
        p.error('--set needs --out')
    try:
        return args.func(args)
    except (RuntimeError, ValueError, OSError) as e:
        print(f'(x) {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
