// Blockfall - the map: terrain, props, chests, dropped items, structures, collision, raycasts
#pragma once
#include "../core/math.h"
#include "../core/rng.h"
#include "building.h"
#include "types.h"
#include <string>
#include <vector>

namespace bf {

constexpr float MAP_HALF = 400.0f;   // map is 800m x 800m centred on the origin (room for 100 players)
constexpr float HEIGHTMAP_STEP = 1.0f;

enum class PropKind : int {
    Ground, Trunk, Leaves, Rock, HouseWall, HouseRoof, HouseFloor, Barrel,
    Silo,     // tall metal cylinder (farm)
    Log,      // wooden log lying along x (lumber yard)
    Hay,      // hay bale lying along x (farm)
    Fence,    // thin wooden rail
    Crate,    // wooden box (depot, market)
    Road,     // flat dark slab you can walk on (town)
    Post,     // thin wooden/metal post (canopies, market stalls)
    Canopy,   // flat roof on posts (gas station, market)
    Stone,    // old stone wall (ruins)
    Well,     // stone ring (village)
    Step,     // stair step inside towers
};

// What kind of place a POI is. Each kind is built differently so they all look distinct.
enum class PoiKind : int { Town, Farm, Tower, Lumber, Quarry, GasStation, Camp, Ruins, Market, Depot, Village, Lookout, Count };
const char* poiKindName(PoiKind k);

struct StaticBox {
    AABB box;
    Vec4 color;
    PropKind kind;
    bool collide;      // blocks movement
    bool blockShots;   // blocks bullets and line of sight
    int group = -1;    // props that break together (a tree's trunk + leaves); -1 = can't be harvested
    bool alive = true;
};

// A harvestable prop (tree, rock, house wall/roof). Hit it with the pickaxe for materials.
struct PropGroup {
    float hp = 0, maxHp = 0;
    int matsPerHit = 0;
    int matType = 0;    // BuildMat: 0 wood, 1 brick, 2 metal
    float hitAnim = 0;  // wobble after a hit
    bool alive = true;
};

struct HarvestResult {
    int mats = 0;
    int matType = 0;    // BuildMat
    bool destroyed = false;
};

// A named place on the map: a cluster of houses with extra loot around it.
struct Poi {
    std::string name;
    Vec3 center;
    float radius = 20.0f;
    bool discovered = false;
    PoiKind kind = PoiKind::Town;
};

struct Chest {
    Vec3 pos;          // centre of the bottom face
    float yaw = 0;
    bool opened = false;
    float openAnim = 0;  // 0..1 lid animation after opening
    AABB bounds() const { return AABB::fromCenter(pos + Vec3(0, 0.45f, 0), Vec3(0.55f, 0.45f, 0.55f)); }
};

struct Pickup {
    Item item;
    Vec3 pos;
    Vec3 vel;
    bool alive = true;
    bool settled = false;
    float age = 0;
};

enum class HitKind : int { None = 0, Terrain, Static, Piece, Chest, Actor, Bounds };

struct HitInfo {
    HitKind kind = HitKind::None;
    float t = 1e9f;
    Vec3 point;
    Vec3 normal;
    int id = -1;        // piece id / chest index / actor index / static index
    bool head = false;  // actor headshot
};

struct Body {
    Vec3 pos;           // feet
    Vec3 vel;
    float radius = 0.4f;
    float height = 1.8f;
    bool grounded = false;
    // Fall damage bookkeeping: the highest point since leaving the ground.
    bool airborne = false;
    float fallPeak = 0;
    float fallImmune = 0;   // seconds left without fall damage (after a shockwave or the glider)
    AABB box() const { return {{pos.x - radius, pos.y, pos.z - radius}, {pos.x + radius, pos.y + height, pos.z + radius}}; }
};

class World {
public:
    void generate(uint64_t seed);
    int generation = 0;   // bumps every time a new map is generated

    // ---- terrain
    float terrainHeight(float x, float z) const;
    // Sets the terrain to a flat height inside a radius (used by tests and for building sites).
    void flattenTerrain(float x, float z, float radius, float y);
    int hmSize() const { return hmSize_; }
    float hmAt(int ix, int iz) const { return heights_[iz * hmSize_ + ix]; }

    // ---- queries
    // First thing the ray hits (terrain, props, structures, chests). Actors are handled by Game.
    bool raycast(const Ray& r, float maxT, HitInfo& hit, bool includeChests = true) const;
    // Highest walkable surface under the body footprint at or below feetY + stepUp.
    float groundHeight(float x, float z, float radius, float feetY, float stepUp) const;
    void collectSolids(const AABB& region, std::vector<AABB>& out) const;
    // Moves a body with gravity and collision. `extra` are other solid boxes (e.g. other actors).
    void moveBody(Body& b, float dt, const std::vector<AABB>& extra, float gravity = 22.0f) const;

    // ---- content
    BuildSystem build;
    std::vector<StaticBox> statics;
    std::vector<Chest> chests;
    std::vector<Pickup> pickups;
    std::vector<Vec3> houseCenters;
    std::vector<Poi> pois;
    // Index of the POI containing (x,z), or -1.
    int poiAt(float x, float z) const;
    std::vector<PropGroup> groups;
    Vec3 playerSpawn;
    std::vector<Vec3> enemySpawns;   // spread-out spots; one per team (index 0 is the player's team)

    // Build support. A piece is supported if it stands on the ground or a prop, or touches a
    // supported piece. Pieces that lose support break. Returns how many broke.
    bool pieceGrounded(const Slot& s) const;
    bool pieceGrounded(const Piece& p) const;   // uses the piece's real shape (edited tiles don't count)
    bool wouldBeSupported(const Slot& s) const;
    int breakUnsupported(std::vector<int>* brokenIds = nullptr);
    // Pickaxe hit on a static prop. Gives materials and breaks it when its health runs out.
    HarvestResult harvest(int staticIndex, float damage);
    void updateProps(float dt);

    void spawnPickup(const Item& item, const Vec3& pos, const Vec3& vel);
    void updatePickups(float dt);

private:
    void buildHouse(const Vec3& center, int doorSide, Rng& rng);
    // Bigger building: halfX/halfZ metres, wall height, with a door on doorSide and windows.
    void buildBuilding(const Vec3& c, float halfX, float halfZ, float wallH, int doorSide, const Vec4& wallCol, const Vec4& roofCol,
                       bool openFront, Rng& rng);
    void buildTower(const Vec3& c, int floors, Rng& rng);
    void buildPoi(Poi& poi, Rng& rng);
    void addTree(float x, float z, Rng& rng, float scale = 1.0f);
    void addRock(float x, float z, Rng& rng, float scale = 1.0f);
    void addChest(float x, float z, Rng& rng);
    void addBarrel(float x, float z);
    int newGroup(float hp, int matsPerHit, int matType);
    bool spotFree(float x, float z, float r) const;

    int hmSize_ = 0;
    std::vector<float> heights_;
    std::vector<Vec3> occupied_;   // xz + radius in .y for placement rejection
};

}  // namespace bf
