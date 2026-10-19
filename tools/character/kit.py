# The outfit kit: 14 pieces shared by all skins. Each skin wears its own combination (see OUTFITS
# in skins_catalog.py) and the game colours them from the skin's palette by zone.
#
# Pieces are built around the athletic body: tubes are wrapped a few millimetres outside the body
# surface (rays cast from the bone axis find it), and pads, pouches and buckles are rounded boxes
# placed on the surface. Skin weights are copied from the body under each piece, so sleeves bend
# exactly like arms. The heavy and slim builds get reshaped copies (builds.displace).
import math

import numpy as np
from mathutils import Vector

from meshutil import (MeshBuilder, wrap_tube, make_object, transfer_weights, chain_weights,
                      Z_MAIN, Z_SECOND, Z_ACCENT, Z_LEATHER, Z_METAL, Z_DARK, Z_LENS)

KIT = ['jacket', 'hood', 'vest', 'pauldrons', 'gauntlets', 'shinguards', 'boots', 'belt', 'cape', 'scarf',
       'backpack', 'goggles', 'mask', 'holster']
HEAD_PIECES = {'goggles', 'mask'}   # the head is the same in every build: one mesh serves all


# ------------------------------------------------------------------ shape helpers
def superellipsoid(mb, center, ax, ay, az, e=0.35, rings=8, segs=16, zone=Z_MAIN, front_zone=None, front_dot=0.7):
    """A rounded box (e < 1 squarer, e = 1 an ellipsoid). ax/ay/az are the half-size axis vectors.
    front_zone colours the faces pointing along +az (a lens, a pad face)."""
    c = Vector(center)
    sgn = lambda x: math.copysign(abs(x) ** e, x)
    rows = []
    for i in range(rings + 1):
        th = -math.pi / 2 + math.pi * i / rings
        row = []
        for j in range(segs):
            ph = 2 * math.pi * j / segs
            p = c + ax * (sgn(math.cos(th)) * sgn(math.cos(ph))) + ay * (sgn(math.cos(th)) * sgn(math.sin(ph))) + az * sgn(math.sin(th))
            row.append(mb.vert(p))
        rows.append(row)
    start = len(mb.f)
    mb.grid(rows, zone, closed=True)
    if front_zone is not None:
        n = az.normalized()
        for k in range(start, len(mb.f)):
            ctr = sum((Vector(mb.v[i]) for i in mb.f[k]), Vector()) / len(mb.f[k])
            if (ctr - c).normalized().dot(n) > front_dot:
                mb.zone[k] = front_zone


def dome(mb, center, axis, radii, cap=1.3, rings=7, segs=20, zone=Z_MAIN, rim_zone=Z_ACCENT, thickness=0.008):
    """A shell cap around `axis` (a shoulder pad, a knee cap), cap = half-angle in radians."""
    c, a = Vector(center), Vector(axis).normalized()
    ref = Vector((0, 0, 1)) if abs(a.z) < 0.9 else Vector((1, 0, 0))
    u = (ref - a * ref.dot(a)).normalized()
    w = a.cross(u)
    rows = []
    for i in range(rings + 1):
        th = cap * i / rings
        row = []
        for j in range(segs):
            ph = 2 * math.pi * j / segs
            d = a * math.cos(th) + (u * math.cos(ph) + w * math.sin(ph)) * math.sin(th)
            p = c + Vector((d.x * radii[0], d.y * radii[1], d.z * radii[2]))
            rim = (cap - th) * max(radii)
            row.append(mb.vert(p, rim))
        rows.append(row)
    mb.grid(rows, zone, closed=True)
    # Thick rolled rim.
    edge = rows[-1]
    inner = [mb.vert(c + (Vector(mb.v[k]) - c) * (1 - thickness / max(radii)), 0.0) for k in edge]
    mb.grid([edge, inner], rim_zone, closed=True)


