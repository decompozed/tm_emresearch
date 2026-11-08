from __future__ import annotations

import os
import re
import shutil
import stat
import subprocess
import time
from pathlib import Path, PurePosixPath

FIXED_MTIME = time.mktime((2000, 1, 1, 0, 0, 0, 0, 0, -1))
CAB_SIZE_LIMIT = 0x7FFFFFFF
PROGRESS = re.compile(r'^\s*\d+\.\d+% ')


class CabError(RuntimeError):
    pass


def stage_file(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    os.chmod(target, stat.S_IREAD | stat.S_IWRITE)
    os.utime(target, (FIXED_MTIME, FIXED_MTIME))


def _ddf(root: Path, names: list[PurePosixPath], cab_path: Path, compression: str) -> str:
    lines = [
        '.OPTION EXPLICIT',
        f'.Set CabinetNameTemplate={cab_path.name}',
        f'.Set DiskDirectoryTemplate="{cab_path.parent}"',
        '.Set Cabinet=ON',
        '.Set Compress=ON',
        f'.Set CompressionType={compression}',
        '.Set MaxDiskSize=0',
        '.Set CabinetFileCountThreshold=0',
        '.Set FolderFileCountThreshold=0',
        '.Set FolderSizeThreshold=0',
        '.Set MaxCabinetSize=0',
        '.Set UniqueFiles=OFF',
        '.Set InfFileName=NUL',
        '.Set RptFileName=NUL',
    ]
    if compression == 'LZX':
        lines.append('.Set CompressionMemory=21')
    current_dir = None
    for name in sorted(names, key=lambda n: (n.parent.as_posix().lower(), n.name.lower())):
        directory = '' if name.parent == PurePosixPath('.') else str(name.parent).replace('/', '\\')
        if directory != current_dir:
            lines.append(f'.Set DestinationDir="{directory}"')
            current_dir = directory
        lines.append(f'"{root / name}" "{name.name}"')
    return '\r\n'.join(lines) + '\r\n'


def create_cab(root: Path, names: list[PurePosixPath], cab_path: Path, compression: str = 'MSZIP', log=print) -> None:
    root, cab_path = root.resolve(), cab_path.resolve()
    cab_path.parent.mkdir(parents=True, exist_ok=True)
    if cab_path.exists():
        cab_path.unlink()
    if shutil.which('makecab'):
        _create_with_makecab(root, names, cab_path, compression, log)
    elif shutil.which('gcab'):
        if compression != 'MSZIP':
            raise CabError('gcab only supports MSZIP')
        _create_with_gcab(root, names, cab_path, log)
    else:
        raise CabError('neither makecab (Windows) nor gcab (Linux) found in PATH')
    if not cab_path.is_file():
        raise CabError(f'cabinet not created: {cab_path}')
    if cab_path.stat().st_size >= CAB_SIZE_LIMIT:
        raise CabError(f'{cab_path.name} exceeds the 2 GiB cabinet limit, split the update over more versions')


def _create_with_makecab(root: Path, names, cab_path: Path, compression: str, log) -> None:
    ddf = cab_path.parent / (cab_path.name + '.ddf')
    ddf.write_bytes(_ddf(root, names, cab_path, compression).encode('mbcs' if os.name == 'nt' else 'utf-8'))
    try:
        result = subprocess.run(['makecab', '/F', str(ddf)], cwd=cab_path.parent, capture_output=True, text=True, errors='replace')
        for line in result.stdout.splitlines():
            if line.strip() and not PROGRESS.match(line):
                log(line.strip())
        if result.returncode != 0:
            raise CabError(f'makecab failed ({result.returncode}): {result.stderr.strip() or result.stdout.strip()}')
    finally:
        ddf.unlink(missing_ok=True)


def _create_with_gcab(root: Path, names, cab_path: Path, log) -> None:
    result = subprocess.run(['gcab', '-c', '-z', str(cab_path.resolve()), *sorted(n.as_posix() for n in names)],
                            cwd=root, capture_output=True, text=True, errors='replace')
    if result.returncode != 0:
        raise CabError(f'gcab failed: {result.stderr.strip()}')
    log(f'gcab: {len(names)} files')


def extract_cab(cab_path: Path, out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    if os.name == 'nt':
        cmd = ['expand.exe', str(cab_path), '-F:*', str(out_dir)]
    elif shutil.which('cabextract'):
        cmd = ['cabextract', '-q', '-d', str(out_dir), str(cab_path)]
    elif shutil.which('gcab'):
        cmd = ['gcab', '-x', '-C', str(out_dir), str(cab_path)]
    else:
        raise CabError('no cabinet extractor found (expand.exe, cabextract or gcab)')
    result = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    if result.returncode != 0:
        raise CabError(f'{cmd[0]} failed ({result.returncode}): {result.stdout.strip()} {result.stderr.strip()}')


def list_files(root: Path) -> dict[str, Path]:
    return {p.relative_to(root).as_posix(): p for p in sorted(root.rglob('*')) if p.is_file()}
