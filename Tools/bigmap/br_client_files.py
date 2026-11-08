"""Battle Royale (party game 11) client files from a base cgd.dip that already has the Big Map (map 58, e.g. the output
of package_client.py): only the mapinfo rows change (mapinfo.cdb and ENG/mapinfo.cdb), every other record of the
archive is copied byte for byte, so the other cgd.dip changes (minimap icons, kit drops) merge by running this on their
output. Changes: map 58 playable in SquareMode (mi_mod_nod = 1: the party room's mode 12; the client checks the map
against the room's mode when it loads it, 0xAE7BE0, and uses the map's FFA spawns, CLevel::ExtractSpawnInfo 0x52C2B0).
--server-mapinfo applies the same change to the server's mapinfo.cdb in place."""
import argparse
import os
import sys
import zipfile

import maptables
import ziprecords

CGD_MAPINFO = ('mapinfo.cdb', 'ENG/mapinfo.cdb')
BIG_MAP = 58


def set_square_mode(data, name, log):
    cdb = maptables.load(data, 'mapinfo')
    row = maptables._row(cdb, 'mi_id', BIG_MAP)
    if maptables._get(row, 'mi_mod_nod'):
        log('%s: map %d already playable in SquareMode' % (name, BIG_MAP))
        return data
    maptables._set(row, 'mi_mod_nod', 1)
    out = maptables.dump(cdb)
    changed = [i for i in range(min(len(out), len(data))) if out[i] != data[i]]
    assert len(out) == len(data) and len(changed) == 1, '%s: unexpected changes %s' % (name, changed[:8])
    log('%s: map %d mi_mod_nod 0 -> 1 (byte 0x%X)' % (name, BIG_MAP, changed[0]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base', help='cgd.dip with map %d (read only)' % BIG_MAP)
    ap.add_argument('out', help='writes <out>/data/cgd.dip')
    ap.add_argument('--password-file', required=True, help="cgd.dip's zip password")
    ap.add_argument('--server-mapinfo', help="the server's mapinfo.cdb, changed in place")
    a = ap.parse_args()
    with open(a.password_file, 'rb') as f:
        password = f.read().strip()
    z = zipfile.ZipFile(a.base)
    replace = {n: set_square_mode(z.read(n, pwd=password), n, print) for n in CGD_MAPINFO}
    z.close()
    data = os.path.join(a.out, 'data')
    os.makedirs(data, exist_ok=True)
    ziprecords.rewrite(a.base, os.path.join(data, 'cgd.dip'), replace=replace, password=password)
    print('cgd.dip written to', data)
    if a.server_mapinfo:
        with open(a.server_mapinfo, 'rb') as f:
            server = f.read()
        changed = set_square_mode(server, 'server mapinfo', print)
        if changed != server:
            with open(a.server_mapinfo, 'wb') as f:
                f.write(changed)
    return 0


if __name__ == '__main__':
    sys.exit(main())
