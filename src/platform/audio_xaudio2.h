// Blockfall - sound output with XAudio2 (part of Windows). Plays the game's SoundEvents.
#pragma once
#include "../game/sfx.h"
#include <string>
#include <vector>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace bf {

class Audio {
public:
    // Loads XAudio2 and synthesises every sound. Returns false (and the game stays silent) if
    // there's no XAudio2 or no audio device; `error` says why.
    bool init(std::string& error);
    void shutdown();
    bool ready() const { return master_ != nullptr; }
    void setVolume(float v);   // 0..1, applied to everything
    // Plays this frame's events as heard from `ear`, whose right-hand side is `right`.
    void play(const std::vector<SoundEvent>& events, const Vec3& ear, const Vec3& right);

private:
    void* dll_ = nullptr;
    IXAudio2* xa_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    unsigned outChannels_ = 2;
    std::vector<std::vector<int16_t>> clips_;     // one per Sfx
    std::vector<IXAudio2SourceVoice*> voices_;    // reused; a voice is free once its sound ends
    float volume_ = -1;
    uint32_t pitchSeed_ = 1;
};

}  // namespace bf
