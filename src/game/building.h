// Blockfall - grid building: walls, floors, ramps, cones
#pragma once
#include "../core/math.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace bf {

constexpr float TILE = 6.0f;          // grid cell width (metres)
constexpr float STORY = 4.5f;         // grid cell height (metres)
constexpr float WALL_THICK = 0.25f;
constexpr float FLOOR_THICK = 0.25f;
constexpr float CONE_HEIGHT = 2.4f;
constexpr float CONE_EDIT_HEIGHT = 4.0f;   // the raised side of an edited cone
constexpr float BUILD_UP_TIME = 2.5f;      // seconds for a fresh piece to reach its health
constexpr float BUILD_UP_START = 0.2f;     // fraction of its health a fresh piece starts with
constexpr int BUILD_COST = 10;
constexpr float BUILD_REACH = 0.8f * TILE;

enum class PieceType : int { Wall = 0, Floor, Ramp, Cone, Count };
enum class BuildMat : int { Wood = 0, Brick, Metal, Count };
inline const char* buildMatName(BuildMat m) {
    switch (m) {
        case BuildMat::Brick: return "BRICK";
        case BuildMat::Metal: return "METAL";
        default: return "WOOD";
    }
}

inline const char* pieceName(PieceType t) {
    switch (t) {
        case PieceType::Wall: return "WALL";
        case PieceType::Floor: return "FLOOR";
        case PieceType::Ramp: return "RAMP";
        case PieceType::Cone: return "CONE";
        default: return "?";
    }
}
// Health: wood 150, brick 300, metal 450 (walls); other pieces a little less.
inline float pieceMaxHp(PieceType t, BuildMat m = BuildMat::Wood) {
    static const float wall[3] = {150.0f, 300.0f, 450.0f}, other[3] = {140.0f, 280.0f, 400.0f};
    return (t == PieceType::Wall ? wall : other)[(int)m];
}
// Number of editable tiles along each axis (0 = not editable).
inline int editGridSize(PieceType t) {
    switch (t) {
        case PieceType::Wall: return 3;
        case PieceType::Floor: return 2;
        case PieceType::Cone: return 2;
        case PieceType::Ramp: return 2;
        default: return 0;
    }
}

// Grid slot a piece occupies.
//  Wall:  side 0 = wall on the plane x = i*TILE (spans z in [j, j+1] cells)
//         side 1 = wall on the plane z = j*TILE (spans x in [i, i+1] cells)
//  Ramp:  dir 0..3 = rises toward +z, +x, -z, -x
//  Floor: sits at y = k*STORY.  Cone: base at y = k*STORY.
struct Slot {
    PieceType type = PieceType::Wall;
    int i = 0, j = 0, k = 0;
    int side = 0;  // walls only
    int dir = 0;   // ramps only
};

struct Piece {
    int id = -1;
    Slot slot;
    BuildMat mat = BuildMat::Wood;
    float hp = 0, maxHp = 0;
    float targetHp = 0;     // hp grows toward this after placing or editing (builds "build up")
    uint16_t removed = 0;   // edited tiles (bit per tile). Walls/floors: tile is cut out.
                            // Cones: the selected tiles are the side that rises (see coneHeightAt).
    bool alive = false;
    bool playerBuilt = true;
    float age = 0;          // seconds since placed (for pop-in animation)
};

struct Tri { Vec3 a, b, c; };

// Geometry queries -------------------------------------------------------
// Solid boxes that block movement and shots (walls and floors minus edited tiles).
void pieceBoxes(const Piece& p, std::vector<AABB>& out);
// Walkable sloped surfaces (ramps and cones) as triangles, minus edited tiles.
void pieceTris(const Piece& p, std::vector<Tri>& out);
// Bounds of the whole piece (ignores edits).
AABB pieceBounds(const Slot& s);
// Height of a cone's surface above its base at local (lx, lz) in 0..TILE, with its edits applied.
float coneHeightAt(const Piece& p, float lx, float lz);
float rampHeightAt(const Piece& p, float lx, float lz);
// Stairs edit, Fortnite style: you swipe across the 2x2 grid and the stairs rise the way you
// swiped. Two tiles in a line = straight stairs that way; an L of three = turn stairs climbing
// from the first tile round to the last; four tiles or one tile = back to straight stairs.
// Stores the result in the piece (its direction, and `removed` for turn stairs).
void applyStairEdit(Piece& p, uint16_t selected, int firstTile, int lastTile);
// Wall edit shape rules (door / window / arch / half wall). Tiles are row*3+col, row 0 at the
// bottom. A cut is valid if the removed tiles form one rectangle and every tile left standing
// still reaches the bottom row through other kept tiles (nothing floats). No cut is valid;
// cutting all 9 tiles is not.
bool validWallEdit(uint16_t removed);
// Same check for any piece type: only walls have shape rules, so other types always pass.
bool validEdit(PieceType type, uint16_t removed);
// Height of a ramp/cone surface at (x,z); returns false if (x,z) is outside or on a removed tile.
bool pieceSurfaceHeight(const Piece& p, float x, float z, float& yOut);
// Which tile of an editable piece a ray hits (ignores edits). Returns -1 if none.
int pieceTileUnderRay(const Piece& p, const Ray& r, float maxT);
// Bounds of one tile of an editable piece (for drawing the edit grid).
AABB pieceTileBounds(const Piece& p, int tile);
// Cone quadrant index (0..3 in rotation order) -> tile index in the 2x2 grid.
int coneQuadrantTile(int quadrant);
// Ramp direction from a yaw angle (yaw 0 faces +z).
int cardinalFromYaw(float yaw);

struct BuildTarget {
    Slot slot;
    bool valid = false;       // slot free, supported, and enough materials
    bool occupied = false;
    bool unsupported = false; // would float in the air
};

// Computes where a piece would be placed, Fortnite-style, from where the player stands and aims.
//   feet:      player's feet position
//   yaw:       player's facing yaw
//   aim:       camera ray through the crosshair
//   worldHitT: distance along `aim` to the first thing it hits (or a big number)
Slot computeBuildSlot(PieceType type, const Vec3& feet, float yaw, const Ray& aim, float worldHitT,
                      int rampRotation);

class BuildSystem {
public:
    void clear();
    bool isFree(const Slot& s) const;
    // Returns new piece id or -1 if the slot is taken.
    int place(const Slot& s, bool playerBuilt, BuildMat mat = BuildMat::Wood);
    void destroy(int id);
    // Returns true if the piece was destroyed by this damage.
    bool damage(int id, float amount);
    Piece* get(int id);
    const Piece* get(int id) const;
    const std::vector<Piece>& pieces() const { return pieces_; }
    std::vector<Piece>& pieces() { return pieces_; }
    int aliveCount() const;
    void update(float dt);

    static int64_t slotKey(const Slot& s);

private:
    std::vector<Piece> pieces_;
    std::unordered_map<int64_t, int> slots_;
};

}  // namespace bf
