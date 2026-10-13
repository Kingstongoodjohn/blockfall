// Blockfall - character model loading (glTF via cgltf) and skeletal animation maths
#include "model.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define CGLTF_IMPLEMENTATION
#include "../third_party/cgltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace bf {

// ------------------------------------------------------------------ quaternions
// Hamilton product p*q: rotate by q first, then by p (column-vector convention, like glTF).
static Quat hamilton(const Quat& p, const Quat& q) {
    return {p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y,
            p.w * q.y - p.x * q.z + p.y * q.w + p.z * q.x,
            p.w * q.z + p.x * q.y - p.y * q.x + p.z * q.w,
            p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z};
}

Quat quatAxisAngle(const Vec3& axis, float angle) {
    Vec3 a = normalize(axis);
    float s = std::sin(angle * 0.5f);
    return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
}

Quat quatMul(const Quat& a, const Quat& b) { return hamilton(b, a); }
Quat quatConj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

static Quat quatNormalize(const Quat& q) {
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1e-8f) return {0, 0, 0, 1};
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

Quat quatSlerp(const Quat& a, const Quat& bIn, float t) {
    Quat b = bIn;
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) {
        return quatNormalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    float th = std::acos(d);
    float sa = std::sin((1 - t) * th) / std::sin(th), sb = std::sin(t * th) / std::sin(th);
    return {a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb, a.w * sa + b.w * sb};
}

Mat4 quatToMat(const Quat& q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 m = Mat4::identity();
    // Standard column-vector rotation matrix R, stored transposed for this code base's row vectors.
    m.m[0][0] = 1 - 2 * (y * y + z * z); m.m[0][1] = 2 * (x * y + z * w);     m.m[0][2] = 2 * (x * z - y * w);
    m.m[1][0] = 2 * (x * y - z * w);     m.m[1][1] = 1 - 2 * (x * x + z * z); m.m[1][2] = 2 * (y * z + x * w);
    m.m[2][0] = 2 * (x * z + y * w);     m.m[2][1] = 2 * (y * z - x * w);     m.m[2][2] = 1 - 2 * (x * x + y * y);
    return m;
}

Vec3 quatRotate(const Quat& q, const Vec3& v) {
    Mat4 m = quatToMat(q);
    return {v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0], v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]};
}

