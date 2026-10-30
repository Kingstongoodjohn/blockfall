# Blockfall character builder.
#
# Builds the shared Blockfall character inside Blender, then writes:
#   assets/character.glb            the body in 3 builds x 3 levels of detail, hair cards, hats,
#                                   headband, face parts, the 14 outfit kit pieces, the shared
#                                   skeleton (body + face + swing bones) and every animation
#   assets/character_normal.png     shared 4096 detail atlas: baked normals ...
#   assets/character_material.png   ... and roughness / metalness / AO / zone (see atlas.py)
#   assets/skins/<n>.png            one painted texture per locker skin (only with --paint)
#
#   blender -b --factory-startup --python-use-system-env --python build_character.py -- [options]
#     --out DIR      write somewhere else than assets/
#     --paint        also repaint the 64 skin textures (only needed when the catalogue changes)
#     --atlas N      atlas size (default 4096; 1024 for quick tests)
#     --no-bake      skip the Cycles bakes (flat normals; fast layout checks)
#   It needs Pillow in Blender's Python (PYTHONPATH pointing at a site-packages that has it).
#
# The body starts as a stick figure of joints with radii. Blender's Skin modifier wraps it in a
# chunky rounded mesh (the Fortnite-ish look), Subdivision smooths it, and "Create Armature"
# turns the same joints into the skeleton with weights. Every skin uses this one skeleton, so
# every animation works on every skin.
import bpy, bmesh, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from mathutils import Vector, Matrix, Quaternion
from PIL import Image, ImageDraw


def _args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts = {'out': os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'assets'),
            'paint': False, 'atlas': 4096, 'bake': True}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--out':
            opts['out'] = argv[i + 1]; i += 1
        elif a == '--paint':
            opts['paint'] = True
        elif a == '--atlas':
            opts['atlas'] = int(argv[i + 1]); i += 1
        elif a == '--no-bake':
            opts['bake'] = False
        elif a == '--preview':
            opts['preview'] = argv[i + 1]; i += 1
        i += 1
    return opts


OPTS = _args()
OUT = OPTS['out']
TEX = 2048
FPS = 24

# ------------------------------------------------------------------ the body
# Blender axes: X right (character's left), -Y front, Z up. Metres.
J = {  # joint: (position, (radius x, radius y))
    # Athletic proportions: small head (about 1/6 of the height), thin neck, broad shoulders,
    # narrow waist and hips, slim arms and long legs.
    'Hips':    ((0, 0, 1.00), (0.165, 0.115)),
    'Spine':   ((0, 0, 1.18), (0.13, 0.10)),      # narrow waist
    'Chest':   ((0, 0, 1.46), (0.235, 0.13)),     # broad, flat chest
    'Neck':    ((0, 0, 1.64), (0.05, 0.05)),
    'Head':    ((0, 0.01, 1.82), (0.135, 0.145)),
    'HeadTop': ((0, 0.0, 1.95), (0.085, 0.085)),
}
HK = 0.135 / 0.18   # how much smaller the head is than the old round one (hair and hats scale with it)
for s, sx in (('L', 1), ('R', -1)):
    J['Shoulder_' + s] = ((0.27 * sx, 0, 1.54), (0.085, 0.08))
    J['Elbow_' + s] = ((0.32 * sx, 0.0, 1.25), (0.055, 0.052))
    J['Wrist_' + s] = ((0.345 * sx, 0.0, 1.00), (0.045, 0.042))
    J['Hand_' + s] = ((0.355 * sx, 0.0, 0.89), (0.065, 0.035))   # a flat mitt
    J['Hip_' + s] = ((0.105 * sx, 0, 0.96), (0.10, 0.10))
    J['Knee_' + s] = ((0.115 * sx, 0, 0.52), (0.075, 0.08))
    J['Ankle_' + s] = ((0.12 * sx, 0, 0.12), (0.065, 0.065))
    J['Toe_' + s] = ((0.12 * sx, -0.16, 0.05), (0.08, 0.04))
E = [('Hips', 'Spine'), ('Spine', 'Chest'), ('Chest', 'Neck'), ('Neck', 'Head'), ('Head', 'HeadTop')]
for s in 'LR':
    E += [('Chest', 'Shoulder_' + s), ('Shoulder_' + s, 'Elbow_' + s), ('Elbow_' + s, 'Wrist_' + s), ('Wrist_' + s, 'Hand_' + s),
          ('Hips', 'Hip_' + s), ('Hip_' + s, 'Knee_' + s), ('Knee_' + s, 'Ankle_' + s), ('Ankle_' + s, 'Toe_' + s)]
# Bone names, by the joint at the bone's tail. The root bone is "Hips".
BONE_OF_TAIL = {'Spine': 'Spine', 'Chest': 'Chest', 'Neck': 'Neck', 'Head': 'Head', 'HeadTop': 'HeadTop'}
for s in 'LR':
    BONE_OF_TAIL.update({'Shoulder_' + s: 'Clavicle_' + s, 'Elbow_' + s: 'UpperArm_' + s, 'Wrist_' + s: 'Forearm_' + s,
                         'Hand_' + s: 'Hand_' + s, 'Hip_' + s: 'Pelvis_' + s, 'Knee_' + s: 'Thigh_' + s,
                         'Ankle_' + s: 'Shin_' + s, 'Toe_' + s: 'Foot_' + s})

from skins_catalog import SKINS, SKIN_TONES, slug  # the one list the game also uses

# UV patches used by the rigid add-on meshes (flat colour squares in the texture's free strip).
UV_HAIR = (0.95, 0.95)
UV_ACCENT = (0.95, 0.87)
UV_DARK = (0.95, 0.79)
UV_METAL = (0.95, 0.71)
UV_HAT = (0.95, 0.63)


def override(obj):
    return bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj], selected_editable_objects=[obj])


def build_body():
    names = list(J.keys())
    mesh = bpy.data.meshes.new('Body')
    mesh.from_pydata([J[n][0] for n in names], [(names.index(a), names.index(b)) for a, b in E], [])
    obj = bpy.data.objects.new('Body', mesh)
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    skin = obj.modifiers.new('Skin', 'SKIN')
    skin.use_smooth_shade = True
    sv = mesh.skin_vertices[0].data
    for i, n in enumerate(names):
        sv[i].radius = J[n][1]
        sv[i].use_root = (n == 'Hips')
    with override(obj):
        bpy.ops.object.skin_armature_create(modifier='Skin')
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    arm.name = 'Skeleton'
    sub = obj.modifiers.new('Sub', 'SUBSURF')
    sub.levels = 2
    sub.render_levels = 2
    # Modifier order: Skin -> Subdivision -> Armature.
    with override(obj):
        bpy.ops.object.modifier_move_to_index(modifier='Armature', index=2)

    # Name the bones from the joints they end on.
    bpy.context.view_layer.objects.active = arm
    with override(arm):
        bpy.ops.object.mode_set(mode='EDIT')
        for eb in arm.data.edit_bones:
            if eb.parent is None:
                eb.name = 'Hips'
                continue
            tail = Vector(eb.tail)
            best = min(J.keys(), key=lambda n: (Vector(J[n][0]) - tail).length)
            eb.name = BONE_OF_TAIL[best]
        bpy.ops.object.mode_set(mode='OBJECT')
    # The skin armature names vertex groups after the old bone names; Blender renames them along
    # with the bones, so nothing to do here.
    return obj, arm


def shape_body(mesh):
    """Makes the skin-modifier tubes look more like a person: cross-sections become rounded
    squares instead of circles, the body is flattened front-to-back, and the chest gets a
    V shape toward the waist."""
    segs = []
    for a, b in E:
        pa, pb = Vector(J[a][0]), Vector(J[b][0])
        segs.append((pa, pb, a, b))
    P_EXP = 2.7   # 2 = circle, higher = squarer
    for v in mesh.vertices:
        p = Vector(v.co)
        best = None
        for pa, pb, na, nb in segs:
            d = pb - pa
            t = max(0.0, min(1.0, (p - pa).dot(d) / max(d.length_squared, 1e-9)))
            c = pa + d * t
            dist = (p - c).length
            if best is None or dist < best[0]:
                best = (dist, c, d.normalized(), na, nb, t)
        dist, c, axis, na, nb, t = best
        if dist < 1e-5:
            continue
        r = p - c
        # Local frame: u sideways, w front/back.
        u = Vector((1, 0, 0)) if abs(axis.x) < 0.9 else Vector((0, 0, 1))
        u = (u - axis * u.dot(axis)).normalized()
        w = axis.cross(u)
        ru, rw = r.dot(u), r.dot(w)
        ang = math.atan2(rw, ru)
        f = (abs(math.cos(ang)) ** P_EXP + abs(math.sin(ang)) ** P_EXP) ** (-1.0 / P_EXP)
        head = na in ('Neck', 'Head') or nb in ('Head', 'HeadTop')
        if head:
            f = 1.0 + (f - 1.0) * 0.35   # heads stay mostly round
        flat = 0.9 if not head else 1.0
        if na in ('Hips', 'Spine') and nb in ('Spine', 'Chest'):
            flat = 0.86
        new = c + u * (ru * f) + w * (rw * f * flat)
        v.co = new


