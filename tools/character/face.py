# Face parts: eyes, eyelids, brows ("Face" mesh) and lips ("Mouth" mesh). They sit where the
# skin painter puts the painted face and move with the face bones (see rigging.py), so the game
# can blink and make expressions. The game hides the painted features under them (atlas zone).
import math

import numpy as np
from mathutils import Vector

from meshutil import MeshBuilder, make_object, Z_SKIN, Z_EYE, Z_DARK, Z_HAIR, Z_LIPS
from kit import superellipsoid

EYE_RX, EYE_RZ = 0.0315, 0.0225   # matches the painted eye (0.042 x 0.030 times the head scale)


def build_face(pts, bvh, arm, J, HK):
    hz = J['Head'][0][2]
    face = MeshBuilder()
    bone_of = []   # bone name per vertex (each face part is rigid on one bone)

    def add(builder_fn, bone):
        start = len(face.v)
        builder_fn()
        bone_of.extend([bone] * (len(face.v) - start))

    for s, sx in (('L', 1), ('R', -1)):
        e = pts['eye_' + s]
        front = e.y - 0.012                       # head surface in front of the eye
        eye_c = Vector((e.x, front + 0.0055, e.z))
        add(lambda: superellipsoid(face, eye_c, Vector((EYE_RX, 0, 0)), Vector((0, 0, EYE_RZ)), Vector((0, -0.0075, 0)),
                                   e=0.9, rings=8, segs=16, zone=Z_EYE), 'Head')

        # Upper eyelid: part of a shell just outside the eye, hinged at the lid bone. At rest it
        # covers the top third of the eye; turning the bone brings it down.
        def lid():
            rx, ry, rz = EYE_RX * 1.12, 0.0105, EYE_RZ * 1.18
            rows = []
            na, nb = 5, 9
            for i in range(na + 1):
                el = math.radians(28 + 60 * i / na)
                row = []
                for j in range(nb + 1):
                    b = math.radians(-82 + 164 * j / nb)
                    p = e + Vector((math.sin(b) * math.cos(el) * rx, -math.cos(b) * math.cos(el) * ry - 0.0105,
                                    math.sin(el) * rz))
                    row.append(face.vert(p))
                rows.append(row)
            face.grid(rows, Z_SKIN)   # rows go up, columns left to right: faces point forward (-Y)
            # Lash line along the lower edge.
            lash = [face.vert(Vector(face.v[k]) + Vector((0, -0.0012, -0.0016))) for k in rows[0]]
            face.grid([lash, rows[0]], Z_DARK)
        add(lid, 'Lid_' + s)

        b = pts['brow_' + s]
        slope = -0.25 * sx
        add(lambda: superellipsoid(face, b + Vector((0, -0.004, 0)), Vector((0.026, 0, 0.026 * slope)), Vector((0, 0, 0.0052)),
                                   Vector((0, -0.0045, 0)), e=0.5, rings=4, segs=10, zone=Z_HAIR), 'Brow_' + s)

    # Lips: a soft ring round the mouth, and a dark mouth inside it.
    mouth = MeshBuilder()
    m = pts['mouth']
    rx, rz, tube = 0.048 * HK, 0.0042, 0.0034   # a wide, flat mouth (a round one looks surprised)
    centre = Vector((0, 0, m.z))
    hit = bvh.ray_cast(Vector((0, -1, m.z)), Vector((0, 1, 0)), 2.0)
    centre.y = (hit[0].y if hit[0] is not None else -0.13) + 0.002
    segs, ts = 24, 8
    rows = []
    for j in range(segs):
        a = 2 * math.pi * j / segs
        ring_c = centre + Vector((math.cos(a) * rx, 0, math.sin(a) * rz))
        out_d = Vector((math.cos(a), 0, math.sin(a) * 1.6)).normalized()
        row = []
        for k in range(ts):
            b = 2 * math.pi * k / ts
            p = ring_c + out_d * math.cos(b) * tube + Vector((0, -1, 0)) * math.sin(b) * tube * 0.9
            row.append(mouth.vert(p))
        rows.append(row)
    # Tube around the ring: transpose so each row runs round the tube.
    for j in range(segs):
        j1 = (j + 1) % segs
        for k in range(ts):
            k1 = (k + 1) % ts
            mouth.face((rows[j][k], rows[j][k1], rows[j1][k1], rows[j1][k]), Z_LIPS)
    inner = [mouth.vert(centre + Vector((math.cos(2 * math.pi * j / segs) * rx * 0.92, 0.003,
                                         math.sin(2 * math.pi * j / segs) * rz * 0.8))) for j in range(segs)]
    c_in = mouth.vert(centre + Vector((0, 0.006, 0)))
    for j in range(segs):
        mouth.face((inner[(j + 1) % segs], inner[j], c_in), Z_DARK)

    # Weights. Face parts are rigid. Lips: the corners follow Mouth_L/R, the lower lip the jaw.
    names = ['Head', 'Jaw', 'Mouth_L', 'Mouth_R', 'Lid_L', 'Lid_R', 'Brow_L', 'Brow_R']
    fw = np.zeros((len(face.v), len(names)))
    for i, bn in enumerate(bone_of):
        fw[i, names.index(bn)] = 1.0
    mw = np.zeros((len(mouth.v), len(names)))
    for i, p in enumerate(mouth.v):
        x = (p[0] - centre.x) / rx
        corner = min(1.0, abs(x) ** 3)
        lower = 1.0 if p[2] < centre.z - 0.001 else 0.0
        side = names.index('Mouth_L') if x > 0 else names.index('Mouth_R')
        mw[i, side] = corner
        mw[i, names.index('Jaw')] = (1 - corner) * lower
        mw[i, names.index('Head')] = (1 - corner) * (1 - lower)
    face_obj = make_object('Face', face, arm, weights=fw, names=names)
    mouth_obj = make_object('Mouth', mouth, arm, weights=mw, names=names)
    return face_obj, mouth_obj


def painted_face_mask(X, Y, Z, front, J, HK):
    """Texels of the painted eyes, brows and mouth (copied from the skin painter's rules). The
    game paints them over with skin tone when the 3D face parts are drawn on top."""
    hz = J['Head'][0][2]
    head = Z > hz - 0.14 * HK
    mask = np.zeros(len(X), bool)
    for sx in (1, -1):
        ex, ez = 0.078 * HK * sx, hz + 0.015 * HK
        de = np.sqrt(((X - ex) / (0.042 * HK)) ** 2 + ((Z - ez) / (0.030 * HK)) ** 2)
        mask |= (de < 1.25) & (front > 0.2) & head
        brow_z = hz + 0.06 * HK - (np.abs(X) - 0.078 * HK) * 0.25
        mask |= (np.abs(Z - brow_z) < 0.014 * HK) & (np.abs(X - ex) < 0.058 * HK) & (front > 0.2) & head
    mouth_z = hz - 0.065 * HK + (X / HK) ** 2
    mask |= (np.abs(Z - mouth_z) < 0.012 * HK) & (np.abs(X) < 0.052 * HK) & (front > 0.25) & head
    return mask
