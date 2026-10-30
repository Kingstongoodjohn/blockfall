// Blockfall - occlusion culling (see occlusion.h)
#include "occlusion.h"
#include "scene.h"
#include "world.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace bf {

namespace {
const float INF = std::numeric_limits<float>::infinity();
constexpr int BW = (OCC_W + OCC_BLOCK - 1) / OCC_BLOCK;
constexpr int BH = (OCC_H + OCC_BLOCK - 1) / OCC_BLOCK;

Vec4 clipOf(const Vec3& p, const Mat4& m) { return transform(Vec4(p.x, p.y, p.z, 1.0f), m); }
}  // namespace

void OcclusionCuller::begin(const Mat4& view, const Mat4& proj, const Vec3& eye) {
    view_ = view;
    vp_ = view * proj;
    p00_ = proj.m[0][0];
    p11_ = proj.m[1][1];
    eye_ = eye;
    depth_.assign((size_t)OCC_W * OCC_H, INF);
    blockMax_.assign((size_t)BW * BH, INF);
    occluderTriangles = 0;
    tested = culledFrustum = culledOccluded = 0;
    // Frustum planes from the columns of viewProj (Direct3D clip space: -w<=x<=w, -w<=y<=w, 0<=z<=w).
    auto col = [&](int j) { return Vec4(vp_.m[0][j], vp_.m[1][j], vp_.m[2][j], vp_.m[3][j]); };
    auto add = [](const Vec4& a, const Vec4& b, float k) { return Vec4(a.x + b.x * k, a.y + b.y * k, a.z + b.z * k, a.w + b.w * k); };
    Vec4 c0 = col(0), c1 = col(1), c2 = col(2), c3 = col(3);
    Vec4 p[6] = {add(c3, c0, 1), add(c3, c0, -1), add(c3, c1, 1), add(c3, c1, -1), c2, add(c3, c2, -1)};
    for (int i = 0; i < 6; ++i) {
        float len = std::sqrt(p[i].x * p[i].x + p[i].y * p[i].y + p[i].z * p[i].z);
        planes_[i] = len > 1e-8f ? Vec4(p[i].x / len, p[i].y / len, p[i].z / len, p[i].w / len) : p[i];
    }
}

void OcclusionCuller::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec4 v[3] = {clipOf(a, vp_), clipOf(b, vp_), clipOf(c, vp_)};
    // Cheap reject: all three outside the same side of the view.
    for (int axis = 0; axis < 2; ++axis) {
        bool allLow = true, allHigh = true;
        for (const Vec4& q : v) {
            float x = axis == 0 ? q.x : q.y;
            allLow = allLow && x < -q.w;
            allHigh = allHigh && x > q.w;
        }
        if (allLow || allHigh) return;
    }
    if (v[0].w < OCC_NEAR && v[1].w < OCC_NEAR && v[2].w < OCC_NEAR) return;
    rasterClipped(v, 3);
}

// Clips a polygon against w >= OCC_NEAR, then fans it into triangles.
void OcclusionCuller::rasterClipped(const Vec4* in, int n) {
    Vec4 out[8];
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const Vec4& a = in[i];
        const Vec4& b = in[(i + 1) % n];
        bool ain = a.w >= OCC_NEAR, bin = b.w >= OCC_NEAR;
        if (ain) out[m++] = a;
        if (ain != bin) {
            float t = (OCC_NEAR - a.w) / (b.w - a.w);
            out[m++] = Vec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, OCC_NEAR);
        }
    }
    for (int i = 1; i + 1 < m; ++i) rasterTri(out[0], out[i], out[i + 1]);
}

