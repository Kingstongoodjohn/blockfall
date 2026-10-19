// Blockfall - facial expressions and blinking
//
// The face parts (eyes, lids, brows, lips) hang on seven face bones: Jaw, Brow_L/R, Lid_L/R,
// Mouth_L/R. An expression is a handful of controls (brows up, brow angle, lids closed, mouth
// open, smile); the face eases toward the expression the game asks for and blinks now and then.
#pragma once
#include "model.h"

namespace bf {

enum class Expression : int { Neutral = 0, Focused, Effort, Hurt, Happy, Pain, Count };
const char* expressionName(Expression e);

struct FaceControls {
    float browUp = 0;      // -1 frown .. 1 raised
    float browAngle = 0;   // + angry (inner ends down) .. - worried
    float lids = 0;        // 0 open .. 1 closed
    float mouthOpen = 0;   // 0 .. 1
    float smile = 0;       // -1 frown .. 1 smile
};
FaceControls expressionControls(Expression e);

constexpr float FACE_EASE = 9.0f;          // how fast the face changes expression (per second)
constexpr float BLINK_TIME = 0.14f;        // seconds an eye blink takes
constexpr float BLINK_GAP_MIN = 2.2f;      // seconds between blinks (random in this range)
constexpr float BLINK_GAP_MAX = 5.5f;
// How far the bones move at full strength.
constexpr float FACE_LID_ANGLE = 1.05f;    // radians the upper lid swings down to close
constexpr float FACE_BROW_LIFT = 0.006f;   // metres
constexpr float FACE_BROW_TILT = 0.35f;    // radians
constexpr float FACE_JAW_ANGLE = 0.32f;    // radians
constexpr float FACE_SMILE_LIFT = 0.004f;  // metres the mouth corners move up

struct FaceRig {
    int jaw = -1, browL = -1, browR = -1, lidL = -1, lidR = -1, mouthL = -1, mouthR = -1;
    bool valid() const { return jaw >= 0 && browL >= 0 && browR >= 0 && lidL >= 0 && lidR >= 0 && mouthL >= 0 && mouthR >= 0; }
    void build(const CharacterModel& m);
};

struct FaceState {
    bool init = false;
    FaceControls now;
    float blinkClock = 0;     // counts down to the next blink
    float blinkT = -1;        // >= 0 while blinking
    unsigned seed = 1;
};

// Advances the face by dt toward `target` and poses the face bones in `pose`.
void applyFace(const CharacterModel& m, const FaceRig& rig, FaceState& st, Expression target, float dt, Pose& pose);
// The lid closure this frame (expression + blink), for tests.
float faceLidClosure(const FaceState& st);

}  // namespace bf
