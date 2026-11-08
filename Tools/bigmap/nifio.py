"""Gamebryo NIF 20.x reader/writer driven by the niftools schema (nif.xml, GPL-3.0: downloaded at first use, not
kept in the repo; NIFXML=<path> uses a local copy)."""
import hashlib
import os
import re
import struct
import urllib.request
import xml.etree.ElementTree as ET

NIFXML_COMMIT = '292bb9403cbf4052c58d66e80906b6bde1700779'
NIFXML_SHA256 = '752c79786ceb217641fd7e94be4033898c97e9aa38025bf05f2cbee9cffbbf0d'
NIFXML_URL = 'https://raw.githubusercontent.com/niftools/nifxml/%s/nif.xml' % NIFXML_COMMIT
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.cache', 'nif-%s.xml' % NIFXML_COMMIT[:12])


def schema_path():
    if os.environ.get('NIFXML'):
        return os.environ['NIFXML']
    if not os.path.exists(CACHE):
        data = urllib.request.urlopen(NIFXML_URL, timeout=60).read()
        if hashlib.sha256(data).hexdigest() != NIFXML_SHA256:
            raise RuntimeError('nif.xml download does not match the pinned hash')
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        with open(CACHE, 'wb') as f:
            f.write(data)
    return CACHE

BASIC = {
    'uint64': 'Q', 'int64': 'q', 'ulittle32': 'I', 'uint': 'I', 'int': 'i', 'ushort': 'H', 'short': 'h',
    'char': 'b', 'sbyte': 'b', 'byte': 'B', 'normbyte': 'b', 'bool': 'B', 'BlockTypeIndex': 'H',
    'FileVersion': 'I', 'float': 'f', 'hfloat': 'H', 'Ptr': 'i', 'Ref': 'i', 'StringOffset': 'I',
    'NiFixedString': 'i',
}
RAW_ARRAY = {'byte', 'char', 'sbyte', 'normbyte'}
LINKS = {'Ref': 'ref', 'Ptr': 'ptr', 'NiFixedString': 'str'}
OPS = {'#ADD#': '+', '#SUB#': '-', '#MUL#': '*', '#DIV#': '/', '#AND#': '&&', '#OR#': '||', '#LT#': '<',
       '#GT#': '>', '#LTE#': '<=', '#GTE#': '>=', '#EQ#': '==', '#NEQ#': '!=', '#RSH#': '>>', '#LSH#': '<<',
       '#BITAND#': '&', '#BITOR#': '|'}
TOKEN_RE = re.compile(r'\d+\.\d+\.\d+\.\d+|0x[0-9A-Fa-f]+|\d+\.\d*(?:e[+-]?\d+)?|\d+|&&|\|\||==|!=|<=|>=|<<|>>'
                      r'|[<>&|+\-*/!()]|[A-Za-z_][A-Za-z0-9_ \\:]*')
PY_OPS = {'&&': ' and ', '||': ' or ', '!': ' not ', '/': '//'}


def version_num(text):
    parts = [int(p) for p in text.split('.')]
    parts += [0] * (4 - len(parts))
    return parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3]


class Field:
    __slots__ = ('name', 'type', 'template', 'arg', 'length', 'width', 'cond', 'abstract')

    def __init__(self, e, schema, struct_name):
        self.name = e.get('name')
        self.type = e.get('type')
        self.template = e.get('template')
        self.abstract = e.get('abstract') == 'true'
        ctx = (schema, struct_name)
        self.arg = schema.expr(e.get('arg') or e.get('arg1'), ctx)
        self.length = schema.expr(e.get('length'), ctx)
        self.width = schema.expr(e.get('width'), ctx)
        self.cond = schema.expr(e.get('cond'), ctx)


