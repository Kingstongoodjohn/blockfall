// Blockfall - grid building: walls, floors, ramps, cones
#include "building.h"
#include <cmath>

namespace bf {

namespace {

inline bool tileRemoved(const Piece& p, int tile) { return (p.removed >> tile) & 1u; }

// Corner (min x, min z) of a cell, and its base height.
inline Vec3 cellOrigin(const Slot& s) { return {s.i * TILE, s.k * STORY, s.j * TILE}; }

// Ramp: low edge -> high edge. Returns progress 0..1 along the rise for a point in the cell.
inline float rampProgressDir(int dir, float lx, float lz) {
    switch (dir & 3) {
        case 0: return lz;          // rises toward +z
        case 1: return lx;          // rises toward +x
        case 2: return 1.0f - lz;   // rises toward -z
        default: return 1.0f - lx;  // rises toward -x
    }
}
inline float rampProgress(const Slot& s, float x, float z) {
    return rampProgressDir(s.dir, (x - s.i * TILE) / TILE, (z - s.j * TILE) / TILE);
}

}  // namespace

int coneQuadrantTile(int q) {
    // Quadrants are produced by rotating the (+x,+z) quarter 90 degrees at a time:
    // q0 = (+x,+z), q1 = (+x,-z), q2 = (-x,-z), q3 = (-x,+z).
    // Tile index = tx + 2*tz with tx/tz = 1 on the positive half.
    static const int map[4] = {3, 1, 0, 2};
    return map[q & 3];
}

int cardinalFromYaw(float yaw) {
    float fx = std::sin(yaw), fz = std::cos(yaw);
    if (std::fabs(fx) > std::fabs(fz)) return fx > 0 ? 1 : 3;
    return fz > 0 ? 0 : 2;
}

AABB pieceBounds(const Slot& s) {
    Vec3 o = cellOrigin(s);
    switch (s.type) {
        case PieceType::Wall:
            if (s.side == 0)
                return {{o.x - WALL_THICK * 0.5f, o.y, o.z}, {o.x + WALL_THICK * 0.5f, o.y + STORY, o.z + TILE}};
            return {{o.x, o.y, o.z - WALL_THICK * 0.5f}, {o.x + TILE, o.y + STORY, o.z + WALL_THICK * 0.5f}};
        case PieceType::Floor:
            return {{o.x, o.y - FLOOR_THICK, o.z}, {o.x + TILE, o.y, o.z + TILE}};
        case PieceType::Ramp:
            return {{o.x, o.y, o.z}, {o.x + TILE, o.y + STORY, o.z + TILE}};
        case PieceType::Cone:
            return {{o.x, o.y, o.z}, {o.x + TILE, o.y + CONE_EDIT_HEIGHT, o.z + TILE}};
        default:
            return {o, o};
    }
}

AABB pieceTileBounds(const Piece& p, int tile) {
    const Slot& s = p.slot;
    Vec3 o = cellOrigin(s);
    if (s.type == PieceType::Wall) {
        int col = tile % 3, row = tile / 3;
        float w = TILE / 3.0f, h = STORY / 3.0f;
        float y0 = o.y + row * h;
        if (s.side == 0) {
            float z0 = o.z + col * w;
            return {{o.x - WALL_THICK * 0.5f, y0, z0}, {o.x + WALL_THICK * 0.5f, y0 + h, z0 + w}};
        }
        float x0 = o.x + col * w;
        return {{x0, y0, o.z - WALL_THICK * 0.5f}, {x0 + w, y0 + h, o.z + WALL_THICK * 0.5f}};
    }
    if (s.type == PieceType::Floor || s.type == PieceType::Cone || s.type == PieceType::Ramp) {
        int tx = tile % 2, tz = tile / 2;
        float h = TILE * 0.5f;
        float x0 = o.x + tx * h, z0 = o.z + tz * h;
        if (s.type == PieceType::Floor) return {{x0, o.y - FLOOR_THICK, z0}, {x0 + h, o.y, z0 + h}};
        if (s.type == PieceType::Ramp) {
            // Follow the ramp's surface so the edit grid sits on it (a thin, flat tile).
            float yc = o.y + rampHeightAt(p, x0 + h * 0.5f - o.x, z0 + h * 0.5f - o.z);
            return {{x0, yc - 0.12f, z0}, {x0 + h, yc + 0.12f, z0 + h}};
        }
        // Cones too: a flat tile at the surface height of the quadrant's middle.
        float yc = o.y + coneHeightAt(p, x0 + h * 0.5f - o.x, z0 + h * 0.5f - o.z);
        return {{x0, yc - 0.12f, z0}, {x0 + h, yc + 0.12f, z0 + h}};
    }
    return pieceBounds(s);
}

void pieceBoxes(const Piece& p, std::vector<AABB>& out) {
    if (!p.alive) return;
    if (p.slot.type == PieceType::Wall) {
        for (int t = 0; t < 9; ++t)
            if (!tileRemoved(p, t)) out.push_back(pieceTileBounds(p, t));
    } else if (p.slot.type == PieceType::Floor) {
        for (int t = 0; t < 4; ++t)
            if (!tileRemoved(p, t)) out.push_back(pieceTileBounds(p, t));
    }
}

// Height of a cone's surface above its base at local (lx, lz) in 0..TILE.
//
// Editing a cone moves its peak, like in Fortnite. The surface slopes down from a "high set"
// with the same slope everywhere:
//   nothing edited  -> the high point is the centre (a normal pyramid)
//   1 tile edited   -> the two far edges stay high, the piece slopes down to that corner
//   2 on one side   -> that side drops to the base and the opposite edge stays high: a ramp
//   2 on a diagonal -> two opposite corners stay high
//   3 edited        -> only the untouched corner stays high
//   all 4 edited    -> flat
float coneHeightAt(const Piece& p, float lx, float lz) {
    uint16_t sel = p.removed & 0xF;
    auto plain = [&]() {
        float m = std::max(std::fabs(lx - TILE * 0.5f), std::fabs(lz - TILE * 0.5f)) / (TILE * 0.5f);
        return CONE_HEIGHT * (1.0f - clampf(m, 0.0f, 1.0f));
    };
    // Nothing, a diagonal pair, or everything selected: a plain cone.
    if (sel == 0 || sel == 0xF || sel == 0x6 || sel == 0x9) return plain();
    // The selected tiles are the side that rises (to nearly a full story, so you can get under
    // the high side); the other side slopes down to the floor.
    if (sel == 0x3) return CONE_EDIT_HEIGHT * (1.0f - clampf(lz / TILE, 0.0f, 1.0f));          // -z side up
    if (sel == 0xC) return CONE_EDIT_HEIGHT * clampf(lz / TILE, 0.0f, 1.0f);                   // +z side up
    if (sel == 0x5) return CONE_EDIT_HEIGHT * (1.0f - clampf(lx / TILE, 0.0f, 1.0f));          // -x side up
    if (sel == 0xA) return CONE_EDIT_HEIGHT * clampf(lx / TILE, 0.0f, 1.0f);                   // +x side up
    // One tile: the peak moves to that tile's outer corner. Three tiles: the one left out is
    // the low corner, so the peak is at the opposite corner.
    int bits = (sel & 1) + ((sel >> 1) & 1) + ((sel >> 2) & 1) + ((sel >> 3) & 1);
    int peakTile = 0;
    if (bits == 1) {
        for (int t = 0; t < 4; ++t)
            if ((sel >> t) & 1u) peakTile = t;
    } else {
        for (int t = 0; t < 4; ++t)
            if (!((sel >> t) & 1u)) peakTile = t ^ 3;
    }
    float cx = (peakTile % 2) ? TILE : 0.0f, cz = (peakTile / 2) ? TILE : 0.0f;
    float d = std::max(std::fabs(lx - cx), std::fabs(lz - cz));
    return CONE_EDIT_HEIGHT * (1.0f - clampf(d / TILE, 0.0f, 1.0f));
}

// Height of a ramp above its base at local (lx, lz) in 0..TILE, with its edits applied.
//   no edit          -> a straight ramp in its direction
//   2 tiles, one side-> rises toward that side
//   1 tile           -> rises toward that tile's side (z side of it)
//   3 tiles (an L)   -> turn stairs: spiral up around the untouched corner
//   diagonal / all 4 -> unchanged
float rampHeightAt(const Piece& p, float lx, float lz) {
    uint16_t e = p.removed & 0xF;
    float u = lx / TILE, v = lz / TILE;
    int dir = p.slot.dir;
    int bits = (e & 1) + ((e >> 1) & 1) + ((e >> 2) & 1) + ((e >> 3) & 1);
    if (bits == 3) {
        // Turn stairs: the tile left out is the corner we climb around. Height follows the
        // angle around that corner, from the swipe's first tile (bottom) to its last (top).
        int missing = 0;
        for (int t = 0; t < 4; ++t) if (!((e >> t) & 1u)) missing = t;
        float cx = (missing % 2) ? 1.0f : 0.0f, cz = (missing / 2) ? 1.0f : 0.0f;
        float dx = std::fabs(u - cx), dz = std::fabs(v - cz);
        float ang = std::atan2(dz, dx) / (PI * 0.5f);  // 0 (along x from the corner) .. 1 (along z)
        int first = (p.removed >> 4) & 3;
        bool orderKnown = (p.removed >> 6) & 1;
        // The first tile sits along x from the corner (ang ~0.25) or along z (ang ~0.75).
        float fx = std::fabs(((first % 2) ? 0.75f : 0.25f) - cx), fz = std::fabs(((first / 2) ? 0.75f : 0.25f) - cz);
        bool startAlongX = orderKnown ? fx > fz : ((missing == 0) || (missing == 3));
        return STORY * clampf(startAlongX ? ang : 1.0f - ang, 0.0f, 1.0f);
    }
    return STORY * clampf(rampProgressDir(dir, u, v), 0.0f, 1.0f);
}

void applyStairEdit(Piece& p, uint16_t selected, int firstTile, int lastTile) {
    uint16_t e = selected & 0xF;
    int bits = (e & 1) + ((e >> 1) & 1) + ((e >> 2) & 1) + ((e >> 3) & 1);
    auto dirFromTo = [](int a, int b) {
        int dx = (b % 2) - (a % 2), dz = (b / 2) - (a / 2);
        if (dz > 0) return 0;
        if (dx > 0) return 1;
        if (dz < 0) return 2;
        return 3;
    };
    if (bits == 2 && e != 0x6 && e != 0x9 && firstTile >= 0 && lastTile >= 0 && firstTile != lastTile) {
        // Straight stairs rising the way you swiped.
        p.slot.dir = dirFromTo(firstTile, lastTile);
        p.removed = 0;
    } else if (bits == 3) {
        // Turn stairs. The first tile is the bottom; if the swipe started in the middle tile,
        // the bottom is whichever end wasn't reached last.
        int missing = 0;
        for (int t = 0; t < 4; ++t) if (!((e >> t) & 1u)) missing = t;
        int middle = missing ^ 3;
        int first = firstTile;
        if (first < 0 || !((e >> first) & 1u)) first = -1;
        if (first == middle || first < 0) {
            for (int t = 0; t < 4; ++t)
                if (((e >> t) & 1u) && t != middle && t != lastTile) first = t;
        }
        p.removed = (uint16_t)(e | ((first & 3) << 4) | (1u << 6));
    } else {
        // One tile, a diagonal pair, or everything: plain straight stairs again.
        p.removed = 0;
    }
}

bool validWallEdit(uint16_t removed) {
    removed &= 0x1FF;
    if (removed == 0) return true;
    if (removed == 0x1FF) return false;

    // The cut must be a single rectangle: its tile count equals the area of its bounding box.
    int minR = 3, maxR = -1, minC = 3, maxC = -1, count = 0;
    for (int t = 0; t < 9; ++t) {
        if (!((removed >> t) & 1u)) continue;
        int r = t / 3, c = t % 3;
        minR = std::min(minR, r); maxR = std::max(maxR, r);
        minC = std::min(minC, c); maxC = std::max(maxC, c);
        ++count;
    }
    if (count != (maxR - minR + 1) * (maxC - minC + 1)) return false;

    // Every kept tile must reach a kept bottom-row tile through kept neighbours (flood fill up
    // from the bottom row). Otherwise part of the wall would hang in the air.
    uint16_t kept = (uint16_t)(~removed & 0x1FF);
    uint16_t reached = kept & 0x7;  // bottom row
    for (bool grew = true; grew;) {
        grew = false;
        for (int t = 0; t < 9; ++t) {
            if (!((kept >> t) & 1u) || ((reached >> t) & 1u)) continue;
            int r = t / 3, c = t % 3;
            bool touches = (r > 0 && ((reached >> (t - 3)) & 1u)) || (r < 2 && ((reached >> (t + 3)) & 1u)) ||
                           (c > 0 && ((reached >> (t - 1)) & 1u)) || (c < 2 && ((reached >> (t + 1)) & 1u));
            if (touches) {
                reached |= (uint16_t)(1u << t);
                grew = true;
            }
        }
    }
    return reached == kept;
}

bool validEdit(PieceType type, uint16_t removed) {
    return type != PieceType::Wall || validWallEdit(removed);
}

void pieceTris(const Piece& p, std::vector<Tri>& out) {
    if (!p.alive) return;
    const Slot& s = p.slot;
    Vec3 o = cellOrigin(s);
    if (s.type == PieceType::Ramp) {
        int bits = 0;
        for (int t = 0; t < 4; ++t) bits += (p.removed >> t) & 1u;
        if (bits != 3) {
            // A straight ramp: four corners of the cell at their heights.
            Vec3 c[4] = {{o.x, 0, o.z}, {o.x + TILE, 0, o.z}, {o.x + TILE, 0, o.z + TILE}, {o.x, 0, o.z + TILE}};
            for (auto& v : c) v.y = o.y + rampHeightAt(p, v.x - o.x, v.z - o.z);
            out.push_back({c[0], c[1], c[2]});
            out.push_back({c[0], c[2], c[3]});
            return;
        }
        // Turn stairs: sample the spiral on a grid.
        const int N = 8;
        auto at = [&](int i, int j) {
            float lx = i * TILE / N, lz = j * TILE / N;
            return Vec3(o.x + lx, o.y + rampHeightAt(p, lx, lz), o.z + lz);
        };
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                Vec3 a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
                out.push_back({a, b, c});
                out.push_back({a, c, d});
            }
    } else if (s.type == PieceType::Cone) {
        if ((p.removed & 0xF) == 0) {
            // Plain pyramid: 4 faces of 2 triangles.
            Vec3 apex(o.x + TILE * 0.5f, o.y + CONE_HEIGHT, o.z + TILE * 0.5f);
            float cx = apex.x, cz = apex.z, h = TILE * 0.5f;
            for (int q = 0; q < 4; ++q) {
                auto rot = [&](float lx, float lz) {
                    float x = lx, z = lz;
                    for (int r = 0; r < q; ++r) { float nx = z; float nz = -x; x = nx; z = nz; }
                    return Vec3(cx + x, o.y, cz + z);
                };
                Vec3 a = rot(h, 0), b = rot(h, h), c = rot(0, h);
                out.push_back({apex, a, b});
                out.push_back({apex, b, c});
            }
            return;
        }
        // Edited: sample the height field on a grid. Each square is split along whichever
        // diagonal follows the surface's creases better.
        const int N = 8;
        auto at = [&](int i, int j) {
            float lx = i * TILE / N, lz = j * TILE / N;
            return Vec3(o.x + lx, o.y + coneHeightAt(p, lx, lz), o.z + lz);
        };
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                Vec3 a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
                float mx = (i + 0.5f) * TILE / N, mz = (j + 0.5f) * TILE / N;
                float mid = o.y + coneHeightAt(p, mx, mz);
                float errAC = std::fabs((a.y + c.y) * 0.5f - mid), errBD = std::fabs((b.y + d.y) * 0.5f - mid);
                if (errAC <= errBD) {
                    out.push_back({a, b, c});
                    out.push_back({a, c, d});
                } else {
                    out.push_back({a, b, d});
                    out.push_back({b, c, d});
                }
            }
    }
}

