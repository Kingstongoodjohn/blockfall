# The shared character detail atlas (one normal map + one material map for every skin).
#
# Layout of the second UV set ("Atlas"), in Blender UV space (v = 1 is the top of the image):
#   u 0.0 - 0.5            the body (same islands as the skin textures, packed to fill the half)
#   u 0.5 - 1.0, v < 0.75  outfit kit pieces, scalp caps and face parts
#   u 0.5 - 1.0, v > 0.75  eight hair-strand tiles the hair cards point at
#   u 0.98 - 1.0, v < 0.01 a neutral patch (hats, headband: flat, uses the skin texture's colour)
#
# How the normal map is made: every piece has a "sculpt" (folds, ridges, rivets, muscles...) written
# as a displacement of its surface, plus fine detail (weave, leather grain, stitching, pores). Both
# are evaluated on every atlas texel and their slope becomes the normal: the same result as baking
# a displaced high-poly copy, without the ray misses such a bake has on small or touching parts
# (an earlier Cycles normal bake was tried and dropped for that reason). Ambient occlusion is
# baked with Cycles.
#
# The material map: R roughness, G metalness, B ambient occlusion, A zone * 16 + 8 (meshutil.ZONES).
import math
import os

import bpy
import numpy as np
from mathutils import Vector
from PIL import Image, ImageDraw

from meshutil import (Z_CUT, Z_MAIN, Z_SECOND, Z_ACCENT, Z_LEATHER, Z_METAL, Z_HAIR, Z_SKIN, Z_EYE, Z_DARK, Z_LIPS,
                      Z_BODY, Z_LENS, Z_BODY_SKIN)
import hair_cards

BODY_REGION = (0.0, 0.0, 0.5, 1.0)          # u0, v0, u1, v1
KIT_REGION = (0.5, 0.012, 1.0, 0.75)
NEUTRAL_UV = (0.99, 0.006)
MARGIN_PX = 6
SOFT_PIECES = {'jacket', 'hood', 'vest', 'cape', 'scarf', 'mask', 'backpack', 'belt', 'holster'}


def log(*a):
    print('[atlas]', *a, flush=True)


# ------------------------------------------------------------------ UV layout
def islands(mesh, layer):
    """UV islands of a mesh layer: lists of loop indices (faces sharing a vertex and its UV)."""
    uv = layer.data
    parent = list(range(len(mesh.loops)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    by_vert = {}
    for p in mesh.polygons:
        li = list(p.loop_indices)
        for k in li[1:]:
            union(li[0], k)
        for k in li:
            key = (mesh.loops[k].vertex_index, round(uv[k].uv[0], 5), round(uv[k].uv[1], 5))
            if key in by_vert:
                union(k, by_vert[key])
            else:
                by_vert[key] = k
    groups = {}
    for k in range(len(mesh.loops)):
        groups.setdefault(find(k), []).append(k)
    return list(groups.values())


def pack(entries, region, size):
    """Shelf-packs UV islands into `region`, all at one scale (even texel density).
    entries: (mesh, src_layer, dst_layer, islands). Writes the dst layers."""
    u0, v0, u1, v1 = region
    W, H = u1 - u0, v1 - v0
    m = MARGIN_PX / size
    # Each island is first rescaled so its UV area matches its surface area (smart project sizes
    # every object to fill 0..1, which would give small objects far more texels than big ones).
    boxes = []
    dens = {}
    for e, (mesh, src, dst, isl) in enumerate(entries):
        uv = src.data
        loop_poly = {}
        for p in mesh.polygons:
            for l in p.loop_indices:
                loop_poly[l] = p
        for k, loops in enumerate(isl):
            polys = {loop_poly[l].index: loop_poly[l] for l in loops}.values()
            a3 = sum(p.area for p in polys)
            auv = 0.0
            for p in polys:
                pts = [uv[l].uv for l in p.loop_indices]
                for t in range(1, len(pts) - 1):
                    auv += abs((pts[t][0] - pts[0][0]) * (pts[t + 1][1] - pts[0][1]) - (pts[t + 1][0] - pts[0][0]) * (pts[t][1] - pts[0][1])) / 2
            kd = math.sqrt(a3 / auv) if auv > 1e-12 and a3 > 1e-12 else 1.0
            dens[(e, k)] = kd
            us = [uv[l].uv[0] for l in loops]
            vs = [uv[l].uv[1] for l in loops]
            boxes.append(((max(vs) - min(vs)) * kd, (max(us) - min(us)) * kd, min(us), min(vs), e, k))
    boxes.sort(key=lambda b: -b[0])

    def place(s):
        x = y = 0.0
        shelf = 0.0
        out = []
        for h, w, mu, mv, e, k in boxes:
            bw, bh = w * s + m, h * s + m
            if bw > W:
                return None
            if x + bw > W:
                x, y = 0.0, y + shelf
                shelf = 0.0
            if y + bh > H:
                return None
            out.append((x, y, mu, mv, e, k))
            x += bw
            shelf = max(shelf, bh)
        return out
    lo, hi = 0.01, 20.0
    for _ in range(40):
        mid = (lo + hi) / 2
        if place(mid) is not None:
            lo = mid
        else:
            hi = mid
    spots = place(lo)
    for x, y, mu, mv, e, k in spots:
        mesh, src, dst, isl = entries[e]
        s = lo * dens[(e, k)]
        for l in isl[k]:
            u, v = src.data[l].uv
            dst.data[l].uv = (u0 + x + (u - mu) * s + m / 2, v0 + y + (v - mv) * s + m / 2)
    log('packed %d islands into %s: %.0f texels per metre' % (len(boxes), region, lo * size))


def add_atlas_body(body, size):
    """The body's Atlas UVs: its skin-texture islands repacked into the left half."""
    mesh = body.data
    src = mesh.uv_layers['UVMap']
    dst = mesh.uv_layers.new(name='Atlas')
    pack([(mesh, src, dst, islands(mesh, src))], BODY_REGION, size)
    mesh.uv_layers.active = mesh.uv_layers['UVMap']


def set_neutral_atlas(obj):
    mesh = obj.data
    dst = mesh.uv_layers.get('Atlas') or mesh.uv_layers.new(name='Atlas')
    for l in dst.data:
        l.uv = NEUTRAL_UV
    mesh.uv_layers.active = mesh.uv_layers[0]


def unwrap(obj, only_faces=None):
    """Smart-projects the object (or some of its faces) into its UVMap layer."""
    for o in bpy.context.view_layer.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='DESELECT')
    bpy.ops.object.mode_set(mode='OBJECT')
    for p in obj.data.polygons:
        p.select = only_faces is None or p.index in only_faces
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(62), island_margin=0.002)
    bpy.ops.object.mode_set(mode='OBJECT')


