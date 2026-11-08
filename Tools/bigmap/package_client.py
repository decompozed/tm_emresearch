"""Client files for a merged map: copies of map.dat (+ mapNNN/), audio/sfx/ambient.dat (+ mapNNN/ sounds) and
cgd.dip (+ mapinfo, jumperinfo, iconstag rows); optionally the same mapinfo row in the server's mapinfo.cdb.
Archives that already hold mapNNN/ get only its changed and new files (then without --password-file)."""
import argparse
import os
import sys
import zipfile

import layout as layout_file
import maptables
import ziprecords

CGD_MAPINFO = ('mapinfo.cdb', 'ENG/mapinfo.cdb')


def folder_entries(folder, prefix):
    names = sorted(os.listdir(folder), key=str.lower)
    out = [(prefix + '/', b'')]
    for n in names:
        with open(os.path.join(folder, n), 'rb') as f:
            out.append((prefix + '/' + n, f.read()))
    return out


def folder_changes(dat, folder, prefix):
    entries = folder_entries(folder, prefix)
    with zipfile.ZipFile(dat) as z:
        held = {n.lower(): n for n in z.namelist() if n.lower().startswith(prefix.lower() + '/')}
        if not held:
            return {}, entries
        stale = set(held) - {n.lower() for n, _ in entries}
        assert not stale, '%s: files not in %s: %s' % (dat, folder, sorted(stale))
        replace, add = {}, []
        for n, data in entries:
            old = held.get(n.lower())
            if old is None:
                add.append((n, data))
            elif not n.endswith('/') and z.read(old) != data:
                replace[old] = data
    return replace, add


def map_row_args(a, layout):
    return dict(template_id=layout['maps'][0]['id'], map_id=layout['id'], name=a.name, desc=a.desc,
                max_users=a.max_users, death_height=a.death_height, size=a.size, modes=a.modes.split(','))


def jumper_maps(stage_map):
    """Maps whose jump pads are in the merged colp (jumperinfo is keyed by map and mesh name, 0x7AB720 'Jumper')."""
    from nifio import Nif
    names = set()
    p = os.path.join(stage_map, 'World_colp.nif')
    nif = Nif.load(p)
    for b in nif.blocks:
        if b.base == 'NiPhysXMeshDesc':
            n = nif.string(b.data['Mesh Name'])
            if 'Jumper' in n:
                names.add(n)
    return names


def patch_cgd(src, dst, password, layout, row, log, stage_map):
    z = zipfile.ZipFile(src)
    read = {n: z.read(n, pwd=password) for n in CGD_MAPINFO + ('jumperinfo.cdb', 'iconstag.cdb')}
    out = {}
    for n in CGD_MAPINFO:
        cdb = maptables.load(read[n], 'mapinfo')
        maptables.add_mapinfo(cdb, **row)
        out[n] = maptables.dump(cdb)
    cdb = maptables.load(read['jumperinfo.cdb'], 'jumperinfo')
    wanted = jumper_maps(stage_map)
    seen = set()
    for m in layout['maps']:
        added = maptables.add_jumpers(cdb, m['id'], layout['id'], m.get('actor_renames'))
        for jid, name in added:
            assert name not in seen, 'jump pad %s in two maps: rename it in the merged colp first' % name
            seen.add(name)
            log('jumperinfo %d: %s (from map %d)' % (jid, name, m['id']))
    assert wanted <= seen, 'jump pads without a jumperinfo row: %s' % (wanted - seen)
    out['jumperinfo.cdb'] = maptables.dump(cdb)
    cdb = maptables.load(read['iconstag.cdb'], 'iconstag')
    for idx, tag, icon in maptables.add_icon_tags(cdb, layout.get('icons_from', layout['maps'][0]['id']), layout['id']):
        log('iconstag %d: %s -> icon %d' % (idx, tag, icon))
    out['iconstag.cdb'] = maptables.dump(cdb)
    ziprecords.rewrite(src, dst, replace=out, password=password)


def patch_server_mapinfo(path, row, log):
    with open(path, 'rb') as f:
        cdb = maptables.load(f.read(), 'mapinfo')
    maptables.add_mapinfo(cdb, **row)
    with open(path, 'wb') as f:
        f.write(maptables.dump(cdb))
    log('server mapinfo: added map %d' % row['map_id'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('layout')
    ap.add_argument('stage', help='output of build_bigmap.py (mapNNN/ and ambient/mapNNN/)')
    ap.add_argument('game', help='client folder to copy the archives from (read only)')
    ap.add_argument('out', help='writes <out>/data/map.dat, cgd.dip, audio/sfx/ambient.dat')
    ap.add_argument('--password-file', help="cgd.dip's zip password (without it cgd.dip is not written)")
    ap.add_argument('--name', default='Big Map')
    ap.add_argument('--desc', default='Prototype: several maps side by side.')
    ap.add_argument('--max-users', type=int, default=16)
    ap.add_argument('--death-height', type=int, default=-2000)
    ap.add_argument('--size', type=int, default=27000)
    ap.add_argument('--modes', default='tdm,ffa')
    ap.add_argument('--server-mapinfo', help="also add the row to this server mapinfo.cdb (in place)")
    a = ap.parse_args()
    layout = layout_file.load(a.layout)
    mid = layout['id']
    row = map_row_args(a, layout)
    data = os.path.join(a.out, 'data')
    os.makedirs(os.path.join(data, 'audio', 'sfx'), exist_ok=True)

    def log(s):
        print(s)
    stage_map = os.path.join(a.stage, 'map%03d' % mid)
    sounds = os.path.join(a.stage, 'ambient', 'map%03d' % mid)
    for rel, folder in (('map.dat', stage_map), (os.path.join('audio', 'sfx', 'ambient.dat'), sounds)):
        if not os.path.isdir(folder):
            continue
        src = os.path.join(a.game, 'data', rel)
        replace, add = folder_changes(src, folder, 'map%03d' % mid)
        ziprecords.rewrite(src, os.path.join(data, rel), replace=replace, add=add)
        log('%s: map%03d/ %d files added, %d replaced (%s)' % (rel, mid, len(add), len(replace), ', '.join(sorted(replace)) or '-'))
    if a.password_file:
        with open(a.password_file, 'rb') as f:
            password = f.read().strip()
        patch_cgd(os.path.join(a.game, 'data', 'cgd.dip'), os.path.join(data, 'cgd.dip'), password, layout, row, log,
                  stage_map)
        log('cgd.dip: mapinfo row %d (%s), death height %d, size %d, modes %s'
            % (mid, a.name, a.death_height, a.size, a.modes))
    else:
        log('cgd.dip: not written (no --password-file)')
    if a.server_mapinfo:
        patch_server_mapinfo(a.server_mapinfo, row, log)
    return 0


if __name__ == '__main__':
    sys.exit(main())
