from __future__ import annotations

import hashlib
import struct
import zipfile
import zlib
from pathlib import Path

PATCH_VERSION_CDB = 'patchversioninfo.cdb'
PI_VERSION_KEY = 'pi_version'
def _make_crc_table() -> list[int]:
    table = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        table.append(c)
    return table


_CRC_TABLE = _make_crc_table()


class _ZipCrypto:
    def __init__(self, password: bytes):
        self.k0, self.k1, self.k2 = 0x12345678, 0x23456789, 0x34567890
        for b in password:
            self._update(b)

    def _crc(self, crc: int, b: int) -> int:
        return (crc >> 8) ^ _CRC_TABLE[(crc ^ b) & 0xFF]

    def _update(self, b: int) -> None:
        self.k0 = self._crc(self.k0, b)
        self.k1 = ((self.k1 + (self.k0 & 0xFF)) * 134775813 + 1) & 0xFFFFFFFF
        self.k2 = self._crc(self.k2, self.k1 >> 24)

    def encrypt(self, data: bytes) -> bytes:
        out = bytearray(len(data))
        for i, b in enumerate(data):
            t = (self.k2 | 2) & 0xFFFF
            out[i] = b ^ (((t * (t ^ 1)) >> 8) & 0xFF)
            self._update(b)
        return bytes(out)


def _cdb_field(data: bytes, key: str) -> tuple[int, int]:
    count = struct.unpack_from('<I', data, 0)[0]
    keys = [data[4 + 30 * i:4 + 30 * (i + 1)].rstrip(b'\0').decode('ascii', 'replace') for i in range(count)]
    sizes = list(struct.unpack_from(f'<{count}I', data, 4 + 30 * count))
    if key not in keys:
        raise ValueError(f"cdb has no '{key}' column")
    index = keys.index(key)
    return 4 + 34 * count + sum(sizes[:index]), sizes[index]


def cdb_get_string(data: bytes, key: str) -> str:
    offset, size = _cdb_field(data, key)
    return data[offset:offset + size].split(b'\0')[0].decode('ascii')


def cdb_set_string(data: bytes, key: str, value: str) -> bytes:
    offset, size = _cdb_field(data, key)
    raw = value.encode('ascii')
    if len(raw) >= size:
        raise ValueError(f"'{value}' does not fit in {size} bytes")
    return data[:offset] + raw.ljust(size, b'\0') + data[offset + size:]


def patch_version_entries(dip: Path) -> list[str]:
    with zipfile.ZipFile(dip) as z:
        return [i.filename for i in z.infolist() if i.filename.replace('\\', '/').split('/')[-1].lower() == PATCH_VERSION_CDB]


def read_pi_versions(dip: Path, password: bytes) -> dict[str, str]:
    with zipfile.ZipFile(dip) as z:
        return {name: cdb_get_string(z.read(name, pwd=password), PI_VERSION_KEY) for name in patch_version_entries(dip)}


def write_pi_version(source: Path, target: Path, password: bytes, version: str) -> dict[str, str]:
    names = patch_version_entries(source)
    if not names:
        raise ValueError(f'{source} contains no {PATCH_VERSION_CDB}')
    raw = source.read_bytes()
    with zipfile.ZipFile(source) as z:
        infos = z.infolist()
        replaced = {n: cdb_set_string(z.read(n, pwd=password), PI_VERSION_KEY, version) for n in names}
    out = bytearray()
    central = bytearray()
    for info in infos:
        offset = len(out)
        if info.filename in replaced:
            local, crc, csize, usize = _encrypted_entry(raw, info, replaced[info.filename], password)
        else:
            local, crc, csize, usize = _raw_entry(raw, info)
        out += local
        central += _central_record(info, crc, csize, usize, offset)
    end = struct.pack('<IHHHHIIH', 0x06054B50, 0, 0, len(infos), len(infos), len(central), len(out), 0)
    target.write_bytes(bytes(out + central + end))
    result = read_pi_versions(target, password)
    _check_unchanged(source, target, password, set(names))
    return result


def _raw_entry(raw: bytes, info: zipfile.ZipInfo) -> tuple[bytes, int, int, int]:
    start = info.header_offset
    name_len, extra_len = struct.unpack_from('<HH', raw, start + 26)
    end = start + 30 + name_len + extra_len + info.compress_size
    return raw[start:end], info.CRC, info.compress_size, info.file_size


def _encrypted_entry(raw: bytes, info: zipfile.ZipInfo, content: bytes, password: bytes) -> tuple[bytes, int, int, int]:
    if info.flag_bits & 0x8 or info.compress_type not in (zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED):
        raise ValueError(f'unsupported entry layout for {info.filename}')
    crc = zlib.crc32(content) & 0xFFFFFFFF
    if info.compress_type == zipfile.ZIP_DEFLATED:
        packer = zlib.compressobj(9, zlib.DEFLATED, -15)
        body = packer.compress(content) + packer.flush()
    else:
        body = content
    if info.flag_bits & 0x1:
        seed = hashlib.sha256(info.filename.encode() + content).digest()[:11]
        body = _ZipCrypto(password).encrypt(seed + bytes([crc >> 24]) + body)
    start = info.header_offset
    fixed = bytearray(raw[start:start + 30])
    name_len, extra_len = struct.unpack_from('<HH', fixed, 26)
    name_extra = raw[start + 30:start + 30 + name_len + extra_len]
    struct.pack_into('<III', fixed, 14, crc, len(body), len(content))
    return bytes(fixed) + name_extra + body, crc, len(body), len(content)


def _central_record(info: zipfile.ZipInfo, crc: int, csize: int, usize: int, offset: int) -> bytes:
    name = info.orig_filename.encode('utf-8' if info.flag_bits & 0x800 else 'cp437')
    dos_time = info.date_time[3] << 11 | info.date_time[4] << 5 | info.date_time[5] // 2
    dos_date = (info.date_time[0] - 1980) << 9 | info.date_time[1] << 5 | info.date_time[2]
    return struct.pack('<IBBHHHHHIIIHHHHHII', 0x02014B50, info.create_version, info.create_system,
                       info.extract_version, info.flag_bits, info.compress_type, dos_time, dos_date,
                       crc, csize, usize, len(name), len(info.extra), len(info.comment), 0,
                       info.internal_attr, info.external_attr, offset) + name + info.extra + info.comment


def _check_unchanged(source: Path, target: Path, password: bytes, replaced: set[str]) -> None:
    with zipfile.ZipFile(source) as a, zipfile.ZipFile(target) as b:
        if [i.filename for i in a.infolist()] != [i.filename for i in b.infolist()]:
            raise ValueError('rewritten cgd.dip has a different entry list')
        for info in a.infolist():
            if info.filename not in replaced and a.read(info, pwd=password) != b.read(info.filename, pwd=password):
                raise ValueError(f'rewritten cgd.dip changed {info.filename}')
        b.setpassword(password)
        if b.testzip() is not None:
            raise ValueError('rewritten cgd.dip failed the CRC test')
