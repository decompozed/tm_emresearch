"""Merges several map folders into one: World.nif, World_colp/colb/cols.nif, END_CAM*.nif.
Node names and file names follow the client's map loader (CLevel::Load 0x5281E0 and the functions it calls)."""
import copy
import os
import posixpath

import meshclip
import nifedit as ne
from nifio import Nif

DROPPED = {'dof', 'dof1'}  # background billboards around one arena
RESPAWN_NAMES = ('RESPAWN', 'Respawn')  # 0x52A120 looks for RESPAWN, then falls back to the root
MODES = {'TDM': ('REDTEAM', 'BLUETEAM'), 'FFA': ()}  # 0x52C2B0
REATTACHED = ('animated', 'water001', 'water001_low')  # moved under identity nodes at load (0x45F700, 0x5281E0)
SUN_NAMES = ('Sun', 'SUN')  # 0x535D30
MAX_ACTORS = 100  # actor meshes are renamed Co_%d%d with the map id in a 10 byte buffer (0x7AB720)


class Source:

    def __init__(self, folder, offset, tag, renames, spec=None):
        self.folder, self.offset, self.tag, self.renames = folder, offset, tag, renames
        spec = spec or {}
        self.crop = spec.get('crop')
        self.tdm_team = spec.get('tdm_team')
        self.actor_renames = spec.get('actor_renames', {})
        self.drop_spawns = set(spec.get('drop_spawns', []))
        self.tdm_extra = list(spec.get('tdm_extra', []))
        assert not self.tdm_extra or self.tdm_team, 'tdm_extra needs tdm_team'
        self.loot = []

    def world_nif(self, log=None):
        nif = Nif.load(self.path('World.nif'))
        if self.crop:
            meshclip.crop(nif, (tuple(self.crop[0]), tuple(self.crop[1])), log)
        return nif

    def path(self, name):
        files = {f.lower(): f for f in os.listdir(self.folder)}
        f = files.get(name.lower())
        return os.path.join(self.folder, f) if f else None

    def rename(self, s):
        prefix = ''
        if s.startswith('LMG_'):
            prefix, s = 'LMG_', s[4:]
        head, tail = posixpath.split(s.replace('\\', '/'))
        new = self.renames.get(tail.lower())
        if new is None:
            return prefix + s
        return prefix + (s[:len(s) - len(tail)] + new if head else new)


def _detach(nif, parent, child):
    ne.set_children(nif, parent, [c for c in ne.children(nif, parent) if c != child])


def _append(nif, parent, kids):
    ne.set_children(nif, parent, ne.children(nif, parent) + list(kids))


def _new_node(bld, name, template):
    data = copy.deepcopy(bld.nif.blocks[template].data)
    data['Name'] = {'Index': bld.intern(name)}
    for k, n in (('Children', 'Num Children'), ('Effects', 'Num Effects'), ('Properties', 'Num Properties'),
                 ('Extra Data List', 'Num Extra Data List')):
        data[k], data[n] = [], 0
    data['Controller'] = -1
    data['Collision Object'] = -1
    i = bld.new('NiNode', data)
    ne.set_local(bld.nif.blocks[i], ne.IDENTITY)
    return i


def spawn_node(nif, respawn, key):
    i = respawn
    for part in key.split('/'):
        found = [c for c in ne.children(nif, i) if nif.name(nif.blocks[c]) == part]
        assert len(found) == 1, 'spawn %s: %d nodes named %s' % (key, len(found), part)
        i = found[0]
    assert nif.blocks[i].data.get('Collision Object', -1) >= 0, 'spawn %s has no collision box' % key
    return i


