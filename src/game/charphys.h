// Blockfall - secondary motion: long hair, capes and scarves swing with simple spring physics
//
// Each chain is a row of bones in the skeleton ("Cape_1", "Cape_2", ... "Cape_end"). Its first
// joint follows the animated body; the rest are points simulated with Verlet integration:
// gravity, damping, a spring pulling back toward the animated pose, fixed segment lengths and a
// capsule around the torso so a cape doesn't swing through the body. The bones are then turned
// to point along the simulated chain.
#pragma once
#include "model.h"

namespace bf {

constexpr int SWING_MAX_CHAINS = 4;
constexpr int SWING_MAX_POINTS = 6;   // joints per chain, including the fixed first one
constexpr float SWING_GRAVITY = 9.8f;
constexpr float SWING_DAMPING = 0.9f;        // velocity kept per 1/60 s
constexpr float SWING_TELEPORT = 0.8f;       // metres in one step: more than this (or SWING_MAX_SPEED) resets the chain
constexpr float SWING_MAX_SPEED = 45.0f;     // m/s, faster than anyone moves (skydiving is ~30)
constexpr float SWING_AIR_DRAG = 1.6f;       // the air pushes against the character's motion (per second)
constexpr float SWING_MAX_DRAG = 30.0f;      // m/s^2 cap (skydiving makes a cape stream upward)

struct SwingChainDef {
    int bones[SWING_MAX_POINTS];   // root .. end joint
    int count = 0;
    float stiffness = 0.1f;        // pull toward the animated pose per 1/60 s (0 = limp, 1 = rigid)
    float bodyRadius = 0.0f;       // > 0: keep the points this far from the spine (capes)
};

// The chains a model has (found by bone name). Empty for models without swing bones.
struct SwingRig {
    SwingChainDef chains[SWING_MAX_CHAINS];
    int count = 0;
    int hips = -1, neck = -1;      // spine capsule ends
    void build(const CharacterModel& m);
};

// Per-character simulation state (one per animated character).
struct SwingState {
    bool init = false;
    Vec3 pos[SWING_MAX_CHAINS][SWING_MAX_POINTS];
    Vec3 prev[SWING_MAX_CHAINS][SWING_MAX_POINTS];
    Vec3 lastRoot;            // where the model was last frame (its velocity drives the air drag)
};

// Steps the simulation and bends the chain bones in `pose`. `root` places the model in the world
// (the simulation runs in world space, so walking and turning drag the cape behind). `active`
// chains are bit flags (bit i = rig.chains[i]); inactive chains keep the animated pose.
void applySwing(const CharacterModel& m, const SwingRig& rig, SwingState& st, const Mat4& root, float dt,
                unsigned active, Pose& pose);

}  // namespace bf