def layout_kit(objs, size, copies):
    """Unwraps the kit/face/scalp objects and packs them into the kit region. Card faces (hair)
    keep their tile UVs. `copies` maps an object to the build variants sharing its UVs."""
    entries = []
    for obj in objs:
        mesh = obj.data
        card = mesh.attributes.get('card')
        free = [p.index for p in mesh.polygons if not (card and card.data[p.index].value)]
        if not free:
            continue
        unwrap(obj, set(free))
        src = mesh.uv_layers['UVMap']
        isl = islands(mesh, src)
        loop_face = {}
        for p in mesh.polygons:
            for l in p.loop_indices:
                loop_face[l] = p.index
        free_set = set(free)
        isl = [i for i in isl if loop_face[i[0]] in free_set]
        dst = mesh.uv_layers.get('Atlas') or mesh.uv_layers.new(name='Atlas')
        for l in range(len(mesh.loops)):
            dst.data[l].uv = src.data[l].uv
        entries.append((mesh, src, dst, isl))
    pack(entries, KIT_REGION, size)
    for obj in objs:
        mesh = obj.data
        if 'Atlas' not in mesh.uv_layers:
            continue
        # The game reads the atlas UVs from both sets for these meshes.
        for l in range(len(mesh.loops)):
            mesh.uv_layers['UVMap'].data[l].uv = mesh.uv_layers['Atlas'].data[l].uv
        for other in copies.get(obj.name, []):
            om = other.data
            for name in ('UVMap', 'Atlas'):
                lay = om.uv_layers.get(name) or om.uv_layers.new(name=name)
                for l in range(len(mesh.loops)):
                    lay.data[l].uv = mesh.uv_layers['Atlas'].data[l].uv
        mesh.uv_layers.active = mesh.uv_layers['UVMap']


