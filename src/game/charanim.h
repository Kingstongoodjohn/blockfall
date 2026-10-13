// Blockfall - the animation state system for characters
//
// Picks clips from what a character is doing, blends between them when the action changes,
// layers upper-body actions (aiming, swinging, building) over the legs' locomotion, and adds
// the aim pitch procedurally so the gun follows the crosshair up and down.
#pragma once
#include "model.h"
#include "scene.h"

namespace bf {

struct CharAnimInput {
    Anim anim = Anim::Idle;
    bool gun = false;         // holding a gun
    bool pickaxe = false;     // holding the pickaxe
    float moveAmount = 0;     // 0..1 how much the character is moving
    float walkPhase = 0;      // radians, drives the step cycle so feet match the ground speed
    float aimPitch = 0;       // radians, + looks up
    float reloadFrac = 0;     // 0..1 through a reload
    float swing = -1;         // pickaxe swing 0..1 or -1
    float shot = 0;           // recoil 0..1 (1 right after firing)
    float animT = 0;          // seconds into a one-shot action (jump, build)
    float clock = 0;          // a clock that advances with game time (for blending)
    bool lobby = false;       // lobby stage: wave / dance instead of combat poses
    bool dance = false;
};

struct CharAnimState {
    bool init = false;
    int lower = -1, upper = -1;
    int prevLower = -1, prevUpper = -1;
    float lowerT = 0, upperT = 0, prevLowerT = 0, prevUpperT = 0;
    bool lowerLoop = true, upperLoop = true, prevLowerLoop = true, prevUpperLoop = true;
    float lowerBlend = 1, upperBlend = 1;
    float lastClock = 0;
    float freeT = 0;          // seconds of free-running clip time
    float lowerStart = 0;     // clock when the lower clip started (one-shots like the jump)
};

// Works out the pose for this frame. Returns false if the model has no clips.
bool evalCharacterPose(const CharacterModel& m, CharAnimState& st, const CharAnimInput& in, Pose& out);

// True for bones above the hips (the layer that plays actions over the legs).
bool isUpperBodyBone(const CharacterModel& m, int bone);

constexpr float ANIM_BLEND_TIME = 0.12f;

}  // namespace bf
