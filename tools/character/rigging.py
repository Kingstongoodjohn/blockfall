# Extra bones on top of the 22-bone body skeleton:
#   face:   Jaw, Brow_L/R, Lid_L/R, Mouth_L/R  (posed by the game for expressions and blinking)
#   swing:  HairTail_1..3, Cape_1..3, Scarf_1..2, each chain ending in an "_end" joint
#           (simulated by the game, see src/game/charphys.cpp)
# None of these move the body mesh; only the face parts, hair cards, cape and scarf use them.
import bpy
from mathutils import Vector

# Face feature positions, measured from the head joint the same way the skin painter places the
# painted face (so the 3D features sit where the painted ones were). HK = head scale.
def face_points(J, HK):
    hz = J['Head'][0][2]
    pts = {}
    for s, sx in (('L', 1), ('R', -1)):
        ex, ez = 0.078 * HK * sx, hz + 0.015 * HK
        pts['eye_' + s] = Vector((ex, 0.0, ez))
        pts['brow_' + s] = Vector((ex + 0.012 * HK * sx, 0.0, hz + 0.06 * HK))
        # The painted mouth curves up toward its corners: z = mouth + (x / HK)^2.
        pts['mouth_' + s] = Vector((0.045 * HK * sx, 0.0, hz - 0.065 * HK + 0.045 ** 2))
    pts['mouth'] = Vector((0.0, 0.0, hz - 0.065 * HK))
    pts['jaw'] = Vector((0.0, 0.02, hz - 0.03 * HK))
    return pts


def front_y(bvh, x, z):
    """Y of the head's front surface at (x, z) (the body faces -Y)."""
    hit = bvh.ray_cast(Vector((x, -1.0, z)), Vector((0, 1, 0)), 2.0)
    return hit[0].y if hit[0] is not None else -0.13


# Swing chains (Blender coordinates: X = the character's left, -Y = front, Z = up).
def swing_chains(J, HK):
    hz = J['Head'][0][2]
    return {
        'HairTail': ('Head', [(0, 0.12, hz - 0.02), (0, 0.155, hz - 0.12), (0, 0.175, hz - 0.22), (0, 0.185, hz - 0.33)]),
        'Cape': ('Chest', [(0, 0.15, 1.53), (0, 0.19, 1.22), (0, 0.23, 0.92), (0, 0.27, 0.60)]),
        'Scarf': ('Chest', [(0.05, 0.11, 1.57), (0.07, 0.15, 1.43), (0.08, 0.17, 1.29)]),
    }


def add_extra_bones(arm, J, HK, bvh):
    pts = face_points(J, HK)
    bpy.context.view_layer.objects.active = arm
    for o in bpy.data.objects:
        o.select_set(o == arm)
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm.data.edit_bones
    head = eb['Head']

    def bone(name, h, t, parent):
        b = eb.new(name)
        b.head = Vector(h)
        b.tail = Vector(t)
        b.parent = parent
        b.use_deform = True
        b.use_connect = False
        return b

    fwd = Vector((0, -0.02, 0))
    for s in 'LR':
        e = pts['eye_' + s]
        e.y = front_y(bvh, e.x, e.z) + 0.012
        bone('Lid_' + s, e, e + fwd, head)
        b = pts['brow_' + s]
        b.y = front_y(bvh, b.x, b.z)
        bone('Brow_' + s, b, b + fwd, head)
        m = pts['mouth_' + s]
        m.y = front_y(bvh, m.x, m.z)
        bone('Mouth_' + s, m, m + fwd, head)
    j = pts['jaw']
    bone('Jaw', j, j + Vector((0, -0.07, -0.03)), head)

    for prefix, (parent, chain) in swing_chains(J, HK).items():
        par = eb[parent]
        names = ['%s_%d' % (prefix, i + 1) for i in range(len(chain) - 1)] + ['%s_end' % prefix]
        for i, name in enumerate(names):
            h = Vector(chain[i])
            t = Vector(chain[i + 1]) if i + 1 < len(chain) else h + (h - Vector(chain[i - 1])) * 0.5
            par = bone(name, h, t, par)
    bpy.ops.object.mode_set(mode='OBJECT')
    return pts
