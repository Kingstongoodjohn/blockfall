// Blockfall - game state and the main update loop (no platform code here)
#pragma once
#include "../core/math.h"
#include "../core/rng.h"
#include "building.h"
#include "input.h"
#include "nav.h"
#include "sfx.h"
#include "types.h"
#include "weapons.h"
#include "world.h"
#include <string>
#include <vector>

namespace bf {

constexpr int HOTBAR_SLOTS = 5;
constexpr int MAX_MATS = 999;
constexpr float FALL_SAFE_DROP = 7.0f;     // metres you can drop without damage (about 1.5 stories)
constexpr float FALL_DAMAGE_PER_M = 7.0f;  // damage per metre beyond that
constexpr float FALL_MAX_DAMAGE = 90.0f;
constexpr int START_MATS = 100;
constexpr float PLAYER_MAX_HEALTH = 100.0f;
constexpr float PLAYER_MAX_SHIELD = 100.0f;
constexpr float WALK_SPEED = 4.6f;
constexpr float SPRINT_SPEED = WALK_SPEED * 1.5f;
constexpr float CROUCH_SPEED = 2.6f;
constexpr float JUMP_SPEED = 7.2f;
constexpr float CHEST_OPEN_TIME = 0.5f;
constexpr float INTERACT_RANGE = 2.8f;
constexpr float EDIT_RANGE = 9.0f;
constexpr float BOT_DAMAGE_SCALE = 0.45f;  // bots hit softer than you so they're beatable
constexpr float BOT_VS_BOT_DAMAGE_SCALE = 0.55f;  // ...and softer again against each other (longer matches)
constexpr int PLAYER_ACTOR_ID = -100;      // HitInfo::id when a trace hits the player
constexpr int NO_TARGET = -1000;

// Stamina: sprinting uses it up, it refills when you stop.
constexpr float STAMINA_MAX = 100.0f;
constexpr float STAMINA_DRAIN = 16.0f;        // per second of sprinting (~6 s from full)
constexpr float STAMINA_REGEN = 22.0f;        // per second
constexpr float STAMINA_REGEN_DELAY = 1.0f;   // seconds after sprinting before it refills
constexpr float STAMINA_RECOVER = 25.0f;      // after running dry, need this much to sprint again

enum class Mode : int { Combat = 0, Build, Edit };
enum class DropPhase : int { OnBus = 0, Skydive, Glide, Landed };

// Team size: 1 = Solo, 2 = Duos, 3 = Trios, 4 = Squads.
inline const char* teamModeName(int teamSize) {
    switch (teamSize) {
        case 2: return "DUOS";
        case 3: return "TRIOS";
        case 4: return "SQUADS";
        default: return "SOLO";
    }
}
// Number of teams in a match for each mode (100 players total, like a real battle royale).
constexpr int MATCH_PLAYERS = 100;
inline int teamCountFor(int teamSize) {
    switch (teamSize) {
        case 2: return 50;
        case 3: return 34;
        case 4: return 25;
        default: return MATCH_PLAYERS;
    }
}

struct MatchConfig {
    int teamSize = 1;
    int skin = 0;                    // your equipped skin
    std::string playerName = "PLAYER";
    std::vector<std::string> party;  // friends in your party (become AI teammates with their names)
    uint64_t seed = 1337;
    bool useBus = false;             // start on the battle bus (the lobby turns this on)
};

struct EditState {
    int pieceId = -1;
    uint16_t selected = 0;   // tiles marked to cut out
    int firstTile = -1;      // where the drag started (stairs: the low end)
    int lastTile = -1;       // the tile the drag reached last (stairs: the high end)
    bool dragging = false;
    bool dragSelect = true;  // true: drag adds tiles, false: drag removes them
    Mode returnMode = Mode::Combat;
};

struct Player {
    std::string name = "PLAYER";
    int skin = 0;
    Body body;
    float yaw = 0, pitch = 0;
    float health = PLAYER_MAX_HEALTH, shield = 50.0f;
    Item slots[HOTBAR_SLOTS];
    GunState gunStates[HOTBAR_SLOTS];
    int ammo[(int)AmmoType::Count] = {0, 0, 0, 0};
    int selected = 0;
    bool pickaxeOut = true;   // holding the pickaxe instead of a hotbar item
    float swingCooldown = 0;
    float swingAnim = 0;      // > 0 during a swing
    float stamina = STAMINA_MAX;
    float staminaDelay = 0;
    bool exhausted = false;
    int matCount[(int)BuildMat::Count] = {START_MATS, 0, 0};  // wood, brick, metal
    BuildMat buildMat = BuildMat::Wood;
    int& curMats() { return matCount[(int)buildMat]; }
    int curMats() const { return matCount[(int)buildMat]; }
    Mode mode = Mode::Combat;
    PieceType buildPiece = PieceType::Wall;
    int rampRotation = 0;
    float buildCooldown = 0;
    EditState edit;
    bool crouched = false, sprinting = false, ads = false;
    float medkitTimer = -1;   // >= 0 while using a Med Kit
    float interactTimer = 0;  // chest hold progress
    float walkPhase = 0;
    int lastStep = 0;         // footstep sound: walkPhase / PI when the last one played
    bool alive = true;
    // Knocked (duos/trios/squads): down on the ground, bleeding out, until a teammate revives you.
    bool down = false;
    float downHealth = 0;
    float reviveProgress = 0;   // 0..1, filled by whoever is reviving you
    bool revivedThisFrame = false;
    int lastAttacker = NO_TARGET;
    float hurtTimer = 0;
    int kills = 0;
    float shotAnim = 0;       // recoil kick, counts down after each shot
    float buildAnim = 0;      // hand "slap" after placing a piece
    float idleTime = 0;       // seconds standing still