class Schema:
    """nif.xml compiled for one (version, user version) pair; BS version is always 0 for these files."""

    def __init__(self, version, user=0, path=None):
        self.version, self.user = version, user
        root = ET.parse(path or schema_path()).getroot()
        self.tokens = []
        for tok in root.iter('token'):
            for t in tok:
                self.tokens.append((t.get('token'), t.get('string')))
        self.version_ids = {v.get('id'): version_num(v.get('num')) for v in root.iter('version')}
        self.my_ids = {v.get('id') for v in root.iter('version')
                       if version_num(v.get('num')) == version and str(user) in (v.get('user') or '0').split()}
        self.enums, self.bitfields, self.consts = {}, {}, {}
        for tag in ('enum', 'bitflags'):
            for e in root.iter(tag):
                self.enums[e.get('name')] = e.get('storage')
                for o in e.iter('option'):
                    v = o.get('value')
                    self.consts.setdefault(o.get('name'), int(v, 0) if v else 1 << int(o.get('bit')))
        for e in root.iter('bitfield'):
            self.bitfields[e.get('name')] = (e.get('storage'), {m.get('name'): (int(m.get('mask'), 0), int(m.get('pos')))
                                                              for m in e.iter('member')})
        self.xml = {e.get('name'): e for tag in ('struct', 'niobject') for e in root.iter(tag)}
        self.field_names = {n: {f.get('name') for f in self._chain(n) if f.get('name')} for n in self.xml}
        self.fields = {}

    def _chain(self, name):
        e = self.xml[name]
        own = list(e.iter('field'))
        if e.tag == 'niobject' and e.get('inherit'):
            return self._chain(e.get('inherit')) + own
        return own

    def _vnum(self, text):
        return self.version_ids[text] if text in self.version_ids else version_num(text)

    def _present(self, e):
        if e.get('since') and self.version < self._vnum(e.get('since')):
            return False
        if e.get('until') and self.version > self._vnum(e.get('until')):
            return False
        if e.get('versions'):
            ids = set()
            for v in e.get('versions').split():
                ids |= set(self._expand(v).split())
            if not ids & self.my_ids:
                return False
        if e.get('vercond') and not self.expr(e.get('vercond'), (self, ''))({}, 0):
            return False
        return True

    def struct_fields(self, name):
        if name not in self.fields:
            self.fields[name] = [Field(e, self, name) for e in self._chain(name) if self._present(e)]
        return self.fields[name]

    def _expand(self, s):
        prev = None
        while prev != s:
            prev = s
            s = s.replace('#VER#', str(self.version)).replace('#USER#', str(self.user)).replace('#BSVER#', '0')
            for k, v in OPS.items():
                s = s.replace(k, v)
            for k, v in self.tokens:
                if k not in ('#VER#', '#USER#', '#BSVER#', '#ARG#'):
                    s = s.replace(k, v)
        return s

    def expr(self, text, ctx):
        if not text:
            return None
        _, struct_name = ctx
        names = self.field_names.get(struct_name, set())
        out = []
        for tok in TOKEN_RE.findall(self._expand(text).replace('#ARG#', 'ARG')):
            tok = tok.strip()
            if re.fullmatch(r'\d+\.\d+\.\d+\.\d+', tok):
                out.append(str(version_num(tok)))
            elif tok in PY_OPS:
                out.append(PY_OPS[tok])
            elif tok == 'ARG':
                out.append('ARG')
            elif tok == 'INFINITY':
                out.append('float("inf")')
            elif re.match(r'[A-Za-z_]', tok):
                out.append(self._ident(tok, names, struct_name))
            else:
                out.append(tok)
        return eval('lambda d, ARG: ' + ''.join(out), {'_m': lambda v, m, p: (v & m) >> p})

    def _ident(self, tok, names, struct_name):
        if '\\' in tok:
            head, member = tok.split('\\', 1)
            ftype = next((f.get('type') for f in self._chain(struct_name) if f.get('name') == head), None)
            mask, pos = self.bitfields[ftype][1][member]
            return '_m(d.get(%r, 0), %d, %d)' % (head, mask, pos)
        if tok in names or tok not in self.consts:
            return 'd.get(%r, 0)' % tok
        return str(self.consts[tok])

    def storage(self, t):
        while t not in BASIC:
            t = self.enums.get(t) or self.bitfields[t][0]
        return t