# ------------------------------------------------------------------ rasterising
class Raster:
    """For every atlas texel an object covers: which object, where it is on the mesh (rest pose),
    its normal, zone, rim distance and metres per texel."""

    def __init__(self, objs, size):
        self.size = size
        tri_obj, tri_v, tri_uv, tri_zone, tri_mpt = [], [], [], [], []
        self.objs = objs
        self.pos, self.nrm, self.rim = [], [], []
        idx_img = Image.new('I', (size, size), 0)
        draw = ImageDraw.Draw(idx_img)
        base = 0
        for oi, obj in enumerate(objs):
            mesh = obj.data
            mesh.calc_loop_triangles()
            uvl = mesh.uv_layers['Atlas'].data
            zone_a = mesh.attributes.get('zone')
            rim_a = mesh.attributes.get('rim')
            card_a = mesh.attributes.get('card')
            verts = np.array([v.co[:] for v in mesh.vertices])
            norms = np.array([v.normal[:] for v in mesh.vertices])
            rims = np.array([rim_a.data[i].value for i in range(len(mesh.vertices))]) if rim_a else np.ones(len(verts))
            self.pos.append(verts)
            self.nrm.append(norms)
            self.rim.append(rims)
            for lt in mesh.loop_triangles:
                if card_a and card_a.data[lt.polygon_index].value:
                    continue   # hair cards share the strand tiles (painted separately)
                uvs = [uvl[l].uv[:] for l in lt.loops]
                pts = [(u * size, (1 - v) * size) for u, v in uvs]
                base += 1
                draw.polygon(pts, fill=base, outline=base)
                tri_obj.append(oi)
                tri_v.append(lt.vertices[:])
                tri_uv.append(uvs)
                tri_zone.append(zone_a.data[lt.polygon_index].value if zone_a else Z_BODY)
                a3 = np.cross(verts[lt.vertices[1]] - verts[lt.vertices[0]], verts[lt.vertices[2]] - verts[lt.vertices[0]])
                (ua, va), (ub, vb), (uc, vc) = uvs
                auv = abs((ub - ua) * (vc - va) - (uc - ua) * (vb - va)) * size * size
                tri_mpt.append(math.sqrt(max(np.linalg.norm(a3), 1e-12) / max(auv, 1e-9)))
        idx = np.array(idx_img, dtype=np.int32)
        self.ys, self.xs = np.nonzero(idx)
        ti = idx[self.ys, self.xs] - 1
        tri_v = np.array(tri_v, dtype=np.int64)
        tri_uv = np.array(tri_uv, dtype=np.float64)
        self.obj = np.array(tri_obj, dtype=np.int32)[ti]
        self.zone = np.array(tri_zone, dtype=np.int32)[ti]
        self.mpt = np.array(tri_mpt, dtype=np.float64)[ti]
        px = (self.xs + 0.5) / size
        py = 1 - (self.ys + 0.5) / size
        a, b, c = tri_uv[ti, 0], tri_uv[ti, 1], tri_uv[ti, 2]
        v0, v1 = b - a, c - a
        v2 = np.stack([px, py], 1) - a
        d00 = (v0 * v0).sum(1); d01 = (v0 * v1).sum(1); d11 = (v1 * v1).sum(1); d20 = (v2 * v0).sum(1); d21 = (v2 * v1).sum(1)
        den = d00 * d11 - d01 * d01
        den[np.abs(den) < 1e-14] = 1e-14
        w1 = (d11 * d20 - d01 * d21) / den
        w2 = (d00 * d21 - d01 * d20) / den
        w = np.clip(np.stack([1 - w1 - w2, w1, w2], 1), 0, 1)
        w /= np.maximum(w.sum(1, keepdims=True), 1e-9)
        n = len(ti)
        self.P = np.zeros((n, 3)); self.N = np.zeros((n, 3)); self.R = np.zeros(n)
        tv = tri_v[ti]
        for oi in range(len(objs)):
            sel = self.obj == oi
            if not sel.any():
                continue
            V, Nn, Rr = self.pos[oi], self.nrm[oi], self.rim[oi]
            for k in range(3):
                self.P[sel] += V[tv[sel, k]] * w[sel, k:k + 1]
                self.N[sel] += Nn[tv[sel, k]] * w[sel, k:k + 1]
                self.R[sel] += Rr[tv[sel, k]] * w[sel, k]
        self.N /= np.maximum(np.linalg.norm(self.N, axis=1, keepdims=True), 1e-9)
        self.U = px
        self.V = py
        log('rasterised %d texels for %d objects' % (n, len(objs)))

    def coverage(self, oi):
        m = np.zeros((self.size, self.size), bool)
        sel = self.obj == oi
        m[self.ys[sel], self.xs[sel]] = True
        return m