class WorldMerge:

    def __init__(self, sources, log):
        self.sources, self.log = sources, log
        nifs = [s.world_nif(lambda m, s=s: log('%s: %s' % (s.tag, m))) for s in sources]
        self.bld = ne.Builder(nifs[0])
        maps = [self.bld.add(n, s.rename if i else None) for i, (n, s) in enumerate(zip(nifs, sources))]
        self.nif = self.bld.nif
        self.roots = [m[n.roots[0]] for m, n in zip(maps, nifs)]
        for r in self.roots:
            assert ne.is_identity_rotation(ne.local(self.nif.blocks[r])) and ne.local(self.nif.blocks[r])[0] == (0, 0, 0)
        par = ne.parents(self.nif)
        self.world = {i: ne.world(self.nif, i, par) for r in self.roots for i in ne.find_all(self.nif, lambda *_: True, r)}
        self.top = []

    def pairs(self):
        return zip(self.roots, self.sources)

    def shifted(self, i, s):
        return ne.compose(ne.translation(s.offset), self.world[i])

    def name(self, i):
        return self.bld.name(i)

    def run(self):
        root = _new_node(self.bld, 'Scene Root', self.roots[0])
        first = self.nif.blocks[self.roots[0]].data
        self.nif.blocks[root].data['Flags'] = first['Flags']
        ne.set_list(self.nif.blocks[root], 'Properties', 'Num Properties', first['Properties'])
        self.bake_reattached()
        self.merge_rooms()
        self.merge_spawns(root)
        self.merge_outside_sound()
        self.merge_occluder()
        self.single_sky_and_sun()
        self.add_loot(root)
        for r, s in self.pairs():
            self.bld.set_name(r, s.tag)
            ne.set_local(self.nif.blocks[r], ne.translation(s.offset))
        ne.set_children(self.nif, root, self.top + self.roots)
        self.nif.roots = [root]
        removed, cleared = ne.collect(self.nif)
        self.log('World.nif: %d blocks, %d strings (dropped %d blocks, cleared %d pointers)'
                 % (len(self.nif.blocks), len(self.nif.strings), removed, cleared))
        return self.nif

    def bake_reattached(self):
        for r, s in self.pairs():
            for i in ne.find_all(self.nif, lambda i, b: self.name(i) in REATTACHED, r):
                ne.set_local(self.nif.blocks[i], self.shifted(i, s))
                self.log('%s: %r baked to world (reattached at load)' % (s.tag, self.name(i)))

    def merge_rooms(self):
        """One NiRoomGroup (0x5281E0 keeps the first PORTAL_NDLRG); rooms shifted, lit by their map's root lights."""
        groups = [ne.find(self.nif, 'PORTAL_NDLRG', r, 'NiRoomGroup') for r in self.roots]
        group = next(g for g in groups if g is not None)
        rooms = []
        taken = set()
        for g, (r, s) in zip(groups, self.pairs()):
            if g is None:
                continue
            assert ne.is_identity_rotation(self.world[g]), 'rotated room group'
            assert self.nif.blocks[g].data.get('Shell', -1) == -1, 'room group shell not supported'
            lights = list(self.nif.blocks[r].data.get('Effects') or [])
            for room in self.nif.blocks[g].data['Rooms']:
                b = self.nif.blocks[room]
                if self.name(room) in taken:
                    self.log('%s: room %s renamed %s_%s (room sounds are keyed by room name, 0x52BF20)'
                             % (s.tag, self.name(room), s.tag, self.name(room)))
                    self.bld.set_name(room, '%s_%s' % (s.tag, self.name(room)))
                taken.add(self.name(room))
                ne.set_local(b, ne.compose(ne.translation(s.offset), ne.compose(self.world[g], ne.local(b))))
                fx = list(b.data.get('Effects') or [])
                ne.set_list(b, 'Effects', 'Num Effects', fx + [e for e in lights if e not in fx])
                rooms.append(room)
            _detach(self.nif, ne.parents(self.nif)[g], g)
        ne.set_list(self.nif.blocks[group], 'Rooms', 'Num Rooms', rooms)
        ne.set_children(self.nif, group, rooms)
        self.top.append(group)
        self.log('rooms %d' % len(rooms))

    def merge_spawns(self, template):
        """One RESPAWN with TDM/REDTEAM, TDM/BLUETEAM and FFA; spawn nodes keep their (shifted) world transform
        (0x52CDC0 reads the world box of each child)."""
        respawn = _new_node(self.bld, 'RESPAWN', template)
        counts = []
        for mode, teams in MODES.items():
            mnode = _new_node(self.bld, mode, template)
            _append(self.nif, respawn, [mnode])
            targets = [(t, _new_node(self.bld, t, template)) for t in teams] or [(None, mnode)]
            if teams:
                ne.set_children(self.nif, mnode, [n for _, n in targets])
            for r, s in self.pairs():
                rsp = next((x for x in (ne.find(self.nif, n, r) for n in RESPAWN_NAMES) if x is not None), r)
                src_mode = ne.find(self.nif, mode, rsp)
                for team, dst in targets if src_mode is not None else []:
                    src = ne.find(self.nif, team, src_mode) if team else src_mode
                    kids = ne.children(self.nif, src)
                    key = mode + '/' + (team + '/' if team else '')
                    dropped = [c for c in kids if key + self.name(c) in s.drop_spawns]
                    kids = [c for c in kids if c not in dropped]
                    for c in kids:
                        ne.set_local(self.nif.blocks[c], self.shifted(c, s))
                    if dropped:
                        self.log('%s: spawns left out %s' % (s.tag, ', '.join(key + self.name(c) for c in dropped)))
                    if team and s.tdm_team:
                        dst = dict(targets)[s.tdm_team]
                    _append(self.nif, dst, kids)
                    ne.set_children(self.nif, src, [])
                    counts.append('%s/%s %s: %d%s' % (mode, team or '-', s.tag, len(kids),
                                                      ' -> ' + s.tdm_team if team and s.tdm_team else ''))
                if mode == 'TDM' and s.tdm_extra:
                    copies = [self.copy_spawn(spawn_node(self.nif, rsp, key), s) for key in s.tdm_extra]
                    _append(self.nif, dict(targets)[s.tdm_team], copies)
                    counts.append('TDM extra %s: %d -> %s' % (s.tag, len(copies), s.tdm_team))
        for r in self.roots:
            for x in (ne.find(self.nif, n, r) for n in RESPAWN_NAMES):
                if x is not None:
                    _detach(self.nif, ne.parents(self.nif)[x], x)
        self.top.append(respawn)
        self.log('spawns ' + ', '.join(counts))

    def copy_spawn(self, i, s):
        b = self.nif.blocks[i]
        assert not ne.children(self.nif, i) and b.data.get('Controller', -1) == -1 and not b.data.get('Extra Data List')
        node = self.bld.new(b.type, copy.deepcopy(b.data), b.flag)
        co = self.nif.blocks[b.data['Collision Object']]
        data = copy.deepcopy(co.data)
        data['Target'] = node
        self.nif.blocks[node].data['Collision Object'] = self.bld.new(co.type, data, co.flag)
        ne.set_local(self.nif.blocks[node], self.shifted(i, s))
        return node

    def merge_outside_sound(self):
        """One OUTSIDE_SOUND at the origin; its children keep their (shifted) world transform (0x52C160)."""
        outside = None
        for r, s in self.pairs():
            o = ne.find(self.nif, 'OUTSIDE_SOUND', r)
            if o is None:
                continue
            kids = ne.children(self.nif, o)
            _detach(self.nif, ne.parents(self.nif)[o], o)
            if outside is None:
                outside = o
                ne.set_local(self.nif.blocks[o], ne.IDENTITY)
                ne.set_children(self.nif, o, [])
                self.top.append(o)
            for c in kids:
                ne.set_local(self.nif.blocks[c], self.shifted(c, s))
            _append(self.nif, outside, kids)

    def add_loot(self, template):
        """Fixed item points of the source maps (kitdropinfo rows) as empty nodes under LOOT/<mode>; no client code
        looks this node up, it keeps the points with the map."""
        points = [(s, p) for s in self.sources for p in s.loot]
        if not points:
            return
        loot = _new_node(self.bld, 'LOOT', template)
        groups = {}
        for s, p in points:
            if p['mode'] not in groups:
                groups[p['mode']] = _new_node(self.bld, p['mode'], template)
            n = _new_node(self.bld, '%s_%d' % (s.tag, p['item']), template)
            ne.set_local(self.nif.blocks[n], ne.translation([v + o for v, o in zip(p['pos'], s.offset)]))
            _append(self.nif, groups[p['mode']], [n])
        ne.set_children(self.nif, loot, list(groups.values()))
        self.top.append(loot)
        self.log('loot points %d (%s)' % (len(points), ', '.join('%s %d' % (m, len(ne.children(self.nif, g))) for m, g in groups.items())))

    def merge_occluder(self):
        """One Occluder, a direct child of "Scene Root" (0x52E530 detaches it from there)."""
        occluder = None
        for r, s in self.pairs():
            o = ne.find(self.nif, 'Occluder', r)
            if o is None:
                continue
            w = self.shifted(o, s)
            _detach(self.nif, ne.parents(self.nif)[o], o)
            if occluder is None:
                occluder = o
                ne.set_local(self.nif.blocks[o], w)
                self.top.append(o)
                continue
            inv = ne.inverse(ne.local(self.nif.blocks[occluder]))
            kids = ne.children(self.nif, o)
            for c in kids:
                ne.set_local(self.nif.blocks[c], ne.compose(inv, ne.compose(w, ne.local(self.nif.blocks[c]))))
            _append(self.nif, occluder, kids)
            ne.set_children(self.nif, o, [])

    def single_sky_and_sun(self):
        """First map's Skydome, SUN and ambient light (a direct root child, 0x535D30) are the ones the client uses."""
        for k, (r, s) in enumerate(self.pairs()):
            for i in ne.children(self.nif, r):
                if self.name(i) in DROPPED or (k and self.name(i) == 'Skydome'):
                    _detach(self.nif, r, i)
                    self.log('%s: dropped %r' % (s.tag, self.name(i)))
            if k:
                for i in ne.find_all(self.nif, lambda i, b: self.name(i) in SUN_NAMES, r):
                    self.bld.set_name(i, '%s_%s' % (self.name(i), s.tag))
                    self.log('%s: sun node renamed %r' % (s.tag, self.name(i)))
                continue
            for i in ne.children(self.nif, r):
                if self.nif.blocks[i].base == 'NiAmbientLight':
                    _detach(self.nif, r, i)
                    self.top.append(i)