// ------------------------------------------------------------------ loading
namespace {

// Mirror on X: the glTF file is right-handed, Blockfall is left-handed.
Vec3 mirrorT(const Vec3& t) { return {-t.x, t.y, t.z}; }
Quat mirrorR(const Quat& q) { return {q.x, -q.y, -q.z, q.w}; }
Mat4 mirrorM(const Mat4& a) {
    Mat4 r = a;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if ((i == 0) != (j == 0)) r.m[i][j] = -a.m[i][j];
    return r;
}

Mat4 nodeLocal(const cgltf_node* n) {
    if (n->has_matrix) {
        Mat4 m;
        std::memcpy(m.m, n->matrix, sizeof m.m);  // glTF column-major == our row-vector layout
        return m;
    }
    Vec3 t(0, 0, 0), s(1, 1, 1);
    Quat r;
    if (n->has_translation) t = Vec3(n->translation[0], n->translation[1], n->translation[2]);
    if (n->has_rotation) r = {n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
    if (n->has_scale) s = Vec3(n->scale[0], n->scale[1], n->scale[2]);
    return Mat4::scale(s) * quatToMat(r) * Mat4::translation(t);
}

const CharacterModel* g_model = nullptr;

}  // namespace

const CharacterModel* characterModel() { return g_model; }
void setCharacterModel(const CharacterModel* m) { g_model = m; }

bool CharacterModel::load(const std::string& path, std::string& error) {
    loaded = false;
    cgltf_options opt = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) {
        error = "Could not read " + path;
        return false;
    }
    if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        error = "Could not read the buffers in " + path;
        return false;
    }
    if (data->skins_count < 1) {
        cgltf_free(data);
        error = path + " has no skeleton";
        return false;
    }
    const cgltf_skin& skin = data->skins[0];
    if ((int)skin.joints_count > MAX_BONES) {
        cgltf_free(data);
        error = "Too many bones in " + path;
        return false;
    }

    // Bones, in the skin's joint order (that is what the vertices' joint indices mean).
    bones.clear();
    auto jointIndex = [&](const cgltf_node* n) -> int {
        for (cgltf_size i = 0; i < skin.joints_count; ++i)
            if (skin.joints[i] == n) return (int)i;
        return -1;
    };
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        const cgltf_node* n = skin.joints[i];
        Bone b;
        b.name = n->name ? n->name : "";
        b.parent = n->parent ? jointIndex(n->parent) : -1;
        Vec3 t(0, 0, 0);
        Quat r;
        Vec3 s(1, 1, 1);
        if (n->has_translation) t = Vec3(n->translation[0], n->translation[1], n->translation[2]);
        if (n->has_rotation) r = {n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
        if (n->has_scale) s = Vec3(n->scale[0], n->scale[1], n->scale[2]);
        b.restT = mirrorT(t);
        b.restR = mirrorR(r);
        b.restS = s;
        float ib[16];
        if (skin.inverse_bind_matrices && cgltf_accessor_read_float(skin.inverse_bind_matrices, i, ib, 16)) {
            Mat4 m;
            std::memcpy(m.m, ib, sizeof m.m);
            b.invBind = mirrorM(m);
        } else {
            b.invBind = Mat4::identity();
        }
        if (b.parent < 0) {
            // Fold the armature node (and anything above it) into the root transform.
            Mat4 pre = Mat4::identity();
            for (const cgltf_node* a = n->parent; a; a = a->parent) pre = pre * nodeLocal(a);
            rootXform = mirrorM(pre);
        }
        bones.push_back(b);
    }
    // Rest orientation of each bone in model space (parents come before children in Blender's
    // export order, but walk explicitly to be safe).
    for (size_t i = 0; i < bones.size(); ++i) {
        Quat w = bones[i].restR;
        for (int p = bones[i].parent; p >= 0; p = bones[p].parent) w = hamilton(bones[p].restR, w);
        bones[i].restWorldR = w;
    }

    // Meshes: every node that has a mesh and this skin.
    meshes.clear();
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* n = &data->nodes[ni];
        if (!n->mesh || !n->skin) continue;
        ModelMesh mm;
        mm.name = n->name ? n->name : (n->mesh->name ? n->mesh->name : "");
        for (cgltf_size pi = 0; pi < n->mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = n->mesh->primitives[pi];
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *jnt = nullptr, *wgt = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& a = prim.attributes[ai];
                if (a.type == cgltf_attribute_type_position) pos = a.data;
                else if (a.type == cgltf_attribute_type_normal) nrm = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
                else if (a.type == cgltf_attribute_type_joints && a.index == 0) jnt = a.data;
                else if (a.type == cgltf_attribute_type_weights && a.index == 0) wgt = a.data;
            }
            if (!pos) continue;
            uint32_t base = (uint32_t)mm.verts.size();
            for (cgltf_size vi = 0; vi < pos->count; ++vi) {
                SkinnedVertex v = {};
                float f[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(pos, vi, f, 3);
                v.px = -f[0]; v.py = f[1]; v.pz = f[2];
                if (nrm && cgltf_accessor_read_float(nrm, vi, f, 3)) { v.nx = -f[0]; v.ny = f[1]; v.nz = f[2]; }
                else { v.ny = 1; }
                if (uv && cgltf_accessor_read_float(uv, vi, f, 2)) { v.u = f[0]; v.v = f[1]; }
                cgltf_uint j[4] = {0, 0, 0, 0};
                if (jnt) cgltf_accessor_read_uint(jnt, vi, j, 4);
                float w[4] = {1, 0, 0, 0};
                if (wgt) cgltf_accessor_read_float(wgt, vi, w, 4);
                float sum = w[0] + w[1] + w[2] + w[3];
                if (sum < 1e-6f) { w[0] = 1; sum = 1; }
                for (int k = 0; k < 4; ++k) {
                    v.joints[k] = (uint8_t)std::min<cgltf_uint>(j[k], MAX_BONES - 1);
                    v.weights[k] = w[k] / sum;
                }
                mm.verts.push_back(v);
            }
            if (prim.indices) {
                for (cgltf_size ii = 0; ii + 2 < prim.indices->count; ii += 3) {
                    // Reverse the winding: mirroring flips it.
                    mm.idx.push_back(base + (uint32_t)cgltf_accessor_read_index(prim.indices, ii));
                    mm.idx.push_back(base + (uint32_t)cgltf_accessor_read_index(prim.indices, ii + 2));
                    mm.idx.push_back(base + (uint32_t)cgltf_accessor_read_index(prim.indices, ii + 1));
                }
            } else {
                for (uint32_t ii = 0; ii + 2 < (uint32_t)pos->count; ii += 3) {
                    mm.idx.push_back(base + ii);
                    mm.idx.push_back(base + ii + 2);
                    mm.idx.push_back(base + ii + 1);
                }
            }
        }
        int id = (int)meshes.size();
        if (mm.name == "Body") bodyLod[0] = id;
        else if (mm.name == "Body_LOD1") bodyLod[1] = id;
        else if (mm.name == "Body_LOD2") bodyLod[2] = id;
        else if (mm.name == "Hair0") hair[0] = id;
        else if (mm.name == "Hair1") hair[1] = id;
        else if (mm.name == "Hair2") hair[2] = id;
        else if (mm.name == "Hair3") hair[3] = id;
        else if (mm.name == "Headband") headband = id;
        else if (mm.name == "Hat1") hat[1] = id;
        else if (mm.name == "Hat2") hat[2] = id;
        else if (mm.name == "Hat3") hat[3] = id;
        meshes.push_back(std::move(mm));
    }
    if (bodyLod[0] < 0) {
        cgltf_free(data);
        error = path + " has no Body mesh";
        return false;
    }
    if (bodyLod[1] < 0) bodyLod[1] = bodyLod[0];
    if (bodyLod[2] < 0) bodyLod[2] = bodyLod[1];

    // Animations.
    clips.clear();
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& an = data->animations[ai];
        AnimClip c;
        c.name = an.name ? an.name : "";
        float tMin = 1e9f, tMax = 0;
        for (cgltf_size ci = 0; ci < an.channels_count; ++ci) {
            const cgltf_animation_channel& ch = an.channels[ci];
            int bone = jointIndex(ch.target_node);
            if (bone < 0 || !ch.sampler) continue;
            if (ch.target_path != cgltf_animation_path_type_translation && ch.target_path != cgltf_animation_path_type_rotation) continue;
            AnimChannel* dst = nullptr;
            for (AnimChannel& e : c.channels)
                if (e.bone == bone) dst = &e;
            if (!dst) {
                c.channels.push_back(AnimChannel());
                dst = &c.channels.back();
                dst->bone = bone;
            }
            const cgltf_accessor* in = ch.sampler->input;
            const cgltf_accessor* out = ch.sampler->output;
            std::vector<float>& times = ch.target_path == cgltf_animation_path_type_translation ? dst->tTimes : dst->rTimes;
            for (cgltf_size k = 0; k < in->count; ++k) {
                float t = 0;
                cgltf_accessor_read_float(in, k, &t, 1);
                times.push_back(t);
                tMin = std::min(tMin, t);
                tMax = std::max(tMax, t);
                float f[4] = {0, 0, 0, 1};
                cgltf_accessor_read_float(out, k, f, ch.target_path == cgltf_animation_path_type_translation ? 3 : 4);
                if (ch.target_path == cgltf_animation_path_type_translation) dst->t.push_back(mirrorT(Vec3(f[0], f[1], f[2])));
                else dst->r.push_back(mirrorR({f[0], f[1], f[2], f[3]}));
            }
        }
        if (tMin > tMax) tMin = 0;
        for (AnimChannel& e : c.channels) {
            for (float& t : e.tTimes) t -= tMin;
            for (float& t : e.rTimes) t -= tMin;
        }
        c.duration = std::max(0.001f, tMax - tMin);
        clips.push_back(std::move(c));
    }
    cgltf_free(data);
    loaded = true;
    return true;
}