# ------------------------------------------------------------------ Cycles baking
def _image(name, size, color):
    img = bpy.data.images.get(name)
    if img is None or img.size[0] != size:
        if img:
            bpy.data.images.remove(img)
        img = bpy.data.images.new(name, size, size, alpha=True, float_buffer=True)
    img.generated_color = color
    img.pixels.foreach_set(np.tile(np.array(color, np.float32), size * size))
    return img


def _bake_material(obj, img):
    mat = bpy.data.materials.new('bake_' + obj.name)
    mat.use_nodes = True
    nt = mat.node_tree
    node = nt.nodes.new('ShaderNodeTexImage')
    node.image = img
    nt.nodes.active = node
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    return mat


def _select(objs, active):
    for o in bpy.data.objects:
        if o.name in bpy.context.view_layer.objects:
            o.select_set(o in objs)
    bpy.context.view_layer.objects.active = active


def bake_ao(jobs, size, raster, out, samples=24):
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = samples
    tmp = _image('bake_tmp', size, (1, 1, 1, 1))
    every = [o for o in bpy.data.objects if o is not None and o.type == 'MESH']
    for low, oi in jobs:
        hidden = {o: o.hide_render for o in every}
        for o in every:
            o.hide_render = o is not low
        mats = list(low.data.materials)
        _bake_material(low, tmp)
        low.data.uv_layers.active = low.data.uv_layers['Atlas']
        _select([low], low)
        bpy.ops.object.bake(type='AO', use_selected_to_active=False, margin=2, use_clear=True, target='IMAGE_TEXTURES')
        px = np.empty(size * size * 4, np.float32)
        tmp.pixels.foreach_get(px)
        px = px.reshape(size, size, 4)[::-1]
        cov = raster.coverage(oi)
        out[cov] = px[cov, 0]
        low.data.uv_layers.active = low.data.uv_layers['UVMap']
        low.data.materials.clear()
        for m in mats:
            low.data.materials.append(m)
        for o, h in hidden.items():
            o.hide_render = h
        log('baked AO', low.name)


# ------------------------------------------------------------------ sculpt functions (metres)
def _noise3(P, freq, seed=0):
    """Cheap smooth 3D noise in -1..1 (sum of rotated sines; deterministic)."""
    rng = np.random.default_rng(seed)
    out = np.zeros(len(P))
    for _ in range(4):
        d = rng.normal(size=3)
        d /= np.linalg.norm(d)
        ph = rng.uniform(0, 2 * math.pi)
        out += np.sin(P @ d * freq + ph)
    return out / 4


def sculpt_body(J, HK):
    hz = J['Head'][0][2]

    def f(P, N, zone, rim):
        X, Y, Z = P[:, 0], P[:, 1], P[:, 2]
        front = -N[:, 1]
        h = np.zeros(len(P))
        bump = lambda cx, cy, cz, r, amp: amp * np.clip(1 - ((X - cx) ** 2 + (Y - cy) ** 2 + (Z - cz) ** 2) / r ** 2, 0, 1) ** 2
        for sx in (1, -1):
            h += bump(0.1 * sx, -0.12, 1.43, 0.09, 0.006)                     # chest
            h += bump(0.26 * sx, 0, 1.52, 0.08, 0.005)                        # shoulder
            h += bump(0.31 * sx, -0.04, 1.36, 0.06, 0.004)                    # biceps
            h += bump(0.12 * sx, 0.07, 0.36, 0.07, 0.004)                     # calf
            h += bump(0.115 * sx, -0.07, 0.53, 0.04, 0.003)                   # knee cap
            h += bump(0.07 * sx, 0.11, 1.42, 0.06, 0.003)                     # shoulder blade
        abs_ = (np.abs(X) < 0.07) & (Z > 1.12) & (Z < 1.3) & (front > 0.6)
        h += np.where(abs_, 0.0018 * np.maximum(0, np.sin((Z - 1.12) * 55)) * (1 - np.abs(X) / 0.07), 0)
        # Cloth folds: bunching at the waist, inside the elbows and behind the knees.
        h += np.where((Z > 0.98) & (Z < 1.12) & (Z < 1.62), 0.0016 * np.sin(Z * 160 + 3 * np.sin(X * 25)) ** 2, 0)
        for sx in (1, -1):
            el = np.array(J['Elbow_L'][0]) * np.array((sx, 1, 1))
            de = np.sqrt((X - el[0]) ** 2 + (Z - el[2]) ** 2)
            # Creases run across the limb (rings round the joint looked like a target).
            h += np.where((de < 0.07) & (front > 0.2), 0.0018 * np.sin((Z - el[2]) * 140) ** 2 * (1 - de / 0.07), 0)
            kn = np.array(J['Knee_L'][0]) * np.array((sx, 1, 1))
            dk = np.sqrt((X - kn[0]) ** 2 + (Z - kn[2]) ** 2)
            h += np.where((dk < 0.08) & (front < -0.2), 0.0016 * np.sin((Z - kn[2]) * 120) ** 2 * (1 - dk / 0.08), 0)
        # Face: brow ridge, cheekbones, nose bridge.
        head = Z > hz - 0.14 * HK
        h += np.where(head & (front > 0.3), 0.003 * np.exp(-((Z - (hz + 0.045 * HK)) / 0.012) ** 2), 0)
        for sx in (1, -1):
            h += np.where(head, bump(0.085 * HK * sx, -0.1, hz - 0.03 * HK, 0.03, 0.0025), 0)
        h += np.where(head & (np.abs(X) < 0.012) & (front > 0.85) & (Z > hz - 0.05 * HK) & (Z < hz + 0.02 * HK), 0.004, 0)
        return h
    return f


