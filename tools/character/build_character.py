# Blockfall character builder.
#
# Builds the shared Blockfall character inside Blender (run with the `bpy` Python module or
# `blender -b --python build_character.py`), then writes:
#   assets/character.glb      the body (3 levels of detail), hair styles, headband, the shared
#                             skeleton, and every animation
#   assets/skins/<n>.png      one painted texture per locker skin
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

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'assets')
TEX = 2048
FPS = 24

# ------------------------------------------------------------------ the body
# Blender axes: X right (character's left), -Y front, Z up. Metres.
J = {  # joint: (position, (radius x, radius y))
    'Hips':    ((0, 0, 0.97), (0.19, 0.125)),
    'Spine':   ((0, 0, 1.14), (0.155, 0.11)),     # waist
    'Chest':   ((0, 0, 1.40), (0.25, 0.135)),     # broad, flat chest
    'Neck':    ((0, 0, 1.55), (0.065, 0.065)),
    'Head':    ((0, 0.01, 1.74), (0.18, 0.195)),
    'HeadTop': ((0, 0.0, 1.93), (0.115, 0.12)),
}
for s, sx in (('L', 1), ('R', -1)):
    J['Shoulder_' + s] = ((0.27 * sx, 0, 1.48), (0.095, 0.09))
    J['Elbow_' + s] = ((0.33 * sx, 0.0, 1.21), (0.065, 0.062))
    J['Wrist_' + s] = ((0.36 * sx, 0.0, 0.97), (0.055, 0.05))
    J['Hand_' + s] = ((0.37 * sx, 0.0, 0.86), (0.075, 0.04))     # a flat mitt
    J['Hip_' + s] = ((0.115 * sx, 0, 0.94), (0.115, 0.115))
    J['Knee_' + s] = ((0.125 * sx, 0, 0.52), (0.085, 0.09))
    J['Ankle_' + s] = ((0.13 * sx, 0, 0.12), (0.075, 0.075))
    J['Toe_' + s] = ((0.13 * sx, -0.17, 0.05), (0.09, 0.045))
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


def finalize_body(obj, arm):
    """Applies Skin + Subdivision so the body is a real mesh, unwraps it, and makes the LODs."""
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


def build_hair(arm):
    head = Vector(J['Head'][0])
    objs = []
    # 0: spiky, swept back.
    bm = bmesh.new()
    for k in range(7):
        a = (k - 3) * 0.2
        x = (k - 3) * 0.055
        m = Matrix.Translation(head + Vector((x, -0.03 + abs(k - 3) * 0.025, 0.215))) @ Matrix.Rotation(-0.95 + a * 0.4, 4, 'X') @ \
            Matrix.Rotation((k - 3) * 0.28, 4, 'Y') @ Matrix.Diagonal((0.05, 0.05, 0.15, 1.0))
        bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=1.0, matrix=m)
    sphere(bm, head + Vector((0, 0.02, 0.13)), (0.215, 0.225, 0.105))
    objs.append(rigid_mesh('Hair0', bm, arm, UV_HAIR, head))
    # 1: a flat cap of hair.
    bm = bmesh.new()
    sphere(bm, head + Vector((0, 0.03, 0.12)), (0.22, 0.235, 0.12), 16, 10)
    objs.append(rigid_mesh('Hair1', bm, arm, UV_HAIR, head))
    # 2: mohawk.
    bm = bmesh.new()
    for k in range(5):
        m = Matrix.Translation(head + Vector((0, -0.12 + k * 0.07, 0.24))) @ Matrix.Rotation(-0.6 + k * 0.3, 4, 'X') @ \
            Matrix.Diagonal((0.05, 0.075, 0.2, 1.0))
        bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=1.0, matrix=m)
    objs.append(rigid_mesh('Hair2', bm, arm, UV_HAIR, head))
    # 3: long, down the back.
    bm = bmesh.new()
    sphere(bm, head + Vector((0, 0.03, 0.12)), (0.22, 0.235, 0.12), 16, 10)
    sphere(bm, head + Vector((0, 0.17, -0.12)), (0.19, 0.09, 0.3), 14, 10)
    objs.append(rigid_mesh('Hair3', bm, arm, UV_HAIR, head))
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


