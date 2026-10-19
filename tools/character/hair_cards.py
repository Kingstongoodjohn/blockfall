# Hair made of cards: many thin strips with a see-through strand texture, layered over a solid
# scalp cap (so gaps never show bare head). Four styles, same names as the old solid hair:
#   Hair0 spiky clumps, Hair1 short flat layers, Hair2 mohawk, Hair3 long (swings on HairTail_*).
# Card UVs point into the strand tiles of the shared atlas (tile k: u in TILE_U0 + k*TILE_W,
# v from 1 at the root to 0.75 at the tip); atlas.py paints those tiles.
import math
import random

import numpy as np
from mathutils import Vector

from meshutil import MeshBuilder, make_object, chain_weights, Z_HAIR

TILE_U0, TILE_W, TILE_COUNT = 0.5, 0.0625, 8
TILE_V_ROOT, TILE_V_TIP = 1.0, 0.75


class Scalp:
    """Ray-casts the head from its centre to find the scalp."""

    def __init__(self, bvh, centre):
        self.bvh, self.c = bvh, Vector(centre)

    def point(self, theta, phi, lift=0.0):
        """theta from straight up, phi round from the front (-Y), toward the character's left (+X)."""
        d = Vector((math.sin(theta) * math.sin(phi), -math.sin(theta) * math.cos(phi), math.cos(theta)))
        hit = self.bvh.ray_cast(self.c, d, 0.5)
        p = hit[0] if hit[0] is not None else self.c + d * 0.13
        n = hit[1] if hit[0] is not None else d
        return p + n * lift, n

    def project(self, p, lift):
        loc, n, _, _ = self.bvh.find_nearest(Vector(p))
        return loc + n * lift, n


def hairline_theta(scalp, phi, front_z, side_z, back_z):
    """How far down the cap goes at angle phi: front, sides and back hairline heights."""
    c = math.cos(phi)
    z_line = (front_z * c + side_z * (1 - c)) if c > 0 else (back_z * (-c) + side_z * (1 + c))
    lo, hi = 0.0, 2.6
    for _ in range(24):
        mid = (lo + hi) / 2
        if scalp.point(mid, phi)[0].z > z_line:
            lo = mid
        else:
            hi = mid
    return lo


def cap(mb, scalp, lift, front_z, side_z, back_z, half_width=None, rings=9, segs=28):
    """The solid scalp cap. half_width (metres) limits it to a strip over the top (mohawk)."""
    rows = []
    for i in range(1, rings + 1):
        row = []
        for j in range(segs):
            phi = 2 * math.pi * j / segs
            tmax = hairline_theta(scalp, phi, front_z, side_z, back_z)
            p, _ = scalp.point(tmax * i / rings, phi, lift)
            if half_width is not None:
                p.x = max(-half_width, min(half_width, p.x))
            row.append(mb.vert(p, (rings - i) / rings * 0.05))
        rows.append(row)
    top, _ = scalp.point(0.0, 0.0, lift)
    t = mb.vert(top)
    first = rows[0]
    for j in range(segs):
        mb.face((first[j], first[(j + 1) % segs], t)[::-1], Z_HAIR)
    mb.grid(rows, Z_HAIR, closed=True, flip=True)


def card(mb, path, w_root, w_tip, side_hint, tile, segs=None):
    """One strip along `path`, `side_hint` sets which way it is wide. Returns its vertex indices."""
    P = [Vector(p) for p in path]
    n = len(P)
    u0 = TILE_U0 + tile * TILE_W + 0.002
    u1 = u0 + TILE_W - 0.004
    left, right = [], []
    for i, p in enumerate(P):
        t = (P[min(i + 1, n - 1)] - P[max(i - 1, 0)]).normalized()
        side = Vector(side_hint)
        side = (side - t * side.dot(t)).normalized()
        w = w_root + (w_tip - w_root) * (i / (n - 1))
        left.append(mb.vert(p - side * w / 2))
        right.append(mb.vert(p + side * w / 2))
    vs = lambda i: TILE_V_ROOT + (TILE_V_TIP - TILE_V_ROOT) * i / (n - 1)
    for i in range(n - 1):
        mb.face((left[i], right[i], right[i + 1], left[i + 1]), Z_HAIR,
                uv=[(u0, vs(i)), (u1, vs(i)), (u1, vs(i + 1)), (u0, vs(i + 1))])
    return left + right


def surface_path(scalp, start, direction, steps, step_len, lift, fall_after=None, gravity=0.0):
    """Walks over the scalp from `start` in `direction`, staying `lift` above it. After
    `fall_after` steps the path leaves the head and droops (long hair)."""
    pts = []
    p = Vector(start)
    d = Vector(direction).normalized()
    for k in range(steps):
        pts.append(p.copy())
        if fall_after is not None and k >= fall_after:
            d = (d + Vector((0, 0.05, -gravity))).normalized()
            p = p + d * step_len
            continue
        q, n = scalp.project(p + d * step_len, lift)
        d = (q - p).normalized()
        p = q
    return pts