def sculpt_piece(name):
    soft = name in SOFT_PIECES

    def f(P, N, zone, rim):
        h = np.zeros(len(P))
        if soft:
            # Soft cloth: broad folds and wrinkles; quilted panels on the vest.
            h += 0.0025 * _noise3(P, 45, 1) + 0.0012 * _noise3(P, 110, 2)
            if name == 'vest':
                q = np.sin(P[:, 2] * 70) * np.sin((P[:, 0] + P[:, 1]) * 70)
                h += np.where(zone == Z_MAIN, 0.002 * np.abs(q) ** 0.5, 0)
            if name == 'cape':
                h += 0.006 * np.sin(P[:, 0] * 30 + 2 * np.sin(P[:, 2] * 6)) * np.clip((1.5 - P[:, 2]) / 0.6, 0, 1)
            if name == 'scarf':
                h += 0.003 * np.sin((P[:, 2] * 80 + np.arctan2(P[:, 0], P[:, 1]) * 4))
        else:
            # Hard armour: raised rims, ribs and rivets.
            h += np.where(rim < 0.012, 0.0025 * (1 - rim / 0.012), 0)
            ribs = np.sin(P[:, 2] * 90 + P[:, 0] * 10)
            h += np.where(zone == Z_MAIN, 0.0012 * np.clip(ribs * 3, -1, 1), 0)
            riv = (np.sin(P[:, 0] * 260) * np.sin(P[:, 1] * 260) * np.sin(P[:, 2] * 260))
            h += np.where((rim > 0.004) & (rim < 0.02), 0.002 * np.clip(riv * 4 - 3, 0, 1), 0)
        # Pouches and leather bits get softer bulges; metal stays crisp.
        h += np.where(zone == Z_LEATHER, 0.0012 * _noise3(P, 140, 3), 0)
        return h
    return f


def sculpt_soft(P, N, zone, rim):
    return 0.0008 * _noise3(P, 160, 4)


