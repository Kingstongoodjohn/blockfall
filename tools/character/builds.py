# Body builds. Every build is the athletic body with its vertices pushed in or out from the bones,
# so all builds share the topology and UVs (the painted skin textures fit every build) and the
# skeleton (every animation works). Outfit pieces use the same function so they fit each build.
import numpy as np

# Thickness per joint (1 = athletic). Values in between joints blend smoothly along each bone.
BUILD_SCALE = [
    {},   # 0 athletic
    {     # 1 heavy: broad and thick, a bit of a belly
        'Hips': 1.16, 'Spine': 1.26, 'Chest': 1.17, 'Neck': 1.28, 'Head': 1.0, 'HeadTop': 1.0,
        'Shoulder': 1.17, 'Elbow': 1.18, 'Wrist': 1.12, 'Hand': 1.08,
        'Hip': 1.18, 'Knee': 1.14, 'Ankle': 1.08, 'Toe': 1.05,
    },
    {     # 2 slim: lean runner
        'Hips': 0.92, 'Spine': 0.88, 'Chest': 0.9, 'Neck': 0.92, 'Head': 1.0, 'HeadTop': 1.0,
        'Shoulder': 0.9, 'Elbow': 0.88, 'Wrist': 0.92, 'Hand': 0.95,
        'Hip': 0.9, 'Knee': 0.9, 'Ankle': 0.94, 'Toe': 0.97,
    },
]


def joint_scale(build, joint):
    base = joint.split('_')[0]
    return BUILD_SCALE[build].get(base, 1.0)


def displace(points, build, J, E):
    """Returns `points` (N x 3, rest pose, Blender coordinates) reshaped for `build`.
    Each point moves away from (or toward) every bone segment, weighted by how close it is, so
    the result is smooth where bones meet."""
    pts = np.asarray(points, dtype=np.float64)
    if build == 0:
        return pts.copy()
    num = np.zeros_like(pts)
    den = np.zeros(len(pts))
    for a, b in E:
        pa, pb = np.array(J[a][0], float), np.array(J[b][0], float)
        d = pb - pa
        t = np.clip(((pts - pa) @ d) / max(d @ d, 1e-9), 0.0, 1.0)
        c = pa + t[:, None] * d
        r = pts - c
        dist = np.linalg.norm(r, axis=1)
        s = joint_scale(build, a) * (1 - t) + joint_scale(build, b) * t
        w = 1.0 / (dist ** 4 + 1e-7)
        num += w[:, None] * (c + r * s[:, None])
        den += w
    return num / den[:, None]
