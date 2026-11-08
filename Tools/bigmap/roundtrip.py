"""Reads and rewrites every NIF of the map folders of a map.dat; reports byte-identical files and block types."""
import argparse
import collections
import re
import sys
import zipfile

from nifio import Nif

MAP_NIF = re.compile(r'map\d{3}/[^/]+\.nif$', re.I)


def check(data):
    try:
        nif = Nif.read(data)
    except Exception as e:
        return 'parse: %s' % e, None
    out = nif.to_bytes()
    if out == data:
        return None, nif
    first = next((i for i, (a, b) in enumerate(zip(out, data)) if a != b), min(len(out), len(data)))
    return 'differs at byte %d (size %d -> %d)' % (first, len(data), len(out)), nif


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('map_dat')
    ap.add_argument('--all', action='store_true', help='also the NIFs outside mapNNN/')
    args = ap.parse_args()
    z = zipfile.ZipFile(args.map_dat)
    names = [n for n in z.namelist() if n.lower().endswith('.nif') and (args.all or MAP_NIF.match(n))]
    types = collections.Counter()
    files_per_type = collections.Counter()
    versions = collections.Counter()
    failures = []
    for name in names:
        err, nif = check(z.read(name))
        if nif:
            versions['%08x' % nif.version] += 1
            seen = collections.Counter(b.base for b in nif.blocks)
            types.update(seen)
            files_per_type.update(seen.keys())
        if err:
            failures.append((name, err))
    print('files %d, identical %d, failed %d' % (len(names), len(names) - len(failures), len(failures)))
    print('versions', dict(versions))
    print('block types %d:' % len(types))
    for t, c in types.most_common():
        print('  %-32s blocks %6d  files %5d' % (t, c, files_per_type[t]))
    for name, err in failures:
        print('FAIL', name, err)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
