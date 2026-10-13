// Blockfall - the map: terrain, props, chests, dropped items, structures, collision, raycasts
#include "world.h"
#include <cmath>

namespace bf {

namespace {

float rawTerrain(float x, float z) {
    return 3.2f * std::sin(x * 0.0085f + 0.4f) * std::cos(z * 0.0071f + 1.1f) +   // big hills
           1.4f * std::sin(x * 0.031f + 0.7f) * std::cos(z * 0.027f + 1.3f) +
           0.8f * std::sin(x * 0.013f - z * 0.021f) +
           0.35f * std::sin(x * 0.07f) * std::sin(z * 0.063f);
}

float smoothstep(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec4 rgb(float r, float g, float b) { return {r, g, b, 1.0f}; }

}  // namespace

// ------------------------------------------------------------------ generation
bool World::spotFree(float x, float z, float r) const {
    if (std::fabs(x) > MAP_HALF - 6 || std::fabs(z) > MAP_HALF - 6) return false;
    for (const Vec3& o : occupied_) {
        float dx = x - o.x, dz = z - o.z, rr = r + o.y;
        if (dx * dx + dz * dz < rr * rr) return false;
    }
    return true;
}

const char* poiKindName(PoiKind k) {
    static const char* names[] = {"TOWN", "FARM", "TOWER", "LUMBER YARD", "QUARRY", "GAS STATION", "CAMP", "RUINS", "MARKET", "DEPOT", "VILLAGE", "LOOKOUT"};
    return names[(int)k];
}

void World::addTree(float x, float z, Rng& rng, float scale) {
    float g = terrainHeight(x, z);
    float th = rng.range(4.0f, 5.5f) * scale;
    int grp = newGroup(200.0f, 8, 0);
    float tr = 0.35f * scale;
    statics.push_back({AABB({x - tr, g - 0.3f, z - tr}, {x + tr, g + th, z + tr}),
                       rgb(0.42f, 0.28f, 0.16f), PropKind::Trunk, true, true, grp});
    float cw = rng.range(1.6f, 2.2f) * scale;
    Vec4 leaf = rgb(0.16f + rng.range(0, 0.08f), 0.46f + rng.range(0, 0.12f), 0.18f);
    statics.push_back({AABB({x - cw, g + th - 1.2f * scale, z - cw}, {x + cw, g + th + 1.3f * scale, z + cw}),
                       leaf, PropKind::Leaves, false, true, grp});
    statics.push_back({AABB({x - cw * 0.6f, g + th + 1.3f * scale, z - cw * 0.6f}, {x + cw * 0.6f, g + th + 2.4f * scale, z + cw * 0.6f}),
                       leaf, PropKind::Leaves, false, true, grp});
    occupied_.push_back(Vec3(x, 1.2f * scale, z));
}

void World::addRock(float x, float z, Rng& rng, float scale) {
    float g = terrainHeight(x, z);
    float sx = rng.range(0.8f, 1.8f) * scale, sy = rng.range(0.8f, 2.0f) * scale, sz = rng.range(0.8f, 1.8f) * scale;
    float shade = rng.range(0.45f, 0.6f);
    statics.push_back({AABB({x - sx, g - 0.5f, z - sz}, {x + sx, g + sy, z + sz}),
                       rgb(shade, shade, shade * 1.03f), PropKind::Rock, true, true, newGroup(250.0f * scale, 10, 1)});
    occupied_.push_back(Vec3(x, std::max(sx, sz) + 0.6f, z));
}

void World::addChest(float x, float z, Rng& rng) {
    Chest c;
    c.pos = Vec3(x, terrainHeight(x, z), z);
    c.yaw = rng.rangeInt(0, 3) * PI * 0.5f;
    chests.push_back(c);
    occupied_.push_back(Vec3(x, 1.2f, z));
}

void World::addBarrel(float x, float z) {
    float g = terrainHeight(x, z);
    statics.push_back({AABB({x - 0.45f, g - 0.2f, z - 0.45f}, {x + 0.45f, g + 1.1f, z + 0.45f}),
                       rgb(0.45f, 0.47f, 0.5f), PropKind::Barrel, true, true, newGroup(150.0f, 10, 2)});
    occupied_.push_back(Vec3(x, 1.0f, z));
}

void World::generate(uint64_t seed) {
    Rng rng(seed);
    ++generation;
    build.clear();
    statics.clear();
    groups.clear();
    chests.clear();
    pickups.clear();
    houseCenters.clear();
    occupied_.clear();
    enemySpawns.clear();

    // Named places (POIs): a ring of them around the map plus one in the middle. Each place is
    // a different kind (town, farm, tower, quarry...) so they all look different. They are
    // placed first so the terrain can be flattened under them.
    pois.clear();
    static const struct { const char* name; PoiKind kind; } places[] = {
        {"COBBLE COVE", PoiKind::Town},        {"BARNYARD BLUFF", PoiKind::Farm},    {"HILLTOP HOLLOW", PoiKind::Tower},
        {"SAWMILL SPRINGS", PoiKind::Lumber},  {"BOULDER BEND", PoiKind::Quarry},    {"RUSTY REACH", PoiKind::GasStation},
        {"PINE POCKET", PoiKind::Camp},        {"MOSSY RUINS", PoiKind::Ruins},      {"LANTERN LANE", PoiKind::Market},
        {"SCRAP DEPOT", PoiKind::Depot},       {"WILLOW WALK", PoiKind::Village},    {"LOOKOUT RIDGE", PoiKind::Lookout},
    };
    const int poiCount = (int)(sizeof(places) / sizeof(places[0]));
    float ringStart = rng.range(0, 2 * PI);
    // Two rings: 4 inner places and 7 outer ones, plus the town in the middle.
    for (int i = 0; i < poiCount; ++i) {
        Poi poi;
        poi.name = places[i].name;
        poi.kind = places[i].kind;
        if (i == 0) {
            poi.center = Vec3(rng.range(-20, 20), 0, rng.range(-20, 20));
        } else if (i <= 4) {
            float a = ringStart + 0.4f + (float)(i - 1) * 2 * PI / 4 + rng.range(-0.2f, 0.2f);
            float r = rng.range(MAP_HALF * 0.36f, MAP_HALF * 0.46f);
            poi.center = Vec3(std::sin(a) * r, 0, std::cos(a) * r);
        } else {
            float a = ringStart + (float)(i - 5) * 2 * PI / 7 + rng.range(-0.15f, 0.15f);
            float r = rng.range(MAP_HALF * 0.66f, MAP_HALF * 0.8f);
            poi.center = Vec3(std::sin(a) * r, 0, std::cos(a) * r);
        }
        poi.center.x = clampf(poi.center.x, -MAP_HALF + 55, MAP_HALF - 55);
        poi.center.z = clampf(poi.center.z, -MAP_HALF + 55, MAP_HALF - 55);
        poi.center.y = rawTerrain(poi.center.x, poi.center.z);
        if (poi.kind == PoiKind::Lookout) poi.center.y += 7.0f;   // sits on a hill
        if (poi.kind == PoiKind::Quarry) poi.center.y -= 3.0f;    // dug into the ground
        poi.radius = poi.kind == PoiKind::Town ? 46.0f : 36.0f;
        pois.push_back(poi);
    }
    playerSpawn = Vec3(pois[0].center.x + 2.5f, 0, pois[0].center.z - 52.0f);

    // Heightmap: rolling hills, flattened under every place.
    hmSize_ = (int)(2 * MAP_HALF / HEIGHTMAP_STEP) + 1;
    heights_.assign((size_t)hmSize_ * hmSize_, 0.0f);
    for (int iz = 0; iz < hmSize_; ++iz)
        for (int ix = 0; ix < hmSize_; ++ix) {
            float x = -MAP_HALF + ix * HEIGHTMAP_STEP, z = -MAP_HALF + iz * HEIGHTMAP_STEP;
            float h = rawTerrain(x, z);
            for (const Poi& poi : pois) {
                float d = std::sqrt((x - poi.center.x) * (x - poi.center.x) + (z - poi.center.z) * (z - poi.center.z));
                float flat = poi.radius - 6.0f;
                float w = 1.0f - smoothstep(flat, flat + 18.0f, d);
                h = lerpf(h, poi.center.y, w);
            }
            heights_[(size_t)iz * hmSize_ + ix] = h;
        }
    playerSpawn.y = terrainHeight(playerSpawn.x, playerSpawn.z);
    occupied_.push_back(Vec3(playerSpawn.x, 6.0f, playerSpawn.z));

    // Build every place.
    for (Poi& poi : pois) buildPoi(poi, rng);

    // Trees and rocks everywhere else (denser near the lumber yard and camp).
    for (int n = 0, tries = 0; n < 900 && tries < 40000; ++tries) {
        float x = rng.range(-MAP_HALF, MAP_HALF), z = rng.range(-MAP_HALF, MAP_HALF);
        if (!spotFree(x, z, 2.5f)) continue;
        int pi = poiAt(x, z);
        if (pi >= 0 && pois[pi].kind != PoiKind::Camp && pois[pi].kind != PoiKind::Lumber) continue;
        addTree(x, z, rng);
        ++n;
    }
    for (int n = 0, tries = 0; n < 380 && tries < 40000; ++tries) {
        float x = rng.range(-MAP_HALF, MAP_HALF), z = rng.range(-MAP_HALF, MAP_HALF);
        if (!spotFree(x, z, 3.0f)) continue;
        int pi = poiAt(x, z);
        if (pi >= 0 && pois[pi].kind != PoiKind::Quarry) continue;
        addRock(x, z, rng);
        ++n;
    }
    // Scrap barrels in the open.
    for (int n = 0, tries = 0; n < 120 && tries < 20000; ++tries) {
        float x = rng.range(-MAP_HALF + 15, MAP_HALF - 15), z = rng.range(-MAP_HALF + 15, MAP_HALF - 15);
        if (!spotFree(x, z, 1.5f)) continue;
        addBarrel(x, z);
        ++n;
    }
    // Chests in the open.
    for (int n = 0, tries = 0; n < 90 && tries < 40000; ++tries) {
        float x = rng.range(-MAP_HALF + 15, MAP_HALF - 15), z = rng.range(-MAP_HALF + 15, MAP_HALF - 15);
        if (!spotFree(x, z, 2.0f)) continue;
        addChest(x, z, rng);
        ++n;
    }

    // Enemy spawn points spread around the map, away from the player.
    for (int tries = 0; tries < 200000 && enemySpawns.size() < 100; ++tries) {
        float x = rng.range(-MAP_HALF + 15, MAP_HALF - 15), z = rng.range(-MAP_HALF + 15, MAP_HALF - 15);
        float d = std::sqrt((x - playerSpawn.x) * (x - playerSpawn.x) + (z - playerSpawn.z) * (z - playerSpawn.z));
        if (d < 40.0f || !spotFree(x, z, 2.0f)) continue;
        bool farFromOthers = true;
        for (const Vec3& e : enemySpawns)
            if ((e.x - x) * (e.x - x) + (e.z - z) * (e.z - z) < 28.0f * 28.0f) farFromOthers = false;
        if (!farFromOthers) continue;
        enemySpawns.push_back(Vec3(x, terrainHeight(x, z), z));
    }
}

// ------------------------------------------------------------------ places
void World::buildPoi(Poi& poi, Rng& rng) {
    const Vec3 c = poi.center;
    auto around = [&](float minD, float maxD, float& x, float& z) {
        float a = rng.range(0, 2 * PI), d = rng.range(minD, maxD);
        x = c.x + std::sin(a) * d;
        z = c.z + std::cos(a) * d;
    };
    auto house = [&](float x, float z) {
        houseCenters.push_back(Vec3(x, c.y + rng.range(-0.1f, 0.1f), z));
        occupied_.push_back(Vec3(x, 8.5f, z));
        buildHouse(houseCenters.back(), rng.rangeInt(0, 3), rng);
    };
    auto box = [&](const AABB& b, const Vec4& col, PropKind kind, bool solid, int group) {
        statics.push_back({b, col, kind, solid, solid, group});
    };
    const Vec4 wood = rgb(0.55f, 0.38f, 0.2f), stone = rgb(0.5f, 0.5f, 0.48f), metal = rgb(0.5f, 0.52f, 0.55f);
    int extraChests = 2;
    switch (poi.kind) {
        case PoiKind::Town: {
            // A grid of houses on two crossing roads.
            box(AABB({c.x - 42, c.y - 0.05f, c.z - 3}, {c.x + 42, c.y + 0.04f, c.z + 3}), rgb(0.3f, 0.3f, 0.32f), PropKind::Road, false, -1);
            box(AABB({c.x - 3, c.y - 0.05f, c.z - 42}, {c.x + 3, c.y + 0.04f, c.z + 42}), rgb(0.3f, 0.3f, 0.32f), PropKind::Road, false, -1);
            for (int gx = -1; gx <= 1; ++gx)
                for (int gz = -1; gz <= 1; ++gz) {
                    if (gx == 0 || gz == 0) continue;
                    for (int k = 0; k < 2; ++k) house(c.x + gx * 13.0f + (k ? gx * 13.0f : 0), c.z + gz * 13.0f + rng.range(-1.5f, 1.5f));
                }
            buildBuilding(Vec3(c.x + 26, c.y, c.z - 26), 9, 6, 4.0f, 2, rgb(0.9f, 0.85f, 0.7f), rgb(0.3f, 0.35f, 0.5f), false, rng);  // town hall
            extraChests = 5;
            break;
        }
        case PoiKind::Farm: {
            buildBuilding(Vec3(c.x, c.y, c.z), 8, 6, 6.0f, 0, rgb(0.72f, 0.18f, 0.15f), rgb(0.35f, 0.3f, 0.28f), true, rng);  // the barn
            for (int i = 0; i < 2; ++i) {
                float x = c.x + 14 + i * 7, z = c.z - 8;
                box(AABB({x - 2.2f, c.y - 0.2f, z - 2.2f}, {x + 2.2f, c.y + 9.0f, z + 2.2f}), metal, PropKind::Silo, true, newGroup(300, 10, 2));
                occupied_.push_back(Vec3(x, 3.0f, z));
            }
            house(c.x - 18, c.z + 12);
            // Fenced field with hay bales.
            float fx0 = c.x - 24, fx1 = c.x + 6, fz0 = c.z + 10, fz1 = c.z + 30;
            for (float x = fx0; x < fx1; x += 3)
                for (float z : {fz0, fz1}) box(AABB({x, c.y + 0.4f, z - 0.08f}, {x + 2.6f, c.y + 1.1f, z + 0.08f}), wood, PropKind::Fence, true, newGroup(60, 4, 0));
            for (float z = fz0; z < fz1; z += 3)
                for (float x : {fx0, fx1}) box(AABB({x - 0.08f, c.y + 0.4f, z}, {x + 0.08f, c.y + 1.1f, z + 2.6f}), wood, PropKind::Fence, true, newGroup(60, 4, 0));
            for (int i = 0; i < 6; ++i) {
                float x = rng.range(fx0 + 3, fx1 - 3), z = rng.range(fz0 + 3, fz1 - 3);
                if (!spotFree(x, z, 1.5f)) continue;
                box(AABB({x - 1.1f, c.y - 0.1f, z - 0.8f}, {x + 1.1f, c.y + 1.5f, z + 0.8f}), rgb(0.85f, 0.72f, 0.3f), PropKind::Hay, true, newGroup(40, 3, 0));
                occupied_.push_back(Vec3(x, 1.4f, z));
            }
            break;
        }
        case PoiKind::Tower: {
            buildTower(c, 3, rng);
            house(c.x + 20, c.z + 6);
            house(c.x - 18, c.z - 10);
            break;
        }
        case PoiKind::Lumber: {
            buildBuilding(Vec3(c.x, c.y, c.z), 9, 6, 4.5f, 0, rgb(0.55f, 0.42f, 0.28f), rgb(0.3f, 0.28f, 0.26f), true, rng);  // the mill shed
            // Log piles.
            for (int pile = 0; pile < 4; ++pile) {
                float x, z;
                around(14, 26, x, z);
                if (!spotFree(x, z, 4.0f)) continue;
                int grp = newGroup(300, 10, 0);
                for (int layer = 0; layer < 3; ++layer)
                    for (int k = 0; k < 3 - layer; ++k) {
                        float lz = z + (k - (2 - layer) * 0.5f) * 1.05f;
                        box(AABB({x - 3.5f, c.y + layer * 0.9f, lz - 0.5f}, {x + 3.5f, c.y + layer * 0.9f + 1.0f, lz + 0.5f}), rgb(0.5f, 0.34f, 0.18f), PropKind::Log, true, grp);
                    }
                occupied_.push_back(Vec3(x, 4.0f, z));
            }
            for (int i = 0; i < 25; ++i) {
                float x, z;
                around(16, 34, x, z);
                if (spotFree(x, z, 2.5f)) addTree(x, z, rng, 1.2f);
            }
            break;
        }
        case PoiKind::Quarry: {
            for (int i = 0; i < 26; ++i) {
                float x, z;
                around(4, 32, x, z);
                if (spotFree(x, z, 3.0f)) addRock(x, z, rng, rng.range(0.9f, 2.2f));
            }
            buildBuilding(Vec3(c.x + 24, c.y, c.z + 20), 5, 4, 3.6f, 2, rgb(0.6f, 0.6f, 0.62f), rgb(0.35f, 0.35f, 0.38f), false, rng);  // site office
            for (int i = 0; i < 8; ++i) {
                float x, z;
                around(8, 30, x, z);
                if (spotFree(x, z, 1.0f)) addBarrel(x, z);
            }
            extraChests = 3;
            break;
        }
        case PoiKind::GasStation: {
            buildBuilding(Vec3(c.x - 10, c.y, c.z), 8, 5, 3.6f, 1, rgb(0.92f, 0.92f, 0.9f), rgb(0.85f, 0.25f, 0.2f), false, rng);  // the shop
            // Canopy over the pumps on four posts.
            float px0 = c.x + 4, pz0 = c.z - 8, px1 = c.x + 18, pz1 = c.z + 8;
            for (float x : {px0, px1})
                for (float z : {pz0, pz1}) box(AABB({x - 0.25f, c.y, z - 0.25f}, {x + 0.25f, c.y + 5.0f, z + 0.25f}), metal, PropKind::Post, true, -1);
            box(AABB({px0 - 1.5f, c.y + 5.0f, pz0 - 1.5f}, {px1 + 1.5f, c.y + 5.6f, pz1 + 1.5f}), rgb(0.85f, 0.25f, 0.2f), PropKind::Canopy, true, -1);
            for (int i = 0; i < 3; ++i) {
                float x = c.x + 7 + i * 4.5f;
                box(AABB({x - 0.5f, c.y, c.z - 0.4f}, {x + 0.5f, c.y + 1.6f, c.z + 0.4f}), rgb(0.9f, 0.3f, 0.2f), PropKind::Crate, true, newGroup(120, 8, 2));
                occupied_.push_back(Vec3(x, 1.0f, c.z));
            }
            for (int i = 0; i < 10; ++i) {
                float x, z;
                around(14, 30, x, z);
                if (spotFree(x, z, 1.0f)) addBarrel(x, z);
            }
            break;
        }
        case PoiKind::Camp: {
            for (int i = 0; i < 4; ++i) {
                float x, z;
                around(8, 22, x, z);
                if (!spotFree(x, z, 4.0f)) continue;
                buildBuilding(Vec3(x, c.y, z), 3, 3, 2.6f, rng.rangeInt(0, 3), rgb(0.5f, 0.35f, 0.22f), rgb(0.3f, 0.5f, 0.3f), false, rng);  // huts
                occupied_.push_back(Vec3(x, 5.0f, z));
            }
            // Campfire: a ring of small rocks.
            for (int i = 0; i < 6; ++i) {
                float a = i * PI / 3;
                float x = c.x + std::sin(a) * 1.3f, z = c.z + std::cos(a) * 1.3f;
                box(AABB({x - 0.35f, c.y - 0.1f, z - 0.35f}, {x + 0.35f, c.y + 0.35f, z + 0.35f}), stone, PropKind::Rock, true, newGroup(60, 3, 1));
            }
            occupied_.push_back(Vec3(c.x, 2.0f, c.z));
            for (int i = 0; i < 30; ++i) {
                float x, z;
                around(10, 34, x, z);
                if (spotFree(x, z, 2.5f)) addTree(x, z, rng);
            }
            break;
        }
        case PoiKind::Ruins: {
            // Broken stone walls of an old keep.
            for (int i = 0; i < 14; ++i) {
                float x, z;
                around(3, 26, x, z);
                if (!spotFree(x, z, 3.0f)) continue;
                bool alongX = rng.rangeInt(0, 1) == 0;
                float len = rng.range(3.0f, 7.0f), h = rng.range(1.0f, 4.0f);
                AABB b = alongX ? AABB({x - len * 0.5f, c.y - 0.3f, z - 0.4f}, {x + len * 0.5f, c.y + h, z + 0.4f})
                                : AABB({x - 0.4f, c.y - 0.3f, z - len * 0.5f}, {x + 0.4f, c.y + h, z + len * 0.5f});
                box(b, rgb(0.55f, 0.56f, 0.5f), PropKind::Stone, true, newGroup(260, 8, 1));
                occupied_.push_back(Vec3(x, len * 0.5f + 0.5f, z));
            }
            for (int i = 0; i < 10; ++i) {
                float x, z;
                around(4, 30, x, z);
                if (spotFree(x, z, 2.0f)) addRock(x, z, rng, 0.7f);
            }
            extraChests = 4;
            break;
        }
        case PoiKind::Market: {
            // Stalls: a canopy on posts with crates under it, in two rows.
            for (int i = 0; i < 8; ++i) {
                float x = c.x - 18 + (i % 4) * 12, z = c.z + (i < 4 ? -9 : 9);
                for (int k = 0; k < 4; ++k)
                    box(AABB({x + (k % 2 ? 2.0f : -2.0f) - 0.15f, c.y, z + (k / 2 ? 2.0f : -2.0f) - 0.15f},
                             {x + (k % 2 ? 2.0f : -2.0f) + 0.15f, c.y + 2.8f, z + (k / 2 ? 2.0f : -2.0f) + 0.15f}), wood, PropKind::Post, true, -1);
                Vec4 cloth = (i % 3 == 0) ? rgb(0.85f, 0.3f, 0.3f) : (i % 3 == 1 ? rgb(0.3f, 0.55f, 0.85f) : rgb(0.9f, 0.75f, 0.3f));
                box(AABB({x - 2.8f, c.y + 2.8f, z - 2.8f}, {x + 2.8f, c.y + 3.1f, z + 2.8f}), cloth, PropKind::Canopy, true, -1);
                box(AABB({x - 1.2f, c.y, z - 0.6f}, {x + 1.2f, c.y + 1.0f, z + 0.6f}), rgb(0.6f, 0.45f, 0.25f), PropKind::Crate, true, newGroup(80, 6, 0));
                occupied_.push_back(Vec3(x, 3.5f, z));
            }
            house(c.x + 22, c.z + 22);
            house(c.x - 22, c.z - 22);
            extraChests = 4;
            break;
        }
        case PoiKind::Depot: {
            buildBuilding(Vec3(c.x, c.y, c.z), 12, 8, 6.0f, 1, rgb(0.5f, 0.55f, 0.6f), rgb(0.35f, 0.38f, 0.42f), true, rng);  // warehouse
            // Stacks of crates and lots of barrels.
            for (int i = 0; i < 10; ++i) {
                float x, z;
                around(16, 30, x, z);
                if (!spotFree(x, z, 2.0f)) continue;
                int grp = newGroup(100, 6, 0);
                int stack = rng.rangeInt(1, 3);
                for (int k = 0; k < stack; ++k)
                    box(AABB({x - 1.0f, c.y + k * 1.4f, z - 1.0f}, {x + 1.0f, c.y + k * 1.4f + 1.4f, z + 1.0f}), rgb(0.6f, 0.45f, 0.25f), PropKind::Crate, true, grp);
                occupied_.push_back(Vec3(x, 1.6f, z));
            }
            for (int i = 0; i < 16; ++i) {
                float x, z;
                around(14, 32, x, z);
                if (spotFree(x, z, 1.0f)) addBarrel(x, z);
            }
            extraChests = 4;
            break;
        }
        case PoiKind::Village: {
            // Houses in a ring around a well.
            box(AABB({c.x - 1.6f, c.y - 0.2f, c.z - 1.6f}, {c.x + 1.6f, c.y + 1.1f, c.z + 1.6f}), stone, PropKind::Well, true, newGroup(200, 6, 1));
            occupied_.push_back(Vec3(c.x, 2.5f, c.z));
            for (int i = 0; i < 6; ++i) {
                float a = i * 2 * PI / 6 + rng.range(-0.15f, 0.15f);
                house(c.x + std::sin(a) * 17.0f, c.z + std::cos(a) * 17.0f);
            }
            for (int i = 0; i < 12; ++i) {
                float x, z;
                around(24, 34, x, z);
                if (spotFree(x, z, 2.5f)) addTree(x, z, rng, 0.9f);
            }
            extraChests = 3;
            break;
        }
        case PoiKind::Lookout: {
            buildTower(c, 4, rng);
            house(c.x - 16, c.z + 8);
            for (int i = 0; i < 8; ++i) {
                float x, z;
                around(12, 30, x, z);
                if (spotFree(x, z, 2.5f)) addRock(x, z, rng, 0.8f);
            }
            break;
        }
        default: break;
    }
    // Extra chests and a few barrels in every place.
    for (int n = 0, tries = 0; n < extraChests && tries < 200; ++tries) {
        float x, z;
        around(6, poi.radius - 8, x, z);
        if (!spotFree(x, z, 1.5f)) continue;
        addChest(x, z, rng);
        ++n;
    }
    for (int n = 0, tries = 0; n < 3 && tries < 100; ++tries) {
        float x, z;
        around(6, poi.radius - 6, x, z);
        if (!spotFree(x, z, 1.0f)) continue;
        addBarrel(x, z);
        ++n;
    }
}

void World::buildBuilding(const Vec3& c, float halfX, float halfZ, float wallH, int doorSide, const Vec4& wallCol, const Vec4& roofCol,
                          bool openFront, Rng& rng) {
    const float t = 0.3f, base = c.y;
    (void)rng;
    statics.push_back({AABB({c.x - halfX, base - 0.6f, c.z - halfZ}, {c.x + halfX, base + 0.05f, c.z + halfZ}),
                       rgb(0.45f, 0.42f, 0.4f), PropKind::HouseFloor, true, true});
    const float doorW = openFront ? std::min(halfX, halfZ) * 1.2f : 1.8f, doorH = openFront ? wallH - 0.6f : 2.5f;
    for (int side = 0; side < 4; ++side) {
        bool alongX = (side == 0 || side == 2);
        float half = alongX ? halfX : halfZ, other = alongX ? halfZ : halfX;
        float fixed = (side == 0 || side == 1) ? other - t * 0.5f : -other + t * 0.5f;
        auto addSeg = [&](float a0, float a1, float y0, float y1) {
            AABB b;
            if (alongX) b = AABB({c.x + a0, base + y0, c.z + fixed - t * 0.5f}, {c.x + a1, base + y1, c.z + fixed + t * 0.5f});
            else b = AABB({c.x + fixed - t * 0.5f, base + y0, c.z + a0}, {c.x + fixed + t * 0.5f, base + y1, c.z + a1});
            statics.push_back({b, wallCol, PropKind::HouseWall, true, true, newGroup(220.0f, 6, 1)});
        };
        if (side == doorSide) {
            addSeg(-half, -doorW * 0.5f, 0, wallH);
            addSeg(doorW * 0.5f, half, 0, wallH);
            addSeg(-doorW * 0.5f, doorW * 0.5f, doorH, wallH);
        } else {
            // Windows every ~4 m.
            float x = -half;
            addSeg(-half, half, 0, 1.1f);
            addSeg(-half, half, 2.3f, wallH);
            bool win = true;
            while (x < half) {
                float x1 = std::min(half, x + 2.0f);
                if (!win) addSeg(x, x1, 1.1f, 2.3f);
                win = !win;
                x = x1;
            }
        }
    }
    statics.push_back({AABB({c.x - halfX - 0.4f, base + wallH, c.z - halfZ - 0.4f}, {c.x + halfX + 0.4f, base + wallH + 0.4f, c.z + halfZ + 0.4f}),
                       roofCol, PropKind::HouseRoof, true, true, newGroup(320.0f, 8, 2)});
    occupied_.push_back(Vec3(c.x, std::max(halfX, halfZ) + 3.0f, c.z));
    Chest ch;
    ch.pos = Vec3(c.x + (doorSide == 1 ? -halfX + 1.5f : halfX - 1.5f), base + 0.05f, c.z + (doorSide == 0 ? -halfZ + 1.5f : halfZ - 1.5f));
    ch.yaw = 0;
    chests.push_back(ch);
}

// A square tower with several floors joined by stairs along the walls, and a roof you can
// stand on (reached through the hole in the top floor).
void World::buildTower(const Vec3& c, int floors, Rng& rng) {
    (void)rng;
    const float half = 6.0f, t = 0.3f, fh = 3.6f, base = c.y;
    const Vec4 wall = rgb(0.6f, 0.58f, 0.52f), slab = rgb(0.45f, 0.42f, 0.4f);
    statics.push_back({AABB({c.x - half, base - 0.6f, c.z - half}, {c.x + half, base + 0.05f, c.z + half}), slab, PropKind::HouseFloor, true, true});
    for (int f = 0; f < floors; ++f) {
        float y0 = base + f * fh;
        for (int side = 0; side < 4; ++side) {
            bool alongX = (side == 0 || side == 2);
            float fixed = (side == 0 || side == 1) ? half - t * 0.5f : -half + t * 0.5f;
            auto addSeg = [&](float a0, float a1, float yy0, float yy1) {
                AABB b;
                if (alongX) b = AABB({c.x + a0, yy0, c.z + fixed - t * 0.5f}, {c.x + a1, yy1, c.z + fixed + t * 0.5f});
                else b = AABB({c.x + fixed - t * 0.5f, yy0, c.z + a0}, {c.x + fixed + t * 0.5f, yy1, c.z + a1});
                statics.push_back({b, wall, PropKind::HouseWall, true, true, newGroup(260.0f, 6, 1)});
            };
            if (f == 0 && side == 2) {  // door on the ground floor
                addSeg(-half, -1.0f, y0, y0 + fh);
                addSeg(1.0f, half, y0, y0 + fh);
                addSeg(-1.0f, 1.0f, y0 + 2.5f, y0 + fh);
            } else {  // a window
                addSeg(-half, -1.2f, y0, y0 + fh);
                addSeg(1.2f, half, y0, y0 + fh);
                addSeg(-1.2f, 1.2f, y0, y0 + 1.2f);
                addSeg(-1.2f, 1.2f, y0 + 2.4f, y0 + fh);
            }
        }
        // Stairs up the +x wall to a hole in the next slab.
        const int steps = 8;
        for (int s = 0; s < steps; ++s) {
            float sz0 = c.z - half + t + s * (2 * half - 2 * t) / steps;
            float sz1 = sz0 + (2 * half - 2 * t) / steps;
            float sy = y0 + (s + 1) * fh / steps;
            statics.push_back({AABB({c.x + half - t - 1.6f, sy - 0.25f, sz0}, {c.x + half - t, sy, sz1}), slab, PropKind::Step, true, true});
        }
        // Slab above with a hole at the top of the stairs (the +x,+z corner).
        float sy = y0 + fh;
        statics.push_back({AABB({c.x - half, sy - 0.25f, c.z - half}, {c.x + half - 1.8f, sy, c.z + half}), slab, PropKind::HouseFloor, true, true});
        statics.push_back({AABB({c.x + half - 1.8f, sy - 0.25f, c.z - half}, {c.x + half, sy, c.z + half - 2.6f}), slab, PropKind::HouseFloor, true, true});
    }
    // Roof parapet.
    float top = base + floors * fh;
    for (int side = 0; side < 4; ++side) {
        bool alongX = (side == 0 || side == 2);
        float fixed = (side == 0 || side == 1) ? half - t * 0.5f : -half + t * 0.5f;
        AABB b = alongX ? AABB({c.x - half, top, c.z + fixed - t * 0.5f}, {c.x + half, top + 1.0f, c.z + fixed + t * 0.5f})
                        : AABB({c.x + fixed - t * 0.5f, top, c.z - half}, {c.x + fixed + t * 0.5f, top + 1.0f, c.z + half});
        statics.push_back({b, wall, PropKind::HouseWall, true, true, newGroup(200.0f, 6, 1)});
    }
    occupied_.push_back(Vec3(c.x, half + 3.0f, c.z));
    Chest ch;
    ch.pos = Vec3(c.x - half + 1.5f, top + 0.0f, c.z - half + 1.5f);  // on the roof
    chests.push_back(ch);
    Chest ch2;
    ch2.pos = Vec3(c.x - half + 1.5f, base + 0.05f, c.z + half - 1.5f);
    chests.push_back(ch2);
}

void World::buildHouse(const Vec3& c, int doorSide, Rng& rng) {
    const float half = 5.0f, wallH = 3.6f, t = 0.3f, base = c.y;
    static const Vec4 palette[] = {rgb(0.86f, 0.80f, 0.66f), rgb(0.62f, 0.74f, 0.86f),
                                   rgb(0.88f, 0.66f, 0.56f), rgb(0.74f, 0.82f, 0.64f)};
    Vec4 wallCol = palette[rng.rangeInt(0, 3)];
    static const Vec4 roofs[] = {rgb(0.55f, 0.22f, 0.18f), rgb(0.3f, 0.35f, 0.45f), rgb(0.35f, 0.45f, 0.3f)};
    Vec4 roofCol = roofs[rng.rangeInt(0, 2)];

    // Floor slab.
    statics.push_back({AABB({c.x - half, base - 0.6f, c.z - half}, {c.x + half, base + 0.05f, c.z + half}),
                       rgb(0.50f, 0.42f, 0.34f), PropKind::HouseFloor, true, true});
    // Four walls; the door side gets a gap.
    const float doorW = 1.8f, doorH = 2.5f;
    for (int side = 0; side < 4; ++side) {
        // side 0: +z, 1: +x, 2: -z, 3: -x
        bool alongX = (side == 0 || side == 2);
        float fixed = (side == 0 || side == 1) ? half - t * 0.5f : -half + t * 0.5f;
        auto addSeg = [&](float a0, float a1, float y0, float y1) {
            AABB b;
            if (alongX) b = AABB({c.x + a0, base + y0, c.z + fixed - t * 0.5f}, {c.x + a1, base + y1, c.z + fixed + t * 0.5f});
            else b = AABB({c.x + fixed - t * 0.5f, base + y0, c.z + a0}, {c.x + fixed + t * 0.5f, base + y1, c.z + a1});
            statics.push_back({b, wallCol, PropKind::HouseWall, true, true, newGroup(200.0f, 6, 1)});
        };
        if (side == doorSide) {
            addSeg(-half, -doorW * 0.5f, 0, wallH);
            addSeg(doorW * 0.5f, half, 0, wallH);
            addSeg(-doorW * 0.5f, doorW * 0.5f, doorH, wallH);
        } else if (side == ((doorSide + 2) & 3)) {
            // Window on the opposite wall.
            addSeg(-half, -1.0f, 0, wallH);
            addSeg(1.0f, half, 0, wallH);
            addSeg(-1.0f, 1.0f, 0, 1.1f);
            addSeg(-1.0f, 1.0f, 2.3f, wallH);
        } else {
            addSeg(-half, half, 0, wallH);
        }
    }
    // Roof.
    statics.push_back({AABB({c.x - half - 0.4f, base + wallH, c.z - half - 0.4f}, {c.x + half + 0.4f, base + wallH + 0.35f, c.z + half + 0.4f}),
                       roofCol, PropKind::HouseRoof, true, true, newGroup(300.0f, 8, 2)});

    // Chest in a back corner.
    Chest ch;
    float cx = (doorSide == 1) ? -2.8f : 2.8f;
    float cz = (doorSide == 0) ? -2.8f : 2.8f;
    ch.pos = Vec3(c.x + cx, base + 0.05f, c.z + cz);
    ch.yaw = 0;
    chests.push_back(ch);
}

void World::flattenTerrain(float cx, float cz, float radius, float y) {
    for (int iz = 0; iz < hmSize_; ++iz)
        for (int ix = 0; ix < hmSize_; ++ix) {
            float x = -MAP_HALF + ix * HEIGHTMAP_STEP, z = -MAP_HALF + iz * HEIGHTMAP_STEP;
            float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
            if (d < radius) heights_[(size_t)iz * hmSize_ + ix] = y;
            else if (d < radius + 10) {
                float w = 1.0f - (d - radius) / 10.0f;
                heights_[(size_t)iz * hmSize_ + ix] = lerpf(heights_[(size_t)iz * hmSize_ + ix], y, w);
            }
        }
    ++generation;
}

int World::poiAt(float x, float z) const {
    for (size_t i = 0; i < pois.size(); ++i) {
        float dx = x - pois[i].center.x, dz = z - pois[i].center.z;
        if (dx * dx + dz * dz <= pois[i].radius * pois[i].radius) return (int)i;
    }
    return -1;
}

// ------------------------------------------------------------------ terrain
float World::terrainHeight(float x, float z) const {
    if (hmSize_ == 0) return 0;
    float fx = (clampf(x, -MAP_HALF, MAP_HALF) + MAP_HALF) / HEIGHTMAP_STEP;
    float fz = (clampf(z, -MAP_HALF, MAP_HALF) + MAP_HALF) / HEIGHTMAP_STEP;
    int ix = std::min((int)fx, hmSize_ - 2), iz = std::min((int)fz, hmSize_ - 2);
    float tx = fx - ix, tz = fz - iz;
    float h00 = hmAt(ix, iz), h10 = hmAt(ix + 1, iz), h01 = hmAt(ix, iz + 1), h11 = hmAt(ix + 1, iz + 1);
    return lerpf(lerpf(h00, h10, tx), lerpf(h01, h11, tx), tz);
}

// ------------------------------------------------------------------ queries
bool World::raycast(const Ray& r, float maxT, HitInfo& hit, bool includeChests) const {
    hit = HitInfo();
    hit.t = maxT;
    float t;
    Vec3 n;

    // Terrain: march then bisect.
    {
        const float step = 0.5f;
        float prevT = 0;
        bool prevAbove = r.origin.y > terrainHeight(r.origin.x, r.origin.z);
        for (float tt = step; tt <= hit.t; tt += step) {
            Vec3 p = r.at(tt);
            if (std::fabs(p.x) > MAP_HALF + 1 || std::fabs(p.z) > MAP_HALF + 1) break;
            if (r.dir.y > 0 && p.y > 22.0f) break;  // above every hill
            bool above = p.y > terrainHeight(p.x, p.z);
            if (prevAbove && !above) {
                float lo = prevT, hi = tt;
                for (int i = 0; i < 10; ++i) {
                    float mid = (lo + hi) * 0.5f;
                    Vec3 m = r.at(mid);
                    if (m.y > terrainHeight(m.x, m.z)) lo = mid; else hi = mid;
                }
                hit.kind = HitKind::Terrain;
                hit.t = hi;
                hit.point = r.at(hi);
                float e = 0.3f;
                float hx = terrainHeight(hit.point.x + e, hit.point.z) - terrainHeight(hit.point.x - e, hit.point.z);
                float hz = terrainHeight(hit.point.x, hit.point.z + e) - terrainHeight(hit.point.x, hit.point.z - e);
                hit.normal = normalize(Vec3(-hx, 2 * e, -hz));
                break;
            }
            prevAbove = above;
            prevT = tt;
        }
    }

    for (size_t i = 0; i < statics.size(); ++i) {
        const StaticBox& s = statics[i];
        if (!s.blockShots || !s.alive) continue;
        if (rayAABB(r, s.box, hit.t, t, n) && t < hit.t) {
            hit.kind = HitKind::Static; hit.t = t; hit.normal = n; hit.id = (int)i;
        }
    }

    std::vector<AABB> boxes;
    std::vector<Tri> tris;
    for (const Piece& p : build.pieces()) {
        if (!p.alive) continue;
        // Quick reject on the whole piece.
        AABB pb = pieceBounds(p.slot);
        pb.min -= Vec3(0.05f, 0.05f, 0.05f);
        pb.max += Vec3(0.05f, 0.05f, 0.05f);
        if (!rayAABB(r, pb, hit.t, t, n)) continue;
        boxes.clear();
        tris.clear();
        pieceBoxes(p, boxes);
        pieceTris(p, tris);
        for (const AABB& b : boxes)
            if (rayAABB(r, b, hit.t, t, n) && t < hit.t) {
                hit.kind = HitKind::Piece; hit.t = t; hit.normal = n; hit.id = p.id;
            }
        for (const Tri& tr : tris)
            if (rayTriangle(r, tr.a, tr.b, tr.c, hit.t, t) && t < hit.t) {
                Vec3 nn = normalize(cross(tr.b - tr.a, tr.c - tr.a));
                if (dot(nn, r.dir) > 0) nn = -nn;
                hit.kind = HitKind::Piece; hit.t = t; hit.normal = nn; hit.id = p.id;
            }
    }

    if (includeChests) {
        for (size_t i = 0; i < chests.size(); ++i) {
            if (chests[i].opened) continue;
            if (rayAABB(r, chests[i].bounds(), hit.t, t, n) && t < hit.t) {
                hit.kind = HitKind::Chest; hit.t = t; hit.normal = n; hit.id = (int)i;
            }
        }
    }

    if (hit.kind != HitKind::None) {
        hit.point = r.at(hit.t);
        return true;
    }
    return false;
}

float World::groundHeight(float x, float z, float radius, float feetY, float stepUp) const {
    float best = terrainHeight(x, z);
    float limit = feetY + stepUp;
    float fr = radius * 0.8f;
    AABB foot({x - fr, -1e9f, z - fr}, {x + fr, 1e9f, z + fr});

    auto consider = [&](const AABB& b) {
        if (b.max.y <= limit && b.max.y > best && b.min.x < foot.max.x && b.max.x > foot.min.x &&
            b.min.z < foot.max.z && b.max.z > foot.min.z)
            best = b.max.y;
    };
    for (const StaticBox& s : statics)
        if (s.collide && s.alive) consider(s.box);
    for (const Chest& c : chests) consider(c.bounds());

    std::vector<AABB> boxes;
    const float sx[5] = {0, fr, -fr, 0, 0}, sz[5] = {0, 0, 0, fr, -fr};
    for (const Piece& p : build.pieces()) {
        if (!p.alive) continue;
        AABB pb = pieceBounds(p.slot);
        if (pb.min.x > foot.max.x || pb.max.x < foot.min.x || pb.min.z > foot.max.z || pb.max.z < foot.min.z) continue;
        if (pb.min.y > limit) continue;
        if (p.slot.type == PieceType::Ramp || p.slot.type == PieceType::Cone) {
            for (int i = 0; i < 5; ++i) {
                float y;
                if (pieceSurfaceHeight(p, x + sx[i], z + sz[i], y) && y <= limit && y > best) best = y;
            }
        } else {
            boxes.clear();
            pieceBoxes(p, boxes);
            for (const AABB& b : boxes) consider(b);
        }
    }
    return best;
}

void World::collectSolids(const AABB& region, std::vector<AABB>& out) const {
    for (const StaticBox& s : statics)
        if (s.collide && s.alive && s.box.overlaps(region)) out.push_back(s.box);
    for (const Chest& c : chests)
        if (c.bounds().overlaps(region)) out.push_back(c.bounds());
    std::vector<AABB> boxes;
    for (const Piece& p : build.pieces()) {
        if (!p.alive || (p.slot.type != PieceType::Wall && p.slot.type != PieceType::Floor)) continue;
        if (!pieceBounds(p.slot).overlaps(region)) continue;
        boxes.clear();
        pieceBoxes(p, boxes);
        for (const AABB& b : boxes)
            if (b.overlaps(region)) out.push_back(b);
    }
}

void World::moveBody(Body& b, float dt, const std::vector<AABB>& extra, float gravity) const {
    const float stepUp = 0.8f;
    const float eps = 0.001f;
    float dx = b.vel.x * dt, dz = b.vel.z * dt;

    AABB region = b.box();
    region.min -= Vec3(std::fabs(dx) + 1.0f, 1.5f, std::fabs(dz) + 1.0f);
    region.max += Vec3(std::fabs(dx) + 1.0f, 1.5f + std::max(0.0f, b.vel.y * dt), std::fabs(dz) + 1.0f);
    std::vector<AABB> solids;
    collectSolids(region, solids);
    for (const AABB& e : extra)
        if (e.overlaps(region)) solids.push_back(e);

    auto bodyBox = [&]() {
        AABB bb = b.box();
        bb.min.y += stepUp;
        if (bb.min.y >= bb.max.y) bb.min.y = bb.max.y - 0.1f;
        return bb;
    };

    // Push out of anything we are stuck inside (e.g. a wall just placed on us).
    for (const AABB& s : solids) {
        AABB bb = bodyBox();
        if (!bb.overlaps(s)) continue;
        float pushes[4] = {s.max.x - bb.min.x, bb.max.x - s.min.x, s.max.z - bb.min.z, bb.max.z - s.min.z};
        int best = 0;
        for (int i = 1; i < 4; ++i)
            if (pushes[i] < pushes[best]) best = i;
        if (pushes[best] > 1.5f) continue;  // deeply inside something big; let gravity sort it out
        switch (best) {
            case 0: b.pos.x += pushes[0] + eps; break;
            case 1: b.pos.x -= pushes[1] + eps; break;
            case 2: b.pos.z += pushes[2] + eps; break;
            default: b.pos.z -= pushes[3] + eps; break;
        }
    }

    // Sloped pieces (ramps, cones) are solid too: you can walk up them, under them, but not
    // through them. Blocked when the surface at the new spot is too high to step onto and the
    // body would cross it.
    std::vector<const Piece*> slopes;
    for (const Piece& p : build.pieces())
        if (p.alive && (p.slot.type == PieceType::Ramp || p.slot.type == PieceType::Cone) && pieceBounds(p.slot).overlaps(region))
            slopes.push_back(&p);
    auto slopeBlocks = [&]() {
        for (const Piece* p : slopes) {
            const float r = b.radius * 0.7f;
            const float sx[5] = {0, r, -r, 0, 0}, sz[5] = {0, 0, 0, r, -r};
            for (int i = 0; i < 5; ++i) {
                float h;
                if (!pieceSurfaceHeight(*p, b.pos.x + sx[i], b.pos.z + sz[i], h)) continue;
                bool tooHigh = h > b.pos.y + stepUp;
                bool crosses = b.pos.y + b.height > h - 0.25f;
                if (tooHigh && crosses) return true;
            }
        }
        return false;
    };

    // Horizontal movement, axis by axis, in small sub-steps.
    int steps = std::max(1, (int)std::ceil(std::max(std::fabs(dx), std::fabs(dz)) / 0.2f));
    for (int s = 0; s < steps; ++s) {
        float sdx = dx / steps, sdz = dz / steps;
        if (sdx != 0) {
            float before = b.pos.x;
            b.pos.x += sdx;
            for (const AABB& sb : solids) {
                AABB bb = bodyBox();
                if (!bb.overlaps(sb)) continue;
                if (sdx > 0) b.pos.x = sb.min.x - b.radius - eps;
                else b.pos.x = sb.max.x + b.radius + eps;
                b.vel.x = 0;
            }
            if (slopeBlocks()) { b.pos.x = before; b.vel.x = 0; }
        }
        if (sdz != 0) {
            float before = b.pos.z;
            b.pos.z += sdz;
            for (const AABB& sb : solids) {
                AABB bb = bodyBox();
                if (!bb.overlaps(sb)) continue;
                if (sdz > 0) b.pos.z = sb.min.z - b.radius - eps;
                else b.pos.z = sb.max.z + b.radius + eps;
                b.vel.z = 0;
            }
            if (slopeBlocks()) { b.pos.z = before; b.vel.z = 0; }
        }
    }
    float lim = MAP_HALF - 0.5f;
    b.pos.x = clampf(b.pos.x, -lim, lim);
    b.pos.z = clampf(b.pos.z, -lim, lim);

    // Vertical movement.
    b.vel.y -= gravity * dt;
    float ground = groundHeight(b.pos.x, b.pos.z, b.radius, b.pos.y, stepUp);
    float newY = b.pos.y + b.vel.y * dt;
    if (b.vel.y <= 0 && newY <= ground) {
        b.pos.y = ground;
        b.vel.y = 0;
        b.grounded = true;
    } else if (b.grounded && b.vel.y <= 0 && b.pos.y - ground < 0.6f) {
        b.pos.y = ground;  // stick to slopes and stairs when walking down
        b.vel.y = 0;
        b.grounded = true;
    } else {
        if (b.vel.y > 0) {
            // Ceiling.
            AABB head({b.pos.x - b.radius, b.pos.y + b.height, b.pos.z - b.radius},
                      {b.pos.x + b.radius, newY + b.height, b.pos.z + b.radius});
            for (const AABB& sb : solids)
                if (sb.overlaps(head) && sb.min.y >= b.pos.y + b.height - 0.05f) {
                    newY = std::min(newY, sb.min.y - b.height - eps);
                    b.vel.y = 0;
                }
        }
        b.pos.y = newY;
        b.grounded = false;
    }
    float terr = terrainHeight(b.pos.x, b.pos.z);
    if (b.pos.y < terr - 1.0f) {
        b.pos.y = terr;
        b.vel.y = 0;
        b.grounded = true;
    }
}

// ------------------------------------------------------------------ build support
namespace {
bool touching(const AABB& a, const AABB& b, float gap) {
    AABB e = a;
    e.min -= Vec3(gap, gap, gap);
    e.max += Vec3(gap, gap, gap);
    return e.overlaps(b);
}
}  // namespace

// The solid parts of a piece: the remaining tiles of an edited wall/floor, or the whole bounds
// for ramps and cones.
static void solidParts(const Piece& p, std::vector<AABB>& out) {
    out.clear();
    if (p.slot.type == PieceType::Wall || p.slot.type == PieceType::Floor) pieceBoxes(p, out);
    if (out.empty()) out.push_back(pieceBounds(p.slot));
}

static bool groundedBox(const World& w, const AABB& b) {
    // Standing on (or dug into) the ground: check the bottom corners and the middle.
    const float pts[5][2] = {{b.min.x, b.min.z}, {b.max.x, b.min.z}, {b.min.x, b.max.z}, {b.max.x, b.max.z},
                             {(b.min.x + b.max.x) * 0.5f, (b.min.z + b.max.z) * 0.5f}};
    // (A piece snaps to the grid, so its base can sit up to ~1 m above sloping ground.)
    for (const auto& pt : pts)
        if (b.min.y <= w.terrainHeight(pt[0], pt[1]) + 1.1f) return true;
    // Resting on a house or rock.
    for (const StaticBox& st : w.statics)
        if (st.alive && st.collide && touching(b, st.box, 0.3f)) return true;
    return false;
}

bool World::pieceGrounded(const Slot& s) const {
    Piece p;
    p.slot = s;
    p.alive = true;
    return pieceGrounded(p);
}

bool World::pieceGrounded(const Piece& p) const {
    std::vector<AABB> parts;
    solidParts(p, parts);
    for (const AABB& b : parts)
        if (groundedBox(*this, b)) return true;
    return false;
}

bool World::wouldBeSupported(const Slot& s) const {
    if (pieceGrounded(s)) return true;
    AABB b = pieceBounds(s);
    std::vector<AABB> parts;
    for (const Piece& p : build.pieces()) {
        if (!p.alive) continue;
        solidParts(p, parts);
        for (const AABB& q : parts)
            if (touching(b, q, 0.3f)) return true;
    }
    return false;
}

int World::breakUnsupported(std::vector<int>* brokenIds) {
    std::vector<Piece>& pieces = build.pieces();
    std::vector<char> ok(pieces.size(), 0);
    std::vector<int> stack;
    for (const Piece& p : pieces)
        if (p.alive && pieceGrounded(p)) {
            ok[p.id] = 1;
            stack.push_back(p.id);
        }
    // Flood the support from the grounded pieces through everything they touch (through the
    // parts that are really there - a cut-out tile holds nothing up).
    std::vector<std::vector<AABB>> parts(pieces.size());
    for (const Piece& p : pieces)
        if (p.alive) solidParts(p, parts[p.id]);
    auto partsTouch = [&](int a, int c) {
        for (const AABB& x : parts[a])
            for (const AABB& y : parts[c])
                if (touching(x, y, 0.3f)) return true;
        return false;
    };
    while (!stack.empty()) {
        int id = stack.back();
        stack.pop_back();
        for (const Piece& q : pieces)
            if (q.alive && !ok[q.id] && partsTouch(id, q.id)) {
                ok[q.id] = 1;
                stack.push_back(q.id);
            }
    }
    int broke = 0;
    for (const Piece& p : pieces)
        if (p.alive && !ok[p.id]) {
            build.destroy(p.id);
            if (brokenIds) brokenIds->push_back(p.id);
            ++broke;
        }
    return broke;
}

// ------------------------------------------------------------------ harvesting
int World::newGroup(float hp, int matsPerHit, int matType) {
    PropGroup g;
    g.hp = g.maxHp = hp;
    g.matsPerHit = matsPerHit;
    g.matType = matType;
    groups.push_back(g);
    return (int)groups.size() - 1;
}

HarvestResult World::harvest(int staticIndex, float damage) {
    HarvestResult r;
    if (staticIndex < 0 || staticIndex >= (int)statics.size()) return r;
    int gi = statics[staticIndex].group;
    if (gi < 0 || gi >= (int)groups.size() || !groups[gi].alive) return r;
    PropGroup& g = groups[gi];
    r.mats = g.matsPerHit;
    r.matType = g.matType;
    g.hp -= damage;
    g.hitAnim = 0.25f;
    if (g.hp <= 0) {
        g.alive = false;
        r.destroyed = true;
        for (StaticBox& s : statics)
            if (s.group == gi) s.alive = false;
    }
    return r;
}

void World::updateProps(float dt) {
    for (PropGroup& g : groups) g.hitAnim = std::max(0.0f, g.hitAnim - dt);
}

// ------------------------------------------------------------------ pickups
void World::spawnPickup(const Item& item, const Vec3& pos, const Vec3& vel) {
    Pickup p;
    p.item = item;
    p.pos = pos;
    p.vel = vel;
    pickups.push_back(p);
}

void World::updatePickups(float dt) {
    for (Pickup& p : pickups) {
        if (!p.alive) continue;
        p.age += dt;
        if (p.settled) continue;
        p.vel.y -= 15.0f * dt;
        p.vel.x *= (1.0f - 1.5f * dt);
        p.vel.z *= (1.0f - 1.5f * dt);
        p.pos += p.vel * dt;
        float g = groundHeight(p.pos.x, p.pos.z, 0.2f, p.pos.y - 0.5f, 0.6f);
        if (p.vel.y < 0 && p.pos.y <= g + 0.5f) {
            p.pos.y = g + 0.5f;
            p.settled = true;
        }
    }
    // Drop dead pickups once there are many.
    if (pickups.size() > 64) {
        std::vector<Pickup> keep;
        for (auto& p : pickups)
            if (p.alive) keep.push_back(p);
        pickups.swap(keep);
    }
}

}  // namespace bf