int CharacterModel::boneIndex(const char* name) const {
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].name == name) return (int)i;
    return -1;
}

int CharacterModel::clipIndex(const char* name) const {
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i].name == name) return (int)i;
    return -1;
}

const AnimClip* CharacterModel::clip(const char* name) const {
    int i = clipIndex(name);
    return i < 0 ? nullptr : &clips[i];
}

// ------------------------------------------------------------------ posing
void CharacterModel::restPose(Pose& out) const {
    for (size_t i = 0; i < bones.size() && i < MAX_BONES; ++i) {
        out.t[i] = bones[i].restT;
        out.r[i] = bones[i].restR;
        out.s[i] = bones[i].restS;
    }
}

namespace {
template <typename T, typename F>
T sampleTrack(const std::vector<float>& times, const std::vector<T>& vals, float t, F mix) {
    if (vals.empty()) return T();
    if (vals.size() == 1 || t <= times.front()) return vals.front();
    if (t >= times.back()) return vals.back();
    size_t hi = (size_t)(std::upper_bound(times.begin(), times.end(), t) - times.begin());
    size_t lo = hi - 1;
    float span = times[hi] - times[lo];
    float f = span > 1e-6f ? (t - times[lo]) / span : 0.0f;
    return mix(vals[lo], vals[hi], f);
}
}  // namespace

