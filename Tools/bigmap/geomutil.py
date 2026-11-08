"""Small geometry helpers: triangle normals, box clipping, polygon triangulation, ray casts."""
import math

EPS = 1e-6


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def unit(v):
    n = math.sqrt(dot(v, v))
    return (v[0] / n, v[1] / n, v[2] / n) if n > EPS else (0.0, 0.0, 0.0)


def normal(t):
    return unit(cross(sub(t[1], t[0]), sub(t[2], t[0])))


def area(t):
    return 0.5 * math.sqrt(dot(*(2 * [cross(sub(t[1], t[0]), sub(t[2], t[0]))])))


def inside_box(p, box, tol=1e-3):
    lo, hi = box
    return all(lo[k] - tol <= p[k] <= hi[k] + tol for k in range(3))


def overlaps_box(t, box):
    lo, hi = box
    return all(max(p[k] for p in t) >= lo[k] and min(p[k] for p in t) <= hi[k] for k in range(3))


def _split(poly, axis, value, keep_below):
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        da = (value - a[axis]) if keep_below else (a[axis] - value)
        db = (value - b[axis]) if keep_below else (b[axis] - value)
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            f = da / (da - db)
            out.append(tuple(a[k] + f * (b[k] - a[k]) for k in range(3)))
    return out


def fan(poly):
    return [(poly[0], poly[i], poly[i + 1]) for i in range(1, len(poly) - 1)]


def subtract_box(t, box, min_area=0.01):
    lo, hi = box
    pieces = []
    rest = [tuple(p) for p in t]
    for axis in range(3):
        for value, keep_below in ((lo[axis], True), (hi[axis], False)):
            if len(rest) < 3:
                break
            outside = _split(rest, axis, value, keep_below)
            if len(outside) >= 3:
                pieces.append(outside)
            rest = _split(rest, axis, value, not keep_below)
    inside = sum(area(x) for x in fan(rest)) if len(rest) >= 3 else 0.0
    return [x for p in pieces for x in fan(p) if area(x) > min_area], inside


def signed_area2d(poly):
    return 0.5 * sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
                     for i in range(len(poly)))


def ccw(poly):
    return list(poly) if signed_area2d(poly) > 0 else list(reversed(poly))


def triangulate(poly):
    idx = list(range(len(poly)))
    out = []

    def cr(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = poly[i0], poly[i1], poly[i2]
            if cr(a, b, c) <= EPS:
                continue
            if any(cr(a, b, poly[j]) >= -EPS and cr(b, c, poly[j]) >= -EPS and cr(c, a, poly[j]) >= -EPS
                   for j in idx if j not in (i0, i1, i2)):
                continue
            out.append((i0, i1, i2))
            idx.pop(k)
            break
        else:
            raise ValueError('polygon is not simple or not counter-clockwise')
    out.append(tuple(idx))
    return out


def ray_triangle(o, d, t, two_sided=True):
    e1, e2 = sub(t[1], t[0]), sub(t[2], t[0])
    p = cross(d, e2)
    det = dot(e1, p)
    if abs(det) < 1e-12 or (not two_sided and det < 0):
        return None
    inv = 1.0 / det
    s = sub(o, t[0])
    u = dot(s, p) * inv
    if u < -1e-9 or u > 1 + 1e-9:
        return None
    q = cross(s, e1)
    v = dot(d, q) * inv
    if v < -1e-9 or u + v > 1 + 1e-9:
        return None
    dist = dot(e2, q) * inv
    return dist if dist >= 0 else None


class TriIndex:

    def __init__(self, tris, cell=250.0):
        self.cell, self.tris, self.grid = cell, tris, {}
        for k, (_, t) in enumerate(tris):
            for key in self._cells(min(p[0] for p in t), max(p[0] for p in t), min(p[1] for p in t), max(p[1] for p in t)):
                self.grid.setdefault(key, []).append(k)

    def _cells(self, x0, x1, y0, y1):
        c = self.cell
        return [(i, j) for i in range(int(math.floor(x0 / c)), int(math.floor(x1 / c)) + 1)
                for j in range(int(math.floor(y0 / c)), int(math.floor(y1 / c)) + 1)]

    def segment(self, a, b):
        d = sub(b, a)
        cand = set()
        steps = max(1, int(math.sqrt(d[0] ** 2 + d[1] ** 2) / (self.cell / 2)) + 1)
        for s in range(steps + 1):
            x, y = a[0] + d[0] * s / steps, a[1] + d[1] * s / steps
            cand.update(self.grid.get((int(math.floor(x / self.cell)), int(math.floor(y / self.cell))), ()))
        best = None
        for k in cand:
            tag, t = self.tris[k]
            f = ray_triangle(a, d, t)
            if f is not None and f <= 1 and (best is None or f < best[0]):
                best = (f, tag, t)
        return best

    def column(self, x, y):
        out = []
        for k in self.grid.get((int(math.floor(x / self.cell)), int(math.floor(y / self.cell))), ()):
            tag, t = self.tris[k]
            f = ray_triangle((x, y, -1e5), (0, 0, 2e5), t)
            if f is not None and f <= 1:
                out.append((-1e5 + 2e5 * f, tag, normal(t)[2]))
        return sorted(out)
