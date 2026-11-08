"""Checks a merged map against its sources: every mesh, spawn, sound node, room wall, portal and PhysX actor of the
sources must be in the output at its world transform shifted by the map offset (meshes under nodes the client
reattaches at load are compared after that reattachment), with the tdm_extra copies in the TDM group."""
import argparse
import collections
import hashlib
import os
import re
import sys

import layout as layout_file
import mapmerge
import nifedit as ne
from nifio import Nif

TOL = 0.05


def walk(nif, reattach=True):
    out = []
    stack = [(r, ne.IDENTITY) for r in nif.roots]
    while stack:
        i, parent = stack.pop()
        b = nif.blocks[i]
        if 'Translation' not in b.data:
            continue
        base = ne.IDENTITY if reattach and nif.name(b) in mapmerge.REATTACHED else parent
        w = ne.compose(base, ne.local(b))
        out.append((i, w))
        stack += [(c, w) for c in ne.children(nif, i)]
    return out


def mesh_key(nif, i):
    h = hashlib.md5()
    for ds in nif.blocks[i].data.get('Datastreams', []):
        h.update(nif.blocks[ds['Stream']].data['Data'])
    return 'mesh:' + h.hexdigest()


def keyed(nif, skip_names=(), reattach=True, original=False, tdm_team=None, drop=()):
    out = collections.defaultdict(list)
    par = ne.parents(nif)
    skip = set()
    for i, b in enumerate(nif.blocks):
        if nif.name(b) in skip_names:
            skip |= set(ne.find_all(nif, lambda *_: True, i))
    for i, w in walk(nif, reattach):
        if i in skip:
            continue
        b = nif.blocks[i]
        name = nif.name(b)
        parent_name = nif.name(nif.blocks[par[i]]) if i in par else None
        if b.base == 'NiMesh':
            out[mesh_key(nif, i)].append(w)
        elif b.base in ('NiRoom', 'NiPortal'):
            out['%s:%s' % (b.base, re.sub(r'^m\d{3}_', '', name) if b.base == 'NiRoom' else name)].append(w)
        elif parent_name == 'OUTSIDE_SOUND' or (parent_name in ('REDTEAM', 'BLUETEAM', 'FFA')
                                                 and b.data.get('Collision Object', -1) >= 0):
            group = parent_name
            if parent_name in ('REDTEAM', 'BLUETEAM'):
                group = '%s/%s' % (nif.name(nif.blocks[par[par[i]]]), parent_name)
            if original and group not in ('FFA', 'OUTSIDE_SOUND', 'TDM/REDTEAM', 'TDM/BLUETEAM'):
                continue
            if original and '%s/%s' % (group, name) in drop:
                continue
            if tdm_team and group.startswith('TDM/'):
                group = 'TDM/' + tdm_team
            out['%s:%s' % (group, name)].append(w)
    return out


def close(a, b):
    return (all(abs(x - y) <= TOL for x, y in zip(a[0], b[0]))
            and all(abs(x - y) <= 1e-4 for x, y in zip(a[1], b[1])) and abs(a[2] - b[2]) <= 1e-4)


def compare(expected, got, what):
    errors = []
    for key, ws in expected.items():
        have = list(got.get(key, []))
        for w in ws:
            j = next((k for k, h in enumerate(have) if close(w, h)), None)
            if j is None:
                errors.append('%s %s missing at %s' % (what, key, [round(v, 1) for v in w[0]]))
            else:
                have.pop(j)
    extra = sum(len(v) for v in got.values()) - sum(len(v) for v in expected.values())
    return errors, extra


def actors(nif):
    out = collections.defaultdict(list)
    for b in nif.blocks:
        if b.base != 'NiPhysXActorDesc':
            continue
        shapes = b.data['Shape Descriptions']
        mesh = nif.blocks[nif.blocks[shapes[0]].data['Mesh']].data['Mesh Data']['Data']
        p = b.data['Poses'][0]
        out[hashlib.md5(mesh).hexdigest()].append((p['m14'], p['m24'], p['m34']))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('layout')
    ap.add_argument('sources', help='folder holding the extracted source mapNNN/ folders')
    ap.add_argument('merged', help='the merged mapNNN/ folder')
    a = ap.parse_args()
    layout = layout_file.load(a.layout)
    expected = collections.defaultdict(list)
    exp_actors = {n: collections.defaultdict(list) for n in ('World_colp.nif', 'World_colb.nif', 'World_cols.nif')}
    for m in layout['maps']:
        src = mapmerge.Source(os.path.join(a.sources, 'map%03d' % m['id']), tuple(m['offset']), '', {}, m)
        shift = ne.translation(m['offset'])
        nif = src.world_nif()
        dropped = mapmerge.DROPPED | ({'Skydome'} if m is not layout['maps'][0] else set())
        for k, ws in keyed(nif, dropped, original=True, tdm_team=m.get('tdm_team'), drop=set(m.get('drop_spawns', []))).items():
            expected[k] += [ne.compose(shift, w) for w in ws]
        if m.get('tdm_extra'):
            rsp = next(x for x in (ne.find(nif, n, nif.roots[0]) for n in mapmerge.RESPAWN_NAMES) if x is not None)
            par = ne.parents(nif)
            for key in m['tdm_extra']:
                i = mapmerge.spawn_node(nif, rsp, key)
                expected['TDM/%s:%s' % (m['tdm_team'], nif.name(nif.blocks[i]))].append(ne.compose(shift, ne.world(nif, i, par)))
        for name in exp_actors:
            p = src.path(name)
            if p:
                for k, ps in actors(Nif.load(p)).items():
                    exp_actors[name][k] += [tuple(v + o for v, o in zip(q, m['offset'])) for q in ps]
    got = keyed(Nif.load(os.path.join(a.merged, 'World.nif')))
    errors, extra = compare(expected, got, 'World.nif')
    print('World.nif: %d objects checked, %d missing/misplaced, %d extra' % (sum(map(len, expected.values())), len(errors), extra))
    for e in errors[:30]:
        print('  ', e)
    bad = len(errors)
    for name, exp in exp_actors.items():
        p = os.path.join(a.merged, name)
        if not os.path.exists(p):
            continue
        have = actors(Nif.load(p))
        miss = [(k, q) for k, qs in exp.items() for q in qs
                if not any(all(abs(x - y) < 1e-2 for x, y in zip(q, h)) for h in have.get(k, []))]
        print('%s: %d actors checked, %d missing/misplaced' % (name, sum(map(len, exp.values())), len(miss)))
        bad += len(miss)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
