// Blockfall - bot pathfinding (see nav.h)
#include "nav.h"
#include "world.h"
#include <cmath>
#include <cstdint>
#include <queue>

namespace bf {

namespace {

// The walkability grid for one search. Built fresh each time, so builds placed or broken a
// moment ago are always taken into account.
struct NavGrid {
    float minX = 0, minZ = 0;
    int w = 0, h = 0;
    std::vector<uint8_t> blocked;
    std::vector<float> ground;   // terrain height per cell (NAN until needed)
    const World* world = nullptr;

    int index(int i, int j) const { return j * w + i; }
    bool inside(int i, int j) const { return i >= 0 && j >= 0 && i < w && j < h; }
    float cx(int i) const { return minX + (i + 0.5f) * NAV_CELL; }
    float cz(int j) const { return minZ + (j + 0.5f) * NAV_CELL; }
    int cellI(float x) const { return (int)std::floor((x - minX) / NAV_CELL); }
    int cellJ(float z) const { return (int)std::floor((z - minZ) / NAV_CELL); }
    float groundAt(int i, int j) {
        float& g = ground[index(i, j)];
        if (std::isnan(g)) g = world->terrainHeight(cx(i), cz(j));
        return g;
    }
    bool free(int i, int j) const { return inside(i, j) && !blocked[index(i, j)]; }

    // Straight walk between two cell centres without touching a blocked cell.
    bool lineClear(int i0, int j0, int i1, int j1) const {
        float x0 = cx(i0), z0 = cz(j0), x1 = cx(i1), z1 = cz(j1);
        float len = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
        int steps = std::max(1, (int)std::ceil(len / (NAV_CELL * 0.25f)));
        for (int s = 0; s <= steps; ++s) {
            float t = (float)s / steps;
            if (!free(cellI(x0 + (x1 - x0) * t), cellJ(z0 + (z1 - z0) * t))) return false;
        }
        return true;
    }
};

}  // namespace

bool findPath(const World& world, const Vec3& from, const Vec3& toIn, std::vector<Vec3>& out) {
    out.clear();
    Vec3 to = toIn;
    Vec3 delta(to.x - from.x, 0, to.z - from.z);
    float dist = length(delta);
    bool hop = dist > NAV_MAX_REACH;
    if (hop) to = from + delta * (NAV_MAX_REACH / dist);

    // Grid window: the start and goal plus a margin, kept inside the map.
    const float lim = MAP_HALF - 1.0f;
    NavGrid g;
    g.world = &world;
    g.minX = clampf(std::min(from.x, to.x) - NAV_MARGIN, -lim, lim);
    g.minZ = clampf(std::min(from.z, to.z) - NAV_MARGIN, -lim, lim);
    float maxX = clampf(std::max(from.x, to.x) + NAV_MARGIN, -lim, lim);
    float maxZ = clampf(std::max(from.z, to.z) + NAV_MARGIN, -lim, lim);
    g.w = std::max(1, (int)std::ceil((maxX - g.minX) / NAV_CELL));
    g.h = std::max(1, (int)std::ceil((maxZ - g.minZ) / NAV_CELL));
    g.blocked.assign((size_t)g.w * g.h, 0);
    g.ground.assign((size_t)g.w * g.h, NAN);

    // Mark every cell where a solid sits at body height, grown by the body radius.
    float lowY = std::min(from.y, to.y) - 20.0f, highY = std::max(from.y, to.y) + 20.0f;
    std::vector<AABB> solids;
    world.collectSolids(AABB{{g.minX, lowY, g.minZ}, {maxX, highY, maxZ}}, solids);
    for (const AABB& b : solids) {
        int i0 = std::max(0, (int)std::ceil((b.min.x - NAV_BODY_RADIUS - g.minX) / NAV_CELL - 0.5f));
        int i1 = std::min(g.w - 1, (int)std::floor((b.max.x + NAV_BODY_RADIUS - g.minX) / NAV_CELL - 0.5f));
        int j0 = std::max(0, (int)std::ceil((b.min.z - NAV_BODY_RADIUS - g.minZ) / NAV_CELL - 0.5f));
        int j1 = std::min(g.h - 1, (int)std::floor((b.max.z + NAV_BODY_RADIUS - g.minZ) / NAV_CELL - 0.5f));
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                uint8_t& bl = g.blocked[g.index(i, j)];
                if (bl) continue;
                float gy = g.groundAt(i, j);
                if (b.max.y > gy + NAV_STEP && b.min.y < gy + NAV_BODY_HEIGHT) bl = 1;
            }
    }

