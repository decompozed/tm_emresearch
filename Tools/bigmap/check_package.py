"""Offline checks of packaged client archives against the originals: untouched records identical, added entries
listed, merged NIFs readable and consistent, new cdb rows printed."""
import argparse
import os
import sys
import zipfile

import maptables
import nifcheck
import ziprecords
from nifio import Nif


def compare_records(orig, new, label):
    a, _ = ziprecords.records(orig)
    b, _ = ziprecords.records(new)
    old = {n: (l, c) for n, l, c in a}
    same = changed = 0
    added = []
    for n, l, c in b:
        if n not in old:
            added.append(n)
        elif old[n][0] == l and old[n][1][:42] == c[:42] and old[n][1][46:] == c[46:]:
            same += 1
        else:
            changed += 1
    missing = len(old) - same - changed
    print('%s: %d records identical, %d changed, %d added, %d missing' % (label, same, changed, len(added), missing))
    return added, missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game')
    ap.add_argument('out')
    ap.add_argument('map_id', type=int)
    ap.add_argument('--password-file', help='without it the cdb rows of cgd.dip are not checked')
    a = ap.parse_args()
    bad = 0
    prefix = 'map%03d/' % a.map_id
    for rel in ('map.dat', 'audio/sfx/ambient.dat', 'cgd.dip'):
        src, dst = os.path.join(a.game, 'data', rel), os.path.join(a.out, 'data', rel)
        if os.path.exists(dst):
            added, missing = compare_records(src, dst, rel)
            bad += missing + sum(1 for n in added if not n.startswith(prefix))
    with zipfile.ZipFile(os.path.join(a.out, 'data', 'map.dat')) as z:
        for n in z.namelist():
            if n.startswith(prefix) and n.lower().endswith('.nif'):
                nif = Nif.read(z.read(n))
                errs, unreached, _ = nifcheck.problems(nif)
                assert nif.to_bytes() == z.read(n)
                print('%s: %d blocks, %d strings, %d link errors, %d unreachable' % (n, len(nif.blocks), len(nif.strings),
                                                                                     len(errs), unreached))
                bad += len(errs) + unreached
    if not a.password_file or not os.path.exists(os.path.join(a.out, 'data', 'cgd.dip')):
        print('cgd.dip rows not checked')
        print('problems: %d' % bad)
        return 1 if bad else 0
    with open(a.password_file, 'rb') as f:
        pw = f.read().strip()
    with zipfile.ZipFile(os.path.join(a.out, 'data', 'cgd.dip')) as z:
        for name, key in (('mapinfo.cdb', 'mi_id'), ('ENG/mapinfo.cdb', 'mi_id'), ('jumperinfo.cdb', 'ji_map_id'),
                          ('iconstag.cdb', 'it_icontag')):
            cdb = maptables.load(z.read(name, pwd=pw), 'check')
            rows = [e for e in cdb.entries if (str(e[[f.key for f in e].index(key)].value).endswith('_%d' % a.map_id)
                                               if key == 'it_icontag' else e[[f.key for f in e].index(key)].value == a.map_id)]
            print('%s: %d rows for map %d' % (name, len(rows), a.map_id))
            for r in rows:
                print('   ', {f.key: f.value for f in r if not f.key.startswith('mi_mod_') or f.value})
            bad += not rows
    print('problems: %d' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