def tube_along(mb, path, radius_fn, segs=12, zone=Z_MAIN, cap=True):
    """A tube following a polyline (the hood roll). radius_fn(i) gives the radius at point i."""
    P = [Vector(p) for p in path]
    rows = []
    for i, p in enumerate(P):
        t = (P[min(i + 1, len(P) - 1)] - P[max(i - 1, 0)]).normalized()
        ref = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
        u = (ref - t * ref.dot(t)).normalized()
        w = t.cross(u)
        r = radius_fn(i)
        rows.append([mb.vert(p + (u * math.cos(2 * math.pi * j / segs) + w * math.sin(2 * math.pi * j / segs)) * r) for j in range(segs)])
    mb.grid(rows, zone, closed=True)
    if cap:
        for end, flip in ((0, True), (len(rows) - 1, False)):
            ctr = mb.vert(P[end], 0.0)
            ring = rows[end]
            for j in range(segs):
                tri = (ring[j], ring[(j + 1) % segs], ctr)
                mb.face(tri[::-1] if flip else tri, zone)


def ribbon(mb, bvh, path, width, offset, zone, back_zone=None, thickness=0.004):
    """A strap lying on the body surface along `path` (points near the surface)."""
    pts, nrm = [], []
    for p in path:
        loc, n, _, _ = bvh.find_nearest(Vector(p))
        pts.append(loc + n * offset)
        nrm.append(n)
    left, right, bl, br = [], [], [], []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        side = t.cross(nrm[i]).normalized() * (width / 2)
        left.append(mb.vert(p - side, 0.0))
        right.append(mb.vert(p + side, 0.0))
        bl.append(mb.vert(p - side - nrm[i] * thickness, 0.0))
        br.append(mb.vert(p + side - nrm[i] * thickness, 0.0))
    for i in range(len(pts) - 1):
        mb.face((left[i], right[i], right[i + 1], left[i + 1]), zone)
        mb.face((bl[i + 1], br[i + 1], br[i], bl[i]), back_zone or zone)
        mb.face((left[i + 1], bl[i + 1], bl[i], left[i]), zone)
        mb.face((right[i], br[i], br[i + 1], right[i + 1]), zone)


def surface_frame(bvh, origin, direction):
    """Where a ray from inside the body leaves it: (point, outward normal)."""
    hit = bvh.ray_cast(Vector(origin), Vector(direction).normalized(), 1.0)
    if hit[0] is None:
        return Vector(origin) + Vector(direction).normalized() * 0.15, Vector(direction).normalized()
    return hit[0], hit[1]


def pad_on(mb, bvh, origin, direction, half_w, half_h, depth, zone, up=Vector((0, 0, 1)), e=0.3, front_zone=None):
    """A rounded box sitting on the body where the ray leaves it (pouches, plates, buckles)."""
    p, n = surface_frame(bvh, origin, direction)
    up = Vector(up)
    x = up.cross(n).normalized()
    y = n.cross(x).normalized()
    superellipsoid(mb, p + n * depth, x * half_w, y * half_h, n * depth, e=e, rings=6, segs=12, zone=zone,
                   front_zone=front_zone)


