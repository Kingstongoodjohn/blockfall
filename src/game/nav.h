// Blockfall - bot pathfinding: A* on a small walkability grid built around the bot when it needs it
#pragma once
#include "../core/math.h"
#include <vector>

namespace bf {

class World;

constexpr float NAV_CELL = 0.5f;          // grid cell size (metres); doors are 1.8 m, so ~2 free cells
constexpr float NAV_BODY_RADIUS = 0.4f;   // solids are grown by this so the path keeps the body clear
constexpr float NAV_STEP = 0.75f;         // anything lower than this above the ground can be stepped on
constexpr float NAV_BODY_HEIGHT = 1.75f;  // anything starting higher than this can be walked under
constexpr float NAV_MARGIN = 10.0f;       // grid extends this far past the start and goal
constexpr float NAV_MAX_REACH = 40.0f;    // goals further away are planned toward in 40 m hops
constexpr int NAV_MAX_EXPANSIONS = 9000;  // A* gives up after this many cells (keeps the cost bounded)
constexpr int NAV_PLANS_PER_FRAME = 6;    // how many bots may plan a path in one frame

// Finds a walkable route on the ground from `from` to `to`, going around walls, props and builds.
// Only plans along the terrain (not up ramps or across upper floors). Writes waypoints (not
// including `from`; the last one is the goal or the closest reachable spot) and returns true if a
// route was found that gets closer to the goal than standing still.
bool findPath(const World& world, const Vec3& from, const Vec3& to, std::vector<Vec3>& out);

}  // namespace bf
