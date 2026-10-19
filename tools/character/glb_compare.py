# Compares the meshes of two character .glb files: vertex counts, positions and UVs per mesh,
# plus bone and animation names. Used to check that a rebuilt character still matches the painted
# skin textures (the body's UVs must not change, or every skin texture would need repainting).
#
#   python tools/character/glb_compare.py old.glb new.glb
import json, struct, sys

import numpy as np

COMPONENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
DTYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}


def load(path):
    data = open(path, 'rb').read()
    magic, _, _ = struct.unpack_from('<III', data, 0)
    assert magic == 0x46546C67, path + ' is not a .glb'
    jlen, _ = struct.unpack_from('<II', data, 12)
    doc = json.loads(data[20:20 + jlen])
    off = 20 + jlen
    blen, _ = struct.unpack_from('<II', data, off)
    return doc, data[off + 8:off + 8 + blen]


def accessor(doc, binary, index):
    acc = doc['accessors'][index]
    view = doc['bufferViews'][acc['bufferView']]
    n, comps = acc['count'], COMPONENTS[acc['type']]
    dtype = DTYPES[acc['componentType']]
    start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = view.get('byteStride', 0) or comps * np.dtype(dtype).itemsize
    raw = np.frombuffer(binary, dtype=np.uint8, count=stride * (n - 1) + comps * np.dtype(dtype).itemsize, offset=start)
    rows = np.lib.stride_tricks.as_strided(raw, shape=(n, comps * np.dtype(dtype).itemsize), strides=(stride, 1))
    return np.ascontiguousarray(rows).view(dtype).reshape(n, comps)


def meshes(doc, binary):
    out = {}
    for node in doc.get('nodes', []):
        if 'mesh' not in node:
            continue
        prim = doc['meshes'][node['mesh']]['primitives']
        pos = np.concatenate([accessor(doc, binary, p['attributes']['POSITION']) for p in prim])
        uv = np.concatenate([accessor(doc, binary, p['attributes']['TEXCOORD_0']) for p in prim if 'TEXCOORD_0' in p['attributes']]) \
            if any('TEXCOORD_0' in p['attributes'] for p in prim) else None
        out[node.get('name', '?')] = (pos, uv)
    return out


def main(a, b):
    da, ba = load(a)
    db, bb = load(b)
    ma, mb = meshes(da, ba), meshes(db, bb)
    same = True
    for name in sorted(set(ma) | set(mb)):
        if name not in ma or name not in mb:
            print('%-14s only in %s' % (name, 'old' if name in ma else 'new'))
            continue
        (pa, ua), (pb, ub) = ma[name], mb[name]
        if len(pa) != len(pb):
            print('%-14s vertex count %d -> %d' % (name, len(pa), len(pb)))
            same = False
            continue
        dp = float(np.abs(pa - pb).max())
        du = float(np.abs(ua - ub).max()) if ua is not None and ub is not None else 0.0
        print('%-14s %6d verts  max position diff %.6f  max uv diff %.6f' % (name, len(pa), dp, du))
        same = same and dp < 1e-4 and du < 1e-4
    bones = lambda d: [d['nodes'][j].get('name') for j in d['skins'][0]['joints']] if d.get('skins') else []
    anims = lambda d: [x.get('name') for x in d.get('animations', [])]
    print('bones', 'same' if bones(da) == bones(db) else '%s -> %s' % (bones(da), bones(db)))
    print('clips', 'same' if anims(da) == anims(db) else '%s -> %s' % (anims(da), anims(db)))
    print('MATCH' if same else 'DIFFERENT')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