def finalize_body(obj, arm, before_lods=None):
    """Applies Skin + Subdivision so the body is a real mesh, unwraps it, and makes the LODs.
    before_lods(obj) runs after the unwrap, so anything it adds (the atlas UVs) carries over."""
    with override(obj):
        bpy.ops.object.modifier_apply(modifier='Skin')
        bpy.ops.object.modifier_apply(modifier='Sub')
    mesh = obj.data
    shape_body(mesh)
    for p in mesh.polygons:
        p.use_smooth = True
    # Unwrap.
    for p in mesh.polygons:
        p.select = True
    for o in bpy.data.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.015)
    bpy.ops.object.mode_set(mode='OBJECT')
    # Keep the right/top strip of the texture free for the flat colour patches.
    uv = mesh.uv_layers.active.data
    for l in uv:
        l.uv = (l.uv[0] * 0.9, l.uv[1] * 0.9)
    if before_lods:
        before_lods(obj)
    lods = [obj]
    for i, ratio in enumerate((0.3, 0.08)):
        lod = obj.copy()
        lod.data = mesh.copy()
        lod.name = 'Body_LOD%d' % (i + 1)
        lod.data.name = lod.name
        bpy.context.collection.objects.link(lod)
        dec = lod.modifiers.new('Dec', 'DECIMATE')
        dec.ratio = ratio
        with override(lod):
            bpy.ops.object.modifier_move_to_index(modifier='Dec', index=0)
            bpy.ops.object.modifier_apply(modifier='Dec')
        lods.append(lod)
    for o in lods:
        o.parent = arm
    return lods


def rigid_mesh(name, bm, arm, uv_patch, head_pos):
    """A mesh that follows the Head bone rigidly (hair, headband)."""
    mesh = bpy.data.meshes.new(name)
    # Hair and hats were modelled for the old, bigger head: shrink them about the head centre.
    bmesh.ops.translate(bm, verts=bm.verts, vec=-Vector(head_pos))
    bmesh.ops.scale(bm, verts=bm.verts, vec=(HK, HK, HK))
    bmesh.ops.translate(bm, verts=bm.verts, vec=Vector(head_pos))
    uvl = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        f.smooth = True
        for l in f.loops:
            l[uvl].uv = uv_patch
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    vg = obj.vertex_groups.new(name='Head')
    vg.add(list(range(len(mesh.vertices))), 1.0, 'REPLACE')
    mod = obj.modifiers.new('Armature', 'ARMATURE')
    mod.object = arm
    obj.parent = arm
    return obj


def sphere(bm, center, radius, segs=12, rings=8):
    m = Matrix.Translation(Vector(center)) @ Matrix.Diagonal((radius[0], radius[1], radius[2], 1.0))
    bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=rings, radius=1.0, matrix=m)


def build_hats(arm):
    """Hats and the headband (the hair styles are cards now, see hair_cards.py)."""
    head = Vector(J['Head'][0])
    objs = []
    # Hats: baseball cap, beanie, helmet (all follow the head).
    bm = bmesh.new()
    sphere(bm, head + Vector((0, 0.03, 0.13)), (0.225, 0.235, 0.11), 16, 10)
    m = Matrix.Translation(head + Vector((0, -0.2, 0.1))) @ Matrix.Diagonal((0.14, 0.13, 0.015, 1.0))
    bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=6, radius=1.0, matrix=m)  # the bill
    objs.append(rigid_mesh('Hat1', bm, arm, UV_HAT, head))
    bm = bmesh.new()
    sphere(bm, head + Vector((0, 0.03, 0.12)), (0.23, 0.24, 0.17), 16, 10)
    m = Matrix.Translation(head + Vector((0, 0.03, 0.06))) @ Matrix.Diagonal((0.235, 0.245, 1.0, 1.0))
    bmesh.ops.create_cone(bm, cap_ends=False, segments=24, radius1=1.0, radius2=1.0, depth=0.07, matrix=m)  # rolled rim
    objs.append(rigid_mesh('Hat2', bm, arm, UV_HAT, head))
    bm = bmesh.new()
    sphere(bm, head + Vector((0, 0.02, 0.08)), (0.24, 0.25, 0.2), 16, 10)
    m = Matrix.Translation(head + Vector((0, 0.0, 0.0))) @ Matrix.Diagonal((0.29, 0.3, 1.0, 1.0))
    bmesh.ops.create_cone(bm, cap_ends=True, segments=24, radius1=1.0, radius2=1.0, depth=0.03, matrix=m)  # brim
    objs.append(rigid_mesh('Hat3', bm, arm, UV_HAT, head))
    # Headband.
    bm = bmesh.new()
    m = Matrix.Translation(head + Vector((0, 0.0, 0.055))) @ Matrix.Diagonal((0.215, 0.225, 1.0, 1.0))
    bmesh.ops.create_cone(bm, cap_ends=False, segments=24, radius1=1.0, radius2=1.0, depth=0.045, matrix=m)
    objs.append(rigid_mesh('Headband', bm, arm, UV_ACCENT, head))
    return objs


# ------------------------------------------------------------------ animation
class Rig:
    def __init__(self, arm):
        self.arm = arm
        self.rest = {b.name: b.matrix_local.to_3x3() for b in arm.data.bones}
        arm.rotation_mode = 'QUATERNION'
        for pb in arm.pose.bones:
            pb.rotation_mode = 'QUATERNION'

    def clear(self):
        for pb in self.arm.pose.bones:
            pb.rotation_quaternion = (1, 0, 0, 0)
            pb.location = (0, 0, 0)

    def rot(self, bone, fwd=0.0, out=0.0, twist=0.0):
        """Rotation in the character's frame: fwd = the bone's tip swings toward the front (deg),
        out = the tip swings away from the body's centre line (deg, mirrored for the right side),
        twist = roll around the bone (deg). The signs account for which way the bone points, so
        "forward" means the same thing for a hanging arm and an upright spine."""
        side = -1.0 if bone.endswith('_R') else 1.0
        d = self.rest[bone].col[1]   # bone direction (its local Y) in armature space
        up = 1.0 if d.z >= -0.3 else -1.0   # up-ish bones rotate the opposite way to hanging ones
        if bone.startswith('Foot'):
            up = 1.0   # feet: fwd = toe down
        # Applied right to left: twist around the bone first, then out to the side, then forward.
        q = Quaternion((1, 0, 0), math.radians(fwd * up)) @ Quaternion((0, 1, 0), math.radians(-out * side * -up)) @ \
            Quaternion((0, 0, 1), math.radians(twist * side))
        R = self.rest[bone]
        local = R.inverted() @ q.to_matrix() @ R
        pb = self.arm.pose.bones[bone]
        pb.rotation_quaternion = local.to_quaternion()
        return q

    def rot_about_tail(self, bone, fwd=0.0, out=0.0, twist=0.0, dx=0.0, dy=0.0, dz=0.0):
        """Like rot(), but the bone's tip stays put (plus an offset) instead of its base. Used for the
        root bone, whose base sits a metre behind the hips, so the whole body can tip over at the hips."""
        q = self.rot(bone, fwd, out, twist)
        b = self.arm.data.bones[bone]
        v = b.tail_local - b.head_local
        d = v - (q @ v)
        self.move(bone, dx=d.x + dx, dy=-d.y + dy, dz=d.z + dz)

    def move(self, bone, dx=0.0, dy=0.0, dz=0.0):
        """Translation in the character's frame (dy = forward)."""
        R = self.rest[bone]
        pb = self.arm.pose.bones[bone]
        pb.location = R.inverted() @ Vector((dx, -dy, dz))

    def key(self, frame):
        for pb in self.arm.pose.bones:
            pb.keyframe_insert('rotation_quaternion', frame=frame)
            pb.keyframe_insert('location', frame=frame)