void OcclusionCuller::rasterTri(const Vec4& a, const Vec4& b, const Vec4& c) {
    ++occluderTriangles;
    struct S { float x, y, iw; };
    auto toScreen = [](const Vec4& v) {
        float iw = 1.0f / v.w;
        return S{(v.x * iw * 0.5f + 0.5f) * OCC_W, (0.5f - v.y * iw * 0.5f) * OCC_H, iw};
    };
    S p0 = toScreen(a), p1 = toScreen(b), p2 = toScreen(c);
    float area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
    if (std::fabs(area) < 1e-6f) return;
    if (area < 0) { std::swap(p1, p2); area = -area; }
    int x0 = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
    int x1 = std::min(OCC_W - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x})));
    int y0 = std::max(0, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
    int y1 = std::min(OCC_H - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y})));
    if (x0 > x1 || y0 > y1) return;
    float inv = 1.0f / area;
    // Edge functions at the first pixel centre, stepped by their x/y slopes.
    auto edge = [](const S& a, const S& b, float px, float py) { return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x); };
    float sx = x0 + 0.5f, sy = y0 + 0.5f;
    float e0r = edge(p1, p2, sx, sy), e1r = edge(p2, p0, sx, sy), e2r = edge(p0, p1, sx, sy);
    float e0dx = -(p2.y - p1.y), e1dx = -(p0.y - p2.y), e2dx = -(p1.y - p0.y);
    float e0dy = p2.x - p1.x, e1dy = p0.x - p2.x, e2dy = p1.x - p0.x;
    for (int y = y0; y <= y1; ++y) {
        float w0 = e0r, w1 = e1r, w2 = e2r;
        float* row = &depth_[(size_t)y * OCC_W];
        for (int x = x0; x <= x1; ++x) {
            // Only pixels whose centre is inside: occluders never cover more than they really do.
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                float iw = (w0 * p0.iw + w1 * p1.iw + w2 * p2.iw) * inv;
                float d = 1.0f / std::max(iw, 1e-6f);
                if (d < row[x]) row[x] = d;
            }
            w0 += e0dx; w1 += e1dx; w2 += e2dx;
        }
        e0r += e0dy; e1r += e1dy; e2r += e2dy;
    }
}

