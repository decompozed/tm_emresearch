from __future__ import annotations

import zlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from xml.etree import ElementTree as ET

NEW_SUFFIX = '.new'
LAUNCHER_CHECKSUMS_ELEMENT = 'LauncherCheckSums'


@dataclass(frozen=True)
class PackageFile:
    rel_path: PurePosixPath
    source: Path
    checksum: str

    @property
    def cab_name(self) -> str:
        return self.rel_path.name + NEW_SUFFIX


def adler32_file(path: Path) -> str:
    value = 1
    with path.open('rb') as f:
        while chunk := f.read(1 << 20):
            value = zlib.adler32(chunk, value)
    return format(value & 0xffffffff, '08x')


def duplicate_basenames(files: list[PackageFile]) -> list[str]:
    counts = Counter(f.rel_path.name for f in files)
    return sorted(name for name, n in counts.items() if n > 1)


def build_manifest(files: list[PackageFile], version: str, product: str = 'microvolts') -> bytes:
    root = ET.Element('DeltaInfo', Name=product, Version=version)
    updated = ET.SubElement(root, 'UpdatedFiles')
    dirs: dict[PurePosixPath, ET.Element] = {PurePosixPath('.'): updated}
    for f in sorted(files, key=lambda x: x.rel_path.as_posix().lower()):
        parent = _dir_element(dirs, f.rel_path.parent)
        ET.SubElement(parent, 'File', Name=f.rel_path.name, CheckSum=f.checksum)
    if not duplicate_basenames(files):
        extra = ET.SubElement(root, LAUNCHER_CHECKSUMS_ELEMENT)
        for f in sorted(files, key=lambda x: x.cab_name.lower()):
            ET.SubElement(extra, 'File', Name=f.cab_name, CheckSum=f.checksum)
    ET.indent(root, space='    ')
    return b'<?xml version="1.0" encoding="utf-8" ?>\r\n' + ET.tostring(root, encoding='utf-8').replace(b'\n', b'\r\n') + b'\r\n'


def _dir_element(dirs: dict[PurePosixPath, ET.Element], path: PurePosixPath) -> ET.Element:
    if path in dirs:
        return dirs[path]
    parent = _dir_element(dirs, path.parent)
    element = ET.SubElement(parent, 'Dir', Name=path.name)
    dirs[path] = element
    return element


def parse_manifest(data: bytes) -> tuple[str, dict[str, str], dict[str, str]]:
    root = ET.fromstring(data)
    tree: dict[str, str] = {}
    updated = root.find('UpdatedFiles')
    if updated is None:
        raise ValueError("manifest has no UpdatedFiles element")
    _walk(updated, PurePosixPath('.'), tree)
    flat: dict[str, str] = {}
    extra = root.find(LAUNCHER_CHECKSUMS_ELEMENT)
    if extra is not None:
        flat = {e.get('Name'): e.get('CheckSum') for e in extra.findall('File')}
    return root.get('Version', ''), tree, flat


def _walk(element: ET.Element, prefix: PurePosixPath, out: dict[str, str]) -> None:
    for child in element:
        name = child.get('Name')
        if child.tag == 'Dir':
            _walk(child, prefix / name, out)
        elif child.tag == 'File':
            out[(prefix / name).as_posix()] = child.get('CheckSum')


def launcher_checksum_keys(data: bytes) -> dict[str, str] | None:
    names = [(e.get('Name'), e.get('CheckSum')) for e in ET.fromstring(data).iter('File')]
    names = [(n, c) for n, c in names if n is not None and c is not None]
    if len({n for n, _ in names}) != len(names):
        return None
    return dict(names)