# ------------------------------------------------------------------ texture-space detail
def _height_to_normal(H, cover, mpt_img, P_img, strength=1.0):
    """Tangent-space normals (OpenGL: +x = +u, +y = +v) from a height image in metres. Two texels
    only count as neighbours if they are also neighbours on the mesh (not across a UV seam)."""
    mpt = np.maximum(mpt_img, 1e-5)
    gx = np.zeros_like(H)
    gy = np.zeros_like(H)
    def near(a, b, m):
        return np.linalg.norm(P_img[a] - P_img[b], axis=-1) < m * 6
    m_x = mpt[:, 1:-1]
    m_y = mpt[1:-1, :]
    both_x = cover[:, 2:] & cover[:, :-2] & near((slice(None), slice(2, None)), (slice(None), slice(0, -2)), m_x)
    both_y = cover[2:, :] & cover[:-2, :] & near((slice(2, None), slice(None)), (slice(0, -2), slice(None)), m_y)
    gx[:, 1:-1] = np.where(both_x, (H[:, 2:] - H[:, :-2]) * 0.5, 0)
    gy[1:-1, :] = np.where(both_y, (H[2:, :] - H[:-2, :]) * 0.5, 0)
    nx = -gx / mpt * strength
    ny = gy / mpt * strength          # image rows go down, v goes up
    n = np.stack([nx, ny, np.ones_like(H)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def _blend(base, detail):
    """Adds detail normals onto base normals (UDN blend); both in -1..1."""
    n = np.stack([base[..., 0] + detail[..., 0], base[..., 1] + detail[..., 1], base[..., 2]], -1)
    return n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)


def body_regions(X, Y, Z, N, J, HK):
    """Body texel classes, using the same boundaries as the skin painter."""
    hz = J['Head'][0][2]
    front = -N[:, 1]
    torso = np.abs(X) < 0.3
    head = Z > hz - 0.14 * HK
    cls = np.full(len(X), 'cloth', dtype=object)
    cls[(Z >= 1.05) & (np.abs(X) >= 0.3) & (Z <= 1.22)] = 'skin'
    cls[(Z < 1.0) & (np.abs(X) > 0.3)] = 'leather'      # gloves
    cls[Z < 0.21] = 'leather'                            # boots
    cls[Z < 0.08] = 'rubber'                             # soles
    cls[(Z >= 0.98) & (Z < 1.06) & torso] = 'leather'   # belt
    cls[(Z >= 0.99) & (Z < 1.05) & (np.abs(X) < 0.035) & (front > 0.4)] = 'metal'
    cls[head] = 'skin'
    hairline = hz + 0.12 * HK - 0.05 * HK * np.clip(front, 0, 1)
    hair = head & ((Z > hairline) | ((Y > 0.09 * HK) & (Z > hz - 0.04 * HK)) | ((np.abs(X) > 0.16 * HK) & (Z > hz + 0.04 * HK)))
    cls[hair] = 'hair'
    return cls


MATERIAL = {   # class: (roughness, metalness)
    'cloth': (0.86, 0.0), 'leather': (0.48, 0.0), 'rubber': (0.7, 0.0), 'metal': (0.3, 1.0), 'skin': (0.55, 0.0),
    'hair': (0.5, 0.0),
}
ZONE_MATERIAL = {
    Z_MAIN: (0.5, 0.0), Z_SECOND: (0.85, 0.0), Z_ACCENT: (0.5, 0.0), Z_LEATHER: (0.46, 0.0), Z_METAL: (0.28, 1.0),
    Z_HAIR: (0.48, 0.0), Z_SKIN: (0.55, 0.0), Z_EYE: (0.12, 0.0), Z_DARK: (0.62, 0.0), Z_LIPS: (0.38, 0.0),
    Z_LENS: (0.05, 0.0), Z_BODY: (0.6, 0.0),
}


def paint_hair_tiles(size, nrm, mat):
    """Strand textures for the hair cards: see-through gaps (zone 0), darker at the roots."""
    rng = np.random.default_rng(11)
    th = int(size * 0.25)
    tw = int(size * hair_cards.TILE_W)
    for k in range(hair_cards.TILE_COUNT):
        x0 = int(size * (hair_cards.TILE_U0 + k * hair_cards.TILE_W))
        cover = np.zeros((th, tw), np.float32)
        across = np.zeros((th, tw), np.float32)
        count = 70 + 10 * k
        yy = np.arange(th)[:, None]
        for _ in range(count):
            cx = rng.uniform(0.08, 0.92) * tw
            length = rng.uniform(0.55, 1.0) * th
            width = rng.uniform(1.2, 2.6) * (size / 4096)
            wave = rng.uniform(0, 3)
            xs = cx + np.sin(yy / th * 6 + wave) * 3 * (size / 4096)
            taper = np.clip(1 - yy / length, 0, 1) ** 0.6
            d = (np.arange(tw)[None, :] - xs) / np.maximum(width * (0.3 + 0.7 * taper), 0.3)
            strand = np.clip(1 - np.abs(d), 0, 1) * (yy < length)
            mask = strand > cover
            across = np.where(mask, np.clip(d, -1, 1), across)
            cover = np.maximum(cover, strand)
        solid = cover > 0.35
        n = np.stack([across * 0.7, np.zeros_like(across), np.ones_like(across)], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        nrm[0:th, x0:x0 + tw] = n
        root = np.clip(np.arange(th) / th, 0, 1)[:, None]
        ao = (0.55 + 0.45 * root) * (0.75 + 0.25 * cover)
        m = mat[0:th, x0:x0 + tw]
        m[..., 0] = 0.45 + 0.1 * (1 - cover)
        m[..., 1] = 0.0
        m[..., 2] = ao
        m[..., 3] = np.where(solid, Z_HAIR, Z_CUT)


def nearest_fill(filled, steps):
    """For empty texels within `steps` of an island: the flat index of a nearby filled texel
    (-1 elsewhere). Copying (not averaging) keeps the zone channel valid."""
    h, w = filled.shape
    idx = np.where(filled, np.arange(h * w, dtype=np.int32).reshape(h, w), -1).astype(np.int32)
    for _ in range(steps):
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = np.full_like(idx, -1)
            ys = slice(max(dy, 0), h + min(dy, 0))
            yd = slice(max(-dy, 0), h + min(-dy, 0))
            xs = slice(max(dx, 0), w + min(dx, 0))
            xd = slice(max(-dx, 0), w + min(-dx, 0))
            sh[ys, xs] = idx[yd, xd]
            take = (idx < 0) & (sh >= 0)
            idx[take] = sh[take]
    return idx


def apply_fill(img, filled, idx):
    flat = img.reshape(-1, img.shape[-1])
    sel = ((idx >= 0) & ~filled).ravel()
    flat[sel] = flat[idx.ravel()[sel]]


# ------------------------------------------------------------------ main entry
def build_atlas(body, kit_objs, face_objs, hair_objs, J, HK, size, out_dir, bake=True, eye_points=None):
    """Bakes and writes character_normal.png and character_material.png."""
    objs = [body] + kit_objs + face_objs + hair_objs
    names = [o.name.split('_B')[0] for o in objs]
    raster = Raster(objs, size)
    cover_all = np.zeros((size, size), bool)
    cover_all[raster.ys, raster.xs] = True

    nrm = np.zeros((size, size, 3), np.float32)
    nrm[..., 2] = 1.0
    ao = np.ones((size, size), np.float32)
    if bake:
        bake_ao([(o, i) for i, o in enumerate(objs)], size, raster, ao)

    # Per-texel classes, height and materials.
    P, N, Zn, R = raster.P, raster.N, raster.zone.copy(), raster.R
    X, Y, Z = P[:, 0], P[:, 1], P[:, 2]
    oi = raster.obj
    is_body = oi == 0
    rough = np.zeros(len(P)); metal = np.zeros(len(P)); height = np.zeros(len(P))
    # The sculpt (muscles, folds, ridges, rivets...), evaluated on every texel: the slope of the
    # displacement becomes the normal, which is exactly what baking a displaced high-poly copy
    # gives, without the ray-sampling errors a bake has on small or touching parts.
    for i, o in enumerate(objs):
        sel = oi == i
        if not sel.any():
            continue
        fn = sculpt_body(J, HK) if i == 0 else (sculpt_soft if (o in hair_objs or o in face_objs) else sculpt_piece(names[i]))
        height[sel] += fn(P[sel], N[sel], Zn[sel], R[sel])
    U, Vv = raster.U * size, raster.V * size   # texel units (for patterns that must stay sharp)
    weave = 0.00012 * np.sin(U * 2 * math.pi / 4) * np.sin(Vv * 2 * math.pi / 4)
    grain = 0.00018 * _noise3(P, 1500, 7) + 0.0001 * _noise3(P, 3200, 8)
    brushed = 0.00006 * np.sin(U * 2 * math.pi / 3 + 3 * _noise3(P, 40, 9))
    pores = 0.00005 * _noise3(P, 4000, 10)
    stitch_dash = np.sin((X + Y + Z) * 900) > 0.2

    # Body.
    cls = body_regions(X, Y, Z, N, J, HK)
    for c, (r, m) in MATERIAL.items():
        sel = is_body & (cls == c)
        rough[sel] = r
        metal[sel] = m
    height += np.where(is_body & (cls == 'cloth'), weave, 0)
    height += np.where(is_body & ((cls == 'leather') | (cls == 'rubber')), grain, 0)
    height += np.where(is_body & (cls == 'skin'), pores, 0)
    height += np.where(is_body & (cls == 'hair'), 0.0002 * np.sin(X * 2600 + Z * 900), 0)
    import face as face_mod
    fmask = face_mod.painted_face_mask(X, Y, Z, -N[:, 1], J, HK)
    Zn[is_body] = Z_BODY
    Zn[is_body & (cls == 'skin')] = Z_BODY_SKIN
    Zn[is_body & fmask] = Z_SKIN
    # Stitched seams on the body clothes, where the painter draws them.
    side = np.abs(N[:, 0]) > 0.8
    seam = is_body & (cls == 'cloth') & (side | ((np.abs(X) < 0.008) & (-N[:, 1] > 0.5) & (Z > 1.08) & (Z < 1.55)))
    height -= np.where(seam, 0.0004, 0)
    height -= np.where(seam & stitch_dash, 0.0002, 0)

    # Kit, face and scalp.
    kit = ~is_body
    for z, (r, m) in ZONE_MATERIAL.items():
        sel = kit & (Zn == z)
        rough[sel] = r
        metal[sel] = m
    soft_obj = np.array([names[i] in SOFT_PIECES for i in range(len(objs))])[oi]
    cloth_z = kit & ((Zn == Z_MAIN) | (Zn == Z_SECOND) | (Zn == Z_ACCENT))
    height += np.where(cloth_z & soft_obj, weave * 1.3, 0)
    height += np.where(cloth_z & ~soft_obj, 0.00004 * _noise3(P, 2500, 12), 0)
    height += np.where(kit & ((Zn == Z_LEATHER) | (Zn == Z_DARK)), grain, 0)
    height += np.where(kit & (Zn == Z_METAL), brushed, 0)
    height += np.where(kit & (Zn == Z_HAIR), 0.00022 * np.sin((X * 0.3 + Y) * 2400 + Z * 300), 0)
    height += np.where(kit & ((Zn == Z_SKIN) | (Zn == Z_LIPS)), pores, 0)
    stitches = kit & (R > 0.006) & (R < 0.009) & stitch_dash & (Zn != Z_METAL) & (Zn != Z_LENS)
    height -= np.where(stitches, 0.00035, 0)
    rough += np.where(kit | is_body, 0.04 * _noise3(P, 300, 13), 0)
    # Eyes: a dark pupil in the middle of each eye.
    if eye_points:
        for e in eye_points:
            c = np.array((e.x, e.y - 0.012 - 0.002, e.z))
            d = np.linalg.norm(P - c, axis=1)
            Zn[kit & (Zn == Z_EYE) & (d < 0.0115)] = Z_DARK
            rough[kit & (Zn == Z_DARK) & (d < 0.0115)] = 0.08

    # Images.
    H = np.zeros((size, size), np.float32)
    H[raster.ys, raster.xs] = height
    mpt_img = np.zeros((size, size), np.float32)
    mpt_img[raster.ys, raster.xs] = raster.mpt
    P_img = np.zeros((size, size, 3), np.float32)
    P_img[raster.ys, raster.xs] = P
    detail = _height_to_normal(H, cover_all, mpt_img, P_img)
    del P_img
    nrm = _blend(nrm, detail)

    mat = np.zeros((size, size, 4), np.float32)
    mat[..., 0] = 0.6
    mat[..., 2] = 1.0
    mat[..., 3] = Z_BODY
    mat[raster.ys, raster.xs, 0] = np.clip(rough, 0.03, 1)
    mat[raster.ys, raster.xs, 1] = metal
    mat[raster.ys, raster.xs, 2] = np.clip(ao[raster.ys, raster.xs], 0, 1)
    mat[raster.ys, raster.xs, 3] = Zn
    # Bleed past island edges (mipmaps would show the empty background otherwise).
    idx = nearest_fill(cover_all, MARGIN_PX + 2)
    nrm = np.ascontiguousarray(nrm, dtype=np.float32)
    apply_fill(nrm, cover_all, idx)
    apply_fill(mat, cover_all, idx)

    paint_hair_tiles(size, nrm, mat)
    # Neutral patch.
    nu, nv = int(NEUTRAL_UV[0] * size), int((1 - NEUTRAL_UV[1]) * size)
    r = max(4, size // 256)
    nrm[nv - r:nv + r, nu - r:nu + r] = (0, 0, 1)
    mat[nv - r:nv + r, nu - r:nu + r] = (0.6, 0.0, 1.0, Z_BODY)

    os.makedirs(out_dir, exist_ok=True)
    n8 = np.clip((nrm * 0.5 + 0.5) * 255 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(n8, 'RGB').save(os.path.join(out_dir, 'character_normal.png'), optimize=True)
    m8 = np.zeros((size, size, 4), np.uint8)
    m8[..., :3] = np.clip(mat[..., :3] * 255 + 0.5, 0, 255).astype(np.uint8)
    m8[..., 3] = (np.clip(mat[..., 3], 0, 15).astype(np.uint8) * 16 + 8)
    Image.fromarray(m8, 'RGBA').save(os.path.join(out_dir, 'character_material.png'), optimize=True)
    log('wrote atlas maps', size)