def merge_world(sources, log):
    return WorldMerge(sources, log).run()


def merge_physx(sources, name, log):
    present = [(p, s) for p, s in ((s.path(name), s) for s in sources) if p]
    if not present:
        return None
    nifs = [Nif.load(p) for p, _ in present]
    bld = ne.Builder(nifs[0])
    maps = [bld.add(n, (lambda x, r=s.actor_renames: r.get(x, x)) if s.actor_renames else None) for n, (_, s) in zip(nifs, present)]
    for _, s in present:
        for old, new in s.actor_renames.items():
            log('%s %s: %s renamed %s' % (name, s.tag, old, new))
    nif = bld.nif
    debug_roots = [m[n.roots[0]] for m, n in zip(maps, nifs)]
    props = [next(m[r] for r in n.roots if n.blocks[r].base == 'NiPhysXProp') for m, n in zip(maps, nifs)]
    descs = [nif.blocks[p].data['Snapshot'] for p in props]
    assert len({nif.blocks[p].data['PhysX to World Scale'] for p in props}) == 1

    for r, (_, s) in zip(debug_roots, present):
        ne.set_local(nif.blocks[r], ne.compose(ne.translation(s.offset), ne.local(nif.blocks[r])))
        bld.set_name(r, s.tag)
    debug = _new_node(bld, 'Scene Root', debug_roots[0])
    nif.blocks[debug].data['Flags'] = nif.blocks[debug_roots[0]].data['Flags']
    ne.set_children(nif, debug, debug_roots)

    materials = list(nif.blocks[descs[0]].data['Materials'])
    actors = []
    for d, (_, s) in zip(descs, present):
        keymap = _merge_materials(nif, materials, nif.blocks[d].data['Materials'])
        for a in nif.blocks[d].data['Actors']:
            _shift_actor(nif, a, s.offset, keymap)
            actors.append(a)
    assert len(actors) < MAX_ACTORS
    ne.set_list(nif.blocks[descs[0]], 'Actors', 'Num Actors', actors)
    ne.set_list(nif.blocks[descs[0]], 'Materials', 'Num Materials', materials)
    nif.roots = [debug, props[0]]
    removed, cleared = ne.collect(nif)
    log('%s: %d actors, %d materials, %d blocks (dropped %d, cleared %d pointers)'
        % (name, len(actors), len(materials), len(nif.blocks), removed, cleared))
    return nif