bool pieceSurfaceHeight(const Piece& p, float x, float z, float& yOut) {
    if (!p.alive) return false;
    const Slot& s = p.slot;
    Vec3 o = cellOrigin(s);
    if (x < o.x || x > o.x + TILE || z < o.z || z > o.z + TILE) return false;
    if (s.type == PieceType::Ramp) {
        yOut = o.y + rampHeightAt(p, x - o.x, z - o.z);
        return true;
    }
    if (s.type == PieceType::Cone) {
        yOut = o.y + coneHeightAt(p, x - o.x, z - o.z);
        return true;
    }
    return false;
}

int pieceTileUnderRay(const Piece& p, const Ray& r, float maxT) {
    const Slot& s = p.slot;
    Vec3 o = cellOrigin(s);
    // Intersect with the piece's main plane, then map the hit to a tile.
    Vec3 n;
    float d;
    if (s.type == PieceType::Wall) {
        n = s.side == 0 ? Vec3(1, 0, 0) : Vec3(0, 0, 1);
        d = s.side == 0 ? o.x : o.z;
    } else if (s.type == PieceType::Floor) {
        n = Vec3(0, 1, 0);
        d = o.y;
    } else if (s.type == PieceType::Ramp || s.type == PieceType::Cone) {
        // Hit the sloped surface itself, so the tile you're looking at is the one you get.
        std::vector<Tri> tris;
        pieceTris(p, tris);
        float best = maxT;
        bool any = false;
        for (const Tri& tr : tris) {
            float t;
            if (rayTriangle(r, tr.a, tr.b, tr.c, best, t)) { best = t; any = true; }
        }
        if (!any) return -1;
        Vec3 h = r.at(best);
        int u = (int)((h.x - o.x) / (TILE * 0.5f)), v = (int)((h.z - o.z) / (TILE * 0.5f));
        if (u < 0 || u > 1 || v < 0 || v > 1) return -1;
        return u + 2 * v;
    } else {
        return -1;
    }
    float denom = dot(r.dir, n);
    if (std::fabs(denom) < 1e-5f) return -1;
    float t = (d - dot(r.origin, n)) / denom;
    if (t < 0 || t > maxT) return -1;
    Vec3 h = r.at(t);
    if (s.type == PieceType::Wall) {
        float u = (s.side == 0 ? h.z - o.z : h.x - o.x) / (TILE / 3.0f);
        float v = (h.y - o.y) / (STORY / 3.0f);
        if (u < 0 || u >= 3 || v < 0 || v >= 3) return -1;
        return (int)u + 3 * (int)v;
    }
    float u = (h.x - o.x) / (TILE * 0.5f);
    float v = (h.z - o.z) / (TILE * 0.5f);
    if (u < 0 || u >= 2 || v < 0 || v >= 2) return -1;
    return (int)u + 2 * (int)v;
}

