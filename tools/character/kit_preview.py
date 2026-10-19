# Quick check pictures of outfits straight from the build (Blender Workbench, a few seconds each):
# the skin's build, hair, hat and kit pieces, coloured by zone from its palette. Front, side, back.
import math
import os

import bpy
from mathutils import Vector

from meshutil import (Z_CUT, Z_MAIN, Z_SECOND, Z_ACCENT, Z_LEATHER, Z_METAL, Z_HAIR, Z_SKIN, Z_EYE, Z_DARK, Z_LIPS,
                      Z_BODY, Z_LENS)
from skins_catalog import SKINS, SKIN_TONES, KIT, outfit


def palette(skin):
    name, shirt, pants, hair, accent, hatcol, hs, hat, band, pat, tone, rar = skin
    t = SKIN_TONES[tone]
    return {Z_CUT: (0, 0, 0), Z_MAIN: shirt, Z_SECOND: pants, Z_ACCENT: accent, Z_LEATHER: (0.25, 0.16, 0.1),
            Z_METAL: (0.75, 0.76, 0.8), Z_HAIR: hair, Z_SKIN: t, Z_EYE: (0.95, 0.95, 0.95), Z_DARK: (0.06, 0.06, 0.07),
            Z_LIPS: (t[0] * 0.8, t[1] * 0.5, t[2] * 0.5), Z_BODY: (shirt[0] * 0.7 + 0.15, shirt[1] * 0.7 + 0.15, shirt[2] * 0.7 + 0.15),
            Z_LENS: (0.1, 0.3, 0.4)}


def colour(obj, pal, default=None):
    mesh = obj.data
    col = mesh.color_attributes.get('Col') or mesh.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    za = mesh.attributes.get('zone')
    for p in mesh.polygons:
        z = za.data[p.index].value if za else Z_BODY
        c = default if (default is not None and not za) else pal[z]
        for li in p.loop_indices:
            col.data[li].color = (c[0], c[1], c[2], 1.0)
    mesh.color_attributes.active_color = col


def render(out_dir, skin_names, arm):
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    sh = scene.display.shading
    sh.light = 'STUDIO'
    sh.color_type = 'VERTEX'
    sh.show_cavity = True
    sh.cavity_type = 'BOTH'
    sh.show_shadows = True
    sh.show_backface_culling = False
    scene.render.resolution_x, scene.render.resolution_y = 520, 780
    scene.render.film_transparent = False
    cam = bpy.data.objects.new('PreviewCam', bpy.data.cameras.new('PreviewCam'))
    scene.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = 2.15
    scene.camera = cam
    meshes = {o.name: o for o in bpy.data.objects if o.type == 'MESH'}
    arm.data.pose_position = 'REST'
    # name: (camera location, rotation, ortho scale)
    views = {'front': ((0, -4, 1.0), (math.radians(90), 0, 0), 2.15),
             'side': ((4, 0, 1.0), (math.radians(90), 0, math.radians(90)), 2.15),
             'back': ((0, 4, 1.0), (math.radians(90), 0, math.radians(180)), 2.15),
             'face': ((0.12, -4, 1.8), (math.radians(90), 0, math.radians(2)), 0.42),
             'torso': ((0.25, -4, 1.3), (math.radians(90), 0, math.radians(4)), 0.9),
             'feet': ((0.35, -4, 0.25), (math.radians(90), 0, math.radians(8)), 0.7)}
    for sname in skin_names:
        idx = [s[0] for s in SKINS].index(sname)
        skin = SKINS[idx]
        build, mask, _ = outfit(sname)
        pal = palette(skin)
        show = {'Body' if build == 0 else 'Body_B%d' % build, 'Face', 'Mouth', 'Hair%d' % skin[6]}
        if skin[7]:
            show.add('Hat%d' % skin[7])
        for k, piece in enumerate(KIT):
            if (mask >> k) & 1:
                show.add(piece if piece in meshes else '%s_B%d' % (piece, build))
        if (mask >> KIT.index('mask')) & 1:
            show.discard('Mouth')
        for name, o in meshes.items():
            o.hide_render = name not in show
            if name in show:
                colour(o, pal, default=pal[Z_BODY] if name.startswith('Body') else (skin[5] if name.startswith('Hat') else None))
        for view, (loc, rot, scale) in views.items():
            cam.location = loc
            cam.rotation_euler = rot
            cam.data.ortho_scale = scale
            scene.render.filepath = os.path.join(out_dir, '%s_%s.png' % (sname.lower().replace(' ', '_'), view))
            bpy.ops.render.render(write_still=True)
        print('preview', sname)
    for o in meshes.values():
        o.hide_render = False
    arm.data.pose_position = 'POSE'