def arms_down(rig, sway=0.0, bend=12.0):
    rig.rot('UpperArm_L', fwd=sway, out=4)
    rig.rot('UpperArm_R', fwd=-sway, out=4)
    rig.rot('Forearm_L', fwd=bend)
    rig.rot('Forearm_R', fwd=bend)


def hold_gun(rig, recoil=0.0, left_drop=0.0):
    """Both hands forward on a gun, close to the centre line. Right hand is the trigger hand."""
    rig.rot('UpperArm_R', fwd=45 + recoil * 12, out=-36)
    rig.rot('Forearm_R', fwd=70 + recoil * 6, out=-16)
    rig.rot('Hand_R', fwd=-6)
    k = 1 - left_drop
    rig.rot('UpperArm_L', fwd=(90 * k + 10 * left_drop) + recoil * 10, out=-34 * k + 10 * left_drop)
    rig.rot('Forearm_L', fwd=2 * k + 40 * left_drop, out=-4 * k)
    rig.rot('Chest', twist=-10 * k)



# ---- legs: two-bone IK and a gait generator, so feet stand on the ground and don't slide.
# Leg bone lengths come from the joints (hip -> knee -> ankle).
LEG_THIGH = (Vector(J['Knee_L'][0]) - Vector(J['Hip_L'][0])).length
LEG_SHIN = (Vector(J['Ankle_L'][0]) - Vector(J['Knee_L'][0])).length
LEG_REACH = (LEG_THIGH + LEG_SHIN) * 0.985     # never lock the knee completely straight
HIP_Z = J['Hip_L'][0][2]
ANKLE_Z = J['Ankle_L'][0][2]                   # ankle height with the foot flat on the ground
FOOT_LEN = 0.16                                # ankle -> ball of the foot, forward

# Ground distance of one full step cycle (two steps) for each gait. MUST match the stride rates in
# src/game/game.h (STRIDE_WALK / STRIDE_SPRINT / STRIDE_CROUCH, radians of walkPhase per metre):
# cycle = 2*pi / rate. Then a planted foot moves back at exactly the speed the body moves forward.
CYCLE_WALK = 2 * math.pi / 2.4
CYCLE_SPRINT = 2 * math.pi / 1.8
CYCLE_CROUCH = 2 * math.pi / 3.4


def leg_ik(rig, side, hips, fwd, z, out=0.0, toe=0.0):
    """Bends the thigh and shin so the ankle lands at `fwd` metres in front of the hips' rest spot
    and `z` metres up; keeps the foot level (plus `toe` degrees toe-down). `hips` is the (dx, dy, dz)
    the Hips bone was moved by."""
    dx, dy, dz = hips
    a = fwd - dy                                # ankle forward of the hip joint
    b = (HIP_Z + dz) - z                        # ankle below the hip joint
    b /= max(0.5, math.cos(math.radians(out)))  # the leg is splayed out a little: it needs to be longer
    d = min(max(math.hypot(a, b), 0.08), LEG_REACH)
    L1, L2 = LEG_THIGH, LEG_SHIN
    phi = math.atan2(a, b)
    alpha = math.acos(max(-1.0, min(1.0, (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d))))
    knee = math.acos(max(-1.0, min(1.0, (L1 * L1 + L2 * L2 - d * d) / (2 * L1 * L2))))
    theta = math.degrees(phi + alpha)
    beta = 180.0 - math.degrees(knee)
    rig.rot('Thigh_' + side, fwd=theta, out=out)
    rig.rot('Shin_' + side, fwd=-beta)
    rig.rot('Foot_' + side, fwd=theta - beta + toe)


def gait_foot(ph, cycle, stance, lift, heel=0.07):
    """Where one foot is at phase `ph` (0..1, heel strike at 0) of a step cycle.
    Returns (forward, ankle height, toe-down degrees, planted)."""
    c = cycle * stance / 2.0
    if ph < stance:
        u = ph / stance
        f = c * (1.0 - 2.0 * u)
        hl = heel * max(0.0, (u - 0.6) / 0.4) ** 2    # heel peels up at the end of the stance
        return f, ANKLE_Z + hl, math.degrees(math.asin(min(1.0, hl / FOOT_LEN))), True
    u = (ph - stance) / (1.0 - stance)
    e = 0.5 - 0.5 * math.cos(math.pi * u)
    f = -c + 2.0 * c * e
    z = ANKLE_Z + heel * (1.0 - u) ** 2 + lift * math.sin(math.pi * u ** 0.75)
    toe = 28.0 * (1.0 - u) ** 2 - 12.0 * u ** 3        # toe trails behind, then lifts for the heel strike
    return f, z, toe, False


def gait_hips(cycle, stance, lift, base_dz, dy=0.0, n=240):
    """The hips' height over one cycle: as high as `base_dz` allows while every planted foot can
    still reach the ground, smoothed so the body bobs instead of jerking."""
    raw = []
    for i in range(n):
        t = i / n
        lim = base_dz
        for ph in (t, (t + 0.5) % 1.0):
            f, z, _, planted = gait_foot(ph, cycle, stance, lift)
            if planted:
                a = f - dy
                lim = min(lim, z + math.sqrt(max(0.0, LEG_REACH ** 2 - a * a)) - HIP_Z - 0.005)
        raw.append(lim)
    k = n // 24
    smooth = [sum(raw[(i + j) % n] for j in range(-k, k + 1)) / (2 * k + 1) for i in range(n)]
    return [min(sm, r + 0.015) for sm, r in zip(smooth, raw)]


def legs_gait(rig, t, cycle, stance, lift, hips_table, dx=0.0, dy=0.0, out=4.0):
    """Moves the hips and plants both feet for phase t. Left heel strikes at t=0, right at 0.5 (the
    game plays a footstep sound at both)."""
    n = len(hips_table)
    i = t * n
    i0 = int(i) % n
    dz = hips_table[i0] + (hips_table[(i0 + 1) % n] - hips_table[i0]) * (i - int(i))
    hips = (dx, dy, dz)
    rig.move('Hips', dx=dx, dy=dy, dz=dz)
    for side, ph in (('L', t % 1.0), ('R', (t + 0.5) % 1.0)):
        f, z, toe, _ = gait_foot(ph, cycle, stance, lift)
        leg_ik(rig, side, hips, f, z, out=out, toe=toe)
    return hips


def arms_run(rig, t, swing, base, elbow, elbow_swing, out=7.0):
    """Arms swing opposite to the legs: right arm forward when the left heel strikes (t=0)."""
    c = math.cos(t * 2 * math.pi)
    rig.rot('UpperArm_R', fwd=base + swing * c, out=out)
    rig.rot('UpperArm_L', fwd=base - swing * c, out=out)
    rig.rot('Forearm_R', fwd=elbow + elbow_swing * max(0.0, c), out=-6)
    rig.rot('Forearm_L', fwd=elbow + elbow_swing * max(0.0, -c), out=-6)
    rig.rot('Hand_R', fwd=-8)
    rig.rot('Hand_L', fwd=-8)


def make_clip(rig, name, frames, loop, pose_fn):
    arm = rig.arm
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    if arm.animation_data is None:
        arm.animation_data_create()
    arm.animation_data.action = act
    if hasattr(arm.animation_data, 'action_slot') and len(act.slots) == 0:
        slot = act.slots.new(id_type='OBJECT', name='Skeleton')
        arm.animation_data.action_slot = slot
    for f in range(frames + 1):
        rig.clear()
        pose_fn(f / float(frames))
        rig.key(f)
    # Loops repeat the first frame at the end so they blend seamlessly.
    fcurves = []
    if hasattr(act, 'fcurves'):
        fcurves = list(act.fcurves)
    else:
        for layer in act.layers:
            for st in layer.strips:
                for bag in st.channelbags:
                    fcurves += list(bag.fcurves)
    for fc in fcurves:
        for kp in fc.keyframe_points:
            kp.interpolation = 'LINEAR' if loop else 'BEZIER'
    arm.animation_data.action = None
    track = arm.animation_data.nla_tracks.new()
    track.name = name
    strip = track.strips.new(name, 1, act)
    if hasattr(strip, 'action_slot') and len(act.slots):
        strip.action_slot = act.slots[0]
    track.mute = True
    return act