    int si = std::clamp(g.cellI(from.x), 0, g.w - 1), sj = std::clamp(g.cellJ(from.z), 0, g.h - 1);
    int gi = std::clamp(g.cellI(to.x), 0, g.w - 1), gj = std::clamp(g.cellJ(to.z), 0, g.h - 1);
    g.blocked[g.index(si, sj)] = 0;   // a bot hugging a wall still has to start somewhere
    // Goal inside something (a chest, a boxed-up enemy)? Aim for the nearest free cell instead.
    bool goalExact = !g.blocked[g.index(gi, gj)];
    if (!goalExact) {
        int bestD = -1, bi = gi, bj = gj;
        for (int dj = -6; dj <= 6; ++dj)
            for (int di = -6; di <= 6; ++di) {
                int d2 = di * di + dj * dj;
                if (g.free(gi + di, gj + dj) && (bestD < 0 || d2 < bestD)) { bestD = d2; bi = gi + di; bj = gj + dj; }
            }
        gi = bi;
        gj = bj;
    }

    // A* with 8 neighbours. Diagonal steps can't cut a blocked corner.
    const int n = g.w * g.h;
    const int start = g.index(si, sj), goal = g.index(gi, gj);
    std::vector<float> cost(n, 1e30f);
    std::vector<int> parent(n, -1);
    std::vector<uint8_t> closed(n, 0);
    auto heuristic = [&](int idx) {
        float dx = std::fabs((float)(idx % g.w - gi)), dz = std::fabs((float)(idx / g.w - gj));
        return (dx + dz + (1.41421356f - 2.0f) * std::min(dx, dz)) * NAV_CELL;  // octile distance
    };
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    cost[start] = 0;
    open.push({heuristic(start), start});
    int best = start;
    float bestH = heuristic(start);
    int expansions = 0;
    static const int DI[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DJ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty() && expansions < NAV_MAX_EXPANSIONS) {
        int cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        ++expansions;
        float h = heuristic(cur);
        if (h < bestH) { bestH = h; best = cur; }
        if (cur == goal) break;
        int ci = cur % g.w, cj = cur / g.w;
        for (int k = 0; k < 8; ++k) {
            int ni = ci + DI[k], nj = cj + DJ[k];
            if (!g.free(ni, nj)) continue;
            if (k >= 4 && (!g.free(ci + DI[k], cj) || !g.free(ci, cj + DJ[k]))) continue;
            int nb = g.index(ni, nj);
            float c = cost[cur] + (k >= 4 ? 1.41421356f : 1.0f) * NAV_CELL;
            if (c < cost[nb]) {
                cost[nb] = c;
                parent[nb] = cur;
                open.push({c + heuristic(nb), nb});
            }
        }
    }
    // No way through: still walk to the reachable cell closest to the goal, if that's progress.
    int end = closed[goal] ? goal : best;
    if (end == start || (end != goal && bestH > heuristic(start) - 1.0f)) return false;

    std::vector<int> cells;
    for (int c = end; c != -1; c = parent[c]) cells.push_back(c);
    std::reverse(cells.begin(), cells.end());

    // Smooth: from each corner, jump to the furthest cell still in a straight clear line.
    size_t anchor = 0;
    while (anchor + 1 < cells.size()) {
        size_t next = anchor + 1;
        int ai = cells[anchor] % g.w, aj = cells[anchor] / g.w;
        for (size_t k = cells.size() - 1; k > anchor + 1; --k)
            if (g.lineClear(ai, aj, cells[k] % g.w, cells[k] / g.w)) { next = k; break; }
        int ni = cells[next] % g.w, nj = cells[next] / g.w;
        out.push_back(Vec3(g.cx(ni), g.groundAt(ni, nj), g.cz(nj)));
        anchor = next;
    }
    if (end == goal && goalExact && !hop) out.back() = toIn;  // finish exactly on the goal
    return !out.empty();
}

}  // namespace bf
