// Blockfall - sound output with XAudio2 (see audio_xaudio2.h)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // XAudio2 2.8+ headers need Windows 8 or later
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "audio_xaudio2.h"
#include <windows.h>
#include <xaudio2.h>
#include <algorithm>
#include <cmath>

namespace bf {

namespace {

constexpr int VOICE_COUNT = 32;   // sounds that can play at once
typedef HRESULT(WINAPI* XAudio2CreateFn)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);

}  // namespace

bool Audio::init(std::string& error) {
    // XAudio2 expects COM to be started on this thread. Fine if something already did.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Load it at runtime instead of linking, so a PC without it still runs the game (silently).
    // 2.9 ships with Windows 10 and 11; 2.8 with Windows 8. Only look in the system folder.
    HMODULE mod = nullptr;
    const char* dlls[] = {"xaudio2_9.dll", "xaudio2_8.dll"};
    for (const char* name : dlls)
        if ((mod = LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) != nullptr) break;
    if (!mod) {
        error = "XAudio2 is not installed";
        return false;
    }
    dll_ = mod;
    auto create = (XAudio2CreateFn)(void*)GetProcAddress(mod, "XAudio2Create");
    if (!create || FAILED(create(&xa_, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        error = "XAudio2Create failed";
        shutdown();
        return false;
    }
    if (FAILED(xa_->CreateMasteringVoice(&master_))) {
        error = "no audio output device";
        master_ = nullptr;
        shutdown();
        return false;
    }
    XAUDIO2_VOICE_DETAILS details{};
    master_->GetVoiceDetails(&details);
    outChannels_ = std::max(1u, (unsigned)details.InputChannels);

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = SFX_SAMPLE_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = SFX_SAMPLE_RATE * 2;
    for (int i = 0; i < VOICE_COUNT; ++i) {
        IXAudio2SourceVoice* v = nullptr;
        if (SUCCEEDED(xa_->CreateSourceVoice(&v, &fmt))) voices_.push_back(v);
    }
    clips_.resize((size_t)Sfx::Count);
    for (int i = 0; i < (int)Sfx::Count; ++i) clips_[i] = synthSfx((Sfx)i);
    return !voices_.empty();
}

void Audio::shutdown() {
    for (IXAudio2SourceVoice* v : voices_) v->DestroyVoice();   // waits for the voice to stop
    voices_.clear();
    if (master_) master_->DestroyVoice();
    master_ = nullptr;
    if (xa_) xa_->Release();
    xa_ = nullptr;
    if (dll_) FreeLibrary((HMODULE)dll_);
    dll_ = nullptr;
}

void Audio::setVolume(float v) {
    if (!master_ || v == volume_) return;
    volume_ = v;
    master_->SetVolume(clampf(v, 0.0f, 1.0f));
}

void Audio::play(const std::vector<SoundEvent>& events, const Vec3& ear, const Vec3& right) {
    if (!ready() || events.empty()) return;

    // Work out how each sound is heard; the same sound twice in one frame plays once (the louder).
    struct Heard { Sfx id; float gain, pan; };
    std::vector<Heard> heard;
    for (const SoundEvent& e : events) {
        float gain = e.volume, pan = 0;
        if (!e.ui) {
            sfxMix(ear, right, e.pos, sfxRange(e.id), gain, pan);
            gain *= e.volume;
        }
        if (gain < 0.01f) continue;
        auto same = std::find_if(heard.begin(), heard.end(), [&](const Heard& h) { return h.id == e.id; });
        if (same == heard.end()) heard.push_back({e.id, gain, pan});
        else if (gain > same->gain) *same = {e.id, gain, pan};
    }
    std::sort(heard.begin(), heard.end(), [](const Heard& a, const Heard& b) { return a.gain > b.gain; });

    size_t next = 0;
    for (const Heard& h : heard) {
        // Next voice that has finished its last sound. None free: drop the quieter sounds.
        IXAudio2SourceVoice* voice = nullptr;
        for (; next < voices_.size() && !voice; ++next) {
            XAUDIO2_VOICE_STATE st{};
            voices_[next]->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (st.BuffersQueued == 0) voice = voices_[next];
        }
        if (!voice) break;

        const std::vector<int16_t>& clip = clips_[(size_t)h.id];
        // Equal-power pan between the front left and right speakers.
        float matrix[32] = {};
        float a = (h.pan + 1.0f) * PI * 0.25f;
        if (outChannels_ == 1) {
            matrix[0] = 1.0f;
        } else {
            matrix[0] = std::cos(a);
            matrix[1] = std::sin(a);
        }
        voice->SetOutputMatrix(master_, 1, std::min(outChannels_, 32u), matrix);
        voice->SetVolume(h.gain);
        // World sounds get a slightly different pitch each time so repeats don't sound robotic.
        pitchSeed_ = pitchSeed_ * 1664525u + 1013904223u;
        bool isUi = h.id == Sfx::UiClick || h.id == Sfx::HitBody || h.id == Sfx::HitHead || h.id == Sfx::Elimination;
        float pitch = isUi ? 1.0f : 0.95f + 0.1f * (float)((pitchSeed_ >> 8) & 0xFFFF) / 65535.0f;
        voice->SetFrequencyRatio(pitch);
        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = (UINT32)(clip.size() * sizeof(int16_t));
        buf.pAudioData = (const BYTE*)clip.data();
        buf.Flags = XAUDIO2_END_OF_STREAM;
        if (SUCCEEDED(voice->SubmitSourceBuffer(&buf))) voice->Start(0);
    }
}

}  // namespace bf