Slot computeBuildSlot(PieceType type, const Vec3& feet, float yaw, const Ray& aim, float worldHitT,
                      int rampRotation) {
    Slot s;
    s.type = type;
    int ci = (int)std::floor(feet.x / TILE);
    int cj = (int)std::floor(feet.z / TILE);
    int ck = (int)std::floor((feet.y + 0.5f) / STORY);
    int facing = cardinalFromYaw(yaw);

    // Reach point: where the aim ray leaves a cylinder of radius BUILD_REACH around the player,
    // or where it hits something first.
    Vec3 rel = aim.origin - feet;
    float a = aim.dir.x * aim.dir.x + aim.dir.z * aim.dir.z;
    float b = 2.0f * (rel.x * aim.dir.x + rel.z * aim.dir.z);
    float c = rel.x * rel.x + rel.z * rel.z - BUILD_REACH * BUILD_REACH;
    float tReach = 8.0f;
    if (a > 1e-6f) {
        float disc = b * b - 4 * a * c;
        if (disc >= 0) tReach = (-b + std::sqrt(disc)) / (2 * a);
    }
    if (tReach < 0) tReach = 0;
    float t = std::min(tReach, std::max(0.0f, worldHitT - 0.05f));
    Vec3 P = aim.at(t);
    P.y = clampf(P.y, feet.y - STORY * 0.9f, feet.y + STORY * 2.0f);

    auto clampLevel = [&](int k) { return std::max(ck - 1, std::min(ck + 2, k)); };
    // Levels are picked relative to the player's own level from how far above or below the
    // feet the aim point is, so hills don't shift the result.
    float relY = P.y - feet.y;
    int up = relY > STORY * 1.5f ? 2 : (relY > STORY * 0.55f ? 1 : (relY < -STORY * 0.4f ? -1 : 0));

    switch (type) {
        case PieceType::Floor:
            s.i = (int)std::floor(P.x / TILE);
            s.j = (int)std::floor(P.z / TILE);
            s.k = clampLevel(ck + up);
            break;
        case PieceType::Cone:
            // Like Fortnite: looking ahead puts the pyramid on the ground in the cell in front of
            // you; looking up puts it over your head (a roof on your box).
            s.i = (int)std::floor(P.x / TILE);
            s.j = (int)std::floor(P.z / TILE);
            s.k = clampLevel(ck + std::max(0, up));
            break;
        case PieceType::Ramp:
            s.i = (int)std::floor(P.x / TILE);
            s.j = (int)std::floor(P.z / TILE);
            s.k = clampLevel(ck + up);
            s.dir = (facing + rampRotation) & 3;
            break;
        case PieceType::Wall: {
            // Wall goes on the edge of the player's own cell that they are facing.
            int wi = ci, wj = cj;
            float planePos;
            bool alongX;  // true if the wall plane is x = const
            switch (facing) {
                case 0: s.side = 1; wj = cj + 1; alongX = false; break;  // +z edge
                case 1: s.side = 0; wi = ci + 1; alongX = true; break;   // +x edge
                case 2: s.side = 1; wj = cj; alongX = false; break;      // -z edge
                default: s.side = 0; wi = ci; alongX = true; break;      // -x edge
            }
            s.i = wi;
            s.j = wj;
            planePos = alongX ? wi * TILE : wj * TILE;
            float o = alongX ? aim.origin.x : aim.origin.z;
            float d = alongX ? aim.dir.x : aim.dir.z;
            int k = ck;
            if (std::fabs(d) > 1e-4f) {
                float tp = (planePos - o) / d;
                if (tp > 0) k = (int)std::floor(aim.at(tp).y / STORY);
            }
            s.k = clampLevel(k);
            break;
        }
        default:
            break;
    }
    return s;
}