# ------------------------------------------------------------------ the pieces
def build_pieces(J, HK, bvh):
    """Returns {piece: (MeshBuilder, weight spec)} for the athletic body. weight spec is
    ('body',) to copy body weights, ('rigid', bone) or ('chain', bones, heads, anchor_bone, anchor_above)."""
    hz = J['Head'][0][2]
    V = lambda name: Vector(J[name][0])
    out = {}

    # Jacket: torso, stand-up collar open at the front, sleeves to the elbow, trim on every edge.
    mb = MeshBuilder()
    wrap_tube(mb, bvh, (0, 0, 0.965), (0, 0, 1.6), 0.0, 1.0, 16, 40, 0.017, Z_MAIN, rim_zone=Z_ACCENT,
              radius_fn=lambda t, a, r: r * (1.0 + 0.06 * max(0.0, 0.15 - t) / 0.15))
    wrap_tube(mb, bvh, (0, 0, 1.57), (0, 0, 1.69), 0.0, 1.0, 5, 28, 0.02, Z_MAIN, arc=(0.55, 2 * math.pi - 0.55),
              rim_zone=Z_ACCENT, radius_fn=lambda t, a, r: r + 0.025 * t)
    for s in 'LR':
        wrap_tube(mb, bvh, V('Shoulder_' + s), V('Elbow_' + s), -0.12, 0.97, 11, 20, 0.015, Z_MAIN, rim_zone=Z_ACCENT,
                  fallback=0.07, radius_fn=lambda t, a, r: r * (1.0 + 0.1 * max(0.0, t - 0.85) / 0.12))
    out['jacket'] = (mb, ('body',))

    # Hood, down: a soft roll behind the neck with a bag hanging down the upper back.
    mb = MeshBuilder()
    path = []
    for k in range(17):
        phi = -1.9 + 3.8 * k / 16    # 0 = straight back
        r = 0.115 + 0.02 * math.cos(phi)
        path.append((math.sin(phi) * r, 0.02 + math.cos(phi) * r, 1.6 - 0.07 * math.cos(phi / 2) ** 4))
    tube_along(mb, path, lambda i: 0.032 + 0.035 * math.cos((i - 8) / 8 * math.pi / 2) ** 2, segs=14)
    superellipsoid(mb, (0, 0.17, 1.47), Vector((0.1, 0, 0)), Vector((0, 0.045, 0.02)), Vector((0, -0.01, 0.1)), e=0.8,
                   rings=6, segs=14, zone=Z_MAIN)
    out['hood'] = (mb, ('rigid', 'Chest'))

    # Tactical vest: thick torso shell, chest plates, a row of pouches, buckles.
    mb = MeshBuilder()
    wrap_tube(mb, bvh, (0, 0, 1.05), (0, 0, 1.55), 0.0, 1.0, 12, 40, 0.028, Z_MAIN, rim_zone=Z_LEATHER, thickness=0.012)
    for sx in (1, -1):
        pad_on(mb, bvh, (0.0, 0, 1.39), (0.42 * sx, -1, 0), 0.06, 0.055, 0.016, Z_MAIN, e=0.25)
        pad_on(mb, bvh, (0.0, 0, 1.37), (0.3 * sx, 1, 0), 0.065, 0.075, 0.014, Z_MAIN, e=0.25)
    for x in (-0.6, 0.0, 0.6):
        pad_on(mb, bvh, (0, 0, 1.13), (x, -1, 0), 0.034, 0.04, 0.02, Z_SECOND, e=0.3)
        pad_on(mb, bvh, (0, 0, 1.165), (x, -1, 0), 0.036, 0.012, 0.022, Z_LEATHER, e=0.3)
    for sx in (1, -1):
        pad_on(mb, bvh, (0, 0, 1.25), (sx, -0.3, 0), 0.012, 0.018, 0.034, Z_METAL, e=0.3)
    out['vest'] = (mb, ('body',))

    # Pauldrons: two overlapping plates per shoulder.
    mb = MeshBuilder()
    for s, sx in (('L', 1), ('R', -1)):
        sh = V('Shoulder_' + s)
        # The shoulder surface is ~0.085 m from the joint: plates sit just outside it, wrapping down.
        dome(mb, sh + Vector((0.01 * sx, 0, 0.005)), (0.35 * sx, 0, 1), (0.108, 0.1, 0.098), cap=1.4)
        dome(mb, sh + Vector((0.035 * sx, 0, -0.05)), (1.0 * sx, 0, 0.3), (0.092, 0.092, 0.085), cap=1.2)
        pad_on(mb, bvh, sh, (0.2 * sx, -1, 0.1), 0.012, 0.03, 0.01, Z_LEATHER)   # strap to the chest
    out['pauldrons'] = (mb, ('body',))

    # Gauntlets: forearm shells with a flared cuff and two straps.
    mb = MeshBuilder()
    for s in 'LR':
        e, w = V('Elbow_' + s), V('Wrist_' + s)
        wrap_tube(mb, bvh, e, w, 0.3, 1.03, 9, 18, 0.016, Z_MAIN, fallback=0.05, rim_zone=Z_ACCENT,
                  radius_fn=lambda t, a, r: r * (1.0 + 0.3 * max(0.0, 0.45 - t) / 0.15))
        for t in (0.62, 0.86):
            wrap_tube(mb, bvh, e, w, t - 0.025, t + 0.025, 3, 18, 0.022, Z_LEATHER, fallback=0.05, rim_zone=Z_METAL)
    out['gauntlets'] = (mb, ('body',))

    # Shin guards: open at the back, with a knee cap and straps round the calf.
    mb = MeshBuilder()
    for s in 'LR':
        k, a = V('Knee_' + s), V('Ankle_' + s)
        wrap_tube(mb, bvh, k, a, 0.06, 0.8, 10, 14, 0.016, Z_MAIN, fallback=0.06, arc=(-1.75, 1.75), rim_zone=Z_ACCENT)
        dome(mb, k + Vector((0, -0.055, 0.0)), (0, -1, 0.15), (0.075, 0.06, 0.075), cap=1.15)
        for t in (0.3, 0.65):
            wrap_tube(mb, bvh, k, a, t - 0.02, t + 0.02, 3, 16, 0.011, Z_LEATHER, fallback=0.06, rim_zone=Z_METAL)
    out['shinguards'] = (mb, ('body',))

    # Boots: a chunky shell over ankle and foot, a thick sole, a trim at the top.
    mb = MeshBuilder()
    for s in 'LR':
        k, a, t = V('Knee_' + s), V('Ankle_' + s), V('Toe_' + s)
        # Shaft round the lower shin (stops above the ankle, where rays would leave through the foot).
        wrap_tube(mb, bvh, k, a, 0.66, 0.9, 6, 18, 0.02, Z_LEATHER, fallback=0.06, rim_zone=Z_ACCENT)
        # The shoe: a rounded shape enclosing ankle and foot, and a thick sole under it.
        shoe = Vector((a.x, (a.y + t.y) * 0.5 - 0.01, 0.085))
        superellipsoid(mb, shoe, Vector((0.078, 0, 0)), Vector((0, 0.14, 0)), Vector((0, 0, 0.075)), e=0.55, rings=10,
                       segs=20, zone=Z_LEATHER)
        superellipsoid(mb, Vector((shoe.x, shoe.y - 0.005, 0.018)), Vector((0.082, 0, 0)), Vector((0, 0.15, 0)),
                       Vector((0, 0, 0.02)), e=0.25, rings=6, segs=20, zone=Z_DARK)
    out['boots'] = (mb, ('body',))

    # Belt: a leather band, a metal buckle and two pouches.
    mb = MeshBuilder()
    wrap_tube(mb, bvh, (0, 0, 0.99), (0, 0, 1.055), 0.0, 1.0, 4, 44, 0.013, Z_LEATHER, rim_zone=Z_LEATHER, thickness=0.004)
    pad_on(mb, bvh, (0, 0, 1.022), (0, -1, 0), 0.03, 0.024, 0.012, Z_METAL, e=0.25)
    for sx in (1, -1):
        pad_on(mb, bvh, (0, 0, 1.0), (0.8 * sx, 0.6, 0), 0.04, 0.05, 0.026, Z_SECOND, e=0.3)
        pad_on(mb, bvh, (0, 0, 1.045), (0.8 * sx, 0.6, 0), 0.042, 0.014, 0.03, Z_LEATHER, e=0.3)
    out['belt'] = (mb, ('body',))

    # Cape: hangs from the shoulders to the knees, lining in the accent colour on the inside.
    mb = MeshBuilder()
    rows, inner = [], []
    nr, nc = 14, 13
    for i in range(nr):
        s = i / (nr - 1)
        z = 1.56 - 0.98 * s
        hw = 0.2 + 0.17 * s
        row, irow = [], []
        for j in range(nc):
            a = -1 + 2 * j / (nc - 1)
            y = 0.13 + 0.12 * s - (0.07 * a * a) * (1 - s) ** 3 + 0.03 * (1 - a * a)
            p = Vector((a * hw, y, z))
            rim = min(1 - abs(a), s, 1 - s) * 0.3
            row.append(mb.vert(p, rim))
            irow.append(mb.vert(p + Vector((0, -0.006, 0)), rim))
        rows.append(row)
        inner.append(irow)
    mb.grid(rows, Z_MAIN)              # rows run down, columns left to right: faces point back (+Y)
    mb.grid(inner, Z_ACCENT, flip=True)
    for i in range(nr - 1):   # side edges
        for side in (0, nc - 1):
            q = (rows[i][side], rows[i + 1][side], inner[i + 1][side], inner[i][side])
            mb.face(q if side == 0 else q[::-1], Z_ACCENT)
    for side in range(nc - 1):  # bottom edge
        q = (rows[-1][side], rows[-1][side + 1], inner[-1][side + 1], inner[-1][side])
        mb.face(q[::-1], Z_ACCENT)
    out['cape'] = (mb, ('chain', ['Cape_1', 'Cape_2', 'Cape_3', 'Cape_3'], 'Cape', 'Chest', 1.5))

    # Scarf: a thick wrap round the neck and a tail down the back.
    mb = MeshBuilder()
    wrap_tube(mb, bvh, (0, 0, 1.555), (0, 0, 1.675), 0.0, 1.0, 6, 32, 0.03, Z_ACCENT,
              offset_fn=lambda t, a: 0.026 + 0.012 * math.sin(3 * a + t * 5) * math.sin(t * math.pi))
    tail = [(0.05 + 0.01 * k, 0.115 + 0.02 * k, 1.585 - 0.07 * k) for k in range(6)]
    ribbon(mb, bvh, tail, 0.07, 0.02, Z_ACCENT, thickness=0.01)
    out['scarf'] = (mb, ('scarf',))

    # Backpack: a rounded pack, a top flap, side pockets and two shoulder straps.
    mb = MeshBuilder()
    superellipsoid(mb, (0, 0.215, 1.3), Vector((0.15, 0, 0)), Vector((0, 0, 0.19)), Vector((0, 0.075, 0)), e=0.35, rings=10,
                   segs=20, zone=Z_SECOND)
    superellipsoid(mb, (0, 0.205, 1.45), Vector((0.155, 0, 0)), Vector((0, 0, 0.05)), Vector((0, 0.088, 0)), e=0.3, rings=6,
                   segs=16, zone=Z_MAIN)
    for sx in (1, -1):
        superellipsoid(mb, (0.16 * sx, 0.215, 1.24), Vector((0.03, 0, 0)), Vector((0, 0, 0.08)), Vector((0, 0.05, 0)), e=0.4,
                       rings=6, segs=12, zone=Z_MAIN)
        strap = [(0.09 * sx, 0.14, 1.5), (0.12 * sx, 0.08, 1.6), (0.14 * sx, 0.0, 1.61), (0.13 * sx, -0.08, 1.55),
                 (0.12 * sx, -0.12, 1.42), (0.12 * sx, -0.12, 1.3), (0.13 * sx, -0.09, 1.2)]
        ribbon(mb, bvh, strap, 0.04, 0.012, Z_LEATHER)
        pad_on(mb, bvh, (0.1 * sx, 0, 1.33), (0.1 * sx, -1, 0), 0.016, 0.012, 0.012, Z_METAL)
    out['backpack'] = (mb, ('backpack',))

    # Goggles on the forehead: a strap round the head, two lenses in dark frames.
    mb = MeshBuilder()
    hc = Vector((0, 0.01, hz))
    wrap_tube(mb, bvh, hc + Vector((0, 0, 0.035 * HK)), hc + Vector((0, 0, 0.08 * HK)), 0.0, 1.0, 3, 32, 0.006, Z_SECOND,
              rim_zone=Z_SECOND, thickness=0.003)
    for sx in (1, -1):
        p, n = surface_frame(bvh, hc + Vector((0, 0, 0.06 * HK)), (0.42 * sx, -1, 0.15))
        x = Vector((0, 0, 1)).cross(n).normalized()
        y = n.cross(x).normalized()
        superellipsoid(mb, p + n * 0.012, x * 0.03, y * 0.024, n * 0.014, e=0.45, rings=6, segs=14, zone=Z_DARK,
                       front_zone=Z_LENS, front_dot=0.6)
    out['goggles'] = (mb, ('rigid', 'Head'))

    # Bandana mask over the nose and mouth.
    mb = MeshBuilder()
    mouth_z = hz - 0.065 * HK
    wrap_tube(mb, bvh, (0, 0.012, mouth_z - 0.045), (0, 0.012, hz - 0.012 * HK), 0.0, 1.0, 6, 24, 0.007, Z_MAIN,
              arc=(-1.55, 1.55), rim_zone=Z_SECOND, thickness=0.003)
    out['mask'] = (mb, ('rigid', 'Head'))

    # Holster: a thigh strap with a pouch on the outside of the right leg.
    mb = MeshBuilder()
    h, k = V('Hip_R'), V('Knee_R')
    for t in (0.3, 0.55):
        wrap_tube(mb, bvh, h, k, t - 0.025, t + 0.025, 3, 20, 0.01, Z_LEATHER, fallback=0.09, rim_zone=Z_METAL)
    ax = (k - h).normalized()
    c = h + (k - h) * 0.42
    superellipsoid(mb, c + Vector((-0.115, 0.0, 0)), Vector((0, 0.06, 0)), ax * 0.085, Vector((-0.03, 0, 0)), e=0.3, rings=6,
                   segs=14, zone=Z_LEATHER)
    superellipsoid(mb, c + Vector((-0.122, 0.0, 0.05)), Vector((0, 0.062, 0)), ax * 0.03, Vector((-0.032, 0, 0)), e=0.3, rings=4,
                   segs=12, zone=Z_SECOND)
    out['holster'] = (mb, ('body',))
    return out


