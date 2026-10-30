# Measures the animation clips in a character .glb using the skinned Body mesh itself:
#   - how far the soles sink below (-) or float above (+) the ground (z = 0) in ground clips,
#   - how much a planted foot slides compared with the ground speed the game moves the body at
#     (gait clips: the foot should move backward at exactly cycle length / clip length),
#   - for gun clips, how far apart the hands are.
# Prints one line per clip. Used to check animation fixes without looking at every frame.
#
#   blender -b --python tools/character/anim_check.py -- assets/character.glb
#   (or: python tools/character/anim_check.py -- assets/character.glb with the bpy module)
import math
import sys

import bpy

path = sys.argv[sys.argv.index('--') + 1] if '--' in sys.argv else sys.argv[-1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
body = bpy.data.objects['Body']
ad = arm.animation_data
scene = bpy.context.scene
fps = scene.render.fps
# Clips that stand on the ground, and for walking ones the ground distance of one cycle in metres
# (2*pi / the stride rate in src/game/game.h).
GROUND = {'idle': 0, 'walk': 2 * math.pi / 2.4, 'sprint': 2 * math.pi / 1.8, 'crouch': 0,
          'crouch_walk': 2 * math.pi / 3.4, 'heal': 0}

rest = [v.co.copy() for v in body.data.vertices]
feet = {s: [i for i, p in enumerate(rest) if p.z < 0.2 and (p.x > 0) == (s == 'L')] for s in 'LR'}


def sole(points, ids):
    low = min(points[i].z for i in ids)
    near = [points[i] for i in ids if points[i].z < low + 0.02]
    return low, sum((p.x for p in near), 0.0) / len(near), sum((p.y for p in near), 0.0) / len(near)


for t in ad.nla_tracks:
    t.mute = True
for act in sorted(bpy.data.actions, key=lambda a: a.name):
    ad.action = act
    if hasattr(ad, 'action_slot') and len(act.slots):
        ad.action_slot = act.slots[0]
    f0, f1 = (int(x) for x in act.frame_range)
    frames = f1 - f0
    lows, errs, hands = [], [], []
    prev = {}
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = body.evaluated_get(dg)
        pts = [body.matrix_world @ v.co for v in ev.data.vertices]
        cur = {s: sole(pts, feet[s]) for s in 'LR'}
        lows.append(min(cur['L'][0], cur['R'][0]))
        if act.name in GROUND and GROUND[act.name]:
            want = GROUND[act.name] / frames      # metres per frame, backward (+y is back in Blender)
            for s in 'LR':
                if s in prev and cur[s][0] < 0.02 and prev[s][0] < 0.02:
                    vy = cur[s][2] - prev[s][2]
                    errs.append(abs(vy - want))
        prev = cur
        pl, pr = arm.pose.bones['Hand_L'], arm.pose.bones['Hand_R']
        hands.append(((arm.matrix_world @ pl.head) - (arm.matrix_world @ pr.head)).length)
    msg = '%-12s %3d frames  sole %+.3f..%+.3f m' % (act.name, frames + 1, min(lows), max(lows))
    if act.name in GROUND and GROUND[act.name]:
        msg += '  foot slide error %.3f m/frame (of %.3f)' % (sum(errs) / max(1, len(errs)), GROUND[act.name] / frames)
    if act.name in ('aim', 'shoot', 'reload'):
        msg += '  hands apart %.2f..%.2f m' % (min(hands), max(hands))
    print(msg)