// --------------------------------------------------------------- BuildSystem
int64_t BuildSystem::slotKey(const Slot& s) {
    // type:2 | side:1 | i:16 | j:16 | k:12   (all offset to positive)
    int64_t side = s.type == PieceType::Wall ? s.side : 0;
    int64_t key = (int64_t)s.type;
    key = (key << 1) | side;
    key = (key << 16) | (int64_t)((s.i + 32768) & 0xFFFF);
    key = (key << 16) | (int64_t)((s.j + 32768) & 0xFFFF);
    key = (key << 12) | (int64_t)((s.k + 2048) & 0xFFF);
    return key;
}

void BuildSystem::clear() {
    pieces_.clear();
    slots_.clear();
}

bool BuildSystem::isFree(const Slot& s) const { return slots_.find(slotKey(s)) == slots_.end(); }

int BuildSystem::place(const Slot& s, bool playerBuilt, BuildMat mat) {
    int64_t key = slotKey(s);
    if (slots_.count(key)) return -1;
    Piece p;
    p.id = (int)pieces_.size();
    p.slot = s;
    p.mat = mat;
    p.maxHp = pieceMaxHp(s.type, mat);
    p.targetHp = p.maxHp;
    p.hp = p.maxHp * BUILD_UP_START;  // builds up over BUILD_UP_TIME
    p.alive = true;
    p.playerBuilt = playerBuilt;
    pieces_.push_back(p);
    slots_[key] = p.id;
    return p.id;
}

void BuildSystem::destroy(int id) {
    Piece* p = get(id);
    if (!p || !p->alive) return;
    p->alive = false;
    slots_.erase(slotKey(p->slot));
}

bool BuildSystem::damage(int id, float amount) {
    Piece* p = get(id);
    if (!p || !p->alive) return false;
    p->hp -= amount;
    p->targetHp -= amount;  // damage taken while building up is kept
    if (p->hp <= 0) {
        destroy(id);
        return true;
    }
    return false;
}

Piece* BuildSystem::get(int id) {
    if (id < 0 || id >= (int)pieces_.size()) return nullptr;
    return &pieces_[id];
}
const Piece* BuildSystem::get(int id) const {
    if (id < 0 || id >= (int)pieces_.size()) return nullptr;
    return &pieces_[id];
}

int BuildSystem::aliveCount() const {
    int n = 0;
    for (auto& p : pieces_) n += p.alive ? 1 : 0;
    return n;
}

void BuildSystem::update(float dt) {
    for (auto& p : pieces_)
        if (p.alive) {
            p.age += dt;
            if (p.hp < p.targetHp) p.hp = std::min(p.targetHp, p.hp + p.maxHp * dt / BUILD_UP_TIME);
        }
}

}  // namespace bf