    Vec3 eyePivot() const { return body.pos + Vec3(0, crouched ? 1.15f : 1.55f, 0); }
    Vec3 forward() const;
};

enum class AIState : int { Wander = 0, Spot, Chase, Shoot, TakeCover, Flee };
// How a bot plays. Fighters push and build ramps; hiders run, hide in houses and box up; mixed
// bots do a bit of everything.
enum class BotStyle : int { Fighter = 0, Hider, Mixed, Count };
const char* botStyleName(BotStyle s);

inline const char* aiStateName(AIState s) {
    switch (s) {
        case AIState::Wander: return "WANDER";
        case AIState::Spot: return "SPOT";
        case AIState::Chase: return "CHASE";
        case AIState::Shoot: return "SHOOT";
        case AIState::TakeCover: return "COVER";
        default: return "?";
    }
}

// Every other player in the match. Team 0 is your team; the rest are enemies.
struct Bot {
    int index = 0;
    int team = 1;
    std::string name;
    bool isFriend = false;   // a friend from your party
    int skin = 0;
    Body body;
    float yaw = 0;
    float health = 100.0f;
    Item gun;
    GunState gunState;
    AIState state = AIState::Wander;
    float stateTime = 0;
    float reaction = 0;
    float losTimer = 0;      // time until the next look-around
    bool canSee = false;     // can see its current target
    int targetId = NO_TARGET;  // PLAYER_ACTOR_ID or a bot index
    float unseenTime = 0;
    Vec3 moveTarget;
    Vec3 lastKnown;
    Vec3 coverPos;
    Vec3 coverWaypoint;      // corner to run around when the cover spot isn't in a straight line
    bool useWaypoint = false;
    bool hasCover = false;
    bool triedCover = false;
    int burstLeft = 0;
    float burstPause = 0;
    float strafe = 1;
    float strafeTimer = 0;
    float stuckTime = 0;
    float detourTime = 0;
    float detourAngle = 0;
    // Pathfinding: when a bot gets stuck it plans a route around the obstacle (see nav.h).
    std::vector<Vec3> path;  // corners to walk through; empty = walk straight at the target
    size_t pathNext = 0;
    Vec3 pathGoal;           // the target the path was planned for
    float pathAge = 0;
    int pathFails = 0;       // got stuck again while following a path
    int paths = 0;           // paths planned (stats / tests)
    float coverTime = 0;     // time spent hiding at the cover spot
    float walkPhase = 0;
    int lastStep = 0;
    float hurtTimer = 0;
    int kills = 0;
    float shotAnim = 0;
    BotStyle style = BotStyle::Mixed;
    int mats[3] = {0, 0, 0};   // wood, brick, metal for building
    float shield = 0;          // from potions it finds
    int medkits = 0, minis = 0, bigpots = 0;
    float healTimer = -1;      // >= 0 while using a heal item
    ItemKind healing = ItemKind::None;
    float lootTimer = 0;
    int looted = 0;            // items picked up (stats / tests)
    float buildCooldown = 0;
    int lastAttacker = NO_TARGET;
    float fleeTime = 0;
    int builds = 0;            // pieces placed (stats / tests)
    DropPhase drop = DropPhase::Landed;
    float dropAt = 0;          // bus time (0..1) when this bot jumps
    Vec3 landing;              // where it steers to while gliding
    bool alive = true;
    bool down = false;         // knocked: crawling, bleeding out, waiting for a revive
    float downHealth = 0;
    float reviveProgress = 0;
    bool revivedThisFrame = false;
    int reviveTarget = NO_TARGET;  // the teammate this bot is going to pick up
};

// Knocked phase (team modes only). A knocked player bleeds out in KNOCK_HEALTH / KNOCK_BLEED seconds
// unless a standing teammate holds Interact next to them for REVIVE_TIME.
constexpr float KNOCK_HEALTH = 100.0f;
constexpr float KNOCK_BLEED = 2.5f;      // down-health lost per second
constexpr float REVIVE_TIME = 5.0f;
constexpr float REVIVE_RANGE = 2.6f;
constexpr float REVIVE_HEALTH = 30.0f;
constexpr float CRAWL_SPEED = 1.3f;

// Running damage total on one target; resets after a second without a hit.
struct DamageStack {
    int victim = NO_TARGET;
    float total = 0;
    float sinceHit = 0;
    bool head = false;
    Vec3 pos;
};

struct DamageNumber {
    Vec3 pos;
    int amount;
    Vec4 color;
    float age = 0;
    bool mats = false;  // "+8" materials instead of damage
};

// A thrown shockwave grenade, and the blast ring it leaves.
struct Grenade {
    Vec3 pos, vel;
    float age = 0;
    float fuse = -1;    // once it lands: seconds left before it goes off (-1 while still flying)
    bool alive = true;
};
constexpr float SHOCKWAVE_FUSE = 1.0f;  // it sits where it lands for this long, then blows
struct ShockRing {
    Vec3 pos;
    float age = 0;
};

struct Tracer {
    Vec3 from, to;
    Vec4 color;
    float age = 0;
};

struct Message {
    std::string text;
    Vec4 color;
    float age = 0;
};

struct Camera {
    Vec3 eye, forward, right, up;
    float fovY = deg2rad(70.0f);
    Ray aimRay;          // through the exact centre of the screen
    float aimStartT = 0; // skip this far along the aim ray (stuff behind the player)
};

struct InteractPrompt {
    enum Kind { None, OpenChest, PickUp, Revive } kind = None;
    int index = -1;
    std::string text;
    float progress = 0;
};

// The storm: a circle that shrinks in stages. Outside it you take damage every second.
struct Storm {
    Vec3 center;                 // current circle (y unused)
    float radius = 400.0f;
    Vec3 nextCenter;             // where it's shrinking to
    float nextRadius = 400.0f;
    int stage = 0;               // 0 = waiting before the first shrink
    bool shrinking = false;
    float timer = 0;             // seconds left in this wait / shrink
    float stageLength = 1;
    float damagePerSecond = 1;
    float tickTimer = 0;         // damage ticks once a second
    bool inside(float x, float z) const {
        float dx = x - center.x, dz = z - center.z;
        return dx * dx + dz * dz <= radius * radius;
    }
};

// The battle bus flies across the map at the start. Everyone jumps off, falls, and glides in.
struct Bus {
    bool flying = false;
    Vec3 start, end;      // path across the map, at BUS_HEIGHT
    float t = 0;          // 0..1 along the path
    float duration = 32;  // seconds for the trip
    Vec3 pos() const { return lerp(start, end, t); }
    Vec3 dir() const { return normalize(end - start); }
};
constexpr float BUS_HEIGHT = 130.0f;

class Game {
public:
    explicit Game(uint64_t seed = 1337);
    void startMatch(const MatchConfig& cfg);
    void reset(uint64_t seed);  // Solo match with default settings (used by tests)
    void confirmEdit();         // applies the edit selection (public for tests)
    void updateBot(Bot& b, float dt);  // one bot's thinking + movement (public for tests)
    void updateKnockedBot(Bot& b, float dt);
    bool botRevive(Bot& b, float dt);
    void update(const InputState& in, float dt);

