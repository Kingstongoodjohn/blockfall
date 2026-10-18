// Blockfall - sound effect synthesis (see sfx.h)
#include "sfx.h"
#include <cmath>

namespace bf {

namespace {

constexpr float SR = (float)SFX_SAMPLE_RATE;

// Repeatable white noise in -1..1 (its own generator, so sounds don't depend on the game's RNG).
struct Noise {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return (float)((s >> 9) & 0x7FFFFF) / (float)0x400000 - 1.0f;
    }
};
// One-pole filters: low-pass keeps the rumble, high-pass keeps the hiss.
struct LowPass {
    float y = 0, a;
    explicit LowPass(float hz) : a(1.0f - std::exp(-2.0f * PI * hz / SR)) {}
    float step(float x) { return y += a * (x - y); }
};
struct HighPass {
    LowPass lp;
    explicit HighPass(float hz) : lp(hz) {}
    float step(float x) { return x - lp.step(x); }
};

// Quick rise, then exponential fade (`decay` = seconds to drop to ~37%).
float env(float t, float attack, float decay) { return t < attack ? t / attack : std::exp(-(t - attack) / decay); }

std::vector<float> buffer(float seconds) { return std::vector<float>((size_t)(seconds * SR), 0.0f); }

// A sine whose pitch slides from f0 to f1, with optional 2nd/3rd harmonics for a brighter tone.
void tone(std::vector<float>& o, float start, float len, float f0, float f1, float decay, float amp, float h2 = 0, float h3 = 0) {
    size_t i0 = (size_t)(start * SR), n = (size_t)(len * SR);
    float ph = 0;
    for (size_t k = 0; k < n && i0 + k < o.size(); ++k) {
        float t = k / SR;
        ph += 2.0f * PI * (f0 + (f1 - f0) * (t / len)) / SR;
        float s = std::sin(ph) + h2 * std::sin(2 * ph) + h3 * std::sin(3 * ph);
        o[i0 + k] += s * env(t, 0.003f, decay) * amp;
    }
}

// Filtered noise burst. highHz > 0 also removes the low end (a click or hiss instead of a thud).
void noise(std::vector<float>& o, float start, float len, float lowHz, float decay, float amp, uint32_t seed, float highHz = 0) {
    Noise n{seed};
    LowPass lp(lowHz);
    HighPass hp(highHz > 0 ? highHz : 1.0f);
    size_t i0 = (size_t)(start * SR), cnt = (size_t)(len * SR);
    for (size_t k = 0; k < cnt && i0 + k < o.size(); ++k) {
        float t = k / SR;
        float s = lp.step(n.next());
        if (highHz > 0) s = hp.step(s);
        o[i0 + k] += s * env(t, 0.001f, decay) * amp;
    }
}

// A gunshot: a sharp high crack, a filtered noise blast, and a low thump that drops in pitch.
void gunshot(std::vector<float>& o, uint32_t seed, float blastHz, float decay, float thumpHz, float thumpDecay, float crack) {
    Noise n{seed};
    LowPass lp(blastHz);
    HighPass hp(3000.0f);
    float ph = 0;
    for (size_t i = 0; i < o.size(); ++i) {
        float t = i / SR;
        float w = n.next();
        float blast = lp.step(w) * env(t, 0.0008f, decay) * 2.2f;
        float cr = hp.step(w) * crack * std::exp(-t / 0.006f);
        ph += 2.0f * PI * thumpHz * (0.6f + 0.4f * std::exp(-t / 0.03f)) / SR;
        float thump = std::sin(ph) * env(t, 0.002f, thumpDecay) * 0.8f;
        o[i] += blast + cr + thump;
    }
}

// Adds a quieter copy `delay` seconds later (an echo off distant hills).
void echo(std::vector<float>& o, float delay, float amount) {
    size_t d = (size_t)(delay * SR);
    for (size_t i = o.size(); i-- > d;) o[i] += o[i - d] * amount;
}

// Scales to the given peak, fades out the last few milliseconds (no click at the end) and
// converts to 16-bit.
std::vector<int16_t> finish(std::vector<float>& o, float peak) {
    size_t fade = std::min(o.size(), (size_t)(0.005f * SR));
    for (size_t k = 0; k < fade; ++k) o[o.size() - 1 - k] *= (float)k / fade;
    float m = 1e-6f;
    for (float v : o) m = std::max(m, std::fabs(v));
    std::vector<int16_t> out(o.size());
    for (size_t i = 0; i < o.size(); ++i) out[i] = (int16_t)std::lround(clampf(o[i] / m * peak, -1.0f, 1.0f) * 32767.0f);
    return out;
}

}  // namespace

const char* sfxName(Sfx s) {
    static const char* names[] = {"SHOT_AR", "SHOT_SHOTGUN", "SHOT_SMG", "SHOT_PISTOL", "SHOT_SNIPER", "RELOAD", "DRY_FIRE",
                                  "HIT_BODY", "HIT_HEAD", "HURT", "PICKAXE_HIT", "BUILD_PLACE", "BUILD_BREAK", "EDIT",
                                  "FOOTSTEP", "LAND", "CHEST_OPEN", "PICKUP", "HEAL_DONE", "SHOCKWAVE", "KNOCK",
                                  "ELIMINATION", "REVIVE", "STORM_TICK", "STORM_SHRINK", "UI_CLICK"};
    static_assert(sizeof(names) / sizeof(names[0]) == (size_t)Sfx::Count, "one name per sound");
    int i = (int)s;
    return i >= 0 && i < (int)Sfx::Count ? names[i] : "?";
}