void CharacterModel::sampleClip(int ci, float time, bool loop, Pose& out) const {
    if (ci < 0 || ci >= (int)clips.size()) return;
    const AnimClip& c = clips[ci];
    float t = time;
    if (loop) {
        t = std::fmod(t, c.duration);
        if (t < 0) t += c.duration;
    } else {
        t = clampf(t, 0.0f, c.duration);
    }
    for (const AnimChannel& ch : c.channels) {
        if (ch.bone < 0 || ch.bone >= MAX_BONES) continue;
        if (!ch.t.empty())
            out.t[ch.bone] = sampleTrack(ch.tTimes, ch.t, t, [](const Vec3& a, const Vec3& b, float f) { return a + (b - a) * f; });
        if (!ch.r.empty())
            out.r[ch.bone] = sampleTrack(ch.rTimes, ch.r, t, [](const Quat& a, const Quat& b, float f) { return quatSlerp(a, b, f); });
    }
}

void CharacterModel::boneWorld(const Pose& p, Mat4* out) const {
    for (size_t i = 0; i < bones.size() && i < MAX_BONES; ++i) {
        Mat4 local = Mat4::scale(p.s[i]) * quatToMat(p.r[i]) * Mat4::translation(p.t[i]);
        int par = bones[i].parent;
        out[i] = par >= 0 ? local * out[par] : local * rootXform;
    }
}

void CharacterModel::skinMatrices(const Mat4* world, Mat4* out) const {
    for (size_t i = 0; i < bones.size() && i < MAX_BONES; ++i) out[i] = bones[i].invBind * world[i];
}

void CharacterModel::rotateInCharacterFrame(int bone, const Quat& q, Pose& p) const {
    if (bone < 0 || bone >= (int)bones.size()) return;
    // World rotation W becomes q*W; in the bone's own space that is L * (W^-1 q W).
    const Quat& W = bones[bone].restWorldR;
    Quat x = hamilton(hamilton(quatConj(W), q), W);
    p.r[bone] = quatNormalize(hamilton(p.r[bone], x));
}

Vec3 CharacterModel::bonePosition(const Mat4* world, int bone) const {
    if (bone < 0 || bone >= (int)bones.size()) return Vec3(0, 0, 0);
    return Vec3(world[bone].m[3][0], world[bone].m[3][1], world[bone].m[3][2]);
}

void blendPose(const Pose& a, const Pose& b, float t, Pose& out) {
    for (int i = 0; i < MAX_BONES; ++i) {
        out.t[i] = a.t[i] + (b.t[i] - a.t[i]) * t;
        out.r[i] = quatSlerp(a.r[i], b.r[i], t);
        out.s[i] = a.s[i] + (b.s[i] - a.s[i]) * t;
    }
}

void blendPoseMasked(const Pose& a, const Pose& b, float t, const bool* mask, Pose& out) {
    for (int i = 0; i < MAX_BONES; ++i) {
        if (!mask[i]) {
            out.t[i] = a.t[i];
            out.r[i] = a.r[i];
            out.s[i] = a.s[i];
            continue;
        }
        out.t[i] = a.t[i] + (b.t[i] - a.t[i]) * t;
        out.r[i] = quatSlerp(a.r[i], b.r[i], t);
        out.s[i] = a.s[i] + (b.s[i] - a.s[i]) * t;
    }
}

}  // namespace bf
