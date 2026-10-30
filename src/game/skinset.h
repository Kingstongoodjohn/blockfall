// Blockfall - which skins a player owns (a bit per skin, room for 128)
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

namespace bf {

constexpr int MAX_SKINS = 128;

struct SkinSet {
    uint64_t w[2] = {1, 0};   // skin 0 is free, so always owned
    bool has(int i) const { return i >= 0 && i < MAX_SKINS && ((w[i >> 6] >> (i & 63)) & 1ull); }
    void add(int i) {
        if (i >= 0 && i < MAX_SKINS) w[i >> 6] |= 1ull << (i & 63);
    }
    int count() const {
        int n = 0;
        for (int i = 0; i < MAX_SKINS; ++i) n += has(i) ? 1 : 0;
        return n;
    }
    bool operator==(const SkinSet& o) const { return w[0] == o.w[0] && w[1] == o.w[1]; }
    bool operator!=(const SkinSet& o) const { return !(*this == o); }
    // Saved as two numbers: "skins=" (skins 0-63, the old format) and "skins2=" (64-127), so old
    // files still load.
    std::string low() const { return std::to_string(w[0]); }
    std::string high() const { return std::to_string(w[1]); }
    void setLow(const std::string& v) { w[0] = (uint64_t)std::strtoull(v.c_str(), nullptr, 10) | 1ull; }
    void setHigh(const std::string& v) { w[1] = (uint64_t)std::strtoull(v.c_str(), nullptr, 10); }
};

}  // namespace bf
