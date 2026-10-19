# Mesh helpers for the outfit kit, hair cards and face parts: building objects from arrays,
# wrapping tubes around the body, and copying skin weights from the body.
#
# Every generated mesh carries two extra layers the atlas baker reads:
#   face attribute  "zone"  what the face is made of (see ZONES), used for colour and materials
#   point attribute "rim"   metres from the nearest open edge (stitching runs along rims)
import math

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

# Zones: what a texel is. The game colours each zone from the skin's palette (see the shader).
# Z_BODY and Z_BODY_SKIN take their colour from the painted skin texture instead (Z_BODY_SKIN is
# shaded as skin); Z_SKIN on the body marks the painted face features hidden under the 3D face.
# Keep in step with the zone list in src/platform/renderer_d3d11.cpp.
Z_CUT, Z_MAIN, Z_SECOND, Z_ACCENT, Z_LEATHER, Z_METAL, Z_HAIR, Z_SKIN, Z_EYE, Z_DARK, Z_LIPS, Z_BODY, Z_LENS, Z_BODY_SKIN = range(14)

FAR_RIM = 1.0


class MeshBuilder:
    """Collects vertices, faces, per-face zones and per-vertex rim distances."""

    def __init__(self):
        self.v = []
        self.f = []
        self.zone = []
        self.rim = []
        self.fuv = []   # per face: None (unwrapped later) or one UV per corner (hair cards)

    def vert(self, p, rim=FAR_RIM):
        self.v.append(tuple(p))
        self.rim.append(rim)
        return len(self.v) - 1

    def face(self, idx, zone, uv=None):
        self.f.append(tuple(idx))
        self.zone.append(zone)
        self.fuv.append(uv)

    def grid(self, rows, zone, closed=False, flip=False):
        """Quads between consecutive rows of vertex indices. closed: rows wrap around."""
        for r in range(len(rows) - 1):
            a, b = rows[r], rows[r + 1]
            n = len(a) if closed else len(a) - 1
            for j in range(n):
                j1 = (j + 1) % len(a)
                q = (a[j], a[j1], b[j1], b[j])
                self.face(q[::-1] if flip else q, zone)

    def merge(self, other):
        off = len(self.v)
        self.v += other.v
        self.rim += other.rim
        self.f += [tuple(i + off for i in f) for f in other.f]
        self.zone += other.zone
        self.fuv += other.fuv

    def transform(self, fn):
        self.v = [tuple(fn(Vector(p))) for p in self.v]


def body_bvh(body):
    mesh = body.data
    return BVHTree.FromPolygons([v.co[:] for v in mesh.vertices], [p.vertices[:] for p in mesh.polygons], all_triangles=False)


def surface_radius(bvh, origin, direction, fallback, max_dist=0.6):
    """Distance from `origin` (inside the body) along `direction` to the body's surface."""
    hit = bvh.ray_cast(Vector(origin), Vector(direction).normalized(), max_dist)
    return (hit[0] - Vector(origin)).length if hit[0] is not None else fallback


def frame(axis):
    """Two unit vectors perpendicular to `axis`: `u` points to the character's front (-Y) where it
    can, `w` completes a right-handed frame."""
    axis = Vector(axis).normalized()
    ref = Vector((0, -1, 0)) if abs(axis.y) < 0.9 else Vector((0, 0, 1))
    u = (ref - axis * ref.dot(axis)).normalized()
    w = axis.cross(u).normalized()
    return u, w


def wrap_tube(mb, bvh, a, b, t0, t1, rings, segs, offset, zone, fallback=0.1, arc=None, radius_fn=None,
              offset_fn=None, cap_rims=True, rim_zone=None, thickness=0.006):
    """A sleeve-like shell around the body along a->b (from t0 to t1 of the segment).
    arc = (start, end) radians limits it to part of the way round (0 = front). radius_fn(t, ang, r)
    and offset_fn(t, ang) can reshape it. Returns the vertex index rows (outer surface)."""
    a, b = Vector(a), Vector(b)
    axis = b - a
    u, w = frame(axis)
    length_m = axis.length * (t1 - t0)
    closed = arc is None
    angs = [2 * math.pi * j / segs for j in range(segs)] if closed else \
        [arc[0] + (arc[1] - arc[0]) * j / (segs - 1) for j in range(segs)]
    rows = []
    for i in range(rings):
        t = t0 + (t1 - t0) * i / (rings - 1)
        c = a + axis * t
        dist_end = min(i, rings - 1 - i) / (rings - 1) * length_m
        row = []
        for ang in angs:
            d = u * math.cos(ang) + w * math.sin(ang)
            r = surface_radius(bvh, c, d, fallback)
            if radius_fn:
                r = radius_fn(t, ang, r)
            off = offset_fn(t, ang) if offset_fn else offset
            rim = dist_end
            if not closed:
                rim = min(rim, min(abs(ang - arc[0]), abs(arc[1] - ang)) * r)
            row.append(mb.vert(c + d * (r + off), rim))
        rows.append(row)
    mb.grid(rows, zone, closed=closed)
    if cap_rims:
        # Fold each open end inward a little so the shell has visible thickness.
        rz = zone if rim_zone is None else rim_zone
        for end, sign in ((0, 1), (rings - 1, -1)):
            inner = []
            for vi in rows[end]:
                p = Vector(mb.v[vi])
                t = t0 + (t1 - t0) * end / (rings - 1)
                c = a + axis * t
                inward = (c - p)
                inward = inward - axis.normalized() * inward.dot(axis.normalized())
                inner.append(mb.vert(p + inward.normalized() * thickness, 0.0))
            if sign > 0:
                mb.grid([inner, rows[end]], rz, closed=closed)
            else:
                mb.grid([rows[end], inner], rz, closed=closed)
    return rows


