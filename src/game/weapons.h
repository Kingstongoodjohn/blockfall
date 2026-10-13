// Blockfall - gun handling: fire rate, spread/bloom, reloads, damage falloff
#pragma once
#include "../core/math.h"
#include "../core/rng.h"
#include "types.h"

namespace bf {

struct GunState {
    float cooldown = 0;     // seconds until the next shot is allowed
    float bloom = 0;        // extra spread in degrees from recent shots
    float reloadTimer = 0;  // > 0 while reloading
    float sinceShot = 99;   // seconds since the last shot
    void reset() { *this = GunState(); }
};

struct SpreadContext {
    bool ads = false;
    bool crouched = false;
    bool moving = false;
    bool airborne = false;
};

// Total cone angle (degrees) for the next shot.
float shotSpread(const GunStats& g, const GunState& s, const SpreadContext& c);
// Random direction inside a cone of `angleDeg` (full-angle) around `dir`.
Vec3 spreadDirection(const Vec3& dir, float angleDeg, Rng& rng);
// Damage for one bullet/pellet at a distance; 0 if out of range.
float bulletDamage(const Item& gun, float distance, bool headshot);
// Ticks cooldown/bloom/reload. Returns true on the frame a reload finishes.
bool tickGun(GunState& s, const GunStats& g, float dt);
// Called when a shot is fired.
void onShotFired(GunState& s, const GunStats& g);

}  // namespace bf