class Reader:
    def __init__(self, data, schema):
        self.d, self.o, self.s = data, 0, schema

    def basic(self, t, n=None):
        code = BASIC[t]
        if n is None:
            v = struct.unpack_from('<' + code, self.d, self.o)[0]
            self.o += struct.calcsize(code)
            return v
        if t in RAW_ARRAY:
            v = bytes(self.d[self.o:self.o + n]); self.o += n
            return v
        v = list(struct.unpack_from('<%d%s' % (n, code), self.d, self.o))
        self.o += n * struct.calcsize(code)
        return v

    def value(self, t, T=None, ARG=0):
        if t == '#T#':
            t = T
        if t in BASIC or t in self.s.enums or t in self.s.bitfields:
            return self.basic(self.s.storage(t))
        if t == 'SizedString':
            n = self.basic('uint'); v = bytes(self.d[self.o:self.o + n]); self.o += n
            return v
        if t == 'DataStreamData':
            v = bytes(self.d[self.o:self.o + ARG]); self.o += ARG
            return v
        return self.struct(t, T, ARG)

    def struct(self, name, T=None, ARG=0):
        d = {}
        for f in self.s.struct_fields(name):
            if f.abstract or (f.cond and not f.cond(d, ARG)):
                continue
            ft = T if f.type == '#T#' else f.type
            sub_t = T if f.template == '#T#' else f.template
            arg = f.arg(d, ARG) if f.arg else 0
            if f.length is None:
                d[f.name] = self.value(ft, sub_t, arg)
                continue
            n = f.length(d, ARG)
            if f.width is not None:
                w = f.width(d, ARG)
                d[f.name] = [self.array(ft, w[i] if isinstance(w, list) else w, sub_t, arg) for i in range(n)]
            else:
                d[f.name] = self.array(ft, n, sub_t, arg)
        return d

    def array(self, t, n, T, ARG):
        st = self.s.storage(t) if (t in BASIC or t in self.s.enums or t in self.s.bitfields) else None
        if st:
            return self.basic(st, n)
        return [self.value(t, T, ARG) for _ in range(n)]


class Writer:
    def __init__(self, schema):
        self.out, self.s = bytearray(), schema

    def basic(self, t, v):
        code = BASIC[t]
        if isinstance(v, (bytes, bytearray)):
            self.out += v
        elif isinstance(v, list):
            self.out += struct.pack('<%d%s' % (len(v), code), *v)
        else:
            self.out += struct.pack('<' + code, v)

    def value(self, t, v, T=None, ARG=0):
        if t == '#T#':
            t = T
        if t in BASIC or t in self.s.enums or t in self.s.bitfields:
            self.basic(self.s.storage(t), v)
        elif t == 'SizedString':
            self.out += struct.pack('<I', len(v)) + v
        elif t == 'DataStreamData':
            self.out += v
        else:
            self.struct(t, v, T, ARG)

    def struct(self, name, d, T=None, ARG=0):
        for f in self.s.struct_fields(name):
            if f.abstract or (f.cond and not f.cond(d, ARG)):
                continue
            ft = T if f.type == '#T#' else f.type
            sub_t = T if f.template == '#T#' else f.template
            arg = f.arg(d, ARG) if f.arg else 0
            v = d[f.name]
            if f.length is None:
                self.value(ft, v, sub_t, arg)
                continue
            n = f.length(d, ARG)
            assert len(v) == n, (name, f.name, len(v), n)
            rows = v if f.width is not None else [v]
            for row in rows:
                self.array(ft, row, sub_t, arg)

    def array(self, t, v, T, ARG):
        if t in BASIC or t in self.s.enums or t in self.s.bitfields:
            self.basic(self.s.storage(t), v)
        else:
            for x in v:
                self.value(t, x, T, ARG)


def visit(schema, name, d, fn, T=None, ARG=0):
    for f in schema.struct_fields(name):
        if f.abstract or (f.cond and not f.cond(d, ARG)):
            continue
        ft = T if f.type == '#T#' else f.type
        sub_t = T if f.template == '#T#' else f.template
        arg = f.arg(d, ARG) if f.arg else 0
        v = d[f.name]
        if ft in LINKS:
            kind = LINKS[ft]
            if f.length is None:
                d[f.name] = fn(kind, v)
            elif f.width is not None:
                d[f.name] = [[fn(kind, x) for x in row] for row in v]
            else:
                d[f.name] = [fn(kind, x) for x in v]
        elif ft in schema.xml and ft not in ('DataStreamData',):
            items = v if f.length is None else (v if f.width is None else [x for row in v for x in row])
            for x in ([items] if f.length is None else items):
                visit(schema, ft, x, fn, sub_t, arg)


class Block:
    __slots__ = ('type', 'flag', 'data', 'raw')

    def __init__(self, type_name, data, flag=0, raw=None):
        self.type, self.data, self.flag, self.raw = type_name, data, flag, raw

    @property
    def base(self):
        return self.type.split('\x01')[0]


