// Blockfall - the map picture (used by the mini map and the full-screen map)
#include "map.h"
#include <cmath>

namespace bf {

namespace {
uint32_t pack(float r, float g, float b) {
    auto c = [](float v) { return (uint32_t)(clampf(v, 0, 1) * 255.0f + 0.5f); };
    return 0xFF000000u | (c(b) << 16) | (c(g) << 8) | c(r);
}
}  // namespace

void buildMapImage(const World& w, int size, std::vector<uint32_t>& out) {
    out.assign((size_t)size * size, 0xFF000000u);
    auto toPixel = [&](float x, float z, int& px, int& py) {
        px = (int)((x + MAP_HALF) / (2 * MAP_HALF) * size);
        py = (int)((1.0f - (z + MAP_HALF) / (2 * MAP_HALF)) * size);
    };
    auto put = [&](int px, int py, uint32_t c) {
        if (px >= 0 && py >= 0 && px < size && py < size) out[(size_t)py * size + px] = c;
    };

    // Terrain: grass shaded by height and slope, with a faint contour every metre.
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px) {
            float x = ((px + 0.5f) / size) * 2 * MAP_HALF - MAP_HALF;
            float z = (1.0f - (py + 0.5f) / size) * 2 * MAP_HALF - MAP_HALF;
            float h = w.terrainHeight(x, z);
            float hx = w.terrainHeight(x + 1, z) - w.terrainHeight(x - 1, z);
            float shade = clampf(0.5f - hx * 0.25f, 0.3f, 0.7f) + 0.5f;  // light from the west
            float t = clampf((h + 2.5f) / 5.0f, 0, 1);
            float r = lerpf(0.26f, 0.42f, t) * shade, g = lerpf(0.52f, 0.66f, t) * shade, b = lerpf(0.22f, 0.30f, t) * shade;
            float frac = h - std::floor(h);
            if (frac < 0.06f) { r *= 0.88f; g *= 0.88f; b *= 0.88f; }
            out[(size_t)py * size + px] = pack(r, g, b);
        }

    // Props.
    for (const StaticBox& s : w.statics) {
        if (!s.alive) continue;
        Vec3 c = s.box.center(), sz = s.box.size();
        uint32_t col;
        float rx = sz.x * 0.5f, rz = sz.z * 0.5f;
        switch (s.kind) {
            case PropKind::Leaves: col = pack(0.12f, 0.36f, 0.14f); rx *= 1.1f; rz *= 1.1f; break;
            case PropKind::Trunk: continue;
            case PropKind::Rock: col = pack(0.52f, 0.52f, 0.54f); break;
            case PropKind::Barrel: col = pack(0.4f, 0.42f, 0.46f); break;
            case PropKind::HouseRoof: col = pack(0.62f, 0.26f, 0.20f); break;
            case PropKind::HouseFloor: col = pack(0.74f, 0.66f, 0.52f); break;
            case PropKind::Road: col = pack(0.36f, 0.36f, 0.38f); break;
            case PropKind::Canopy: col = pack(0.7f, 0.3f, 0.25f); break;
            case PropKind::Silo: col = pack(0.55f, 0.57f, 0.6f); break;
            case PropKind::Stone: col = pack(0.5f, 0.5f, 0.46f); break;
            case PropKind::Crate: case PropKind::Log: case PropKind::Hay: col = pack(0.5f, 0.36f, 0.2f); break;
            default: continue;  // walls are under the roof anyway
        }
        int x0, y0, x1, y1;
        toPixel(c.x - rx, c.z + rz, x0, y0);
        toPixel(c.x + rx, c.z - rz, x1, y1);
        if (x1 < x0 + 1) x1 = x0 + 1;  // at least 2 px so small things still show
        if (y1 < y0 + 1) y1 = y0 + 1;
        bool round = s.kind == PropKind::Leaves || s.kind == PropKind::Rock || s.kind == PropKind::Barrel;
        for (int py = y0; py <= y1; ++py)
            for (int px = x0; px <= x1; ++px) {
                if (round) {
                    float fx = (px - (x0 + x1) * 0.5f) / std::max(1.0f, (x1 - x0) * 0.5f);
                    float fy = (py - (y0 + y1) * 0.5f) / std::max(1.0f, (y1 - y0) * 0.5f);
                    if (fx * fx + fy * fy > 1.0f) continue;
                }
                put(px, py, col);
            }
    }
    // Roof edges a touch darker so houses read as buildings.
    for (const StaticBox& s : w.statics) {
        if (!s.alive || s.kind != PropKind::HouseRoof) continue;
        Vec3 c = s.box.center(), sz = s.box.size();
        int x0, y0, x1, y1;
        toPixel(c.x - sz.x * 0.5f, c.z + sz.z * 0.5f, x0, y0);
        toPixel(c.x + sz.x * 0.5f, c.z - sz.z * 0.5f, x1, y1);
        if (x1 - x0 < 3 || y1 - y0 < 3) continue;  // too small for an outline
        for (int px = x0; px <= x1; ++px) { put(px, y0, pack(0.4f, 0.16f, 0.12f)); put(px, y1, pack(0.4f, 0.16f, 0.12f)); }
        for (int py = y0; py <= y1; ++py) { put(x0, py, pack(0.4f, 0.16f, 0.12f)); put(x1, py, pack(0.4f, 0.16f, 0.12f)); }
    }
    // Map border.
    for (int i = 0; i < size; ++i) {
        put(i, 0, pack(0.9f, 0.85f, 0.6f)); put(i, size - 1, pack(0.9f, 0.85f, 0.6f));
        put(0, i, pack(0.9f, 0.85f, 0.6f)); put(size - 1, i, pack(0.9f, 0.85f, 0.6f));
    }
}

std::string mapGridName(float x, float z) {
    int col = (int)clampf((x + MAP_HALF) / (2 * MAP_HALF) * 6.0f, 0, 5.99f);
    int row = (int)clampf((1.0f - (z + MAP_HALF) / (2 * MAP_HALF)) * 6.0f, 0, 5.99f);
    return std::string(1, (char)('A' + col)) + std::to_string(row + 1);
}

}  // namespace bf
