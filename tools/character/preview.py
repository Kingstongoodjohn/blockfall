# Renders preview pictures of assets/character.glb with each skin texture (Cycles, CPU).
# usage: python3 preview.py out_dir [skin indices...]
import bpy, os, sys, math
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, '..', '..', 'assets')
out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'preview')
skins = [int(a) for a in sys.argv[2:]] or [0]
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.environ.get('GLB') or os.path.join(ASSETS, 'character.glb'))
scene = bpy.context.scene
arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
meshes = {o.name: o for o in bpy.data.objects if o.type == 'MESH'}
print('meshes', sorted(meshes), 'bones', len(arm.data.bones), 'actions', [a.name for a in bpy.data.actions])

SKIN_DIR = os.environ.get('SKINS_DIR') or os.path.join(ASSETS, 'skins')
skin_files = {int(f.split('_')[0]): f for f in os.listdir(SKIN_DIR) if f.endswith('.png')}
sys.path.insert(0, HERE)
from skins_catalog import SKINS
HAIR = {0: 'Hair0', 1: 'Hair1', 2: 'Hair2', 3: 'Hair3'}
HAT = {1: 'Hat1', 2: 'Hat2', 3: 'Hat3'}

mat = bpy.data.materials.new('Skin')
mat.use_nodes = True
nt = mat.node_tree
tex = nt.nodes.new('ShaderNodeTexImage')
tex.interpolation = 'Closest'
bsdf = nt.nodes['Principled BSDF']
bsdf.inputs['Roughness'].default_value = 0.8
nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
for o in meshes.values():
    o.data.materials.clear()
    o.data.materials.append(mat)

# Lights + camera.
sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN'))
sun.data.energy = 3.0
sun.rotation_euler = (math.radians(55), 0, math.radians(35))
scene.collection.objects.link(sun)
fill = bpy.data.objects.new('Fill', bpy.data.lights.new('Fill', 'SUN'))
fill.data.energy = 1.0
fill.rotation_euler = (math.radians(70), 0, math.radians(-120))
scene.collection.objects.link(fill)
world = bpy.data.worlds.new('W')
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.55, 0.7, 0.9, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.8
scene.world = world
if os.environ.get('SHOTS'):   # a floor, so you can see whether the feet touch it
    bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, 0))
    floor = bpy.context.active_object
    fm = bpy.data.materials.new('Floor')
    fm.use_nodes = True
    fm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.35, 0.4, 0.3, 1)
    floor.data.materials.append(fm)
cam = bpy.data.objects.new('Cam', bpy.data.cameras.new('Cam'))
scene.collection.objects.link(cam)
scene.camera = cam
scene.render.engine = 'CYCLES'
scene.cycles.samples = 24
scene.cycles.device = 'CPU'
scene.render.resolution_x = 480
scene.render.resolution_y = 720
scene.render.film_transparent = False


def look(cam, pos, target):
    cam.location = pos
    d = Vector(target) - Vector(pos)
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def set_action(name, frame):
    for t in arm.animation_data.nla_tracks:
        t.mute = True
    act = bpy.data.actions.get(name)
    arm.animation_data.action = act
    if hasattr(arm.animation_data, 'action_slot') and act and len(act.slots):
        arm.animation_data.action_slot = act.slots[0]
    scene.frame_set(frame)


shots = [('idle', 1, (0.0, -4.2, 1.3), (0, 0, 1.0))]
if os.environ.get('SHOTS'):   # e.g. SHOTS=walk:1,walk:7,sprint:1 renders those clip frames from the side
    views = {'side': (4.0, 0.0, 1.0), 'front': (0.0, -3.6, 1.1), 'back': (0.0, 3.6, 1.1)}
    shots = []
    for c in os.environ['SHOTS'].split(','):
        parts = c.split(':')
        shots.append((parts[0] + '-' + parts[1] + ('-' + parts[2] if len(parts) > 2 else ''), int(parts[1]),
                      views[parts[2] if len(parts) > 2 else 'side'], (0, 0, 0.95)))
if os.environ.get('ALL_SHOTS'):
    shots += [('idle_side', 1, (3.2, -2.8, 1.3), (0, 0, 1.0)),
              ('walk', 7, (2.6, -3.4, 1.2), (0, 0, 0.95)), ('sprint', 5, (2.6, -3.4, 1.2), (0, 0, 0.95)),
              ('aim', 1, (-2.0, -3.6, 1.3), (0, 0, 1.1)), ('swing', 8, (2.6, -3.2, 1.3), (0, 0, 1.1)),
              ('crouch', 1, (2.4, -3.4, 1.0), (0, 0, 0.7)), ('skydive', 1, (0.0, 3.0, 3.5), (0, 0, 1.0)),
              ('glide', 1, (0.0, 3.6, 1.6), (0, 0, 1.0)), ('reload', 18, (-2.0, -3.6, 1.3), (0, 0, 1.1))]
for si in skins:
    tex.image = bpy.data.images.load(os.path.join(SKIN_DIR, skin_files[si]))
    tex.image.alpha_mode = 'CHANNEL_PACKED'
    sk = SKINS[si]
    hs, hat, band = sk[6], sk[7], sk[8]
    hair_shows = hat == 0 or hs in (1, 3)
    for k, o in meshes.items():
        if k == 'Plane':
            continue
        o.hide_render = not (k == 'Body' or (k == HAIR[hs] and hair_shows) or (hat and k == HAT.get(hat)) or (k == 'Headband' and band))
    for name, frame, pos, target in shots:
        action = name.split('-')[0] if '-' in name else name.split('_')[0]
        set_action(action, frame)
        look(cam, pos, target)
        scene.render.filepath = os.path.join(out_dir, 'skin%d_%s.png' % (si, name))
        bpy.ops.render.render(write_still=True)
        print('rendered', scene.render.filepath)