def fix_normals(obj, away_from=None, facing=None, zones=None):
    """Makes each connected part's faces point outward (Blender's recalc), then optionally forces
    some faces (by zone) to point away from a point or along a direction (open sheets)."""
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if away_from is not None or facing is not None:
        zl = bm.faces.layers.int.get('zone')
        for f in bm.faces:
            if zones is not None and zl is not None and f[zl] not in zones:
                continue
            if away_from is not None:
                ref = min(away_from, key=lambda c: (f.calc_center_median() - Vector(c)).length)
                want = f.calc_center_median() - Vector(ref)
            else:
                want = Vector(facing)
            if f.normal.dot(want) < 0:
                f.normal_flip()
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def body_weights(body):
    """(vertex count x bone count) weight matrix of the body, and the bone names."""
    names = [g.name for g in body.vertex_groups]
    W = np.zeros((len(body.data.vertices), len(names)))
    for v in body.data.vertices:
        for g in v.groups:
            W[v.index, g.group] = g.weight
    return W, names


def transfer_weights(points, body, bvh, W, names):
    """Weights for arbitrary points copied from the nearest place on the body surface."""
    mesh = body.data
    out = np.zeros((len(points), len(names)))
    for i, p in enumerate(points):
        loc, _, poly, _ = bvh.find_nearest(Vector(p))
        if poly is None:
            continue
        vids = mesh.polygons[poly].vertices
        d = np.array([(mesh.vertices[k].co - loc).length for k in vids]) + 1e-5
        wv = 1.0 / d
        wv /= wv.sum()
        out[i] = (W[list(vids)] * wv[:, None]).sum(0)
    return out


def make_object(name, mb, arm, weights=None, names=None, rigid=None, smooth=True, uv=None):
    """Creates the mesh object, parented to the armature with skin weights. Either
    weights/names (a weight matrix) or rigid (one bone name for everything)."""
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(mb.v, [], mb.f)
    mesh.update()
    za = mesh.attributes.new('zone', 'INT', 'FACE')
    za.data.foreach_set('value', mb.zone)
    ra = mesh.attributes.new('rim', 'FLOAT', 'POINT')
    ra.data.foreach_set('value', mb.rim)
    for p in mesh.polygons:
        p.use_smooth = smooth
    uvl = mesh.uv_layers.new(name='UVMap')
    if any(u is not None for u in mb.fuv):
        ca = mesh.attributes.new('card', 'INT', 'FACE')
        ca.data.foreach_set('value', [0 if u is None else 1 for u in mb.fuv])
        for p, uvs in zip(mesh.polygons, mb.fuv):
            if uvs is None:
                continue
            for li, uv in zip(p.loop_indices, uvs):
                uvl.data[li].uv = uv
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    if rigid:
        vg = obj.vertex_groups.new(name=rigid)
        vg.add(list(range(len(mesh.vertices))), 1.0, 'REPLACE')
    elif weights is not None:
        groups = {}
        for i in range(len(mb.v)):
            row = weights[i]
            top = np.argsort(row)[::-1][:4]
            total = row[top].sum()
            if total <= 1e-8:
                continue
            for k in top:
                if row[k] <= 1e-4:
                    continue
                n = names[k]
                if n not in groups:
                    groups[n] = obj.vertex_groups.new(name=n)
                groups[n].add([i], float(row[k] / total), 'REPLACE')
    mod = obj.modifiers.new('Armature', 'ARMATURE')
    mod.object = arm
    obj.parent = arm
    return obj


def chain_weights(points, bones, heads):
    """Weights that blend along a chain of bones by distance along it (capes, scarf tails, long
    hair). heads = head position of each bone; the first bone is the anchor (rigid parent)."""
    pts = np.asarray(points, float)
    H = np.asarray(heads, float)
    out = np.zeros((len(pts), len(bones)))
    for i, p in enumerate(pts):
        # Project on the polyline through the heads.
        best = (1e9, 0, 0.0)
        for k in range(len(H) - 1):
            d = H[k + 1] - H[k]
            t = float(np.clip((p - H[k]) @ d / max(d @ d, 1e-9), 0, 1))
            dist = np.linalg.norm(H[k] + d * t - p)
            if dist < best[0]:
                best = (dist, k, t)
        _, k, t = best
        out[i, k] += 1 - t
        out[i, min(k + 1, len(bones) - 1)] += t
    return out