def piece_weights(name, spec, points, body, bvh, W, names, J, chains):
    """Weight matrix (points x len(names)) for a piece, or ('rigid', bone)."""
    kind = spec[0]
    if kind == 'rigid':
        return ('rigid', spec[1])
    base = transfer_weights(points, body, bvh, W, names)
    if kind == 'body':
        return base
    ext = list(names)
    def col(bone):
        if bone not in ext:
            ext.append(bone)
        return ext.index(bone)
    if kind == 'chain':
        _, bones, prefix, anchor, above = spec
        heads = [Vector(p) for p in chains[prefix][1]]
        cw = chain_weights(points, bones, heads)
        out = np.zeros((len(points), len(names) + 8))
        for i, p in enumerate(points):
            k = min(1.0, max(0.0, (p[2] - above) / 0.06))   # top of the cape stays on the chest
            out[i, col(anchor)] += k
            for b, wv in zip(bones, cw[i]):
                out[i, col(b)] += wv * (1 - k)
        return out[:, :len(ext)], ext
    if kind == 'scarf':
        # The wrap follows the neck; the tail (below the wrap) swings on the scarf chain.
        heads = [Vector(p) for p in chains['Scarf'][1]]
        cw = chain_weights(points, ['Scarf_1', 'Scarf_2', 'Scarf_2'], heads)
        out = np.zeros((len(points), len(names) + 4))
        out[:, :len(names)] = base
        for i, p in enumerate(points):
            if p[2] < 1.555 and p[1] > 0.08:
                out[i, :] = 0
                for b, wv in zip(['Scarf_1', 'Scarf_2', 'Scarf_2'], cw[i]):
                    out[i, col(b)] += wv
        return out[:, :len(ext)], ext
    if kind == 'backpack':
        # The pack rides on the chest bone; the straps bend with the body.
        out = np.zeros((len(points), len(names)))
        out[:, :] = base
        ci = names.index('Chest')
        for i, p in enumerate(points):
            if p[1] > 0.13 and p[2] > 1.05:
                out[i, :] = 0
                out[i, ci] = 1
        return out
    return base
