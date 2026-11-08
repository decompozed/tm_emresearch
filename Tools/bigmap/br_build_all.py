"""Battle Royale client files from a clean client, every step in order (the final update of a client):
  1. build_bigmap.py      map058 of layouts/map058_f4.json (four arenas, corridors, covers) + its ambient sounds
  2. verify_corridor.py   openings, corridors and covers checked; verify_spawns.py: TDM spawns and team balance
  3. package_client.py    map.dat, audio/sfx/ambient.dat, cgd.dip (map 58 rows, Studio jump pads, mi_size 29500)
  4. minimap58.py         ui.dat (TAB map, loading map, thumbnail, names) + cgd.dip (iconsinfo / iconstag)
  5. br_loot_rows.py      cgd.dip (kitdropinfo rows ki_mod 12 of map 58)
  6. br_client_files.py   cgd.dip (map 58 playable in SquareMode)
  7. br_storm_files.py    map.dat (storm walls), ui.dat (TAB lines, ribbon, room title, announcements), cgd.dip (icon
                          rows), ui/scene/SCENE_BATTLE.xml (announcement images)
Each step reads the archives the step before left in <work>/client/data (a copy of the clean client's files).

  --install <client>  copies the result into a client (originals of the first install kept in data/_br_all_backup/);
                      that client's ENG/serverinfo.cdb record stays (e.g. a local server)
  --compare <client>  record by record against a client's installed files, ENG/serverinfo.cdb left out

Usage: python br_build_all.py <clean client> <work> --password-file <pw> --physx-cooking <copy of PhysXCooking.dll>
           --texconv <texconv.exe> [--install <client>]... [--compare <client>]... [--skip-build]
"""
import argparse
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ziprecords  # noqa: E402

LAYOUT = os.path.join(HERE, 'layouts', 'map058_f4.json')
MAP_ID = 58
MAP_SIZE = 29500
FILES = ('map.dat', 'ui.dat', 'cgd.dip', 'audio/sfx/ambient.dat', 'ui/scene/SCENE_BATTLE.xml')
ARCHIVES = ('map.dat', 'ui.dat', 'cgd.dip', 'audio/sfx/ambient.dat')
SERVERINFO = 'eng/serverinfo.cdb'
BACKUP = '_br_all_backup'


def run(*args):
    print('>', os.path.basename(args[0]), *[os.path.basename(a) if os.path.isabs(a) else a for a in args[1:]], flush=True)
    subprocess.run([sys.executable, *args], check=True, cwd=HERE)


def take(step_out, client):
    data = os.path.join(step_out, 'data')
    for name in FILES:
        src = os.path.join(data, name)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(client, 'data', name))


def copy_client(base, client):
    for name in FILES:
        dst = os.path.join(client, 'data', name)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(base, 'data', name), dst)


def kitdrop_table(cgd, password, out):
    with zipfile.ZipFile(cgd) as z:
        names = {n.lower(): n for n in z.namelist()}
        with open(out, 'wb') as f:
            f.write(z.read(names['eng/kitdropinfo.cdb'], pwd=password))


def build(a, password):
    if not os.path.exists(os.path.join(HERE, 'cook', 'cook.exe')):
        sys.exit('cook/cook.exe missing: run cook/build.bat first')
    work = os.path.abspath(a.work)
    client = os.path.join(work, 'client')
    copy_client(a.base, client)
    data = os.path.join(client, 'data')
    kitdrop = os.path.join(work, 'kitdropinfo.cdb')
    kitdrop_table(os.path.join(data, 'cgd.dip'), password, kitdrop)
    stage = os.path.join(work, 'stage')
    run('build_bigmap.py', LAYOUT, os.path.join(data, 'map.dat'), stage, '--ambient', os.path.join(data, 'audio', 'sfx', 'ambient.dat'),
        '--work', os.path.join(work, 'build'), '--physx-cooking', a.physx_cooking, '--kitdrop', kitdrop)
    run('verify_corridor.py', LAYOUT, os.path.join(work, 'build', 'merged', 'map%03d' % MAP_ID), os.path.join(stage, 'map%03d' % MAP_ID))
    run('verify_spawns.py', LAYOUT, os.path.join(work, 'build'), os.path.join(stage, 'map%03d' % MAP_ID),
        '--image', os.path.join(work, 'tdm_spawns.png'))
    pkg = os.path.join(work, 'package')
    run('package_client.py', LAYOUT, stage, client, pkg, '--password-file', a.password_file, '--size', str(MAP_SIZE))
    take(pkg, client)
    minimap = os.path.join(work, 'minimap')
    run('minimap58.py', LAYOUT, client, os.path.join(data, 'cgd.dip'), minimap, '--password-file', a.password_file, '--texconv', a.texconv)
    take(minimap, client)
    loot = os.path.join(work, 'loot', 'data')
    os.makedirs(loot, exist_ok=True)
    run('br_loot_rows.py', LAYOUT, os.path.join(stage, 'map%03d' % MAP_ID), os.path.join(data, 'cgd.dip'), os.path.join(loot, 'cgd.dip'),
        '--password-file', a.password_file)
    take(os.path.dirname(loot), client)
    nod = os.path.join(work, 'squaremode')
    run('br_client_files.py', os.path.join(data, 'cgd.dip'), nod, '--password-file', a.password_file)
    take(nod, client)
    storm = os.path.join(work, 'storm')
    run('br_storm_files.py', client, storm, '--password-file', a.password_file)
    take(storm, client)


