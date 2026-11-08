"""Zip archives of the client (map.dat, cgd.dip, ambient.dat) rewritten record by record: untouched entries keep
their local header, data and central record byte for byte; replaced and added entries are deflated (level 9) with the
same header layout as the client's own entries (version 2.0, no extra field, no data descriptor)."""
import os
import random
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'nif'))
from make_hardpoint_icon import ZipCrypto  # noqa: E402

DOS_TIME, DOS_DATE = 0x0000, 0x5D45


def records(path):
    with open(path, 'rb') as f:
        raw = f.read()
    eocd = raw.rfind(b'PK\x05\x06')
    count, _, cd_offset, comment_len = struct.unpack('<HIIH', raw[eocd + 10:eocd + 22])
    out, p = [], cd_offset
    for _ in range(count):
        assert raw[p:p + 4] == b'PK\x01\x02'
        flags = struct.unpack_from('<H', raw, p + 8)[0]
        csize = struct.unpack_from('<I', raw, p + 20)[0]
        nlen, elen, clen = struct.unpack_from('<HHH', raw, p + 28)
        lho = struct.unpack_from('<I', raw, p + 42)[0]
        central = raw[p:p + 46 + nlen + elen + clen]
        name = raw[p + 46:p + 46 + nlen].decode('cp437')
        assert raw[lho:lho + 4] == b'PK\x03\x04' and not flags & 8, name
        lnlen, lelen = struct.unpack_from('<HH', raw, lho + 26)
        out.append((name, raw[lho:lho + 30 + lnlen + lelen + csize], central))
        p += len(central)
    return out, raw[eocd + 22:eocd + 22 + comment_len]


def _payload(name, data, password):
    crc = zlib.crc32(data) & 0xFFFFFFFF
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    body = c.compress(data) + c.flush()
    flags = 0
    if password:
        rng = random.Random('%s:%08x' % (name, crc))
        header = bytes(rng.getrandbits(8) for _ in range(11)) + bytes([crc >> 24])
        body = ZipCrypto(password).encrypt(header + body)
        flags = 1
    return crc, body, flags


def new_record(name, data, password=None):
    nb = name.encode('cp437')
    if name.endswith('/'):
        crc, body, flags, method, attr = 0, b'', 0, 0, 0x10
    else:
        crc, body, flags = _payload(name, data, password)
        method, attr = 8, 0x20
    local = struct.pack('<IHHHHHIIIHH', 0x04034B50, 20, flags, method, DOS_TIME, DOS_DATE, crc, len(body), len(data),
                        len(nb), 0) + nb + body
    central = struct.pack('<IHHHHHHIIIHHHHHII', 0x02014B50, 20, 20, flags, method, DOS_TIME, DOS_DATE, crc, len(body),
                          len(data), len(nb), 0, 0, 0, 0, attr, 0) + nb
    return local, central


def replaced_record(local, central, data, password=None):
    name = central[46:46 + struct.unpack_from('<H', central, 28)[0]].decode('cp437')
    crc, body, flags = _payload(name, data, password)
    keep = struct.unpack_from('<H', local, 6)[0] & ~0x9
    fields = struct.pack('<HHIII', keep | flags, 8, crc, len(body), len(data))
    lnlen, lelen = struct.unpack_from('<HH', local, 26)
    new_local = local[:6] + fields[:4] + local[10:14] + fields[4:] + local[26:30 + lnlen + lelen] + body
    new_central = central[:8] + fields[:4] + central[12:16] + fields[4:] + central[28:]
    return new_local, new_central


def rewrite(src, dst, replace=None, add=None, password=None):
    replace, add = dict(replace or {}), list(add or [])
    recs, comment = records(src)
    names = {n for n, _, _ in recs}
    missing = set(replace) - names
    assert not missing, missing
    clash = {n for n, _ in add} & names
    assert not clash, clash
    out = [replaced_record(l, c, replace[n], password) if n in replace else (l, c) for n, l, c in recs]
    out += [new_record(n, d, password) for n, d in add]
    tmp = dst + '.tmp'
    with open(tmp, 'wb') as f:
        centrals = []
        for local, central in out:
            centrals.append(central[:42] + struct.pack('<I', f.tell()) + central[46:])
            f.write(local)
        cd_offset = f.tell()
        for c in centrals:
            f.write(c)
        f.write(b'PK\x05\x06' + struct.pack('<HHHHIIH', 0, 0, len(centrals), len(centrals), f.tell() - cd_offset,
                                            cd_offset, len(comment)) + comment)
    os.replace(tmp, dst)
