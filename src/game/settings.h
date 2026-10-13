// Blockfall - player settings, keybinds and friends list (saved as a small text file)
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "keybinds.h"

namespace bf {

constexpr int MAX_NAME_LEN = 16;
constexpr int MAX_FRIENDS = 50;

// Each option is Off (0), Low (1), Medium (2) or High (3).
enum class GfxOption : int { MeshDetail = 0, SmoothShading, NormalMaps, Materials, Lighting, Shadows, AntiAliasing, PostFx, Count };
const char* gfxOptionName(GfxOption o);
const char* gfxOptionHint(GfxOption o);   // one line on what it does
const char* gfxLevelName(int level);      // OFF / LOW / MEDIUM / HIGH

struct GraphicsSettings {
    int level[(int)GfxOption::Count] = {2, 2, 2, 2, 2, 2, 2, 2};
    int get(GfxOption o) const { return level[(int)o]; }
    void set(GfxOption o, int v) { level[(int)o] = v < 0 ? 0 : (v > 3 ? 3 : v); }
    static GraphicsSettings preset(int p);  // 0 performance, 1 balanced, 2 epic
    bool operator==(const GraphicsSettings& o) const {
        for (int i = 0; i < (int)GfxOption::Count; ++i)
            if (level[i] != o.level[i]) return false;
        return true;
    }
    bool operator!=(const GraphicsSettings& o) const { return !(*this == o); }
};

struct Settings {
    std::string playerName = "PLAYER";
    GraphicsSettings gfx;
    float sensitivity = 0.0022f;  // radians per mouse count
    bool vsync = true;
    Bindings binds = Bindings::defaults();
    std::vector<std::string> friends;
    int kbucks = 0;
    uint64_t ownedSkins = 1;   // bit per skin; skin 0 is free
    int equippedSkin = 0;
    std::string account;       // name of the signed-in account (empty = not signed in)
    bool ownsSkin(int i) const { return i >= 0 && i < 64 && ((ownedSkins >> i) & 1ull); }

    std::string serialize() const;
    // Reads what serialize() wrote. Unknown or broken lines are skipped.
    void parse(const std::string& text);
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Friends. Names are upper-cased and trimmed. Returns false if invalid/duplicate/full.
    bool addFriend(const std::string& name);
    bool removeFriend(const std::string& name);
    bool hasFriend(const std::string& name) const;
};

// Upper-cases, keeps A-Z 0-9 _ - and spaces, trims, and limits the length.
std::string cleanName(const std::string& raw);

}  // namespace bf