def build_animations(arm):
    rig = Rig(arm)

    def idle(t):  # relaxed stance: weight shifts slowly, chest breathes, arms hang loose
        s = math.sin(t * 2 * math.pi)
        c = math.cos(t * 2 * math.pi)
        hips = (0.012 * s, 0.0, -0.025 + 0.004 * math.cos(t * 4 * math.pi))
        rig.move('Hips', dx=hips[0], dz=hips[2])
        leg_ik(rig, 'L', hips, 0.05, ANKLE_Z, out=5)
        leg_ik(rig, 'R', hips, -0.04, ANKLE_Z, out=6)
        rig.rot('Spine', fwd=2, out=-0.8 * s)
        rig.rot('Chest', fwd=1.2 * math.sin(t * 4 * math.pi))
        rig.rot('Head', fwd=-1.5, out=1.5 * c)
        rig.rot('UpperArm_L', fwd=5 + 1.5 * s, out=9)
        rig.rot('UpperArm_R', fwd=5 - 1.5 * s, out=9)
        rig.rot('Forearm_L', fwd=16)
        rig.rot('Forearm_R', fwd=16)
        rig.rot('Hand_L', fwd=8, twist=-10)
        rig.rot('Hand_R', fwd=8, twist=-10)
    make_clip(rig, 'idle', 72, True, idle)

    # Jog (the normal run, 4.6 m/s): short ground contact, a little flight, arms bent.
    WALK = (CYCLE_WALK, 0.44, 0.18)
    walk_hips = gait_hips(*WALK, base_dz=-0.05)

    def walk(t):
        legs_gait(rig, t, *WALK, walk_hips, out=3)
        c = math.cos(t * 2 * math.pi)
        rig.rot('Spine', fwd=7, twist=5 * c)
        rig.rot('Chest', fwd=2, twist=3 * c)
        rig.rot('Head', fwd=-7, twist=-6 * c)
        arms_run(rig, t, swing=32, base=8, elbow=72, elbow_swing=14)
    make_clip(rig, 'walk', 24, True, walk)

    # Sprint: leaning forward, long strides, high heel kick, big arm drive.
    SPRINT = (CYCLE_SPRINT, 0.27, 0.34)
    sprint_hips = gait_hips(*SPRINT, base_dz=-0.07, dy=0.04)

    def sprint(t):
        legs_gait(rig, t, *SPRINT, sprint_hips, dy=0.04, out=3)
        c = math.cos(t * 2 * math.pi)
        rig.rot('Spine', fwd=17, twist=8 * c)
        rig.rot('Chest', fwd=6, twist=4 * c)
        rig.rot('Head', fwd=-19, twist=-9 * c)
        arms_run(rig, t, swing=55, base=14, elbow=88, elbow_swing=18, out=9)
    make_clip(rig, 'sprint', 16, True, sprint)

    CROUCH_DZ = -0.40

    def crouch_upper(t):
        s = math.sin(t * 2 * math.pi)
        rig.rot('Spine', fwd=24)
        rig.rot('Chest', fwd=5 + 1.0 * s)
        rig.rot('Head', fwd=-25)
        rig.rot('UpperArm_L', fwd=24, out=10)
        rig.rot('UpperArm_R', fwd=24, out=10)
        rig.rot('Forearm_L', fwd=48)
        rig.rot('Forearm_R', fwd=48)

    def crouch(t):  # low and steady: one foot ahead, the back heel up
        s = math.sin(t * 2 * math.pi)
        hips = (0.0, -0.03, CROUCH_DZ + 0.006 * s)
        rig.move('Hips', dy=hips[1], dz=hips[2])
        leg_ik(rig, 'L', hips, 0.16, ANKLE_Z, out=12)
        leg_ik(rig, 'R', hips, -0.16, ANKLE_Z + 0.06, out=10, toe=math.degrees(math.asin(0.06 / FOOT_LEN)))
        crouch_upper(t)
    make_clip(rig, 'crouch', 48, True, crouch)

    CROUCHW = (CYCLE_CROUCH, 0.58, 0.11)
    crouch_hips = gait_hips(*CROUCHW, base_dz=CROUCH_DZ + 0.02)

    def crouch_walk(t):
        legs_gait(rig, t, *CROUCHW, crouch_hips, dy=-0.02, out=12)
        crouch_upper(t)
        c = math.cos(t * 2 * math.pi)
        rig.rot('UpperArm_R', fwd=24 + 10 * c, out=10)
        rig.rot('UpperArm_L', fwd=24 - 10 * c, out=10)
        rig.rot('Spine', fwd=24, twist=4 * c)
    make_clip(rig, 'crouch_walk', 32, True, crouch_walk)

    def jump(t):  # legs tuck, arms up
        k = min(1.0, t * 3)
        rig.rot('Thigh_L', fwd=55 * k, out=10)
        rig.rot('Thigh_R', fwd=30 * k, out=10)
        rig.rot('Shin_L', fwd=-80 * k)
        rig.rot('Shin_R', fwd=-60 * k)
        rig.rot('Spine', fwd=6)
        rig.rot('UpperArm_L', fwd=150 * k, out=25)
        rig.rot('UpperArm_R', fwd=120 * k, out=25)
        rig.rot('Forearm_L', fwd=20)
        rig.rot('Forearm_R', fwd=20)
    make_clip(rig, 'jump', 12, False, jump)

    def skydive(t):  # belly down, arms and legs out like a real skydiver
        s = math.sin(t * 2 * math.pi)
        # The whole body tips over at the hips so it lies flat, head first, chest to the ground.
        rig.rot_about_tail('Hips', fwd=78 + 2 * s, dz=0.1)
        rig.rot('Spine', fwd=8)
        rig.rot('Chest', fwd=4)
        rig.rot('Head', fwd=-45)
        rig.rot('UpperArm_L', fwd=-30, out=80 + 3 * s)
        rig.rot('UpperArm_R', fwd=-30, out=80 - 3 * s)
        rig.rot('Forearm_L', fwd=70)
        rig.rot('Forearm_R', fwd=70)
        rig.rot('Thigh_L', fwd=-22, out=20)
        rig.rot('Thigh_R', fwd=-22, out=20)
        rig.rot('Shin_L', fwd=-60 + 4 * s)
        rig.rot('Shin_R', fwd=-60 - 4 * s)
    make_clip(rig, 'skydive', 48, True, skydive)

    def glide(t):  # hanging from the glider handles, legs swinging a little
        s = math.sin(t * 2 * math.pi)
        rig.rot('UpperArm_L', fwd=168, out=12)
        rig.rot('UpperArm_R', fwd=168, out=12)
        rig.rot('Forearm_L', fwd=10)
        rig.rot('Forearm_R', fwd=10)
        rig.rot('Spine', fwd=-6)
        rig.rot('Head', fwd=4)
        rig.rot('Thigh_L', fwd=12 + 6 * s, out=4)
        rig.rot('Thigh_R', fwd=12 - 6 * s, out=4)
        rig.rot('Shin_L', fwd=-30)
        rig.rot('Shin_R', fwd=-30)
    make_clip(rig, 'glide', 48, True, glide)

    def aim(t):
        s = math.sin(t * 2 * math.pi)
        hold_gun(rig)
        rig.rot('Chest', fwd=1.0 * s, twist=-12)
    make_clip(rig, 'aim', 48, True, aim)

    def shoot(t):  # a quick kick
        k = max(0.0, 1.0 - t * 1.6)
        hold_gun(rig, recoil=k)
        rig.rot('Chest', fwd=-4 * k, twist=-12)
    make_clip(rig, 'shoot', 8, False, shoot)

    def reload(t):
        drop = math.sin(min(1.0, t) * math.pi)
        hold_gun(rig, left_drop=drop)
        rig.rot('Head', fwd=25 * drop)
        rig.rot('Chest', fwd=6 * drop, twist=-12)
    make_clip(rig, 'reload', 36, False, reload)

    def build(t):
        s = math.sin(t * 4 * math.pi)
        rig.rot('UpperArm_L', fwd=95 + 8 * s, out=-10)
        rig.rot('UpperArm_R', fwd=95 - 8 * s, out=-10)
        rig.rot('Forearm_L', fwd=25)
        rig.rot('Forearm_R', fwd=25)
        rig.rot('Hand_L', twist=40)
        rig.rot('Hand_R', twist=40)
        rig.rot('Spine', fwd=6)
    make_clip(rig, 'build', 16, True, build)

    def heal(t):
        s_ = math.sin(t * 2 * math.pi)
        hips = (0.0, -0.02, -0.12 + 0.005 * s_)
        rig.move('Hips', dy=hips[1], dz=hips[2])
        leg_ik(rig, 'L', hips, 0.10, ANKLE_Z, out=8)
        leg_ik(rig, 'R', hips, -0.08, ANKLE_Z, out=8)
        rig.rot('UpperArm_L', fwd=70, out=-25)
        rig.rot('UpperArm_R', fwd=70, out=-25)
        rig.rot('Forearm_L', fwd=70)
        rig.rot('Forearm_R', fwd=70)
        rig.rot('Head', fwd=30)
        rig.rot('Spine', fwd=12)
    make_clip(rig, 'heal', 24, True, heal)

    def pick_idle(t):  # pickaxe held low, swaying a little
        sw = math.sin(t * 2 * math.pi)
        rig.move('Hips', dz=0.012 * sw)
        rig.rot('UpperArm_R', fwd=22 + 2 * sw, out=6)
        rig.rot('Forearm_R', fwd=28)
        rig.rot('Hand_R', fwd=-8)
        rig.rot('UpperArm_L', fwd=4, out=5)
        rig.rot('Forearm_L', fwd=14)
        rig.rot('Chest', fwd=1.5 * sw)
    make_clip(rig, 'pick_idle', 72, True, pick_idle)

    def swing(t):  # pickaxe: wind up over the shoulder, chop down, recover
        if t < 0.35:
            k = t / 0.35
            a = 60 + 100 * k
        elif t < 0.6:
            k = (t - 0.35) / 0.25
            a = 160 - 140 * k
        else:
            k = (t - 0.6) / 0.4
            a = 20 + 40 * k
        rig.rot('UpperArm_R', fwd=a, out=15)
        rig.rot('Forearm_R', fwd=25)
        rig.rot('Hand_R', fwd=-10)
        rig.rot('UpperArm_L', fwd=25, out=8)
        rig.rot('Forearm_L', fwd=30)
        rig.rot('Spine', fwd=(a - 60) * 0.08, twist=(a - 60) * 0.12)
    make_clip(rig, 'swing', 18, False, swing)

    def wave(t):
        s = math.sin(t * 6 * math.pi)
        rig.rot('UpperArm_R', fwd=140, out=40)
        rig.rot('Forearm_R', fwd=30, out=25 * s)
        rig.rot('Hand_R', out=10 * s)
        rig.rot('UpperArm_L', fwd=3, out=4)
        rig.rot('Forearm_L', fwd=12)
        rig.rot('Head', out=-8)
    make_clip(rig, 'wave', 48, True, wave)

    def dance(t):
        s = math.sin(t * 4 * math.pi)
        c = math.cos(t * 4 * math.pi)
        rig.move('Hips', dz=-0.05 + 0.05 * abs(s), dx=0.04 * c)
        rig.rot('Spine', twist=12 * s)
        rig.rot('Chest', twist=8 * s)
        rig.rot('Head', out=12 * c)
        rig.rot('UpperArm_L', fwd=90 + 60 * s, out=40)
        rig.rot('UpperArm_R', fwd=90 - 60 * s, out=40)
        rig.rot('Forearm_L', fwd=60)
        rig.rot('Forearm_R', fwd=60)
        rig.rot('Thigh_L', fwd=10 * max(0, s), out=12)
        rig.rot('Thigh_R', fwd=10 * max(0, -s), out=12)
        rig.rot('Shin_L', fwd=-25 * max(0, s))
        rig.rot('Shin_R', fwd=-25 * max(0, -s))
    make_clip(rig, 'dance', 48, True, dance)
    rig.clear()


