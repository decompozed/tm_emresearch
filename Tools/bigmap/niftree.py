"""Prints the scene graph of a NIF (children, effects, properties, extra data) down to a depth."""
import argparse

from nifio import Nif


def label(nif, i):
    b = nif.blocks[i]
    name = nif.name(b)
    t = b.data.get('Translation')
    pos = ' @(%.0f, %.0f, %.0f)' % (t['x'], t['y'], t['z']) if t else ''
    extra = ''
    if b.base.endswith('ExtraData') and 'String Data' in b.data:
        extra = ' = %r' % nif.string(b.data['String Data']['Index'])
    return '%d %s %r%s%s' % (i, b.type.split('\x01')[0], name, pos, extra)


def show(nif, i, depth, max_depth, props):
    b = nif.blocks[i]
    print('  ' * depth + label(nif, i))
    if depth >= max_depth:
        return
    keys = ['Children', 'Effects']
    if props:
        keys += ['Properties', 'Extra Data List']
    for k in keys:
        v = b.data.get(k)
        refs = v if isinstance(v, list) else ([v] if isinstance(v, int) and k == 'Shell' else [])
        for c in refs:
            if c >= 0:
                if k != 'Children':
                    print('  ' * (depth + 1) + '[%s]' % k)
                show(nif, c, depth + 1 + (k != 'Children'), max_depth, props)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('nif')
    ap.add_argument('--depth', type=int, default=2)
    ap.add_argument('--props', action='store_true')
    a = ap.parse_args()
    nif = Nif.load(a.nif)
    for r in nif.roots:
        show(nif, r, 0, a.depth, a.props)


if __name__ == '__main__':
    main()
