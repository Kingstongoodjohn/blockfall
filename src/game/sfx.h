// Blockfall - sound effects: the events the game makes, and the code that synthesises each sound.
// No audio files: every sound is generated from noise, tones and envelopes at startup. The platform
// layer (src/platform/audio_xaudio2.cpp) plays them.
#pragma once
#include "../core/math.h"
#include "types.h"
#include <cstdint>
#include <vector>

namespace bf {

constexpr int SFX_SAMPLE_RATE = 44100;
constexpr int MAX_SOUND_EVENTS = 256;   // per frame; extra events are dropped

enum class Sfx : int {
    ShotAR = 0, ShotShotgun, ShotSMG, ShotPistol, ShotSniper,
    Reload, DryFire,
    HitBody, HitHead, Hurt,
    PickaxeHit, BuildPlace, BuildBreak, Edit,
    Footstep, Land,
    ChestOpen, Pickup, HealDone,
    Shockwave, Knock, Elimination, Revive,
    StormTick, StormShrink,
    UiClick,
    Count
};

// One sound to play this frame. `ui` sounds play at full volume in the middle (hit markers, menu
// clicks); the rest are placed in the world and get quieter with distance.
struct SoundEvent {
    Sfx id = Sfx::UiClick;
    Vec3 pos;
    float volume = 1.0f;
    bool ui = false;
};

const char* sfxName(Sfx s);
Sfx shotSfx(GunType g);   // the gunshot sound for a gun
// Distance (metres) at which a world sound has faded to silence.
float sfxRange(Sfx s);
// Synthesises a sound: mono, 16-bit, SFX_SAMPLE_RATE. The same sound comes out every call.
std::vector<int16_t> synthSfx(Sfx s);
// How loud (0..1) and how far left/right (-1..1) a world sound at `src` is for a listener at
// `ear` whose right-hand direction is `right`.
void sfxMix(const Vec3& ear, const Vec3& right, const Vec3& src, float range, float& gain, float& pan);

}  // namespace bf