# ------------------------------------------------------------------ textures
def paint_textures(body, only=None):
    """Paints every skin's texture. Each texel is mapped back to where it sits on the body, and
    the colour comes from a small "shader" of that position: shirt here, belt there, eyes on
    the front of the head..."""
    mesh = body.data
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active.data
    verts = np.array([v.co[:] for v in mesh.vertices], dtype=np.float64)
    vnorm = np.array([v.normal[:] for v in mesh.vertices], dtype=np.float64)
    tris = []
    for lt in mesh.loop_triangles:
        tris.append((lt.vertices, [uv[l].uv[:] for l in lt.loops]))

    # Rasterise the triangle index into the texture.
    idx_img = Image.new('I', (TEX, TEX), 0)
    d = ImageDraw.Draw(idx_img)
    for i, (vi, uvs) in enumerate(tris):
        pts = [(u * TEX, (1 - v) * TEX) for u, v in uvs]
        d.polygon(pts, fill=i + 1, outline=i + 1)
    idx = np.array(idx_img, dtype=np.int32)
    ys, xs = np.nonzero(idx)
    ti = idx[ys, xs] - 1
    tri_v = np.array([t[0] for t in tris], dtype=np.int32)
    tri_uv = np.array([t[1] for t in tris], dtype=np.float64)
    px = (xs + 0.5) / TEX
    py = 1 - (ys + 0.5) / TEX
    a = tri_uv[ti, 0]
    b = tri_uv[ti, 1]
    c = tri_uv[ti, 2]
    v0 = b - a
    v1 = c - a
    v2 = np.stack([px, py], 1) - a
    d00 = (v0 * v0).sum(1); d01 = (v0 * v1).sum(1); d11 = (v1 * v1).sum(1); d20 = (v2 * v0).sum(1); d21 = (v2 * v1).sum(1)
    den = d00 * d11 - d01 * d01
    den[np.abs(den) < 1e-12] = 1e-12
    w1 = (d11 * d20 - d01 * d21) / den
    w2 = (d00 * d21 - d01 * d20) / den
    w0 = 1 - w1 - w2
    w = np.clip(np.stack([w0, w1, w2], 1), 0, 1)
    w /= w.sum(1, keepdims=True)
    P = (verts[tri_v[ti, 0]] * w[:, :1] + verts[tri_v[ti, 1]] * w[:, 1:2] + verts[tri_v[ti, 2]] * w[:, 2:3])
    N = (vnorm[tri_v[ti, 0]] * w[:, :1] + vnorm[tri_v[ti, 1]] * w[:, 1:2] + vnorm[tri_v[ti, 2]] * w[:, 2:3])
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-6)
    X, Y, Z = P[:, 0], P[:, 1], P[:, 2]
    front = -N[:, 1]  # 1 facing the front

    rng = np.random.default_rng(7)
    noise = rng.random(len(X))
    # Low-frequency cloth variation.
    cloth = 0.94 + 0.06 * (0.5 + 0.5 * np.sin(X * 60) * np.sin(Z * 55 + Y * 30))

    def paint(skin_index):
        name, shirt, pants, hair, accent, hatcol, hair_style, hat, headband, pattern, tone_i, rarity = SKINS[skin_index]
        tone = np.array(SKIN_TONES[tone_i])
        shirt = np.array(shirt); pants = np.array(pants); hair = np.array(hair); accent = np.array(accent); hatcol = np.array(hatcol)
        dark = np.array((0.11, 0.11, 0.13))
        col = np.zeros((len(X), 3))
        mask = np.zeros(len(X))  # accent mask (alpha)

        def region(cond, c, m=0.0):
            col[cond] = c
            mask[cond] = m

        # ---- the outfit is built like real clothing: a fitted long-sleeve top with a front panel,
        # darker side panels, piping and an arm band; trousers with side stripes, a knee pad and
        # thigh straps; leather belt with pouches; gloves; tall laced boots.
        region(np.ones(len(X), bool), tone)
        torso = np.abs(X) < 0.29
        arm = (np.abs(X) >= 0.29) & (Z > 0.86)
        leg = (Z < 1.0) & torso
        out_side = N[:, 0] * np.sign(X)           # 1 on the outer side of a limb
        leather = np.array((0.36, 0.24, 0.15)) * 0.75 + pants * 0.25
        metal = np.array((0.72, 0.73, 0.76))
        region(leg, pants)                                                                            # trousers
        top = ((Z >= 1.0) & torso) | (arm & (Z >= 1.0))
        region(top, shirt)                                                                            # top + sleeves
        shirt_area = top
        second = shirt * 0.6 + accent * 0.4
        if pattern == 1:    # stripes
            region(shirt_area & (np.sin(Z * 55) > 0.3), second)
        elif pattern == 2:  # camo blobs
            blob = np.sin(X * 31 + Z * 17) * np.sin(Y * 27 - Z * 23 + 1.3) * np.sin(X * 13 - Y * 19 + Z * 29)
            region(shirt_area & (blob > 0.15), shirt * 0.7)
            region(shirt_area & (blob < -0.25), second)
        elif pattern == 3:  # checker
            region(shirt_area & (((np.floor(X * 14) + np.floor(Z * 14)) % 2) == 0), second)
        elif pattern == 4:  # hex plates
            hx = X * 22
            hz_ = Z * 22 / 0.866
            row = np.floor(hz_)
            cx = np.floor(hx + 0.5 * (row % 2)) - 0.5 * (row % 2) + 0.5
            cz = row + 0.5
            dd = np.sqrt((hx - cx) ** 2 + ((hz_ - cz) * 0.866) ** 2)
            region(shirt_area & (dd > 0.42), shirt * 0.55)
        elif pattern == 5:  # stars (dots)
            sx_ = np.floor(X * 16) + 0.5
            sz_ = np.floor(Z * 16) + 0.5
            dd = np.sqrt((X * 16 - sx_) ** 2 + (Z * 16 - sz_) ** 2)
            region(shirt_area & (dd < 0.22), accent, 1)
        elif pattern == 6:  # flames rising from the belt
            fh = 1.08 + 0.2 * (0.5 + 0.5 * np.sin(X * 60 + Y * 25)) + 0.08 * np.sin(X * 140)
            region(shirt_area & torso & (Z < fh), accent, 1)
            region(shirt_area & torso & (Z < fh - 0.08), accent * 0.75 + np.array((0.3, 0.05, 0.0)) * 0.25, 1)
        elif pattern == 7:  # sash across the chest
            region(shirt_area & torso & (np.abs(X + (Z - 1.3) * 0.8) < 0.06) & (front > -0.3), accent, 1)
        elif pattern == 8:  # diagonal racing stripes
            region(shirt_area & (np.sin((X + Z) * 48) > 0.55), second)
            region(shirt_area & (np.sin((X + Z) * 48) > 0.85), accent, 1)
        elif pattern == 9:  # plaid
            a_ = (np.sin(X * 70) > 0.4); b_ = (np.sin(Z * 70) > 0.4)
            region(shirt_area & (a_ ^ b_), second)
            region(shirt_area & a_ & b_, shirt * 0.55)
        elif pattern == 10:  # two-tone: one half of the top in the second colour
            region(shirt_area & (X * (1 if skin_index % 2 else -1) > 0.0), second)
        elif pattern == 11:  # chevrons pointing up
            region(shirt_area & torso & (np.sin((Z + 0.5 * np.abs(X)) * 50) > 0.4), second)
        elif pattern == 12:  # lightning bolt down the chest
            bolt = (np.abs(X - 0.06 * np.sign(np.sin((Z - 1.1) * 20))) < 0.035) & (Z > 1.1) & (Z < 1.52) & (front > 0.4)
            region(shirt_area & torso & bolt, accent, 1)
        elif pattern == 13:  # panelled jacket
            region(shirt_area & torso & ((np.abs(X) > 0.2) | (Z > 1.5)), shirt * 0.6)

        # Tailoring on the top: darker side panels, a lighter front panel that narrows to the waist
        # (piped in the trim colour), a raised collar and ribbed cuffs.
        chest = top & torso
        region(chest & (np.abs(X) > 0.17) & (np.abs(front) < 0.75), col[chest & (np.abs(X) > 0.17) & (np.abs(front) < 0.75)] * 0.8)
        pw = 0.05 + np.clip(Z - 1.08, 0, 0.5) * 0.14          # panel half-width: 5 cm at the waist
        panel = chest & (front > 0.35) & (np.abs(X) < pw) & (Z < 1.57)
        region(panel, np.clip(col[panel] * 0.55 + np.array((0.93, 0.93, 0.95)) * 0.45, 0, 1))
        region(chest & (front > 0.35) & (np.abs(np.abs(X) - pw) < 0.007) & (Z < 1.57), accent, 1)  # piping
        collar = chest & (Z >= 1.57)
        region(collar, shirt * 0.62)
        region(collar & (np.abs(np.sin(X * 260)) > 0.8), shirt * 0.5)                         # ribbing
        region(chest & (np.abs(Z - 1.575) < 0.006), accent, 1)
        region(arm & (Z >= 1.0) & (Z < 1.04), shirt * 0.58)                                     # cuffs
        region(arm & (Z >= 1.0) & (Z < 1.04) & (np.abs(np.sin(Y * 300 + X * 300)) > 0.85), shirt * 0.45)
        # Sleeve stripe on the outside of each arm, and an arm band on the left upper arm.
        region(arm & (Z > 1.06) & (out_side > 0.72), accent, 1)
        region(arm & (Z > 1.06) & (out_side > 0.72) & (out_side < 0.8), shirt * 0.55)
        region(arm & (X > 0) & (Z > 1.36) & (Z < 1.41), accent * 0.9, 1)
        # Trousers: double stripe down the outside, knee pads, thigh straps on the right leg.
        region(leg & (Z > 0.3) & (out_side > 0.86), accent, 1)
        region(leg & (Z > 0.3) & (out_side > 0.9) & (out_side < 0.94), pants * 0.7)
        for sx in (1, -1):
            kn = np.array(J['Knee_L'][0]) * np.array((sx, 1, 1))
            dk = np.sqrt(((X - kn[0]) * 1.25) ** 2 + ((Z - kn[2] - 0.015) * 0.8) ** 2)
            pad = (dk < 0.075) & (front > 0.15) & leg
            region(pad, accent * 0.55 + leather * 0.45, 1)
            region(pad & (dk > 0.064), accent * 0.4 + leather * 0.45)                         # rim
            region(pad & (np.abs(X - kn[0]) < 0.004), accent * 0.3 + leather * 0.4)              # pad seam
        for zc in (0.73, 0.82):
            strap = leg & (X < 0) & (np.abs(Z - zc) < 0.014)
            region(strap, leather)
            region(strap & (np.abs(Z - zc) > 0.011), leather * 0.7)
            region(strap & (front > 0.6) & (np.abs(X + 0.11) < 0.018), metal)                   # buckle
        # Belt with a metal buckle, loops and two pouches.
        belt = torso & (Z >= 0.97) & (Z < 1.04)
        region(belt, leather * 0.8)
        region(belt & ((np.abs(Z - 0.975) < 0.004) | (np.abs(Z - 1.035) < 0.004)), leather * 0.55)
        region(belt & (front > 0.6) & (np.abs(X) < 0.032), metal)
        region(belt & (front > 0.6) & (np.abs(X) < 0.02) & (np.abs(Z - 1.005) < 0.014), leather * 0.5)
        for sx in (1, -1):
            pouch = torso & (np.abs(X - 0.17 * sx) < 0.045) & (Z > 0.9) & (Z < 0.99) & (np.abs(front) < 0.85)
            region(pouch, leather * 0.95)
            region(pouch & (Z > 0.965), leather * 0.75)                                         # flap
            region(pouch & (np.abs(Z - 0.965) < 0.003), leather * 0.5)
        # Gloves: dark with a padded back and a cuff in the trim colour.
        glove = arm & (Z < 1.0)
        gcol = np.array((0.2, 0.2, 0.22)) * 0.7 + pants * 0.3
        region(glove, gcol)
        region(glove & (out_side > 0.45) & (Z < 0.96), gcol * 0.7)
        region(glove & (Z > 0.965), accent * 0.7 + gcol * 0.3, 1)
        # Tall leather boots: folded cuff, criss-cross laces, toe cap, treaded sole.
        boot = Z < 0.31
        region(boot, leather)
        region(boot & (Z > 0.27), leather * 0.75)
        lace_area = boot & (front > 0.55) & (Z > 0.14) & (Z < 0.27)
        lx = X - np.sign(X) * 0.12
        region(lace_area & (np.abs(lx) < 0.035), leather * 0.6)                                   # tongue
        cross = (np.abs(np.sin((Z + lx * 1.6) * 110)) < 0.16) | (np.abs(np.sin((Z - lx * 1.6) * 110)) < 0.16)
        region(lace_area & (np.abs(lx) < 0.03) & cross, np.array((0.85, 0.82, 0.74)))             # laces
        region(boot & (Z < 0.1) & (front > 0.3), leather * 0.85)                                  # toe cap
        region(boot & (Z < 0.045), np.array((0.12, 0.11, 0.1)))                                   # sole
        region(boot & (Z < 0.045) & (np.sin(Y * 220) > 0.4), np.array((0.08, 0.075, 0.07)))       # tread
        region((Z >= 0.3) & (Z < 0.315) & leg, accent * 0.8, 1)                                    # boot-top trim

        # Head: hair on the top and back. Everything is measured from the head joint so the
        # face follows the head when the proportions change.
        hz = J['Head'][0][2]
        head = Z > hz - 0.14 * HK
        region(head, tone)
        hairline = hz + 0.12 * HK - 0.05 * HK * np.clip(front, 0, 1)
        hair_cond = head & ((Z > hairline) | ((Y > 0.09 * HK) & (Z > hz - 0.04 * HK)) | ((np.abs(X) > 0.16 * HK) & (Z > hz + 0.04 * HK)))
        region(hair_cond, hair, 0)
        # Face: eyes (white + pupil), brows, mouth.
        for sx in (1, -1):
            ex, ez = 0.078 * HK * sx, hz + 0.015 * HK
            de = np.sqrt(((X - ex) / (0.042 * HK)) ** 2 + ((Z - ez) / (0.030 * HK)) ** 2)
            region((de < 1.0) & (front > 0.25) & head, np.array((0.97, 0.97, 0.97)))
            dp = np.sqrt(((X - ex - 0.006 * HK * sx) / (0.02 * HK)) ** 2 + ((Z - ez) / (0.022 * HK)) ** 2)
            region((dp < 1.0) & (front > 0.25) & head, np.array((0.07, 0.06, 0.08)))
            dh = np.sqrt(((X - ex - 0.012 * HK * sx) / (0.007 * HK)) ** 2 + ((Z - ez - 0.008 * HK) / (0.007 * HK)) ** 2)
            region((dh < 1.0) & (front > 0.25) & head, np.array((0.95, 0.95, 0.95)))
            brow_z = hz + 0.06 * HK - (np.abs(X) - 0.078 * HK) * 0.25
            region((np.abs(Z - brow_z) < 0.009 * HK) & (np.abs(X - ex) < 0.05 * HK) & (front > 0.25) & head & ~hair_cond, hair * 0.7)
        mouth_z = hz - 0.065 * HK + (X / HK) ** 2
        region((np.abs(Z - mouth_z) < 0.006 * HK) & (np.abs(X) < 0.045 * HK) & (front > 0.3) & head, np.array((0.45, 0.2, 0.2)))
        # ---- details: seams, a zip, folds where cloth bunches (elbows, knees, waist, ankles) and
        # stitching. Thin dark lines and soft creases make flat colour read as sewn clothing.
        seam = lambda cond: region(cond, col[cond] * 0.72)
        seam(chest & (np.abs(X) < 0.004) & (front > 0.5) & (Z > 1.05) & (Z < 1.57))                       # zip
        seam(chest & (np.abs(np.abs(X) - 0.009) < 0.002) & (front > 0.5) & (Z > 1.05) & (Z < 1.57))
        seam(chest & (np.abs(X) > 0.165) & (np.abs(X) < 0.175) & (np.abs(front) < 0.75))                 # side panel seams
        seam(arm & (Z > 1.06) & (out_side < -0.85))                                                      # inner sleeve seam
        seam(leg & (Z > 0.3) & (out_side < -0.88))                                                       # inseam
        seam(leg & (np.abs(X) < 0.012) & (Z > 0.78) & (Z < 0.97) & (front > 0.5))                        # fly
        stitch = np.sin((Z + X) * 900) > 0.2
        lighten = lambda cond: region(cond, np.clip(col[cond] * 1.18 + 0.03, 0, 1), mask[cond])
        lighten(chest & (front > 0.35) & (np.abs(np.abs(X) - pw - 0.012) < 0.0018) & (Z < 1.57) & stitch)
        lighten(leg & (Z > 0.3) & (np.abs(out_side - 0.8) < 0.012) & stitch)
        folds = np.zeros(len(X))
        for sx in (1, -1):
            el = np.array(J['Elbow_L'][0]) * np.array((sx, 1, 1))
            d_el = np.abs(Z - el[2])
            folds += arm * (d_el < 0.07) * (1 - d_el / 0.07) * np.clip(np.sin((Z - el[2]) * 160 + np.abs(X) * 40), 0, 1)
            kn = np.array(J['Knee_L'][0]) * np.array((sx, 1, 1))
            d_kn = np.abs(Z - kn[2])
            folds += leg * (front < -0.2) * (d_kn < 0.08) * (1 - d_kn / 0.08) * np.clip(np.sin((Z - kn[2]) * 140 + X * 30), 0, 1)
        d_w = np.abs(Z - 1.075)
        folds += chest * (d_w < 0.04) * (1 - d_w / 0.04) * np.clip(np.sin(X * 90 + Z * 40), 0, 1) * 0.7
        d_a = np.abs(Z - 0.33)
        folds += leg * (d_a < 0.04) * (1 - d_a / 0.04) * np.clip(np.sin(X * 120 + Y * 60 + Z * 200), 0, 1)
        crotch_fold = leg & (Z > 0.8) & (np.abs(X) < 0.12) & (front > 0.2)
        folds += crotch_fold * np.clip(np.sin((np.abs(X) * 1.4 - (Z - 0.8)) * 120), 0, 1) * 0.6
        col *= (1.0 - 0.16 * np.clip(folds, 0, 1))[:, None]
        # Face: brow ridge shadow, cheek colour, nose and chin shading, lips.
        hz = J['Head'][0][2]
        ck = 0.9 * HK
        cheeks = head & (np.abs(np.abs(X) - 0.085 * HK) < 0.035 * HK) & (np.abs(Z - (hz - 0.03 * HK)) < 0.03 * HK) & (front > 0.3)
        col[cheeks] = col[cheeks] * 0.92 + np.array((0.9, 0.35, 0.35)) * 0.08
        nose = head & (np.abs(X) < 0.012 * HK) & (Z > hz - 0.05 * HK) & (Z < hz + 0.01 * HK) & (front > 0.8)
        col[nose] *= 0.93
        nostril = head & (np.abs(np.abs(X) - 0.012 * HK) < 0.006 * HK) & (np.abs(Z - (hz - 0.05 * HK)) < 0.005 * HK) & (front > 0.7)
        col[nostril] *= 0.7
        lid = head & (np.abs(X) < 0.13 * HK) & (np.abs(X) > 0.03 * HK) & (Z > hz + 0.043 * HK) & (Z < hz + 0.052 * HK) & (front > 0.3) & ~hair_cond
        col[lid] *= 0.85
        chin = head & (Z < hz - 0.11 * HK) & (front > 0.2)
        col[chin] *= 0.9
        ears = head & (np.abs(X) > 0.11 * HK) & (np.abs(Z - hz) < 0.03 * HK) & (np.abs(front) < 0.4)
        col[ears] *= 0.93
        # Hair: strands.
        strand = 0.85 + 0.15 * (0.5 + 0.5 * np.sin(X * 420 + Y * 260 + Z * 90) * np.sin(Z * 310 - X * 150))
        col[hair_cond] *= strand[hair_cond][:, None]
        # ---- shading: a soft top-down light, darker in the crevices (armpits, crotch, under the
        # chin and the belt), a cloth weave and a little grain.
        shade = 0.90 + 0.10 * np.clip(N[:, 2] * 0.5 + 0.5, 0, 1)
        ao = np.ones(len(X))
        for sx in (1, -1):
            sh = np.array(J['Shoulder_L'][0]) * np.array((sx, 1, 1))
            pit = np.sqrt((X - sh[0]) ** 2 + (Z - sh[2] + 0.14) ** 2)
            ao *= 1.0 - 0.35 * np.clip(1.0 - pit / 0.12, 0, 1)
        crotch = np.sqrt((X * 1.6) ** 2 + (Z - 0.95) ** 2)
        ao *= 1.0 - 0.3 * np.clip(1.0 - crotch / 0.1, 0, 1)
        ao *= 1.0 - 0.25 * np.clip(1.0 - np.abs(Z - 1.63) / 0.05, 0, 1) * (np.abs(X) < 0.12)       # under the chin
        ao *= 1.0 - 0.2 * np.clip(1.0 - np.abs(Z - 1.07) / 0.03, 0, 1) * torso                     # under the belt
        ao *= 1.0 - 0.15 * np.clip(1.0 - np.abs(Z - 1.25) / 0.04, 0, 1) * (np.abs(X) >= 0.3)       # inside the elbow
        weave = 1.0 + 0.035 * np.sin(X * 900 + Y * 300) * np.sin(Z * 900 + Y * 200)
        weave = np.where((Z < 1.64) & ~head & ~boot & ~glove, weave, 1.0)
        col *= (shade * ao * weave)[:, None]
        col *= (0.98 + 0.04 * noise)[:, None]
        # Leather and metal pieces are a touch glossy: lighten their tops a little more.
        hard = boot | glove
        col[hard] *= (0.95 + 0.12 * np.clip(N[hard, 2], 0, 1))[:, None]

        img = np.zeros((TEX, TEX, 4), dtype=np.float32)
        img[ys, xs, :3] = np.clip(col, 0, 1)
        img[ys, xs, 3] = mask
        filled = np.zeros((TEX, TEX), bool)
        filled[ys, xs] = True
        # Bleed colours out past the island edges so seams don't show.
        for _ in range(10):
            empty = ~filled
            acc = np.zeros_like(img)
            cnt = np.zeros((TEX, TEX), np.float32)
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                sh_img = np.roll(img, (dy, dx), (0, 1))
                sh_f = np.roll(filled, (dy, dx), (0, 1))
                acc += sh_img * sh_f[:, :, None]
                cnt += sh_f
            take = empty & (cnt > 0)
            img[take] = acc[take] / cnt[take][:, None]
            filled |= take
        # Flat colour patches for the add-on meshes.
        def patch(uvp, c, m):
            x0 = int(uvp[0] * TEX) - 24; y0 = int((1 - uvp[1]) * TEX) - 24
            img[y0:y0 + 48, x0:x0 + 48, :3] = c
            img[y0:y0 + 48, x0:x0 + 48, 3] = m
        patch(UV_HAIR, hair, 0)
        patch(UV_ACCENT, accent, 1)
        patch(UV_DARK, dark, 0)
        patch(UV_METAL, (0.75, 0.75, 0.8), 0)
        patch(UV_HAT, hatcol, 0)
        out = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), 'RGBA')
        os.makedirs(os.path.join(OUT, 'skins'), exist_ok=True)
        out.save(os.path.join(OUT, 'skins', '%d_%s.png' % (skin_index, slug(name))), optimize=True)
        print('painted', name)

    for i in (only if only is not None else range(len(SKINS))):
        paint(i)