def entries(path, password):
    with zipfile.ZipFile(path) as z:
        return [(i.filename, z.read(i.filename, pwd=password) if not i.is_dir() else b'') for i in z.infolist()]


def compare(built, client, password):
    problems = 0
    for name in FILES:
        mine, theirs = os.path.join(built, 'data', name), os.path.join(client, 'data', name)
        if name not in ARCHIVES:
            same = open(mine, 'rb').read() == open(theirs, 'rb').read()
            print('%s: %s' % (name, 'identical' if same else 'DIFFERENT'))
            problems += not same
            continue
        a = [(n, d) for n, d in entries(mine, password) if n.lower() != SERVERINFO]
        b = [(n, d) for n, d in entries(theirs, password) if n.lower() != SERVERINFO]
        if a == b:
            print('%s: %d records identical (in the same order)' % (name, len(a)))
            continue
        problems += 1
        da, db = dict(a), dict(b)
        only_built = sorted(set(da) - set(db))
        only_client = sorted(set(db) - set(da))
        changed = sorted(n for n in set(da) & set(db) if da[n] != db[n])
        order = [n for n, _ in a] != [n for n, _ in b]
        print('%s: DIFFERENT: %d only built %s, %d only installed %s, %d changed %s, order %s'
              % (name, len(only_built), only_built[:5], len(only_client), only_client[:5], len(changed), changed[:5],
                 'differs' if order else 'same'))
    return problems


def install(built, client, password):
    data = os.path.join(client, 'data')
    backup = os.path.join(data, BACKUP)
    for name in FILES:
        keep = os.path.join(backup, name)
        if not os.path.exists(keep):
            os.makedirs(os.path.dirname(keep), exist_ok=True)
            shutil.copyfile(os.path.join(data, name), keep)
    with zipfile.ZipFile(os.path.join(data, 'cgd.dip')) as z:
        names = {n.lower(): n for n in z.namelist()}
        serverinfo = (names[SERVERINFO], z.read(names[SERVERINFO], pwd=password))
    for name in FILES:
        src = os.path.join(built, 'data', name)
        dst = os.path.join(data, name)
        if name == 'cgd.dip':
            tmp = dst + '.tmp'
            ziprecords.rewrite(src, tmp, replace={serverinfo[0]: serverinfo[1]}, password=password)
            os.replace(tmp, dst)
        else:
            shutil.copyfile(src, dst)
        print('installed', name, 'in', client)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base', help='a clean client folder (data/ of the released client)')
    ap.add_argument('work', help='scratch folder; the result is <work>/client/data')
    ap.add_argument('--password-file', required=True, help="cgd.dip's zip password")
    ap.add_argument('--physx-cooking', help="a copy of the game's PhysXCooking.dll")
    ap.add_argument('--texconv', help='texconv.exe (DirectXTex)')
    ap.add_argument('--install', action='append', default=[], help='client folder to install the result into')
    ap.add_argument('--compare', action='append', default=[], help='client folder to compare the result with')
    ap.add_argument('--skip-build', action='store_true', help='use the result of an earlier run')
    a = ap.parse_args()
    with open(a.password_file, 'rb') as f:
        password = f.read().strip()
    if not a.skip_build:
        if not a.physx_cooking or not a.texconv:
            ap.error('--physx-cooking and --texconv are needed to build')
        build(a, password)
    built = os.path.join(os.path.abspath(a.work), 'client')
    problems = sum(compare(built, client, password) for client in a.compare)
    for client in a.install:
        install(built, client, password)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
