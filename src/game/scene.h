// Blockfall - turns game state into a list of mesh instances for the renderer
#pragma once
#include "../core/math.h"
#include "game.h"
#include <string>
#include <vector>

namespace bf {

enum MeshId : int { MESH_CUBE = 0, MESH_CONE_QUAD, MESH_TERRAIN, MESH_SPHERE, MESH_CYLINDER, MESH_ROCK, MESH_TRI, MESH_COUNT };

// Surface types. The renderer turns these into roughness / metal / bump patterns.
enum MaterialId : int {
    MAT_DEFAULT = 0, MAT_WOOD, MAT_STONE, MAT_GRASS, MAT_CLOTH, MAT_METAL, MAT_LEAF, MAT_SKIN, MAT_PLASTER, MAT_ROOF, MAT_BARK,
    MAT_COUNT
};

// Matches the per-instance vertex buffer layout in the renderer (96 bytes).
struct Instance {
    Mat4 world;
    Vec4 color;
    Vec4 params;  // x = glow (0..1), y = material id
};

// What a character is doing, for the animation.
enum class Anim : int { Idle = 0, Walk, Sprint, Crouch, Jump, Aim, Shoot, Reload, Build, Heal, Pickaxe, Skydive, Glide, Count };
const char* animName(Anim a);
// Picks the player's animation from their state.
Anim playerAnim(const Game& g);
Anim botAnim(const Game& g, const Bot& b);

// One rigged, textured character (drawn with GPU skinning; see model.h).
struct CharacterDraw {
    Mat4 world;
    Vec4 tint;        // multiplies the texture (white = as painted)
    Vec4 accent;      // replaces the texture's accent areas when params.z > 0 (teammate trim)
    Vec4 params;      // x glow, y hurt flash 0..1, z accent strength, w unused
    int skin = 0;     // texture index
    int hair = 0;     // hair style mesh (0..3), -1 none
    int hat = 0;      // 0 none, 1 cap, 2 beanie, 3 helmet
    bool headband = false;
    int build = 0;          // BodyBuild
    uint32_t kit = 0;       // outfit pieces worn (bit per KitPiece)
    int kitMaterial = 0;    // KitMaterial of the main pieces
    int fx = 0;             // emission FX (SKIN_FX_TRIM / SKIN_FX_EYES)
    bool face = true;       // draw the 3D face parts (close up)
    bool shadowOnly = false;  // hidden from the camera (occlusion culling): only casts its shadow
    // Palette for the outfit pieces, hair and face (the body uses its painted skin texture).
    Vec4 main, second, trim, hairColor, skinTone;
    float distance = 0;   // metres from the camera (the renderer picks the level of detail)
    int boneOffset = 0;   // into SceneList::boneMats
    int boneCount = 0;
};

struct SceneList {
    std::vector<Instance> opaque[MESH_COUNT];
    std::vector<Instance> transparent[MESH_COUNT];
    std::vector<Instance> shadowOnly[MESH_COUNT];   // culled from the view but still casting shadows
    std::vector<CharacterDraw> characters;
    std::vector<Mat4> boneMats;
    Mat4 view, proj;
    Vec3 eye;
    Vec3 sunDir;
    Vec4 sky;
    bool drawTerrain = true;
    void clear() {
        for (int i = 0; i < MESH_COUNT; ++i) {
            opaque[i].clear();
            transparent[i].clear();
            shadowOnly[i].clear();
        }
        characters.clear();
        boneMats.clear();
    }
    size_t totalInstances() const {
        size_t n = 0;
        for (int i = 0; i < MESH_COUNT; ++i) n += opaque[i].size() + transparent[i].size();
        return n;
    }
};

Mat4 cameraView(const Camera& c);
Mat4 cameraProj(const Camera& c, float aspect);
void buildScene(const Game& g, float aspect, SceneList& out);

// Occlusion culling in buildScene (on by default) and what it did last frame.
struct OcclusionStats {
    int tested = 0, culledFrustum = 0, culledOccluded = 0, occluderTriangles = 0;
};
void setOcclusionCulling(bool on);
bool occlusionCulling();
OcclusionStats lastOcclusionStats();

// Shirt colour for a team (team 0 = yours).
Vec4 teamColor(int team, bool isPlayer);

// The lobby: you and your party standing on a stage.
struct LobbyMember {
    std::string name;
    Vec4 shirt;
    bool you = false;
    int skin = 0;
    bool dance = false;   // locker preview
};
// Fills `out` and returns the lobby camera (the HUD uses it for name tags).
Camera buildLobbyScene(const std::vector<LobbyMember>& party, float time, float aspect, SceneList& out);
// Where each lobby member stands (for name tags).
Vec3 lobbySpot(int i);

// Unit meshes (positions/normals) shared by the renderer and tests.
struct MeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
};
void makeCubeMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx);
void makeConeQuadMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx);
// A unit triangle (0,0,0)-(1,0,0)-(0,0,1); an instance matrix maps it onto any world triangle.
void makeTriMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx);
Mat4 triangleMatrix(const Vec3& a, const Vec3& b, const Vec3& c);
// lod: 0 = few triangles, 1 = normal, 2 = many (the renderer picks one by distance and the
// Mesh Detail setting, a little like Nanite picking cluster detail).
constexpr int MESH_LODS = 3;
void makeSphereMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int lod = 1);
void makeCylinderMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int lod = 1);
void makeRockMesh(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int lod = 1);
// step: heightmap samples per vertex (1 = finest, 4 = coarsest).
void makeTerrainMesh(const World& w, std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, int step = 2);
// Whether a mesh has LOD variants.
inline bool meshHasLods(int m) { return m == MESH_SPHERE || m == MESH_CYLINDER || m == MESH_ROCK; }

}  // namespace bf
