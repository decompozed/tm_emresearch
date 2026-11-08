"""Fixed item points of maps from kitdropinfo.cdb (ki_mod, ki_type 1, ki_map_id, ki_x/y/z floats): the battery bases,
item boxes, radars and supply points the modes place at fixed positions. Item Match (ki_mod 2) has none: its rows are
for map 0 and drop at the killed player's position."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'CgdManager'))
from CdbParser import Cdb  # noqa: E402

MODES = {3: 'CTB', 4: 'CCM', 5: 'ELM', 7: 'ZSM', 16: 'CL_CTB', 17: 'CL_ELM'}


def f32(v):
    return struct.unpack('<f', struct.pack('<I', v & 0xFFFFFFFF))[0]


def points(cdb_path, map_id):
    cdb = Cdb(cdb_path)
    cdb.parseCdb()
    out, seen = [], set()
    for e in cdb.entries:
        d = {f.key: f.value for f in e}
        if d['ki_map_id'] != map_id or d['ki_type'] != 1:
            continue
        pos = (f32(d['ki_x']), f32(d['ki_y']), f32(d['ki_z']))
        mode = MODES.get(d['ki_mod'], 'M%d' % d['ki_mod'])
        key = (mode.replace('CL_', ''), d['ki_item_id'], tuple(round(v) for v in pos))
        if key in seen:
            continue
        seen.add(key)
        out.append(dict(mode=mode, item=d['ki_item_id'], pos=pos))
    return out
