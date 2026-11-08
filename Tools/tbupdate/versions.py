from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, order=False)
class GameVersion:
    prefix: str
    parts: tuple[int, int, int, int]

    @staticmethod
    def parse(text: str) -> GameVersion:
        text = text.strip()
        prefix, sep, numbers = text.partition('_')
        pieces = numbers.split('.')
        if not sep or len(pieces) != 4 or not all(p.isdigit() for p in pieces):
            raise ValueError(f"invalid version '{text}' (expected PREFIX_a.b.c.d)")
        return GameVersion(prefix, tuple(int(p) for p in pieces))

    def next(self) -> GameVersion:
        parts = list(self.parts)
        parts[-1] += 1
        for i in range(len(parts) - 1, 0, -1):
            if parts[i] > 9:
                parts[i] = 0
                parts[i - 1] += 1
        return GameVersion(self.prefix, tuple(parts))

    def key(self) -> tuple[int, int, int, int]:
        return self.parts

    def __str__(self) -> str:
        return f"{self.prefix}_{'.'.join(map(str, self.parts))}"


@dataclass
class PatchIni:
    versions: list[GameVersion]
    exe: str = 'bin/MicroVolts.exe'

    @property
    def latest(self) -> GameVersion:
        return self.versions[0]

    def sorted_versions(self) -> list[GameVersion]:
        unique = {v.key(): v for v in self.versions}
        return [unique[k] for k in sorted(unique)]

    def with_new_version(self, version: GameVersion) -> PatchIni:
        if version.key() <= self.latest.key():
            raise ValueError(f"new version {version} is not newer than {self.latest}")
        if version.prefix.upper() != self.latest.prefix.upper():
            raise ValueError(f"prefix of {version} differs from {self.latest}")
        history = sorted({v.key(): v for v in self.versions}.values(), key=GameVersion.key, reverse=True)
        return PatchIni([version, *history], self.exe)

    def render(self) -> str:
        lines = ['[patch]', f'version = {self.versions[0]}']
        lines += [f'version{i} = {v}' for i, v in enumerate(self.versions[1:], start=1)]
        lines.append(f'exe = {self.exe}')
        return '\r\n'.join(lines) + '\r\n'


def parse_ini(text: str) -> dict[str, dict[str, str]]:
    sections: dict[str, dict[str, str]] = {}
    current = None
    for raw in text.split('\n'):
        line = raw.strip('\r \t')
        if not line or line.startswith(('//', ';', '#')):
            continue
        if line.startswith('[') and ']' in line:
            current = line.lstrip('[').split(']')[0].strip().lower()
            sections.setdefault(current, {})
            continue
        key, sep, value = line.partition('=')
        if sep and key.strip() and current is not None:
            value = value.strip()
            cut = value.find(' //')
            if cut > 0:
                value = value[:cut].strip()
            sections[current][key.strip().lower()] = value
    return sections


def parse_patch_ini(text: str) -> PatchIni:
    section = parse_ini(text).get('patch', {})
    if 'version' not in section:
        raise ValueError('patch.ini has no [patch] version')
    versions = [GameVersion.parse(section['version'])]
    index = 1
    while f'version{index}' in section:
        versions.append(GameVersion.parse(section[f'version{index}']))
        index += 1
    return PatchIni(versions, section.get('exe', 'bin/MicroVolts.exe'))


def read_patch_ini(path: Path) -> PatchIni:
    return parse_patch_ini(path.read_text(encoding='utf-8-sig'))


def upgrade_path(installed: GameVersion, patch: PatchIni) -> list[tuple[GameVersion, GameVersion]]:
    steps = []
    current = installed
    for version in patch.sorted_versions():
        if installed.key() < version.key() <= patch.latest.key():
            steps.append((current, version))
            current = version
    if not steps and patch.latest.key() > installed.key():
        steps.append((installed, patch.latest))
    return steps


def package_name(product: str, source: GameVersion, target: GameVersion) -> str:
    return f'{product}-{source}-{target}'
