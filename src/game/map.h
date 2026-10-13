// Blockfall - the map picture (used by the mini map and the full-screen map)
#pragma once
#include <cstdint>
#include <vector>
#include "world.h"

namespace bf {

constexpr int MAP_IMAGE_SIZE = 512;

// Draws the whole 300 m x 300 m map top-down into an RGBA8 image: terrain shaded by height,
// houses, trees and rocks. North (+z) is up.
void buildMapImage(const World& w, int size, std::vector<uint32_t>& out);

// World (x,z) -> map image fraction (0..1, v grows downward = south).
inline void worldToMapUV(float x, float z, float& u, float& v) {
    u = (x + MAP_HALF) / (2 * MAP_HALF);
    v = 1.0f - (z + MAP_HALF) / (2 * MAP_HALF);
}

// Grid square name like "C4" (columns A-F west to east, rows 1-6 north to south).
std::string mapGridName(float x, float z);

}  // namespace bf