def _merge_materials(nif, materials, incoming):
    keymap = {}
    for m in incoming:
        data = nif.blocks[m['Material']].data
        same = next((x for x in materials if x['Key'] == m['Key'] and nif.blocks[x['Material']].data == data), None)
        if same:
            keymap[m['Key']] = same['Key']
            continue
        key = m['Key'] if all(x['Key'] != m['Key'] for x in materials) else max(x['Key'] for x in materials) + 1
        materials.append({'Key': key, 'Material': m['Material']})
        keymap[m['Key']] = key
    return keymap


def _shift_actor(nif, actor, offset, keymap):
    data = nif.blocks[actor].data
    for pose in data['Poses']:
        for k, o in zip(('m14', 'm24', 'm34'), offset):
            pose[k] += o
    for sh in data['Shape Descriptions']:
        sd = nif.blocks[sh].data
        sd['Material Index'] = keymap.get(sd['Material Index'], sd['Material Index'])


def shift_end_cam(path, offset):
    nif = Nif.load(path)
    for c in ne.children(nif, nif.roots[0]):
        b = nif.blocks[c]
        ne.set_local(b, ne.compose(ne.translation(offset), ne.local(b)))
        ctrl = b.data.get('Controller', -1)
        while ctrl >= 0:
            cd = nif.blocks[ctrl]
            if cd.base == 'NiTransformController' and cd.data.get('Interpolator', -1) >= 0:
                _shift_interpolator(nif, cd.data['Interpolator'], offset)
            ctrl = cd.data.get('Next Controller', -1)
    return nif


def _shift_interpolator(nif, i, offset):
    d = nif.blocks[i].data
    t = d['Transform']['Translation']
    if all(ne.finite(t[k]) for k in 'xyz'):
        for k, o in zip('xyz', offset):
            t[k] += o
    if d.get('Data', -1) >= 0:
        for key in nif.blocks[d['Data']].data['Translations']['Keys']:
            for k, o in zip('xyz', offset):
                key['Value'][k] += o
