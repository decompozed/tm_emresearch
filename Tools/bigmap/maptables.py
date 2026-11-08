"""cdb rows of a merged map (mapinfo, jumperinfo, iconstag), built with Tools/CgdManager's Cdb."""
import copy
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'CgdManager'))
from CdbParser import Cdb  # noqa: E402

MODES = ('tdm', 'ffa', 'itm', 'ctf', 'ctm', 'sab', 'cim', 'zsm', 'grm', 'mock', 'bmb', 'sni', 'nod', 'pve', 'bot',
         'tut', 'cl_ctf', 'cl_sab', 'cl_tdm', 'cl_bmb')
ICON_TAGS = ('LOADING_MAPNAME_%d', 'LOADING_%d', 'MAP_MINIVIEW_%d', 'ROOM_MAP_TITLE_%d', 'LOADING_MINIMAP_%d',
             'LOADING_MINIMAP_F_%d', 'MAP_BATTLE_%d', 'MAP_BATTLE_F_%d')


def load(data, name='table'):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, name + '.cdb')
        with open(path, 'wb') as f:
            f.write(data)
        cdb = Cdb(path)
        cdb.parseCdb()
        assert dump(cdb) == data, '%s: CdbParser does not round-trip this table' % name
        return cdb


def dump(cdb):
    with tempfile.TemporaryDirectory() as tmp:
        cdb.fileName = 'out'
        cdb.toCdb(tmp)
        with open(os.path.join(tmp, 'out.cdb'), 'rb') as f:
            return f.read()


def _row(cdb, key, value):
    return next(e for e in cdb.entries if _get(e, key) == value)


def _get(entry, key):
    return next(f.value for f in entry if f.key == key)


def _set(entry, key, value):
    field = next(f for f in entry if f.key == key)
    field.value = bool(value) if field.typeSize == 1 else value


def add_mapinfo(cdb, template_id, map_id, name, desc, max_users, death_height, size, modes):
    if any(_get(e, 'mi_id') == map_id for e in cdb.entries):
        raise ValueError('mapinfo already has map %d' % map_id)
    row = copy.deepcopy(_row(cdb, 'mi_id', template_id))
    for f in row:
        if f.typeSize == 1:
            f.value = bool(f.value)
    for k, v in (('mi_id', map_id), ('mi_rid', map_id), ('mi_name', name), ('mi_desc', desc),
                 ('mi_max_user', max_users), ('mi_recommend_max_user', max_users),
                 ('mi_death_height', death_height & 0xFFFFFFFF), ('mi_size', size)):
        _set(row, k, v)
    for m in MODES:
        _set(row, 'mi_mod_' + m, m in modes)
    cdb.addEntry(row)


def add_jumpers(cdb, source_map, map_id, renames=None):
    renames = renames or {}
    rows = [e for e in cdb.entries if _get(e, 'ji_map_id') == source_map]
    next_id = max(_get(e, 'ji_id') for e in cdb.entries) + 1
    added = []
    for e in rows:
        row = copy.deepcopy(e)
        for f in row:
            if f.typeSize == 1:
                f.value = bool(f.value)
        name = _get(row, 'ji_name')
        _set(row, 'ji_id', next_id)
        _set(row, 'ji_map_id', map_id)
        _set(row, 'ji_name', renames.get(name, name))
        cdb.addEntry(row)
        added.append((next_id, renames.get(name, name)))
        next_id += 1
    return added


def add_icon_tags(cdb, template_map, map_id):
    next_index = max(_get(e, 'it_index') for e in cdb.entries) + 1
    added = []
    for fmt in ICON_TAGS:
        src = 'ICONTAG_' + fmt % template_map
        dst = 'ICONTAG_' + fmt % map_id
        if any(_get(e, 'it_icontag') == dst for e in cdb.entries):
            raise ValueError('%s exists' % dst)
        icon = next((_get(e, 'it_iconid') for e in cdb.entries if _get(e, 'it_icontag') == src), None)
        if icon is None:
            continue
        row = copy.deepcopy(next(e for e in cdb.entries if _get(e, 'it_icontag') == src))
        _set(row, 'it_index', next_index)
        _set(row, 'it_icontag', dst)
        cdb.addEntry(row)
        added.append((next_index, dst, icon))
        next_index += 1
    return added