void OcclusionCuller::addBox(const AABB& b) {
    Vec3 s = b.size();
    int big = (s.x >= OCC_MIN_OCCLUDER) + (s.y >= OCC_MIN_OCCLUDER) + (s.z >= OCC_MIN_OCCLUDER);
    if (big < 2) return;
    Vec3 bc = b.center();
    if (dot(bc - eye_, bc - eye_) > OCC_MAX_OCCLUDER_DIST * OCC_MAX_OCCLUDER_DIST) return;   // far walls hide little
    if (!inFrustum(bc, length(s) * 0.5f)) return;
    Vec3 c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = Vec3(i & 1 ? b.max.x : b.min.x, i & 2 ? b.max.y : b.min.y, i & 4 ? b.max.z : b.min.z);
    // Only the faces turned toward the camera matter (the far ones are hidden behind them).
    static const int faces[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
    bool facing[6] = {eye_.x < b.min.x, eye_.x > b.max.x, eye_.y < b.min.y, eye_.y > b.max.y, eye_.z < b.min.z, eye_.z > b.max.z};
    bool inside = !facing[0] && !facing[1] && !facing[2] && !facing[3] && !facing[4] && !facing[5];
    if (inside) return;   // standing inside a box: it hides nothing useful
    for (int f = 0; f < 6; ++f) {
        if (!facing[f]) continue;
        Vec4 q[4];
        for (int k = 0; k < 4; ++k) q[k] = clipOf(c[faces[f][k]], vp_);
        rasterClipped(q, 4);
    }
}

void OcclusionCuller::addGround(const World& w) {
    // A coarse grid whose corners sit below every real height around them, so its flat triangles
    // stay under the bumpy real ground everywhere (never hide something standing on a hill top).
    if (groundGen_ != w.generation) {
        groundGen_ = w.generation;
        groundCell_ = OCC_GROUND_CELL;
        groundX0_ = -MAP_HALF;
        groundN_ = (int)std::ceil(2.0f * MAP_HALF / groundCell_) + 1;
        groundH_.assign((size_t)groundN_ * groundN_, 0.0f);
        const float step = 2.0f;
        for (int j = 0; j < groundN_; ++j)
            for (int i = 0; i < groundN_; ++i) {
                float cx = groundX0_ + i * groundCell_, cz = groundX0_ + j * groundCell_;
                float lo = INF;
                for (float dz = -groundCell_; dz <= groundCell_ + 1e-3f; dz += step)
                    for (float dx = -groundCell_; dx <= groundCell_ + 1e-3f; dx += step) {
                        float x = clampf(cx + dx, -MAP_HALF, MAP_HALF), z = clampf(cz + dz, -MAP_HALF, MAP_HALF);
                        lo = std::min(lo, w.terrainHeight(x, z));
                    }
                groundH_[(size_t)j * groundN_ + i] = lo - OCC_GROUND_SINK;
            }
    }
    auto P = [&](int i, int j) { return Vec3(groundX0_ + i * groundCell_, groundH_[(size_t)j * groundN_ + i], groundX0_ + j * groundCell_); };
    // Blocks of 8x8 cells first, so most of the map is skipped with one frustum test. Ground close
    // to the camera is skipped too: it only hides things under the ground; hills further away are
    // what hide whole villages.
    const int B = 8;
    float nearSkip = OCC_GROUND_NEAR;
    for (int bj = 0; bj + 1 < groundN_; bj += B)
        for (int bi = 0; bi + 1 < groundN_; bi += B) {
            int i1 = std::min(bi + B, groundN_ - 1), j1 = std::min(bj + B, groundN_ - 1);
            float lo = INF, hi = -INF;
            for (int j = bj; j <= j1; ++j)
                for (int i = bi; i <= i1; ++i) {
                    float h = groundH_[(size_t)j * groundN_ + i];
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                }
            Vec3 bmin = P(bi, bj), bmax = P(i1, j1);
            Vec3 c((bmin.x + bmax.x) * 0.5f, (lo + hi) * 0.5f, (bmin.z + bmax.z) * 0.5f);
            float r = 0.5f * std::sqrt((bmax.x - bmin.x) * (bmax.x - bmin.x) + (bmax.z - bmin.z) * (bmax.z - bmin.z) + (hi - lo) * (hi - lo));
            if (!inFrustum(c, r)) continue;
            for (int j = bj; j < j1; ++j)
                for (int i = bi; i < i1; ++i) {
                    Vec3 a = P(i, j), b = P(i + 1, j), cc = P(i + 1, j + 1), d = P(i, j + 1);
                    float dx = std::max({a.x - eye_.x, eye_.x - cc.x, 0.0f}), dz = std::max({a.z - eye_.z, eye_.z - cc.z, 0.0f});
                    if (dx * dx + dz * dz < nearSkip * nearSkip) continue;
                    Vec3 mid = (a + cc) * 0.5f;
                    float hy = std::max({a.y, b.y, cc.y, d.y}) - std::min({a.y, b.y, cc.y, d.y});
                    if (!inFrustum(mid, groundCell_ * 0.75f + hy)) continue;
                    addTriangle(a, b, cc);
                    addTriangle(a, cc, d);
                }
        }
}

void OcclusionCuller::finish() {
    for (int by = 0; by < BH; ++by)
        for (int bx = 0; bx < BW; ++bx) {
            float mx = 0;
            for (int y = by * OCC_BLOCK; y < std::min(OCC_H, (by + 1) * OCC_BLOCK); ++y)
                for (int x = bx * OCC_BLOCK; x < std::min(OCC_W, (bx + 1) * OCC_BLOCK); ++x)
                    mx = std::max(mx, depth_[(size_t)y * OCC_W + x]);
            blockMax_[(size_t)by * BW + bx] = mx;
        }
}

bool OcclusionCuller::inFrustum(const Vec3& c, float r) const {
    for (const Vec4& p : planes_)
        if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
    return true;
}

bool OcclusionCuller::sphereVisible(const Vec3& c, float r) const {
    ++tested;
    if (!inFrustum(c, r)) { ++culledFrustum; return false; }
    // Screen rectangle and nearest depth of the box around the sphere, from its view-space centre.
    Vec3 v = transformPoint(c, view_);
    float nearW = v.z - r, farW = v.z + r;
    if (nearW < OCC_NEAR) return true;   // touches the camera: just draw it
    auto range = [&](float a, float scale, float& lo, float& hi) {
        float x0 = a - r, x1 = a + r;
        float q0 = x0 / (x0 < 0 ? nearW : farW), q1 = x1 / (x1 > 0 ? nearW : farW);
        lo = q0 * scale;
        hi = q1 * scale;
    };
    float nx0, nx1, ny0, ny1;
    range(v.x, p00_, nx0, nx1);
    range(v.y, p11_, ny0, ny1);
    float minX = (nx0 * 0.5f + 0.5f) * OCC_W, maxX = (nx1 * 0.5f + 0.5f) * OCC_W;
    float minY = (0.5f - ny1 * 0.5f) * OCC_H, maxY = (0.5f - ny0 * 0.5f) * OCC_H;
    int x0 = std::max(0, (int)std::floor(minX) - 1), x1 = std::min(OCC_W - 1, (int)std::ceil(maxX) + 1);
    int y0 = std::max(0, (int)std::floor(minY) - 1), y1 = std::min(OCC_H - 1, (int)std::ceil(maxY) + 1);
    if (x0 > x1 || y0 > y1) { ++culledFrustum; return false; }
    for (int by = y0 / OCC_BLOCK; by <= y1 / OCC_BLOCK; ++by)
        for (int bx = x0 / OCC_BLOCK; bx <= x1 / OCC_BLOCK; ++bx) {
            if (blockMax_[(size_t)by * BW + bx] < nearW) continue;   // the whole block is in front
            int ya = std::max(y0, by * OCC_BLOCK), yb = std::min(y1, (by + 1) * OCC_BLOCK - 1);
            int xa = std::max(x0, bx * OCC_BLOCK), xb = std::min(x1, (bx + 1) * OCC_BLOCK - 1);
            for (int y = ya; y <= yb; ++y)
                for (int x = xa; x <= xb; ++x)
                    if (depth_[(size_t)y * OCC_W + x] >= nearW) return true;
        }
    ++culledOccluded;
    return false;
}

int OcclusionCuller::cullScene(SceneList& scene) const {
    int hidden = 0;
    auto filter = [&](std::vector<Instance>& list, std::vector<Instance>& shadow, bool castsShadow) {
        size_t keep = 0;
        for (size_t i = 0; i < list.size(); ++i) {
            const Mat4& m = list[i].world;
            // A sphere around the unit mesh box [-1.25, 1.25]^3 (every unit mesh fits in it).
            // (l0 + l1 + l2 <= sqrt(3 * (l0^2 + l1^2 + l2^2)): one square root instead of three.)
            float sq = m.m[0][0] * m.m[0][0] + m.m[0][1] * m.m[0][1] + m.m[0][2] * m.m[0][2] +
                       m.m[1][0] * m.m[1][0] + m.m[1][1] * m.m[1][1] + m.m[1][2] * m.m[1][2] +
                       m.m[2][0] * m.m[2][0] + m.m[2][1] * m.m[2][1] + m.m[2][2] * m.m[2][2];
            float r = 1.25f * std::sqrt(3.0f * sq);
            Vec3 c(m.m[3][0], m.m[3][1], m.m[3][2]);
            if (sphereVisible(c, r)) {
                list[keep++] = list[i];
            } else {
                ++hidden;
                if (castsShadow) shadow.push_back(list[i]);
            }
        }
        list.resize(keep);
    };
    for (int mesh = 0; mesh < MESH_COUNT; ++mesh) {
        if (mesh == MESH_TERRAIN) continue;
        filter(scene.opaque[mesh], scene.shadowOnly[mesh], true);
        filter(scene.transparent[mesh], scene.shadowOnly[mesh], false);   // see-through things cast no shadow
    }
    for (CharacterDraw& ch : scene.characters) {
        Vec3 c(ch.world.m[3][0], ch.world.m[3][1] + 1.0f, ch.world.m[3][2]);
        if (!sphereVisible(c, 1.4f)) {
            ch.shadowOnly = true;
            ++hidden;
        }
    }
    return hidden;
}

}  // namespace bf
