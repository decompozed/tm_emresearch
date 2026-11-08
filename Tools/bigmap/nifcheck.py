"""Integrity checks on a parsed NIF: link ranges/types, string indices, reachability from the roots."""
import sys

from nifio import Nif


def problems(nif):
    out = []
    n, ns = len(nif.blocks), len(nif.strings)
    used_strings = set()
    edges = {}

    for i, b in enumerate(nif.blocks):
        kids = edges.setdefault(i, [])

        def fn(kind, v, i=i, b=b, kids=kids):
            if kind == 'str':
                if v != -1 and not 0 <= v < ns:
                    out.append('block %d %s: string %d out of range' % (i, b.type, v))
                used_strings.add(v)
            elif v != -1:
                if not 0 <= v < n:
                    out.append('block %d %s: %s %d out of range' % (i, b.type, kind, v))
                else:
                    kids.append(v)
            return v
        nif.links(b, fn)
    for r in nif.roots:
        if not 0 <= r < n:
            out.append('root %d out of range' % r)
    seen, stack = set(), [r for r in nif.roots if 0 <= r < n]
    while stack:
        k = stack.pop()
        if k in seen:
            continue
        seen.add(k)
        stack += edges.get(k, [])
    unreached = n - len(seen)
    unused = ns - len(used_strings & set(range(ns)))
    return out, unreached, unused


def main():
    bad = 0
    for path in sys.argv[1:]:
        nif = Nif.load(path)
        errs, unreached, unused = problems(nif)
        print('%s: blocks %d strings %d errors %d unreached %d unused strings %d'
              % (path, len(nif.blocks), len(nif.strings), len(errs), unreached, unused))
        for e in errs[:20]:
            print('  ', e)
        bad += len(errs)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
