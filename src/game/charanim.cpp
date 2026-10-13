// Blockfall - animation state system
#include "charanim.h"
#include <cmath>
#include <cstring>

namespace bf {

bool isUpperBodyBone(const CharacterModel& m, int bone) {
    if (bone < 0 || bone >= (int)m.bones.size()) return false;
    const std::string& n = m.bones[bone].name;
    auto starts = [&](const char* p) { return n.compare(0, std::strlen(p), p) == 0; };
    return !(starts("Hips") || starts("Pelvis") || starts("Thigh") || starts("Shin") || starts("Foot"));
}

namespace {

struct Choice {
    int clip = -1;
    float time = 0;
    bool loop = true;
    bool phased = false;  // time comes from the step cycle, not the clock
    bool sinceStart = false;  // time counts from when the clip started
};

// Picks the legs/body clip.
Choice pickLower(const CharacterModel& m, const CharAnimInput& in, float freeT) {
    Choice c;
    auto set = [&](const char* name, bool loop, float t, bool phased = false) {
        c.clip = m.clipIndex(name);
        c.loop = loop;
        c.time = t;
        c.phased = phased;
    };
    const bool moving = in.moveAmount > 0.12f;
    float cycle = in.walkPhase / (2.0f * PI);
    if (in.lobby) {
        if (in.dance) set("dance", true, freeT);
        else set("idle", true, freeT);
        return c;
    }
    switch (in.anim) {
        case Anim::Jump: set("jump", false, 0); c.sinceStart = true; return c;
        case Anim::Skydive: set("skydive", true, freeT); return c;
        case Anim::Glide: set("glide", true, freeT); return c;
        case Anim::Sprint: set("sprint", true, cycle, true); return c;
        case Anim::Crouch:
            if (moving) set("crouch_walk", true, cycle, true);
            else set("crouch", true, freeT);
            return c;
        default: break;
    }
    if (moving) {
        set("walk", true, cycle, true);
        return c;
    }
    if (in.anim == Anim::Heal) set("heal", true, freeT);
    else if (in.anim == Anim::Build) set("build", true, freeT);
    else set("idle", true, freeT);
    return c;
}

// Picks the arms/upper body clip, or none (then the lower clip plays on the whole body).
Choice pickUpper(const CharacterModel& m, const CharAnimInput& in, float freeT) {
    Choice c;
    auto set = [&](const char* name, bool loop, float t) {
        c.clip = m.clipIndex(name);
        c.loop = loop;
        c.time = t;
    };
    if (in.lobby) {
        if (in.dance) return c;
        if (in.gun) set("aim", true, freeT);
        else if (in.pickaxe) set("pick_idle", true, freeT);
        else set("wave", true, freeT);
        return c;
    }
    switch (in.anim) {
        case Anim::Jump: case Anim::Skydive: case Anim::Glide: return c;  // whole body
        case Anim::Pickaxe: {
            const AnimClip* s = m.clip("swing");
            if (in.swing >= 0 && s) set("swing", false, in.swing * s->duration);
            else set("pick_idle", true, freeT);
            return c;
        }
        case Anim::Shoot: {
            const AnimClip* s = m.clip("shoot");
            set("shoot", false, s ? (1.0f - in.shot) * s->duration : 0.0f);
            return c;
        }
        case Anim::Reload: {
            const AnimClip* s = m.clip("reload");
            set("reload", false, s ? in.reloadFrac * s->duration : 0.0f);
            return c;
        }
        case Anim::Aim: set("aim", true, freeT); return c;
        case Anim::Build: set("build", true, freeT); return c;
        case Anim::Heal: set("heal", true, freeT); return c;
        case Anim::Sprint: return c;  // gun comes down while sprinting
        default: break;
    }
    if (in.gun) {
        if (in.shot > 0.0f) {
            const AnimClip* s = m.clip("shoot");
            set("shoot", false, s ? (1.0f - in.shot) * s->duration : 0.0f);
        } else {
            set("aim", true, freeT);
        }
        return c;
    }
    if (in.pickaxe) {
        const AnimClip* s = m.clip("swing");
        if (in.swing >= 0 && s) set("swing", false, in.swing * s->duration);
        else set("pick_idle", true, freeT);
        return c;
    }
    return c;
}

}  // namespace

bool evalCharacterPose(const CharacterModel& m, CharAnimState& st, const CharAnimInput& in, Pose& out) {
    if (!m.loaded || m.clips.empty()) return false;
    float dt = st.init ? clampf(in.clock - st.lastClock, 0.0f, 0.1f) : 0.0f;
    st.lastClock = in.clock;
    st.freeT += dt;

    Choice lo = pickLower(m, in, st.freeT);
    Choice up = pickUpper(m, in, st.freeT);

    if (!st.init) {
        st.lower = lo.clip; st.upper = up.clip;
        st.lowerT = lo.time; st.upperT = up.time;
        st.lowerLoop = lo.loop; st.upperLoop = up.loop;
        st.lowerBlend = st.upperBlend = 1.0f;
        st.init = true;
    }
    // A change of clip starts a cross-fade from where the old clip was.
    if (lo.clip != st.lower) {
        st.prevLower = st.lower; st.prevLowerT = st.lowerT; st.prevLowerLoop = st.lowerLoop;
        st.lower = lo.clip; st.lowerLoop = lo.loop; st.lowerBlend = 0.0f;
        st.lowerStart = in.clock;
    }
    if (up.clip != st.upper) {
        st.prevUpper = st.upper; st.prevUpperT = st.upperT; st.prevUpperLoop = st.upperLoop;
        st.upper = up.clip; st.upperLoop = up.loop; st.upperBlend = 0.0f;
    }
    if (lo.phased && st.lower >= 0) st.lowerT = lo.time * m.clips[st.lower].duration;
    else if (lo.sinceStart) st.lowerT = in.clock - st.lowerStart;
    else st.lowerT = lo.time;
    st.upperT = up.time;
    st.prevLowerT += dt;
    st.prevUpperT += dt;
    st.lowerBlend = std::min(1.0f, st.lowerBlend + dt / ANIM_BLEND_TIME);
    st.upperBlend = std::min(1.0f, st.upperBlend + dt / ANIM_BLEND_TIME);

    // Lower / whole body.
    Pose base;
    m.restPose(base);
    m.sampleClip(st.lower, st.lowerT, st.lowerLoop, base);
    if (st.lowerBlend < 1.0f && st.prevLower >= 0) {
        Pose prev;
        m.restPose(prev);
        m.sampleClip(st.prevLower, st.prevLowerT, st.prevLowerLoop, prev);
        Pose mixed;
        blendPose(prev, base, st.lowerBlend, mixed);
        base = mixed;
    }
    // Upper layer over it.
    static const CharacterModel* maskFor = nullptr;
    static bool mask[MAX_BONES];
    if (maskFor != &m) {
        for (int i = 0; i < MAX_BONES; ++i) mask[i] = isUpperBodyBone(m, i);
        maskFor = &m;
    }
    auto layer = [&](int clip, float t, bool loop, float weight) {
        if (clip < 0 || weight <= 0.0f) return;
        Pose upPose;
        m.restPose(upPose);
        m.sampleClip(clip, t, loop, upPose);
        Pose mixed;
        blendPoseMasked(base, upPose, weight, mask, mixed);
        base = mixed;
    };
    if (st.upper >= 0 || st.prevUpper >= 0) {
        if (st.upperBlend < 1.0f) {
            layer(st.prevUpper, st.prevUpperT, st.prevUpperLoop, 1.0f);
            layer(st.upper, st.upperT, st.upperLoop, st.upperBlend);
        } else {
            layer(st.upper, st.upperT, st.upperLoop, 1.0f);
        }
    }
    // Aim pitch: lean the chest and head so the gun follows the crosshair.
    bool aiming = in.gun && in.anim != Anim::Sprint && in.anim != Anim::Jump && !in.lobby;
    if (aiming || in.anim == Anim::Aim) {
        int chest = m.boneIndex("Chest"), head = m.boneIndex("Head");
        m.rotateInCharacterFrame(chest, quatAxisAngle(Vec3(1, 0, 0), -in.aimPitch * 0.75f), base);
        m.rotateInCharacterFrame(head, quatAxisAngle(Vec3(1, 0, 0), -in.aimPitch * 0.2f), base);
    }
    out = base;
    return true;
}

}  // namespace bf
