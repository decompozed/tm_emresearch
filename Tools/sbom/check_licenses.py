#!/usr/bin/env python3
"""Fails when a package in the SPDX SBOM has a license that is not on the allow list, or no license at all.

  check_licenses.py --sbom sbom.spdx.json --allow Tools/sbom/allowed-licenses.txt --overrides Tools/sbom/license-overrides.json \
      --report license-check.json --table license-check.md

Per package the concluded license is used, then the declared one. NOASSERTION and NONE count as missing. A missing license
can only be resolved by an entry in the overrides file, which names the source the information was read from. A license that
is real but not allowed can only be accepted by an entry under "accepted" with a reason. Stale entries fail as well, so the
file cannot collect entries that no longer apply.

SPDX expressions: A AND B needs both allowed, A OR B needs one, "A WITH exception" needs the license and the exception
(exceptions are on the allow list as well), a trailing + is ignored, parentheses group.
"""
import argparse
import json
import re
import sys
from pathlib import Path

MISSING = {'', 'NOASSERTION', 'NONE'}
TOKEN = re.compile(r'\(|\)|[A-Za-z0-9.+:_-]+')


class ExpressionError(ValueError):
    pass


def parse(expression):
    """SPDX expression to a nested tuple: ('id', name) | ('and'|'or', [..]) | ('with', license, exception)."""
    tokens = TOKEN.findall(expression)
    if not tokens or ''.join(tokens) != re.sub(r'\s+', '', expression):
        raise ExpressionError(f'cannot read license expression: {expression!r}')
    position = 0

    def peek():
        return tokens[position] if position < len(tokens) else None

    def take():
        nonlocal position
        position += 1
        return tokens[position - 1]

    def operand():
        if peek() is None:
            raise ExpressionError(f'unexpected end of: {expression!r}')
        token = take()
        if token == '(':
            node = disjunction()
            if peek() != ')':
                raise ExpressionError(f'missing ) in: {expression!r}')
            take()
            return node
        if token == ')' or token.upper() in ('AND', 'OR', 'WITH'):
            raise ExpressionError(f'unexpected {token!r} in: {expression!r}')
        node = ('id', token.rstrip('+'))
        if peek() is not None and peek().upper() == 'WITH':
            take()
            if peek() is None:
                raise ExpressionError(f'missing exception in: {expression!r}')
            node = ('with', node[1], take())
        return node

    def conjunction():
        nodes = [operand()]
        while peek() is not None and peek().upper() == 'AND':
            take()
            nodes.append(operand())
        return nodes[0] if len(nodes) == 1 else ('and', nodes)

    def disjunction():
        nodes = [conjunction()]
        while peek() is not None and peek().upper() == 'OR':
            take()
            nodes.append(conjunction())
        return nodes[0] if len(nodes) == 1 else ('or', nodes)

    node = disjunction()
    if peek() is not None:
        raise ExpressionError(f'unexpected {peek()!r} in: {expression!r}')
    return node


def not_allowed(node, allowed):
    """The identifiers that stop the expression from being allowed. An empty list means it is allowed."""
    kind = node[0]
    if kind == 'id':
        return [] if node[1] in allowed else [node[1]]
    if kind == 'with':
        return [name for name in (node[1], node[2]) if name not in allowed]
    parts = [not_allowed(child, allowed) for child in node[1]]
    if kind == 'and':
        return [name for part in parts for name in part]
    return [] if any(not part for part in parts) else [name for part in parts for name in part]


def read_allow_list(path):
    allowed = set()
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        line = line.split('#', 1)[0].strip()
        if line:
            allowed.add(line)
    return allowed


def license_of(package):
    for field in ('licenseConcluded', 'licenseDeclared'):
        value = (package.get(field) or '').strip()
        if value not in MISSING:
            return value, field
    return None, None