    // State (read by renderer/HUD)
    MatchConfig config;
    World world;
    Player player;
    std::vector<Bot> bots;
    std::vector<DamageNumber> damageNumbers;
    std::vector<DamageStack> damageStacks;
    std::vector<Tracer> tracers;
    std::vector<Grenade> grenades;
    std::vector<ShockRing> shockRings;
    void throwShockwave();
    // Fall damage: call after a body moved. Returns the damage dealt (0 for short drops).
    float checkLanding(Body& b, int actorId, float dt);
    void explodeShockwave(const Vec3& at);
    std::vector<Message> killFeed;
    // Sounds made since the platform layer last played them (it empties the list every frame).
    std::vector<SoundEvent> sounds;
    void sound(Sfx id, const Vec3& pos, float volume = 1.0f);   // a sound out in the world
    void soundUi(Sfx id, float volume = 1.0f);                  // a sound "in your head" (hit markers, menus)
    // Footstep sound each time the walk cycle puts a foot down.
    void footstep(float walkPhase, int& lastStep, const Body& body, float volume);
    Message toast;
    Camera camera;
    InteractPrompt prompt;
    BuildTarget buildTarget;
    float hitMarker = 0;       // seconds left to show the hit marker
    bool hitMarkerHead = false;
    float time = 0;
    bool showHelp = false;   // F1 shows the controls box
    float mouseSensitivity = 0.0022f;
    float aspect = 16.0f / 9.0f;
    int spectating = -1;       // bot index the camera follows after you're eliminated
    int currentPoi = -1;       // POI the player is standing in, or -1
    int aimedPiece = -1;       // build under the crosshair (for its health bar), or -1
    Storm storm;
    bool playerInStorm = false;
    Bus bus;
    DropPhase dropPhase = DropPhase::Landed;
    float busJumpCooldown = 0;
    int lastAliveBuilds = 0;
    int pathPlansLeft = NAV_PLANS_PER_FRAME;   // bot path searches still allowed this frame
    bool buildsDirty = false;   // an edit changed a shape: re-check support
    int lastAliveProps = -1;
    std::string poiBanner;     // name shown at the top when you enter a place
    float poiBannerTime = 0;
    bool poiBannerNew = false; // "DISCOVERED" vs just the name
    int placement = 0;         // final place once your team is out or wins (0 = still playing)

