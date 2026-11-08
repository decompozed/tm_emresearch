"""Builds a merged map folder (mapNNN/) from a layout file and the client's map.dat.

layout.json: {"id": 58, "end_cam": 10, "maps": [{"id": 10, "offset": [x, y, z]}, ...], "corridors": [...]}
With corridors the merge alone is kept in <work>/merged/mapNNN/ (input of verify_merge.py and verify_corridor.py)."""
import argparse
import hashlib
import os
import re
import shutil
import sys
import zipfile

import corridor
import layout as layout_file
import loot_points
import mapmerge
from nifio import Nif

NIFS = ('World.nif', 'World_colp.nif', 'World_colb.nif', 'World_cols.nif')
SKIP = re.compile(r'^(prop_.*|world.*\.nif|end_cam\d+\.nif)$', re.I)


def extract(dat, prefix, dest):
    os.makedirs(dest, exist_ok=True)
    out = {}
    with zipfile.ZipFile(dat) as z:
        for n in z.namelist():
            head, _, tail = n.partition('/')
            if head.lower() == prefix.lower() and tail and '/' not in tail:
                p = os.path.join(dest, tail)
                with open(p, 'wb') as f:
                    f.write(z.read(n))
                out[tail.lower()] = p
    return out


def digest(path):
    with open(path, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


def plan_files(folders, tags):
    taken, renames, copies = {}, [], []
    for folder, tag in zip(folders, tags):
        ren = {}
        for f in sorted(os.listdir(folder)):
            if SKIP.match(f):
                continue
            p = os.path.join(folder, f)
            key = f.lower()
            if key in taken:
                if taken[key] == digest(p):
                    continue
                new = '%s_%s' % (tag, f)
                ren[key] = new
                key = new.lower()
                f = new
            taken[key] = digest(p)
            copies.append((p, f))
        renames.append(ren)
    return renames, copies


def referenced(nif):
    names = set()
    for s in nif.strings:
        s = s[4:] if s.startswith('LMG_') else s
        names.add(s.replace('\\', '/').rsplit('/', 1)[-1].lower())
    return names


def build(layout, map_dat, out_root, work, ambient_dat=None, log=print, cooking_dll=None, kitdrop=None):
    mid = layout['id']
    ids = [m['id'] for m in layout['maps']]
    tags = ['m%03d' % i for i in ids]
    folders = [os.path.join(work, 'map%03d' % i) for i in ids]
    for i, f in zip(ids, folders):
        shutil.rmtree(f, ignore_errors=True)
        extract(map_dat, 'map%03d' % i, f)
    renames, copies = plan_files(folders, tags)
    if ambient_dat:
        for r, extra in zip(renames, sound_renames(ambient_dat, ids, tags, work)):
            r.update(extra)
    for tag, ren in zip(tags, renames):
        for k, v in ren.items():
            log('%s: %s -> %s' % (tag, k, v))
    sources = [mapmerge.Source(f, tuple(m['offset']), t, r, m) for f, m, t, r in zip(folders, layout['maps'], tags, renames)]
    if kitdrop:
        for s, i in zip(sources, ids):
            s.loot = loot_points.points(kitdrop, i)

    out = os.path.join(out_root, 'map%03d' % mid)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    world = mapmerge.merge_world(sources, log)
    world.save(os.path.join(out, 'World.nif'))
    used = referenced(world)
    for name in NIFS[1:]:
        nif = mapmerge.merge_physx(sources, name, log)
        if nif:
            nif.save(os.path.join(out, name))
            used |= referenced(nif)
    cam = sources[ids.index(layout['end_cam'])]
    for k in range(1, 5):
        p = cam.path('END_CAM%02d.nif' % k)
        if p:
            nif = mapmerge.shift_end_cam(p, cam.offset)
            nif.save(os.path.join(out, 'END_CAM%02d.nif' % k))
            used |= referenced(nif)
    before = set()
    for s in sources:
        for f in os.listdir(s.folder):
            if f.lower().endswith('.nif') and not f.lower().startswith('prop_'):
                before |= {s.rename(n).lower() for n in referenced(Nif.load(os.path.join(s.folder, f)))}
    dropped = []
    for src, name in copies:
        if name.lower() in before and name.lower() not in used:
            dropped.append(name)
        else:
            shutil.copyfile(src, os.path.join(out, name))
    log('files copied %d, used only by dropped nodes (left out): %s' % (len(copies) - len(dropped), ', '.join(dropped) or '-'))
    if layout.get('corridors'):
        if not cooking_dll:
            raise ValueError('corridors need --physx-cooking')
        merged = os.path.join(work, 'merged', 'map%03d' % mid)
        shutil.rmtree(merged, ignore_errors=True)
        os.makedirs(merged)
        for name in NIFS:
            if os.path.exists(os.path.join(out, name)):
                shutil.copyfile(os.path.join(out, name), os.path.join(merged, name))
        corridor.apply(out, layout, cooking_dll, log)

    if ambient_dat:
        sounds = os.path.join(out_root, 'ambient', 'map%03d' % mid)
        shutil.rmtree(sounds, ignore_errors=True)
        sfolders = [extract(ambient_dat, 'map%03d' % i, os.path.join(work, 'ambient', 'map%03d' % i)) for i in ids]
        taken = {}
        for files, s in zip(sfolders, sources):
            for key, p in sorted(files.items()):
                name = os.path.basename(p)
                new = s.rename(name)
                if new.lower() in taken and taken[new.lower()] != digest(p):
                    raise ValueError('sound %s differs between maps but was not renamed' % name)
                taken[new.lower()] = digest(p)
                os.makedirs(sounds, exist_ok=True)
                shutil.copyfile(p, os.path.join(sounds, new))
        log('ambient sounds %d' % len(taken))
    return out


def sound_renames(ambient_dat, ids, tags, work):
    """Ambient sounds are looked up by name in ambient/mapNNN/ (0x426BA0): same name, other contents -> rename."""
    taken, out = {}, []
    for i, tag in zip(ids, tags):
        files = extract(ambient_dat, 'map%03d' % i, os.path.join(work, 'ambient_probe', 'map%03d' % i))
        ren = {}
        for key, p in sorted(files.items()):
            d = digest(p)
            if key in taken and taken[key] != d:
                ren[key] = '%s_%s' % (tag, os.path.basename(p))
            else:
                taken[key] = d
        out.append(ren)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('layout')
    ap.add_argument('map_dat')
    ap.add_argument('out')
    ap.add_argument('--ambient', help='audio/sfx/ambient.dat: also build ambient/mapNNN/')
    ap.add_argument('--work', help='scratch folder (default: <out>/_work)')
    ap.add_argument('--physx-cooking', help="a copy of the game's PhysXCooking.dll (needed for corridors)")
    ap.add_argument('--kitdrop', help='kitdropinfo.cdb (from cgd.dip): fixed item points of the maps under a LOOT node')
    a = ap.parse_args()
    layout = layout_file.load(a.layout)
    lines = []

    def log(s):
        print(s)
        lines.append(s)
    build(layout, a.map_dat, a.out, a.work or os.path.join(a.out, '_work'), a.ambient, log, a.physx_cooking, a.kitdrop)
    with open(os.path.join(a.out, 'build_map%03d.log' % layout['id']), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
