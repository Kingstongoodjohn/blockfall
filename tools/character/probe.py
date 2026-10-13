import bpy, bmesh
from mathutils import Vector

bpy.ops.wm.read_factory_settings(use_empty=True)

# joints: name -> (pos, radius xy)
J = {
    'Hips':      ((0, 0, 0.95), (0.20, 0.14)),
    'Spine':     ((0, 0, 1.15), (0.19, 0.13)),
    'Chest':     ((0, 0, 1.38), (0.24, 0.15)),
    'Neck':      ((0, 0, 1.52), (0.08, 0.08)),
    'Head':      ((0, 0, 1.72), (0.20, 0.19)),
    'HeadTop':   ((0, 0, 1.90), (0.12, 0.12)),
}
for s, sx in (('L', 1), ('R', -1)):
    J['Shoulder_' + s] = ((0.26 * sx, 0, 1.44), (0.10, 0.10))
    J['Elbow_' + s] = ((0.32 * sx, 0, 1.18), (0.075, 0.075))
    J['Wrist_' + s] = ((0.36 * sx, 0, 0.94), (0.07, 0.07))
    J['Hand_' + s] = ((0.37 * sx, 0, 0.84), (0.09, 0.07))
    J['Hip_' + s] = ((0.12 * sx, 0, 0.92), (0.13, 0.13))
    J['Knee_' + s] = ((0.13 * sx, 0, 0.52), (0.10, 0.11))
    J['Ankle_' + s] = ((0.14 * sx, 0, 0.12), (0.10, 0.10))
    J['Toe_' + s] = ((0.14 * sx, -0.16, 0.06), (0.11, 0.06))
E = [('Hips', 'Spine'), ('Spine', 'Chest'), ('Chest', 'Neck'), ('Neck', 'Head'), ('Head', 'HeadTop')]
for s in 'LR':
    E += [('Chest', 'Shoulder_' + s), ('Shoulder_' + s, 'Elbow_' + s), ('Elbow_' + s, 'Wrist_' + s), ('Wrist_' + s, 'Hand_' + s),
          ('Hips', 'Hip_' + s), ('Hip_' + s, 'Knee_' + s), ('Knee_' + s, 'Ankle_' + s), ('Ankle_' + s, 'Toe_' + s)]

names = list(J.keys())
mesh = bpy.data.meshes.new('Body')
mesh.from_pydata([J[n][0] for n in names], [(names.index(a), names.index(b)) for a, b in E], [])
obj = bpy.data.objects.new('Body', mesh)
bpy.context.collection.objects.link(obj)
bpy.context.view_layer.objects.active = obj
obj.select_set(True)

skin = obj.modifiers.new('Skin', 'SKIN')
skin.use_smooth_shade = True
sub = obj.modifiers.new('Sub', 'SUBSURF')
sub.levels = 2
sub.render_levels = 2
sv = mesh.skin_vertices[0].data
for i, n in enumerate(names):
    sv[i].radius = J[n][1]
    sv[i].use_root = (n == 'Hips')

with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
    bpy.ops.object.skin_armature_create(modifier='Skin')
arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
print('armature', arm.name, 'bones', len(arm.data.bones))
for b in arm.data.bones:
    print(b.name, tuple(round(x, 2) for x in b.head_local), tuple(round(x, 2) for x in b.tail_local), b.parent.name if b.parent else None)
print('modifiers', [m.type for m in obj.modifiers])
print('vgroups', [g.name for g in obj.vertex_groups][:5], len(obj.vertex_groups))

# apply skin + subsurf, keep armature
with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
    bpy.ops.object.modifier_apply(modifier='Skin')
    bpy.ops.object.modifier_apply(modifier='Sub')
print('verts', len(mesh.vertices), 'polys', len(mesh.polygons))
w = [sum(1 for g in v.groups if g.weight > 0) for v in mesh.vertices]
print('weighted verts', sum(1 for x in w if x > 0), 'max groups', max(w))