def legs_walk(rig, t, amp=32.0, knee=38.0, stride_lift=True):
    """t in 0..1 through the cycle."""
    a = math.sin(t * 2 * math.pi)
    rig.rot('Thigh_L', fwd=a * amp)
    rig.rot('Thigh_R', fwd=-a * amp)
    # Knee bends on the leg that is swinging forward / passing under the body.
    bl = max(0.0, math.sin((t + 0.25) * 2 * math.pi)) * knee
    br = max(0.0, math.sin((t + 0.75) * 2 * math.pi)) * knee
    rig.rot('Shin_L', fwd=-bl)
    rig.rot('Shin_R', fwd=-br)
    rig.rot('Foot_L', fwd=bl * 0.2)
    rig.rot('Foot_R', fwd=br * 0.2)


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

    def idle(t):
        s = math.sin(t * 2 * math.pi)
        rig.move('Hips', dz=0.012 * s)
        rig.rot('Chest', fwd=1.5 * s)
        rig.rot('Head', fwd=-1.0 * s, out=2.0 * math.sin(t * 2 * math.pi + 1))
        arms_down(rig, sway=2.0 * s)
        rig.rot('Thigh_L', out=3)
        rig.rot('Thigh_R', out=3)
    make_clip(rig, 'idle', 72, True, idle)

    def walk(t):
        legs_walk(rig, t)
        rig.move('Hips', dz=0.025 * abs(math.sin(t * 2 * math.pi)))
        rig.rot('Spine', fwd=4, twist=4 * math.sin(t * 2 * math.pi))
        a = math.sin(t * 2 * math.pi)
        rig.rot('UpperArm_L', fwd=-a * 28, out=5)
        rig.rot('UpperArm_R', fwd=a * 28, out=5)
        rig.rot('Forearm_L', fwd=20 + 10 * max(0, -a))
        rig.rot('Forearm_R', fwd=20 + 10 * max(0, a))
    make_clip(rig, 'walk', 24, True, walk)

    def sprint(t):
        legs_walk(rig, t, amp=48, knee=75)
        rig.move('Hips', dz=0.05 * abs(math.sin(t * 2 * math.pi)) - 0.02)
        rig.rot('Spine', fwd=22, twist=6 * math.sin(t * 2 * math.pi))
        rig.rot('Chest', fwd=8)
        rig.rot('Head', fwd=-18)
        a = math.sin(t * 2 * math.pi)
        rig.rot('UpperArm_L', fwd=-a * 55 + 10, out=8)
        rig.rot('UpperArm_R', fwd=a * 55 + 10, out=8)
        rig.rot('Forearm_L', fwd=85)
        rig.rot('Forearm_R', fwd=85)
    make_clip(rig, 'sprint', 16, True, sprint)

    def crouch_pose():
        rig.move('Hips', dz=-0.42, dy=-0.04)
        rig.rot('Spine', fwd=22)
        rig.rot('Chest', fwd=6)
        rig.rot('Head', fwd=-22)
        rig.rot('Thigh_L', fwd=78, out=12)
        rig.rot('Thigh_R', fwd=78, out=12)
        rig.rot('Shin_L', fwd=-105)
        rig.rot('Shin_R', fwd=-105)
        rig.rot('Foot_L', fwd=27)
        rig.rot('Foot_R', fwd=27)

    def crouch(t):
        s = math.sin(t * 2 * math.pi)
        crouch_pose()
        rig.move('Hips', dz=-0.42 + 0.008 * s, dy=-0.04)
        arms_down(rig, bend=30)
    make_clip(rig, 'crouch', 48, True, crouch)

    def crouch_walk(t):
        crouch_pose()
        a = math.sin(t * 2 * math.pi)
        rig.rot('Thigh_L', fwd=78 + a * 22, out=12)
        rig.rot('Thigh_R', fwd=78 - a * 22, out=12)
        rig.rot('Shin_L', fwd=-105 + max(0.0, a) * 15)
        rig.rot('Shin_R', fwd=-105 + max(0.0, -a) * 15)
        rig.rot('UpperArm_L', fwd=-a * 12, out=6)
        rig.rot('UpperArm_R', fwd=a * 12, out=6)
        rig.rot('Forearm_L', fwd=35)
        rig.rot('Forearm_R', fwd=35)
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
        rig.rot('Spine', fwd=80)
        rig.rot('Chest', fwd=6)
        rig.rot('Head', fwd=-55)
        rig.rot('UpperArm_L', fwd=-25, out=75 + 3 * s)
        rig.rot('UpperArm_R', fwd=-25, out=75 - 3 * s)
        rig.rot('Forearm_L', fwd=85)
        rig.rot('Forearm_R', fwd=85)
        rig.rot('Thigh_L', fwd=-25, out=22)
        rig.rot('Thigh_R', fwd=-25, out=22)
        rig.rot('Shin_L', fwd=-55 + 3 * s)
        rig.rot('Shin_R', fwd=-55 - 3 * s)
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
        rig.rot('UpperArm_L', fwd=70, out=-25)
        rig.rot('UpperArm_R', fwd=70, out=-25)
        rig.rot('Forearm_L', fwd=70)
        rig.rot('Forearm_R', fwd=70)
        rig.rot('Head', fwd=30)
        rig.rot('Spine', fwd=12)
        rig.rot('Thigh_L', fwd=20, out=6)
        rig.rot('Thigh_R', fwd=20, out=6)
        rig.rot('Shin_L', fwd=-30)
        rig.rot('Shin_R', fwd=-30)
        rig.move('Hips', dz=-0.1)
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

        # Skin everywhere first, then layer clothes over it.
        region(np.ones(len(X), bool), tone)
        torso = np.abs(X) < 0.3
        region((Z < 1.05), pants * cloth[:, None][(Z < 1.05)].mean() if False else pants)            # pants
        region((Z >= 1.05) & (Z < 1.56) & torso, shirt)                                               # shirt
        region((Z >= 1.05) & (np.abs(X) >= 0.3) & (Z > 1.22), shirt)                                  # sleeves
        region((Z >= 1.05) & (np.abs(X) >= 0.3) & (Z <= 1.22), tone)                                  # forearms
        # Shirt pattern.
        shirt_area = ((Z >= 1.05) & (Z < 1.56) & torso) | ((Z >= 1.05) & (np.abs(X) >= 0.3) & (Z > 1.22))
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
            hz = Z * 22 / 0.866
            row = np.floor(hz)
            cx = np.floor(hx + 0.5 * (row % 2)) - 0.5 * (row % 2) + 0.5
            cz = row + 0.5
            dd = np.sqrt((hx - cx) ** 2 + ((hz - cz) * 0.866) ** 2)
            region(shirt_area & (dd > 0.42), shirt * 0.55)
        elif pattern == 5:  # stars (dots)
            sx_ = np.floor(X * 16) + 0.5
            sz_ = np.floor(Z * 16) + 0.5
            dd = np.sqrt((X * 16 - sx_) ** 2 + (Z * 16 - sz_) ** 2)
            region(shirt_area & (dd < 0.22), accent, 1)
        elif pattern == 6:  # flames rising from the belt
            fh = 1.08 + 0.2 * (0.5 + 0.5 * np.sin(X * 60 + Y * 25)) + 0.08 * np.sin(X * 140)
            region(shirt_area & (Z < fh), accent, 1)
            region(shirt_area & (Z < fh - 0.08), accent * 0.75 + np.array((0.3, 0.05, 0.0)) * 0.25, 1)
        elif pattern == 7:  # sash across the chest
            region(shirt_area & (np.abs(X + (Z - 1.3) * 0.8) < 0.06) & (front > -0.3), accent, 1)
        region((Z < 1.0) & (np.abs(X) > 0.3), dark)                                                   # gloves
        region((Z < 0.21), dark)                                                                      # boots
        region((Z < 0.08), dark * 0.6)                                                                # soles
        region((Z >= 0.98) & (Z < 1.06) & torso, dark)                                                # belt
        region((Z >= 0.99) & (Z < 1.05) & (np.abs(X) < 0.035) & (front > 0.4), np.array((0.75, 0.75, 0.8)))  # buckle
        # Knee pads.
        for sx in (1, -1):
            kn = np.array(J['Knee_L'][0]) * np.array((sx, 1, 1))
            dk = np.sqrt((X - kn[0]) ** 2 + (Z - kn[2]) ** 2)
            region((dk < 0.09) & (front > 0.1) & (Z < 1.0), pants * 0.65, 0)
        # Accent: shoulder pads, chest stripe, collar, boot tops, glove cuffs.
        for sx in (1, -1):
            sh = np.array(J['Shoulder_L'][0]) * np.array((sx, 1, 1))
            ds = np.sqrt((X - sh[0]) ** 2 + (Y - sh[1]) ** 2 + (Z - sh[2]) ** 2)
            region((ds < 0.135) & (Z > 1.38), accent, 1)
        region((Z >= 1.30) & (Z < 1.35) & torso & (front > -0.2), accent, 1)
        region((Z >= 1.49) & (Z < 1.56) & torso, accent, 1)
        region((Z >= 0.19) & (Z < 0.23), accent, 1)
        region((Z >= 0.98) & (Z < 1.01) & (np.abs(X) > 0.3), accent, 1)
        # Chest emblem: a circle on the upper chest.
        em = np.sqrt((X * 1.1) ** 2 + (Z - 1.42) ** 2)
        region((em < 0.055) & (front > 0.5) & torso, accent, 1)
        region((em < 0.03) & (front > 0.5) & torso, shirt, 0)
        # Head: hair on the top and back.
        head = Z > 1.6
        region(head, tone)
        hairline = 1.86 - 0.05 * np.clip(front, 0, 1)
        hair_cond = head & ((Z > hairline) | ((Y > 0.09) & (Z > 1.70)) | ((np.abs(X) > 0.16) & (Z > 1.78)))
        region(hair_cond, hair, 0)
        # Face: eyes (white + pupil), brows, mouth.
        for sx in (1, -1):
            ex, ez = 0.078 * sx, 1.755
            de = np.sqrt(((X - ex) / 0.042) ** 2 + ((Z - ez) / 0.030) ** 2)
            region((de < 1.0) & (front > 0.25) & head, np.array((0.97, 0.97, 0.97)))
            dp = np.sqrt(((X - ex - 0.006 * sx) / 0.02) ** 2 + ((Z - ez) / 0.022) ** 2)
            region((dp < 1.0) & (front > 0.25) & head, np.array((0.07, 0.06, 0.08)))
            dh = np.sqrt(((X - ex - 0.012 * sx) / 0.007) ** 2 + ((Z - ez - 0.008) / 0.007) ** 2)
            region((dh < 1.0) & (front > 0.25) & head, np.array((0.95, 0.95, 0.95)))
            brow_z = 1.80 + (np.abs(X) - 0.078) * 0.25 * -1
            region((np.abs(Z - brow_z) < 0.009) & (np.abs(X - ex) < 0.05) & (front > 0.25) & head & ~hair_cond, hair * 0.7)
        mouth_z = 1.675 + 0.1 * (X ** 2) * 10
        region((np.abs(Z - mouth_z) < 0.006) & (np.abs(X) < 0.045) & (front > 0.3) & head, np.array((0.45, 0.2, 0.2)))
        # A little shading: darker toward the bottom of each body part and a cloth weave.
        shade = 0.92 + 0.08 * np.clip(N[:, 2] * 0.5 + 0.5, 0, 1)
        col *= shade[:, None]
        col *= (0.985 + 0.03 * noise)[:, None]

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
def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    scene = bpy.context.scene
    scene.render.fps = FPS
    body, arm = build_body()
    lods = finalize_body(body, arm)
    extras = build_hair(arm)
    build_animations(arm)
    paint_textures(body)
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


if __name__ == '__main__':
    main()
