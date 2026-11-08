#!/usr/bin/env python3
"""Merges the SPDX file vcpkg writes per port into one SBOM for the whole project.

vcpkg leaves share/<port>/vcpkg.spdx.json in the installed tree, but no single document, no dependency graph between
ports and nothing about libraries that are vendored in the repository. This adds those, and reports how complete the
result is against the NTIA minimum elements.

  merge_vcpkg_spdx.py --installed build/vcpkg_installed --triplet x64-linux --manifest vcpkg.json --out sbom.spdx.json
"""
import argparse
import datetime
import json
import re
import sys
import uuid
from pathlib import Path

PROJECT = 'tm_emulator'
TOOL = 'Tool: merge_vcpkg_spdx.py'
# In the repository under ExternalLibraries, so no package manager knows about them
VENDORED = {
    'libbcrypt': 'https://github.com/trusch/libbcrypt',
    'libcppotp': 'NOASSERTION',
    'IniParser': 'NOASSERTION',
    'visit_struct': 'NOASSERTION',
}


def spdx_id(name):
    return 'SPDXRef-Package-' + re.sub(r'[^A-Za-z0-9.-]', '-', name)


def read_status(database, triplet):
    """Port name to direct dependencies, from vcpkg's own database: the status file plus the updates written after it.

    On a fresh install the status file does not exist yet and everything is in updates/.
    """
    sources = [database / 'status'] if (database / 'status').exists() else []
    sources += sorted((database / 'updates').glob('*')) if (database / 'updates').is_dir() else []
    latest = {}
    for source in sources:
        for block in source.read_text(encoding='utf-8').split('\n\n'):
            fields = dict(line.split(': ', 1) for line in block.splitlines() if ': ' in line)
            if 'Package' in fields:
                latest[(fields['Package'], fields.get('Feature', ''), fields.get('Architecture'))] = fields

    ports = {}
    for (package, _, architecture), fields in latest.items():
        if architecture != triplet or 'install ok installed' not in fields.get('Status', ''):
            continue
        depends = {d.strip().split(':')[0].split('[')[0] for d in fields.get('Depends', '').split(',') if d.strip()}
        ports.setdefault(package, set()).update(depends)
    if not ports:
        raise SystemExit(f'no installed ports found for {triplet} in {database}')
    return ports


def direct_dependencies(manifest):
    data = json.loads(manifest.read_text(encoding='utf-8'))
    names = set()
    for dependency in data.get('dependencies', []):
        names.add(dependency if isinstance(dependency, str) else dependency['name'])
    return names


def build(installed, triplet, manifest):
    status = read_status(installed / 'vcpkg', triplet)
    share = installed / triplet / 'share'
    packages, relationships, build_only = [], [], []
    project_id = spdx_id(PROJECT)
    packages.append({'name': PROJECT, 'SPDXID': project_id, 'versionInfo': 'NOASSERTION', 'downloadLocation': 'NOASSERTION',
                     'licenseConcluded': 'NOASSERTION', 'licenseDeclared': 'NOASSERTION', 'copyrightText': 'NOASSERTION', 'supplier': 'Organization: MacroVolts'})
    relationships.append({'spdxElementId': 'SPDXRef-DOCUMENT', 'relationshipType': 'DESCRIBES', 'relatedSpdxElement': project_id})

    known = set()
    for name in sorted(status):
        if name.startswith('vcpkg-'):   # helpers that only run while building, nothing of them ends up in a binary
            build_only.append(name)
            continue
        document = json.loads((share / name / 'vcpkg.spdx.json').read_text(encoding='utf-8'))
        port = next(p for p in document['packages'] if p['SPDXID'] == 'SPDXRef-port')
        package = {'name': name, 'SPDXID': spdx_id(name), 'versionInfo': port.get('versionInfo', 'NOASSERTION'),
                   'downloadLocation': port.get('downloadLocation', 'NOASSERTION'), 'licenseConcluded': port.get('licenseConcluded', 'NOASSERTION'),
                   'licenseDeclared': port.get('licenseDeclared', 'NOASSERTION'), 'copyrightText': 'NOASSERTION', 'externalRefs': port.get('externalRefs', [])}
        if port.get('homepage'):
            package['homepage'] = port['homepage']
        packages.append(package)
        known.add(name)

    direct = direct_dependencies(manifest)
    for name in sorted(known):
        for dependency in sorted(status[name] & known - {name}):
            relationships.append({'spdxElementId': spdx_id(name), 'relationshipType': 'DEPENDS_ON', 'relatedSpdxElement': spdx_id(dependency)})
    for name in sorted(direct & known):
        relationships.append({'spdxElementId': project_id, 'relationshipType': 'DEPENDS_ON', 'relatedSpdxElement': spdx_id(name)})
    for name, url in sorted(VENDORED.items()):
        packages.append({'name': name, 'SPDXID': spdx_id(name), 'versionInfo': 'NOASSERTION', 'downloadLocation': url, 'licenseConcluded': 'NOASSERTION',
                         'licenseDeclared': 'NOASSERTION', 'copyrightText': 'NOASSERTION', 'comment': 'Vendored in ExternalLibraries, version not recorded upstream.'})
        relationships.append({'spdxElementId': project_id, 'relationshipType': 'CONTAINS', 'relatedSpdxElement': spdx_id(name)})

    now = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    document = {'spdxVersion': 'SPDX-2.3', 'dataLicense': 'CC0-1.0', 'SPDXID': 'SPDXRef-DOCUMENT', 'name': f'{PROJECT}-{triplet}',
                'documentNamespace': f'https://spdx.org/spdxdocs/{PROJECT}-{triplet}-{uuid.uuid4()}',
                'creationInfo': {'created': now, 'creators': [TOOL]}, 'packages': packages, 'relationships': relationships}
    return document, build_only, sorted(n for n in direct - known if not n.startswith('vcpkg-'))


def completeness(document, build_only, missing_direct):
    """How the document does against the NTIA minimum elements, in plain numbers."""
    packages = document['packages'][1:]
    has = lambda value: bool(value) and value != 'NOASSERTION'
    lines = [f'components                  {len(packages)}',
             f'component name              {sum(has(p.get("name")) for p in packages)}',
             f'version                     {sum(has(p.get("versionInfo")) for p in packages)}',
             f'supplier                    {sum(has(p.get("supplier")) for p in packages)}',
             f'unique identifier (purl)    {sum(any(r.get("referenceType") == "purl" for r in p.get("externalRefs", [])) for p in packages)}',
             f'declared or concluded license {sum(has(p.get("licenseConcluded")) or has(p.get("licenseDeclared")) for p in packages)}',
             f'dependency relationships    {sum(r["relationshipType"] in ("DEPENDS_ON", "CONTAINS") for r in document["relationships"])}',
             f'author of the SBOM data     {document["creationInfo"]["creators"][0]}',
             f'timestamp                   {document["creationInfo"]["created"]}',
             f'build-only ports left out   {len(build_only)}']
    if missing_direct:
        lines.append('direct dependency not found: ' + ', '.join(missing_direct))
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--installed', required=True, type=Path)
    parser.add_argument('--triplet', required=True)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    document, build_only, missing_direct = build(args.installed, args.triplet, args.manifest)
    args.out.write_text(json.dumps(document, indent=1) + '\n', encoding='utf-8')
    print(completeness(document, build_only, missing_direct))
    return 1 if missing_direct else 0


if __name__ == '__main__':
    sys.exit(main())