# ------------------------------------------------------------------ main
def copy_object(src, name, positions=None):
    """A copy of a mesh object (same weights, UVs and attributes), optionally with new positions."""
    obj = src.copy()
    obj.data = src.data.copy()
    obj.name = name
    obj.data.name = name
    bpy.context.collection.objects.link(obj)
    if positions is not None:
        obj.data.vertices.foreach_set('co', np.asarray(positions, np.float32).ravel())
        obj.data.update()
    return obj


def mesh_points(obj):
    return np.array([v.co[:] for v in obj.data.vertices])


def main():
    import builds, rigging, kit, face, hair_cards, atlas
    from meshutil import body_bvh, body_weights, make_object, fix_normals
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    scene = bpy.context.scene
    scene.render.fps = FPS
    body, arm = build_body()
    lods = finalize_body(body, arm, before_lods=lambda o: atlas.add_atlas_body(o, OPTS['atlas']))
    bvh = body_bvh(body)
    face_pts = rigging.add_extra_bones(arm, J, HK, bvh)
    chains = rigging.swing_chains(J, HK)

    # Body builds: reshaped copies sharing topology, UVs and weights.
    for b in (1, 2):
        for i, lod in enumerate(lods):
            name = 'Body_B%d' % b + ('_LOD%d' % i if i else '')
            o = copy_object(lod, name, builds.displace(mesh_points(lod), b, J, E))
            o.parent = arm

    extras = build_hats(arm)
    hair = hair_cards.build_hair(bvh, arm, J, HK, chains)
    face_obj, mouth_obj = face.build_face(face_pts, bvh, arm, J, HK)

    # Outfit kit.
    W, wnames = body_weights(body)
    pieces = kit.build_pieces(J, HK, bvh)
    kit_b0, copies = [], {}
    for name in kit.KIT:
        mb, spec = pieces[name]
        pts = np.array(mb.v)
        wspec = kit.piece_weights(name, spec, pts, body, bvh, W, wnames, J, chains)
        objname = name if name in kit.HEAD_PIECES else name + '_B0'
        if isinstance(wspec, tuple) and isinstance(wspec[0], str):   # ('rigid', bone)
            obj = make_object(objname, mb, arm, rigid=wspec[1])
        elif isinstance(wspec, tuple):
            obj = make_object(objname, mb, arm, weights=wspec[0], names=wspec[1])   # extra (chain) bones
        else:
            obj = make_object(objname, mb, arm, weights=wspec, names=wnames)
        fix_normals(obj)
        kit_b0.append(obj)
        if name not in kit.HEAD_PIECES:
            copies[obj.name] = []
            for b in (1, 2):
                o = copy_object(obj, '%s_B%d' % (name, b), builds.displace(pts, b, J, E))
                o.parent = arm
                copies[obj.name].append(o)
    from meshutil import Z_SKIN, Z_DARK, Z_HAIR
    fix_normals(face_obj, away_from=[face_pts['eye_L'], face_pts['eye_R']], zones={Z_SKIN, Z_DARK})
    fix_normals(mouth_obj, facing=(0, -1, 0), zones={Z_DARK})
    for h in hair:
        fix_normals(h, away_from=[(0, 0.01, J['Head'][0][2])], zones={Z_HAIR})

    build_animations(arm)
    if OPTS['paint']:
        paint_textures(body)

    # Atlas: UV layout always (the game needs the second UV set), bakes unless --no-bake.
    atlas.layout_kit(kit_b0 + [face_obj, mouth_obj] + hair, OPTS['atlas'], copies)
    for o in extras:
        atlas.set_neutral_atlas(o)
    eyes = [face_pts['eye_L'], face_pts['eye_R']]
    atlas.build_atlas(body, kit_b0, [face_obj, mouth_obj], hair, J, HK, OPTS['atlas'], OUT, bake=OPTS['bake'], eye_points=eyes)
    os.makedirs(OUT, exist_ok=True)
    for o in bpy.data.objects:
        o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'character.glb'), export_format='GLB', export_apply=False,
                              export_animations=True, export_animation_mode='NLA_TRACKS', export_skins=True,
                              export_materials='NONE', export_texcoords=True, export_normals=True, export_yup=True,
                              export_force_sampling=True, export_frame_step=1, export_def_bones=False,
                              export_optimize_animation_size=False, export_nla_strips_merged_animation_name='',
                              use_selection=True, export_colors=False) if 'export_colors' in bpy.ops.export_scene.gltf.get_rna_type().properties else \
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'character.glb'), export_format='GLB', export_apply=False,
                              export_animations=True, export_animation_mode='NLA_TRACKS', export_skins=True,
                              export_materials='NONE', export_texcoords=True, export_normals=True, export_yup=True,
                              export_force_sampling=True, export_frame_step=1, export_def_bones=False,
                              export_optimize_animation_size=False, use_selection=True)
    print('body tris', sum(len(o.data.polygons) for o in lods), [len(o.data.polygons) for o in lods])
    print('wrote', os.path.join(OUT, 'character.glb'))
    if OPTS.get('preview'):
        import kit_preview
        kit_preview.render(OPTS['preview'], ['STEEL KNIGHT', 'PILOT', 'RAVEN', 'ARCTIC OPS', 'DESERT HAWK', 'VIOLET STORM'], arm)


if __name__ == '__main__':
    main()
