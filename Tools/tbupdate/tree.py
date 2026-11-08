from __future__ import annotations

from pathlib import Path

from .package import PRODUCT, Problems, package_paths, product_root, verify_package
from .versions import GameVersion, read_patch_ini


def hosted_steps(tree: Path, baseline: GameVersion) -> list[tuple[GameVersion, GameVersion]]:
    patch = read_patch_ini(product_root(tree) / 'patch.ini')
    ordered = patch.sorted_versions()
    steps = []
    for previous, version in zip(ordered, ordered[1:]):
        if version.key() > baseline.key():
            steps.append((previous, version))
    return steps


def verify_tree(tree: Path, baseline: GameVersion, cgd_password: bytes | None = None,
                deep: bool = True) -> Problems:
    problems = Problems()
    patch_ini = product_root(tree) / 'patch.ini'
    if not patch_ini.is_file():
        problems.errors.append(f'missing {patch_ini}')
        return problems
    patch = read_patch_ini(patch_ini)
    if baseline.key() not in {v.key() for v in patch.versions}:
        problems.errors.append(f'baseline {baseline} is not listed in patch.ini: clients at {baseline} would '
                               f'request a package named after {baseline} that this tree cannot provide')
    for v in patch.versions:
        if v.prefix.upper() != patch.latest.prefix.upper():
            problems.errors.append(f'{v}: prefix differs from {patch.latest}')
    steps = hosted_steps(tree, baseline)
    if patch.latest.key() > baseline.key() and not steps:
        problems.errors.append('patch.ini is newer than the baseline but no package step was found')
    for source, target in steps:
        cab, xml = package_paths(tree, source, target)
        if not cab.is_file() or not xml.is_file():
            problems.errors.append(f'{target}: missing {cab.relative_to(tree)} or {xml.relative_to(tree)}')
            continue
        if deep:
            result = verify_package(cab, xml, target, cgd_password)
            problems.errors += [f'{target}: {e}' for e in result.errors]
            problems.warnings += [f'{target}: {w}' for w in result.warnings]
    stray = sorted(p.name for p in product_root(tree).iterdir()
                   if p.is_dir() and p.name not in {str(t) for _, t in steps} | {'Full', 'Patcher'})
    for name in stray:
        try:
            if GameVersion.parse(name).key() > baseline.key():
                problems.warnings.append(f'{name}/ exists but is not reachable from patch.ini')
        except ValueError:
            problems.warnings.append(f'unexpected folder {PRODUCT}/{name}')
    return problems