class Nif:
    def __init__(self):
        self.header_string = b''
        self.version = self.user = 0
        self.endian = 1
        self.blocks, self.strings, self.groups, self.roots = [], [], [], []
        self.max_string_length = None
        self.schema = None

    @classmethod
    def read(cls, data, schema_cache=None):
        n = cls()
        nl = data.index(b'\n')
        n.header_string = data[:nl]
        o = nl + 1
        n.version, n.endian, n.user, nblocks = struct.unpack_from('<IBII', data, o); o += 13
        n.schema = _schema(n.version, n.user, schema_cache)
        ntypes = struct.unpack_from('<H', data, o)[0]; o += 2
        types = []
        for _ in range(ntypes):
            k = struct.unpack_from('<I', data, o)[0]; types.append(data[o + 4:o + 4 + k].decode('latin1')); o += 4 + k
        tindex = struct.unpack_from('<%dH' % nblocks, data, o); o += 2 * nblocks
        sizes = struct.unpack_from('<%dI' % nblocks, data, o); o += 4 * nblocks
        nstrings, n.max_string_length = struct.unpack_from('<II', data, o); o += 8
        for _ in range(nstrings):
            k = struct.unpack_from('<I', data, o)[0]; n.strings.append(data[o + 4:o + 4 + k].decode('latin1')); o += 4 + k
        ngroups = struct.unpack_from('<I', data, o)[0]; o += 4
        n.groups = list(struct.unpack_from('<%dI' % ngroups, data, o)); o += 4 * ngroups
        n.type_order = types
        for ti, size in zip(tindex, sizes):
            tname = types[ti & 0x7FFF]
            body = data[o:o + size]
            r = Reader(body, n.schema)
            parsed = r.struct(tname.split('\x01')[0])
            if r.o != size:
                raise ValueError('%s: parsed %d of %d bytes' % (tname, r.o, size))
            n.blocks.append(Block(tname, parsed, ti & 0x8000))
            o += size
        nroots = struct.unpack_from('<I', data, o)[0]; o += 4
        n.roots = list(struct.unpack_from('<%di' % nroots, data, o)); o += 4 * nroots
        if o != len(data):
            raise ValueError('%d trailing bytes' % (len(data) - o))
        return n

    @classmethod
    def load(cls, path, schema_cache=None):
        with open(path, 'rb') as f:
            return cls.read(f.read(), schema_cache)

    def block_bytes(self, b):
        w = Writer(self.schema)
        w.struct(b.base, b.data)
        return bytes(w.out)

    def to_bytes(self):
        types = list(getattr(self, 'type_order', []))
        for b in self.blocks:
            if b.type not in types:
                types.append(b.type)
        used = {b.type for b in self.blocks}
        types = [t for t in types if t in used]
        bodies = [self.block_bytes(b) for b in self.blocks]
        out = bytearray(self.header_string + b'\n')
        out += struct.pack('<IBII', self.version, self.endian, self.user, len(self.blocks))
        out += struct.pack('<H', len(types))
        for t in types:
            e = t.encode('latin1'); out += struct.pack('<I', len(e)) + e
        out += b''.join(struct.pack('<H', types.index(b.type) | b.flag) for b in self.blocks)
        out += b''.join(struct.pack('<I', len(x)) for x in bodies)
        enc = [s.encode('latin1') for s in self.strings]
        out += struct.pack('<II', len(enc), max([len(s) for s in enc], default=0))
        for s in enc:
            out += struct.pack('<I', len(s)) + s
        out += struct.pack('<I', len(self.groups)) + b''.join(struct.pack('<I', g) for g in self.groups)
        out += b''.join(bodies)
        out += struct.pack('<I', len(self.roots)) + b''.join(struct.pack('<i', r) for r in self.roots)
        return bytes(out)

    def save(self, path):
        with open(path, 'wb') as f:
            f.write(self.to_bytes())

    def links(self, b, fn):
        visit(self.schema, b.base, b.data, fn)

    def string(self, i):
        return self.strings[i] if 0 <= i < len(self.strings) else None

    def name(self, b):
        v = b.data.get('Name')
        return self.string(v['Index']) if isinstance(v, dict) else None

    def find(self, name, base=None):
        return [i for i, b in enumerate(self.blocks) if self.name(b) == name and (base is None or b.base == base)]


def _schema(version, user, cache):
    key = (version, user)
    if cache is None:
        cache = _SCHEMAS
    if key not in cache:
        cache[key] = Schema(version, user)
    return cache[key]


_SCHEMAS = {}
