// Blockfall - tiny deterministic random number generator
#pragma once
#include <cstdint>

namespace bf {

class Rng {
public:
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s_(seed ? seed : 1) {}
    void seed(uint64_t s) { s_ = s ? s : 1; }
    uint64_t next() {
        // xorshift64*
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 2685821657736338717ull;
    }
    // [0, 1)
    float uniform() { return (float)((next() >> 40) * (1.0 / 16777216.0)); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    int rangeInt(int lo, int hiInclusive) {
        return lo + (int)(next() % (uint64_t)(hiInclusive - lo + 1));
    }
    bool chance(float p) { return uniform() < p; }

private:
    uint64_t s_;
};

}  // namespace bf
