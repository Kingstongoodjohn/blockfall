// Blockfall - occlusion culling: don't draw what the camera can't see.
//
// Each frame the big solid things in front of the camera (the ground, house walls and floors,
// unedited build walls and floors) are drawn into a small depth picture on the CPU. Then every
// object is checked against that picture: if its bounding sphere is outside the view, or every
// pixel it covers already has something solid nearer to the camera, it is skipped. Hidden objects
// still cast shadows (the renderer draws them in the shadow pass only), so shadows don't pop.
//
// It is conservative: an occluder only covers the pixels whose centres it really covers, the
// ground is drawn a little below the real ground, and anything touching the near plane counts as
// visible. When in doubt, an object is drawn.
#pragma once
#include "../core/math.h"
#include <vector>

namespace bf {

class World;
struct SceneList;

constexpr int OCC_W = 256;              // depth picture size (16:9; any aspect works)
constexpr int OCC_H = 144;
constexpr int OCC_BLOCK = 8;            // pixels per side of a block in the coarse max-depth picture
constexpr float OCC_NEAR = 0.15f;       // occluders closer than this are clipped
constexpr float OCC_GROUND_CELL = 20.0f;  // spacing of the coarse ground used as an occluder (m)
constexpr float OCC_GROUND_SINK = 0.6f;   // the coarse ground sits this far under the lowest real ground
constexpr float OCC_GROUND_NEAR = 30.0f;  // ground closer than this is not used as an occluder
constexpr float OCC_MIN_OCCLUDER = 1.2f;  // boxes smaller than this (2 sides) don't occlude anything
constexpr float OCC_MAX_OCCLUDER_DIST = 220.0f;  // walls further away than this are skipped

class OcclusionCuller {
public:
    // Starts a new frame for this camera (row vectors like the rest; proj is a centred perspective).
    void begin(const Mat4& view, const Mat4& proj, const Vec3& eye);
    // Occluders (solid, opaque, closed).
    void addBox(const AABB& b);
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c);
    // The ground of the whole map, as a coarse heightfield that never pokes above the real one.
    void addGround(const World& w);
    // Call after the occluders and before the tests.
    void finish();

    // True if a sphere might be visible (inside the view and not behind occluders).
    bool sphereVisible(const Vec3& c, float r) const;
    bool inFrustum(const Vec3& c, float r) const;

    // Moves every hidden instance of `scene` (except the ground mesh itself) into its shadow-only
    // lists, and marks hidden characters shadow-only. Returns how many were hidden.
    int cullScene(SceneList& scene) const;

    // Stats for the last frame.
    int occluderTriangles = 0;
    mutable int tested = 0, culledFrustum = 0, culledOccluded = 0;

    const std::vector<float>& depth() const { return depth_; }   // view depth per pixel (tests)

private:
    Mat4 vp_, view_;
    float p00_ = 1, p11_ = 1;           // projection scale in x and y
    Vec3 eye_;
    Vec4 planes_[6];                    // frustum planes (a, b, c, d), inside when dot >= 0
    std::vector<float> depth_;          // nearest occluder view depth per pixel (inf = nothing)
    std::vector<float> blockMax_;       // farthest depth in each OCC_BLOCK block
    void rasterClipped(const Vec4* v, int n);
    void rasterTri(const Vec4& a, const Vec4& b, const Vec4& c);

    // Cached conservative ground grid (rebuilt when the world changes).
    int groundGen_ = -1;
    int groundN_ = 0;
    float groundX0_ = 0, groundCell_ = 0;
    std::vector<float> groundH_;
};

}  // namespace bf