def build_hair(bvh, arm, J, HK, chains):
    hz = J['Head'][0][2]
    scalp = Scalp(bvh, (0, 0.01, hz + 0.01))
    rng = random.Random(5)
    objs = []
    front_z, side_z, back_z = hz + 0.085 * HK, hz + 0.03 * HK, hz - 0.045 * HK

    # 0: spiky clumps swept up and back.
    mb = MeshBuilder()
    cap(mb, scalp, 0.004, front_z, side_z, back_z)
    for k in range(30):
        th = math.radians(rng.uniform(8, 62))
        ph = rng.uniform(-math.pi, math.pi) * (0.75 if k % 3 else 1.0)
        root, n = scalp.point(th, ph, 0.002)
        d = (n + Vector((0, 0.9, 0.5))).normalized()
        length = rng.uniform(0.055, 0.095)
        path = [root + d * (length * s / 4) + Vector((0, 0.012, 0)) * (s / 4) ** 2 for s in range(5)]
        side = n.cross(d)
        for rot in (0.0, math.pi / 2):
            sh = side * math.cos(rot) + n * math.sin(rot)
            card(mb, path, 0.03, 0.004, sh, k % TILE_COUNT)
    objs.append(make_object('Hair0', mb, arm, rigid='Head'))

    # 1: short, flat layers combed back.
    mb = MeshBuilder()
    cap(mb, scalp, 0.004, front_z, side_z, back_z)
    for layer in range(3):
        for k in range(26):
            ph = -math.pi + 2 * math.pi * (k + 0.5 * layer) / 26
            th = hairline_theta(scalp, ph, front_z, side_z, back_z) * rng.uniform(0.25, 0.85)
            start, n = scalp.point(th * 0.4, ph * 0.5, 0.006 + 0.003 * layer)
            end_dir = Vector((math.sin(ph), -math.cos(ph) * 0.4 + 0.6, -0.5)).normalized()
            path = surface_path(scalp, start, end_dir, 6, 0.022, 0.006 + 0.003 * layer)
            card(mb, path, 0.028, 0.012, n.cross(end_dir), (k + layer) % TILE_COUNT)
    objs.append(make_object('Hair1', mb, arm, rigid='Head'))

    # 2: mohawk: a crest of upright fins on a narrow strip, sides shaved.
    mb = MeshBuilder()
    cap(mb, scalp, 0.004, front_z, side_z, back_z, half_width=0.022)
    for k in range(18):
        s = k / 17
        th = -0.55 + 1.75 * s   # front to back over the top (negative = front)
        root, n = scalp.point(abs(th), 0.0 if th < 0 else math.pi, 0.002)
        up = (n + Vector((0, 0.5, 0.2))).normalized()
        length = 0.07 + 0.06 * math.sin(math.pi * min(1.0, s * 1.2))
        path = [root + up * (length * i / 4) + Vector((0, 0.02, 0)) * (i / 4) ** 2 for i in range(5)]
        for off in (-0.004, 0.004):
            card(mb, [p + Vector((off, 0, 0)) for p in path], 0.05, 0.008, Vector((0, 1, 0)).cross(up).cross(up),
                 k % TILE_COUNT)
    objs.append(make_object('Hair2', mb, arm, rigid='Head'))

    # 3: long hair down the back: layers over the top and strands that swing on HairTail_*.
    mb = MeshBuilder()
    cap(mb, scalp, 0.004, front_z, side_z, back_z)
    for k in range(30):
        ph = -math.pi + 2 * math.pi * k / 30
        start, n = scalp.point(0.25, ph * 0.3, 0.007)
        d = Vector((math.sin(ph) * 0.8, 0.6 + 0.4 * abs(math.cos(ph)), -0.25)).normalized()
        path = surface_path(scalp, start, d, 6, 0.024, 0.007)
        card(mb, path, 0.03, 0.014, n.cross(d), k % TILE_COUNT)
    for k in range(34):
        ph = math.radians(-130 + 260 * k / 33)   # round the sides and back
        th = hairline_theta(scalp, math.pi + ph * 0.9, front_z, side_z, back_z) * 0.75
        start, n = scalp.point(th, math.pi + ph * 0.9, 0.008)
        d = Vector((math.sin(ph) * 0.3, 0.5, -1.0)).normalized()
        path = surface_path(scalp, start, d, 9, 0.04, 0.008, fall_after=2, gravity=0.6)
        card(mb, path, 0.035, 0.012, Vector((0, 0, 1)).cross(d), k % TILE_COUNT)
    heads = [Vector(p) for p in chains['HairTail'][1]]
    pts = np.array(mb.v)
    cw = chain_weights(pts, ['HairTail_1', 'HairTail_2', 'HairTail_3', 'HairTail_3'], heads)
    names = ['Head', 'HairTail_1', 'HairTail_2', 'HairTail_3']
    W = np.zeros((len(pts), len(names)))
    for i, p in enumerate(pts):
        k = min(1.0, max(0.0, (p[2] - (hz - 0.03)) / 0.04))   # on the head: rigid; below: swings
        W[i, 0] = k
        for b, wv in zip(['HairTail_1', 'HairTail_2', 'HairTail_3', 'HairTail_3'], cw[i]):
            W[i, names.index(b)] += wv * (1 - k)
    objs.append(make_object('Hair3', mb, arm, weights=W, names=names))
    return objs