Sfx shotSfx(GunType g) {
    switch (g) {
        case GunType::Shotgun: return Sfx::ShotShotgun;
        case GunType::SMG: return Sfx::ShotSMG;
        case GunType::Pistol: return Sfx::ShotPistol;
        case GunType::Sniper: return Sfx::ShotSniper;
        default: return Sfx::ShotAR;
    }
}

float sfxRange(Sfx s) {
    switch (s) {
        case Sfx::ShotSniper: return 250.0f;
        case Sfx::ShotAR: case Sfx::ShotShotgun: case Sfx::ShotPistol: case Sfx::ShotSMG: return 150.0f;
        case Sfx::Shockwave: return 90.0f;
        case Sfx::BuildBreak: return 60.0f;
        case Sfx::BuildPlace: case Sfx::Knock: return 40.0f;
        case Sfx::PickaxeHit: return 30.0f;
        case Sfx::Footstep: case Sfx::Land: case Sfx::ChestOpen: case Sfx::Revive: return 24.0f;
        case Sfx::Reload: case Sfx::Edit: case Sfx::Pickup: return 15.0f;
        case Sfx::DryFire: return 10.0f;
        default: return 1000.0f;   // ui sounds
    }
}

std::vector<int16_t> synthSfx(Sfx s) {
    std::vector<float> o;
    float peak = 0.8f;
    switch (s) {
        case Sfx::ShotAR: o = buffer(0.45f); gunshot(o, 11, 2500, 0.07f, 110, 0.08f, 0.5f); peak = 0.9f; break;
        case Sfx::ShotShotgun: o = buffer(0.7f); gunshot(o, 12, 1400, 0.14f, 75, 0.14f, 0.4f); peak = 0.95f; break;
        case Sfx::ShotSMG: o = buffer(0.25f); gunshot(o, 13, 3500, 0.035f, 140, 0.04f, 0.6f); peak = 0.8f; break;
        case Sfx::ShotPistol: o = buffer(0.35f); gunshot(o, 14, 3000, 0.05f, 120, 0.05f, 0.7f); peak = 0.85f; break;
        case Sfx::ShotSniper:
            o = buffer(1.4f);
            gunshot(o, 15, 1200, 0.3f, 60, 0.25f, 1.2f);
            echo(o, 0.18f, 0.3f);
            peak = 1.0f;
            break;
        case Sfx::Reload:   // mag out, mag in
            o = buffer(0.55f);
            tone(o, 0.0f, 0.05f, 2400, 2200, 0.01f, 0.6f);
            noise(o, 0.0f, 0.04f, 6000, 0.008f, 0.6f, 21, 1500);
            tone(o, 0.32f, 0.06f, 1800, 1700, 0.015f, 0.8f, 0.3f);
            noise(o, 0.32f, 0.05f, 5000, 0.01f, 0.8f, 22, 1200);
            peak = 0.5f;
            break;
        case Sfx::DryFire:
            o = buffer(0.08f);
            tone(o, 0, 0.04f, 3000, 2800, 0.006f, 0.6f);
            noise(o, 0, 0.03f, 7000, 0.004f, 0.6f, 23, 2000);
            peak = 0.45f;
            break;
        case Sfx::HitBody: o = buffer(0.07f); tone(o, 0, 0.07f, 1500, 1400, 0.02f, 0.7f); peak = 0.45f; break;
        case Sfx::HitHead:   // a bright "ding"
            o = buffer(0.35f);
            tone(o, 0, 0.35f, 2600, 2600, 0.09f, 1.0f, 0.3f);
            tone(o, 0, 0.25f, 3900, 3900, 0.06f, 0.3f);
            peak = 0.5f;
            break;
        case Sfx::Hurt:
            o = buffer(0.2f);
            noise(o, 0, 0.2f, 400, 0.05f, 1.0f, 31);
            tone(o, 0, 0.2f, 120, 70, 0.06f, 0.6f);
            peak = 0.6f;
            break;
        case Sfx::PickaxeHit:
            o = buffer(0.2f);
            noise(o, 0, 0.2f, 900, 0.03f, 1.0f, 41);
            tone(o, 0, 0.2f, 260, 180, 0.05f, 0.7f);
            tone(o, 0, 0.1f, 520, 500, 0.02f, 0.3f);
            peak = 0.7f;
            break;
        case Sfx::BuildPlace:   // a hollow wooden knock
            o = buffer(0.22f);
            tone(o, 0, 0.22f, 210, 200, 0.04f, 1.0f, 0.4f);
            tone(o, 0, 0.15f, 330, 330, 0.03f, 0.4f);
            noise(o, 0, 0.02f, 6000, 0.005f, 0.5f, 51, 2000);
            peak = 0.7f;
            break;
        case Sfx::BuildBreak: {   // a crash: low rumble plus splintering crackles
            o = buffer(0.6f);
            noise(o, 0, 0.6f, 600, 0.2f, 1.0f, 61);
            tone(o, 0, 0.4f, 90, 50, 0.1f, 0.7f);
            Noise r{62};
            for (int k = 0; k < 12; ++k) {
                float at = (r.next() * 0.5f + 0.5f) * 0.35f;
                noise(o, at, 0.06f, 2500, 0.02f, 0.5f, 63 + k, 300);
            }
            peak = 0.8f;
            break;
        }
        case Sfx::Edit: o = buffer(0.1f); tone(o, 0, 0.1f, 650, 950, 0.05f, 0.5f); peak = 0.4f; break;
        case Sfx::Footstep: o = buffer(0.09f); noise(o, 0, 0.09f, 500, 0.02f, 1.0f, 71); peak = 0.35f; break;
        case Sfx::Land:
            o = buffer(0.25f);
            noise(o, 0, 0.25f, 350, 0.05f, 1.0f, 81);
            tone(o, 0, 0.25f, 90, 55, 0.07f, 0.7f);
            peak = 0.6f;
            break;
        case Sfx::ChestOpen: {   // rising sparkly arpeggio
            o = buffer(0.9f);
            const float notes[4] = {1047, 1319, 1568, 2093};
            for (int k = 0; k < 4; ++k) tone(o, k * 0.07f, 0.6f, notes[k], notes[k], 0.18f, 0.5f, 0.2f);
            noise(o, 0, 0.9f, 12000, 0.3f, 0.15f, 91, 6000);
            peak = 0.55f;
            break;
        }
        case Sfx::Pickup:
            o = buffer(0.18f);
            tone(o, 0, 0.06f, 880, 880, 0.04f, 0.6f);
            tone(o, 0.06f, 0.12f, 1320, 1320, 0.05f, 0.6f);
            peak = 0.45f;
            break;
        case Sfx::HealDone: {
            o = buffer(0.6f);
            const float notes[3] = {784, 988, 1175};
            for (int k = 0; k < 3; ++k) tone(o, k * 0.1f, 0.4f, notes[k], notes[k], 0.15f, 0.6f, 0.15f);
            peak = 0.5f;
            break;
        }
        case Sfx::Shockwave:   // deep boom with a whoosh
            o = buffer(1.6f);
            tone(o, 0, 1.6f, 70, 28, 0.5f, 1.0f);
            noise(o, 0, 1.2f, 400, 0.35f, 1.0f, 101);
            noise(o, 0, 1.6f, 1200, 0.6f, 0.3f, 102, 300);
            peak = 0.95f;
            break;
        case Sfx::Knock: o = buffer(0.6f); tone(o, 0, 0.55f, 420, 140, 0.25f, 1.0f, 0, 0.2f); peak = 0.6f; break;
        case Sfx::Elimination:
            o = buffer(0.8f);
            tone(o, 0, 0.6f, 1047, 1047, 0.25f, 0.8f, 0.25f);
            tone(o, 0.09f, 0.7f, 1568, 1568, 0.3f, 0.8f, 0.2f);
            peak = 0.6f;
            break;
        case Sfx::Revive: o = buffer(0.7f); tone(o, 0, 0.65f, 300, 900, 0.4f, 0.8f, 0.2f); peak = 0.5f; break;
        case Sfx::StormTick:   // buzzy zap
            o = buffer(0.3f);
            tone(o, 0, 0.3f, 110, 100, 0.1f, 1.0f, 0.5f, 0.33f);
            noise(o, 0, 0.25f, 2000, 0.08f, 0.3f, 111);
            peak = 0.5f;
            break;
        case Sfx::StormShrink:   // two-tone warning horn
            o = buffer(1.6f);
            tone(o, 0, 0.7f, 220, 220, 0.5f, 0.8f, 0.5f, 0.3f);
            tone(o, 0.75f, 0.8f, 185, 185, 0.5f, 0.8f, 0.5f, 0.3f);
            peak = 0.55f;
            break;
        case Sfx::UiClick:
        default:
            o = buffer(0.04f);
            tone(o, 0, 0.04f, 1100, 1000, 0.008f, 0.6f);
            noise(o, 0, 0.01f, 8000, 0.002f, 0.3f, 121, 3000);
            peak = 0.3f;
            break;
    }
    return finish(o, peak);
}

void sfxMix(const Vec3& ear, const Vec3& right, const Vec3& src, float range, float& gain, float& pan) {
    Vec3 d = src - ear;
    float dist = length(d);
    if (dist >= range) {
        gain = 0;
        pan = 0;
        return;
    }
    // Loud up close, falling off with distance, reaching zero at `range`.
    float fall = 1.0f - dist / range;
    gain = fall * fall / (1.0f + dist / 12.0f);
    // Never fully to one side: real ears hear both sides a bit.
    pan = dist > 0.5f ? clampf(dot(d / dist, right), -1.0f, 1.0f) * 0.8f : 0.0f;
}

}  // namespace bf
