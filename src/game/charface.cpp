// Blockfall - facial expressions (see charface.h)
#include "charface.h"
#include <cmath>

namespace bf {

const char* expressionName(Expression e) {
    static const char* names[(int)Expression::Count] = {"NEUTRAL", "FOCUSED", "EFFORT", "HURT", "HAPPY", "PAIN"};
    int i = (int)e;
    return i >= 0 && i < (int)Expression::Count ? names[i] : "?";
}

FaceControls expressionControls(Expression e) {
    FaceControls c;
    switch (e) {
        case Expression::Focused: c.browUp = -0.4f; c.browAngle = 0.5f; c.lids = 0.25f; c.smile = -0.1f; break;   // aiming
        case Expression::Effort: c.browUp = -0.2f; c.browAngle = 0.3f; c.lids = 0.15f; c.mouthOpen = 0.45f; break;  // sprinting, swinging
        case Expression::Hurt: c.browUp = 0.3f; c.browAngle = -0.6f; c.lids = 0.45f; c.mouthOpen = 0.35f; c.smile = -0.5f; break;
        case Expression::Happy: c.browUp = 0.5f; c.lids = 0.1f; c.mouthOpen = 0.15f; c.smile = 1.0f; break;          // lobby, dance
        case Expression::Pain: c.browUp = 0.1f; c.browAngle = -0.8f; c.lids = 0.7f; c.mouthOpen = 0.6f; c.smile = -0.8f; break;  // knocked
        default: break;
    }
    return c;
}

void FaceRig::build(const CharacterModel& m) {
    jaw = m.boneIndex("Jaw");
    browL = m.boneIndex("Brow_L");
    browR = m.boneIndex("Brow_R");
    lidL = m.boneIndex("Lid_L");
    lidR = m.boneIndex("Lid_R");
    mouthL = m.boneIndex("Mouth_L");
    mouthR = m.boneIndex("Mouth_R");
}

namespace {
float rand01(unsigned& s) {
    s = s * 1664525u + 1013904223u;
    return (float)((s >> 8) & 0xFFFF) / 65535.0f;
}
float ease(float now, float target, float k) { return now + (target - now) * k; }
}  // namespace

float faceLidClosure(const FaceState& st) {
    float blink = st.blinkT >= 0 ? std::sin(clampf(st.blinkT / BLINK_TIME, 0.0f, 1.0f) * PI) : 0.0f;
    return clampf(std::max(st.now.lids, blink), 0.0f, 1.0f);
}

void applyFace(const CharacterModel& m, const FaceRig& rig, FaceState& st, Expression target, float dt, Pose& pose) {
    if (!rig.valid()) return;
    FaceControls t = expressionControls(target);
    if (!st.init) {
        st.now = t;
        st.blinkClock = BLINK_GAP_MIN + rand01(st.seed) * (BLINK_GAP_MAX - BLINK_GAP_MIN);
        st.init = true;
    }
    float k = 1.0f - std::exp(-FACE_EASE * dt);
    st.now.browUp = ease(st.now.browUp, t.browUp, k);
    st.now.browAngle = ease(st.now.browAngle, t.browAngle, k);
    st.now.lids = ease(st.now.lids, t.lids, k);
    st.now.mouthOpen = ease(st.now.mouthOpen, t.mouthOpen, k);
    st.now.smile = ease(st.now.smile, t.smile, k);
    // Blinking.
    if (st.blinkT >= 0) {
        st.blinkT += dt;
        if (st.blinkT > BLINK_TIME) st.blinkT = -1;
    } else {
        st.blinkClock -= dt;
        if (st.blinkClock <= 0) {
            st.blinkT = 0;
            st.blinkClock = BLINK_GAP_MIN + rand01(st.seed) * (BLINK_GAP_MAX - BLINK_GAP_MIN);
        }
    }

    // Character frame: x = the character's right, y up, z forward.
    const Vec3 side(1, 0, 0);
    float lid = faceLidClosure(st);
    m.rotateInCharacterFrame(rig.lidL, quatAxisAngle(side, lid * FACE_LID_ANGLE), pose);
    m.rotateInCharacterFrame(rig.lidR, quatAxisAngle(side, lid * FACE_LID_ANGLE), pose);
    m.offsetInCharacterFrame(rig.browL, Vec3(0, st.now.browUp * FACE_BROW_LIFT, 0), pose);
    m.offsetInCharacterFrame(rig.browR, Vec3(0, st.now.browUp * FACE_BROW_LIFT, 0), pose);
    // Angry: the inner ends (toward the nose) go down. Brow_L is on the character's left (-x).
    const Vec3 fwd(0, 0, 1);
    m.rotateInCharacterFrame(rig.browL, quatAxisAngle(fwd, st.now.browAngle * FACE_BROW_TILT), pose);
    m.rotateInCharacterFrame(rig.browR, quatAxisAngle(fwd, -st.now.browAngle * FACE_BROW_TILT), pose);
    m.rotateInCharacterFrame(rig.jaw, quatAxisAngle(side, st.now.mouthOpen * FACE_JAW_ANGLE), pose);
    Vec3 corner(0, st.now.smile * FACE_SMILE_LIFT, -std::fabs(st.now.smile) * 0.001f);
    m.offsetInCharacterFrame(rig.mouthL, corner, pose);
    m.offsetInCharacterFrame(rig.mouthR, corner, pose);
}

}  // namespace bf