def check(document, allowed, overrides, accepted):
    """Returns (rows, problems). A row is a dict with name, version, license, source and status."""
    relationships = document.get('relationships', [])
    described = {r['relatedSpdxElement'] for r in relationships if r.get('relationshipType') == 'DESCRIBES'}
    rows, problems = [], []
    used_overrides, used_accepted, stale = set(), set(), set()
    for package in document.get('packages', []):
        if package.get('SPDXID') in described:
            continue   # the project itself, its license is in LICENSE.md
        name, version = package.get('name', '?'), package.get('versionInfo', 'NOASSERTION')
        expression, where = license_of(package)
        row = {'name': name, 'version': version, 'license': expression, 'source': where, 'status': 'ok'}
        if expression is None:
            override = overrides.get(name)
            if override is None:
                row.update(status='FAIL', license='(none)', source='-')
                problems.append(f'{name}: no license in the SBOM and no entry in the overrides file')
                rows.append(row)
                continue
            used_overrides.add(name)
            expression, row['source'] = override['license'], 'override: ' + override['source']
            row['license'] = expression
        elif name in overrides:
            stale.add(name)
            problems.append(f'{name}: has a license in the SBOM now ({expression}), remove its override')
        try:
            blockers = not_allowed(parse(expression), allowed)
        except ExpressionError as error:
            row['status'] = 'FAIL'
            problems.append(f'{name}: {error}')
            rows.append(row)
            continue
        if blockers:
            entry = accepted.get(name)
            if entry and entry['license'] == expression:
                used_accepted.add(name)
                row['status'] = 'accepted: ' + entry['reason']
            else:
                row['status'] = 'FAIL'
                problems.append(f'{name} {version}: {expression} is not allowed ({", ".join(sorted(set(blockers)))})')
        elif name in accepted:
            stale.add(name)
            problems.append(f'{name}: its license is allowed now, remove the accepted entry')
        rows.append(row)
    for name in sorted(set(overrides) - used_overrides - stale):
        problems.append(f'override for {name} matches no package of the SBOM')
    for name in sorted(set(accepted) - used_accepted - stale):
        problems.append(f'accepted entry for {name} matches no package or license of the SBOM')
    return rows, problems


def table(rows, problems):
    counts = {}
    for row in rows:
        counts[row['license']] = counts.get(row['license'], 0) + 1
    lines = ['### License check', '',
             f'{len(rows)} packages checked, {len(counts)} distinct license expressions, {len(problems)} problem(s).', '',
             '| package | version | license | read from | result |', '|---|---|---|---|---|']
    for row in sorted(rows, key=lambda r: r['name']):
        lines.append(f'| {row["name"]} | {row["version"]} | {row["license"]} | {row["source"]} | {row["status"]} |')
    if problems:
        lines += ['', '**Problems**', ''] + [f'- {p}' for p in problems]
    lines += ['', '| license expression | packages |', '|---|---|'] + [f'| {k} | {v} |' for k, v in sorted(counts.items())]
    return '\n'.join(lines) + '\n'


def load_overrides(path):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    overrides = {}
    for entry in data.get('overrides', []):
        if not entry.get('license') or not entry.get('source'):
            raise SystemExit(f'override for {entry.get("name")} needs a license and the source it was read from')
        overrides[entry['name']] = entry
    accepted = {}
    for entry in data.get('accepted', []):
        if not entry.get('license') or not entry.get('reason'):
            raise SystemExit(f'accepted entry for {entry.get("name")} needs a license and a reason')
        accepted[entry['name']] = entry
    return overrides, accepted


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--sbom', required=True)
    parser.add_argument('--allow', required=True)
    parser.add_argument('--overrides', required=True)
    parser.add_argument('--report')
    parser.add_argument('--table')
    args = parser.parse_args()
    document = json.loads(Path(args.sbom).read_text(encoding='utf-8'))
    overrides, accepted = load_overrides(args.overrides)
    rows, problems = check(document, read_allow_list(args.allow), overrides, accepted)
    text = table(rows, problems)
    print(text)
    if args.table:
        Path(args.table).write_text(text, encoding='utf-8')
    if args.report:
        Path(args.report).write_text(json.dumps({'packages': rows, 'problems': problems}, indent=1) + '\n', encoding='utf-8')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