    // Match status.
    int teamsAlive() const;
    int playersAlive() const;
    int enemiesAlive() const;           // enemy players still standing
    bool playerTeamAlive() const;
    bool victory() const { return playerTeamAlive() && teamsAlive() == 1; }
    bool defeat() const { return !playerTeamAlive(); }
    bool matchOver() const { return victory() || defeat(); }

    const Item& heldItem() const;
    float currentSpread() const;
    float aimDistanceWorld() const;
    std::string actorName(int actorId) const;
    int actorTeam(int actorId) const;
    bool actorAlive(int actorId) const;
    Vec3 actorPos(int actorId) const;

    // Raycast against world + actors. ignoreBot = -1 for none; ignorePlayer to skip the player.
    bool trace(const Ray& r, float maxT, HitInfo& hit, bool ignorePlayer, int ignoreBot,
               bool includeChests) const;

    // Helpers exposed for tests.
    bool giveItem(const Item& it, bool dropIfFull);
    void openChest(int index);
    // Damages the player or a bot. Friendly fire is ignored. Returns damage dealt.
    float damageActor(int victimId, float amount, bool head, const Vec3& at, int attackerId, const char* weapon);
    // Knocked phase helpers.
    bool knockModes() const { return config.teamSize > 1; }
    bool actorDown(int id) const;
    bool teamStanding(int team, int except) const;   // anyone on the team alive and not knocked?
    void knock(int victim, int attacker, const char* weapon);
    void eliminate(int victim, int attacker, const char* weapon);
    void revive(int id);
    void addRevive(int id, float dt);               // one frame of reviving `id`
    void updateKnocked(float dt);
    void damagePlayer(float amount) { damageActor(PLAYER_ACTOR_ID, amount, false, Vec3(), NO_TARGET, ""); }
    bool tryPlaceBuild();
    void updateCamera();
    void swingPickaxe();
    // Puts everyone on the ground right away (used when the bus is skipped).
    void landEveryone();
    // Inventory screen actions.
    void swapSlots(int a, int b);
    bool dropSlot(int i);
    bool dropAmmo(AmmoType t, int amount);
    // Resets the edit on the build you're editing or looking at. Returns true if one was reset.
    bool resetEdit();
    Rng rng;

private:
    void updatePlayer(const InputState& in, float dt);
    void updateMovement(const InputState& in, float dt);
    void updateStamina(bool sprinting, float dt);
    void updateCombat(const InputState& in, float dt);
    void updateBuild(const InputState& in, float dt);
    void updateEdit(const InputState& in, float dt);
    void updateInteract(const InputState& in, float dt);
    void updateBots(float dt);
    void perceive(Bot& b);
    void botShoot(Bot& b);
    bool findCover(Bot& b);
    // Bot building: a wall on the bot's cell edge facing `facing`, a ramp in the cell ahead, or a
    // cone over the bot's cell. Returns false if it has no mats or the spot is taken/unsupported.
    bool botBuild(Bot& b, PieceType type, int facing, int levelOffset = 0);
    void botPanicWall(Bot& b, const Vec3& threat);
    void botBox(Bot& b);
    void enterFlee(Bot& b, const Vec3& threat);
    void botLoot(Bot& b);           // picks up nearby items and opens nearby chests
    bool botWantsGun(const Bot& b, const Item& gun) const;
    void botTakeItem(Bot& b, const Item& it);
    bool botHeal(Bot& b, float dt);  // true while busy healing
    void pickNewWander(Bot& b);
    void steerBot(Bot& b, const Vec3& target, float speed, float dt);
    void firePlayerGun();
    void reloadSelected();
    void startEdit();
    void cancelEdit();
    void pickup(int pickupIndex);
    void tickGuns(float dt);
    void updateSpectate();
    void updatePoi();
    void settleBuilds();   // breaks any build that lost its support
    void updateStorm(float dt);
    void updateBus(const InputState& in, float dt);
    void updateDrop(Body& body, DropPhase& phase, const Vec3& steer, float dt);
    void startStormStage(int stage);

    void setCameraFrom(const Vec3& pivot, float yaw, float pitch, bool ads, float fovDeg);
    void showToast(const std::string& s, const Vec4& c = Vec4(1, 1, 1, 1));
    void addKillMessage(const std::string& s, const Vec4& c);
    void collectActorBoxes(std::vector<AABB>& out, bool includePlayer, int ignoreBot) const;
    AABB botBodyBox(const Bot& b) const;
    AABB botHeadBox(const Bot& b) const;
    AABB playerBodyBox() const;
    AABB playerHeadBox() const;
    bool lineOfSight(const Vec3& from, const Vec3& to) const;
};

}  // namespace bf
