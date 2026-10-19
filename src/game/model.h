// Blockfall - the character model: a rigged, textured, animated mesh loaded from assets/character.glb
//
// The file is made by tools/character/build_character.py (Blender). It holds:
//   - the body at three levels of detail, four hair styles and a headband
//   - one shared skeleton (22 bones) that every skin uses
//   - every animation clip (idle, walk, sprint, aim, swing, ...)
// Each vertex says which bones move it (up to 4) and how much. The animation player works out
// where every bone is each frame; the GPU bends the mesh to match (see the renderer).
#pragma once
#include "../core/math.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bf {

constexpr int MAX_BONES = 48;   // body 22 + face 7 + hair/cape/scarf swing chains 8, with room to spare
constexpr int MAX_BUILDS = 3;   // body builds (see BodyBuild in skins.h)
constexpr int MAX_KIT = 16;     // outfit kit pieces (see KitPiece in skins.h)

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};
Quat quatAxisAngle(const Vec3& axis, float angle);
Quat quatMul(const Quat& a, const Quat& b);   // a then b (like matrices in this code base)
Quat quatSlerp(const Quat& a, const Quat& b, float t);
Quat quatConj(const Quat& q);
Mat4 quatToMat(const Quat& q);
Quat quatFromMat(const Mat4& m);   // the rotation part (rows are normalised, so scale is ignored)
Vec3 quatRotate(const Quat& q, const Vec3& v);
Quat quatFromTo(const Vec3& from, const Vec3& to);   // shortest rotation taking one direction to another

// Matches the vertex buffer layout for skinned meshes in the renderer (60 bytes).
struct SkinnedVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;        // the skin texture
    float u2, v2;      // the shared detail atlas (TEXCOORD_1; same as u, v when the file has none)
    uint8_t joints[4];
    float weights[4];
};

struct ModelMesh {
    std::string name;
    std::vector<SkinnedVertex> verts;
    std::vector<uint32_t> idx;
};

struct Bone {
    std::string name;
    int parent = -1;
    Vec3 restT;        // local rest transform (relative to the parent)
    Quat restR;
    Vec3 restS{1, 1, 1};
    Quat restWorldR;   // rest orientation in model space (used to turn "character frame" rotations into bone space)
    Mat4 invBind;      // model space -> bone space at rest
};

struct AnimChannel {
    int bone = -1;
    std::vector<float> tTimes;
    std::vector<Vec3> t;
    std::vector<float> rTimes;
    std::vector<Quat> r;
};

struct AnimClip {
    std::string name;
    float duration = 0;
    std::vector<AnimChannel> channels;
};

// Local transform of every bone.
struct Pose {
    Vec3 t[MAX_BONES];
    Quat r[MAX_BONES];
    Vec3 s[MAX_BONES];
};

struct CharacterModel {
    bool loaded = false;
    std::vector<ModelMesh> meshes;
    std::vector<Bone> bones;
    std::vector<AnimClip> clips;
    int bodyLod[3] = {-1, -1, -1};   // the athletic build (build 0)
    int buildLod[MAX_BUILDS][3] = {{-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};  // every build; falls back to build 0
    int hair[4] = {-1, -1, -1, -1};
    int hat[4] = {-1, -1, -1, -1};   // 0 unused (no hat), 1 cap, 2 beanie, 3 helmet
    int headband = -1;
    int kit[MAX_KIT][MAX_BUILDS];    // outfit piece per build ("jacket_B1"), -1 if missing
    int face = -1;                   // eyes, eyelids and brows (moved by the face bones)
    int mouth = -1;                  // lips (hidden under a mask)
    CharacterModel() {
        for (auto& k : kit)
            for (int& b : k) b = -1;
    }
    int meshIndex(const std::string& name) const;
    int bodyMesh(int build, int lod) const;   // clamps build and lod
    int kitMesh(int piece, int build) const;  // -1 if the model has no such piece
    Mat4 rootXform = Mat4::identity();  // transform above the root bones (the armature node)

    // Loads a .glb. The file is right-handed (glTF); it is mirrored on X so the character's
    // right hand is on its right in Blockfall's left-handed world.
    bool load(const std::string& path, std::string& error);
    int boneIndex(const char* name) const;
    int clipIndex(const char* name) const;
    const AnimClip* clip(const char* name) const;

    void restPose(Pose& out) const;
    // Samples a clip into `out` (bones the clip doesn't touch keep what `out` already has).
    void sampleClip(int clip, float time, bool loop, Pose& out) const;
    // Model-space matrix per bone for a pose.
    void boneWorld(const Pose& p, Mat4* out) const;
    // Matrices the GPU multiplies vertices with: invBind * boneWorld.
    void skinMatrices(const Mat4* world, Mat4* out) const;
    // Rotates a bone's pose by `q` given in the character's frame (x right, y up, z forward).
    void rotateInCharacterFrame(int bone, const Quat& q, Pose& p) const;
    // Moves a bone by `offset` metres in the character's frame, from its rest position.
    void offsetInCharacterFrame(int bone, const Vec3& offset, Pose& p) const;
    // Turns a bone so that, in model space, it is rotated by `q` from where `world` (the
    // matrices of the current pose) has it. Children follow.
    void rotateBoneInModel(int bone, const Quat& q, const Mat4* world, Pose& p) const;
    // Position of a bone's origin in model space for a pose.
    Vec3 bonePosition(const Mat4* world, int bone) const;
};

void blendPose(const Pose& a, const Pose& b, float t, Pose& out);
// Like blendPose but only for bones where mask[bone] is true (others keep `a`).
void blendPoseMasked(const Pose& a, const Pose& b, float t, const bool* mask, Pose& out);

// The model shared by the scene builder and the renderer. Set once at startup.
const CharacterModel* characterModel();
void setCharacterModel(const CharacterModel* m);

}  // namespace bf
