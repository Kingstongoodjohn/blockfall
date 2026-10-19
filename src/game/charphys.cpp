// Blockfall - secondary motion (see charphys.h)
#include "charphys.h"
#include <cmath>
#include <string>

namespace bf {

void SwingRig::build(const CharacterModel& m) {
    count = 0;
    struct Spec { const char* prefix; float stiffness; float bodyRadius; };
    // Hair springs back quickest; a cape is heavy and slow; a scarf tail is in between.
    static const Spec specs[] = {{"HairTail", 0.10f, 0.0f}, {"Cape", 0.04f, 0.19f}, {"Scarf", 0.06f, 0.0f}};
    for (const Spec& s : specs) {
        if (count >= SWING_MAX_CHAINS) break;
        SwingChainDef c;
        c.stiffness = s.stiffness;
        c.bodyRadius = s.bodyRadius;
        for (int i = 1; c.count < SWING_MAX_POINTS - 1; ++i) {
            int b = m.boneIndex((std::string(s.prefix) + "_" + std::to_string(i)).c_str());
            if (b < 0) break;
            c.bones[c.count++] = b;
        }
        int end = m.boneIndex((std::string(s.prefix) + "_end").c_str());
        if (end >= 0) c.bones[c.count++] = end;
        if (c.count >= 2) chains[count++] = c;
    }
    hips = m.boneIndex("Spine");   // its head sits at the hip joint
    neck = m.boneIndex("Head");    // its head sits at the neck joint
}

namespace {

Vec3 directionToModel(const Vec3& d, const Mat4& invRoot) {
    return Vec3(d.x * invRoot.m[0][0] + d.y * invRoot.m[1][0] + d.z * invRoot.m[2][0],
                d.x * invRoot.m[0][1] + d.y * invRoot.m[1][1] + d.z * invRoot.m[2][1],
                d.x * invRoot.m[0][2] + d.y * invRoot.m[1][2] + d.z * invRoot.m[2][2]);
}

// Pushes p out of a capsule (segment a-b, radius r).
void pushOutOfCapsule(Vec3& p, const Vec3& a, const Vec3& b, float r) {
    Vec3 ab = b - a;
    float t = clampf(dot(p - a, ab) / std::max(dot(ab, ab), 1e-6f), 0.0f, 1.0f);
    Vec3 c = a + ab * t;
    Vec3 d = p - c;
    float dist = length(d);
    if (dist >= r) return;
    if (dist < 1e-5f) {
        d = Vec3(0, 0, -1);
        dist = 1;
    }
    p = c + d * (r / dist);
}

}  // namespace

void applySwing(const CharacterModel& m, const SwingRig& rig, SwingState& st, const Mat4& root, float dt,
                unsigned active, Pose& pose) {
    if (rig.count == 0) return;
    float step = clampf(dt, 0.0f, 1.0f / 30.0f);
    Mat4 world[MAX_BONES];
    m.boneWorld(pose, world);
    Mat4 invRoot = inverse(root);
    auto worldPos = [&](int bone) { return transformPoint(m.bonePosition(world, bone), root); };
    Vec3 spineA = rig.hips >= 0 ? worldPos(rig.hips) : Vec3();
    Vec3 spineB = rig.neck >= 0 ? worldPos(rig.neck) : Vec3();
    // Frame-rate independent spring and damping (the constants are per 1/60 s).
    float frames = step * 60.0f;
    float damp = std::pow(SWING_DAMPING, frames);
    // Air drag against the character's own motion: running drags a cape back, falling lifts it.
    Vec3 rootNow = transformPoint(Vec3(0, 1, 0), root);
    // Moved further than anyone can in one step: a teleport (respawn, the demo placing someone).
    float teleport = std::max(SWING_TELEPORT, SWING_MAX_SPEED * std::max(dt, step));
    bool jumped = st.init && length(rootNow - st.lastRoot) > teleport;
    if (jumped) st.init = false;
    Vec3 drag(0, 0, 0);
    if (st.init && step > 1e-5f) {
        drag = (rootNow - st.lastRoot) * (-SWING_AIR_DRAG / step);
        float dl = length(drag);
        if (dl > SWING_MAX_DRAG) drag = drag * (SWING_MAX_DRAG / dl);
    }
    st.lastRoot = rootNow;

    for (int c = 0; c < rig.count; ++c) {
        const SwingChainDef& ch = rig.chains[c];
        Vec3* pos = st.pos[c];
        Vec3* prev = st.prev[c];
        Vec3 target[SWING_MAX_POINTS];
        for (int i = 0; i < ch.count; ++i) target[i] = worldPos(ch.bones[i]);
        bool reset = !st.init || !((active >> c) & 1u);
        if (reset) {
            for (int i = 0; i < ch.count; ++i) pos[i] = prev[i] = target[i];
            if (!((active >> c) & 1u)) continue;
        }
        float pull = 1.0f - std::pow(1.0f - ch.stiffness, frames);
        pos[0] = prev[0] = target[0];
        for (int i = 1; i < ch.count; ++i) {
            Vec3 vel = (pos[i] - prev[i]) * damp;
            prev[i] = pos[i];
            pos[i] = pos[i] + vel + (Vec3(0, -SWING_GRAVITY, 0) + drag) * (step * step);
            pos[i] = pos[i] + (target[i] - pos[i]) * pull;
        }
        for (int iter = 0; iter < 3; ++iter) {
            for (int i = 1; i < ch.count; ++i) {
                float len = length(target[i] - target[i - 1]);
                Vec3 d = pos[i] - pos[i - 1];
                float dl = length(d);
                pos[i] = dl > 1e-6f ? pos[i - 1] + d * (len / dl) : target[i];
                if (ch.bodyRadius > 0 && rig.hips >= 0 && rig.neck >= 0) pushOutOfCapsule(pos[i], spineA, spineB, ch.bodyRadius);
            }
        }
        // Turn each bone from where the animation has it toward the simulated chain.
        for (int i = 0; i + 1 < ch.count; ++i) {
            Vec3 have = worldPos(ch.bones[i + 1]) - worldPos(ch.bones[i]);
            Vec3 want = pos[i + 1] - pos[i];
            if (length(have) < 1e-5f || length(want) < 1e-5f) continue;
            Quat q = quatFromTo(directionToModel(have, invRoot), directionToModel(want, invRoot));
            m.rotateBoneInModel(ch.bones[i], q, world, pose);
            m.boneWorld(pose, world);
        }
    }
    st.init = true;
}

}  // namespace bf
