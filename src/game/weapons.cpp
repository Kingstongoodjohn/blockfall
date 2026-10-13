// Blockfall - gun handling: fire rate, spread/bloom, reloads, damage falloff
#include "weapons.h"
#include <cmath>

namespace bf {

float shotSpread(const GunStats& g, const GunState& s, const SpreadContext& c) {
    float spread = c.ads ? g.spreadAds : g.spreadHip;
    spread += s.bloom * (c.ads ? 0.5f : 1.0f);
    if (c.crouched) spread *= 0.75f;
    if (c.moving) spread *= 1.25f;
    if (c.airborne) spread *= 1.6f;
    return spread;
}

Vec3 spreadDirection(const Vec3& dir, float angleDeg, Rng& rng) {
    if (angleDeg <= 0.0f) return dir;
    // Uniform point in a disc, mapped onto a cone.
    float half = deg2rad(angleDeg * 0.5f);
    float r = std::sqrt(rng.uniform()) * std::tan(half);
    float a = rng.uniform() * 2.0f * PI;
    Vec3 up = std::fabs(dir.y) > 0.95f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 right = normalize(cross(up, dir));
    Vec3 realUp = cross(dir, right);
    return normalize(dir + right * (r * std::cos(a)) + realUp * (r * std::sin(a)));
}

float bulletDamage(const Item& gun, float distance, bool headshot) {
    const GunStats& g = gunStats(gun.gun);
    if (distance > g.range) return 0.0f;
    float falloff = 1.0f;
    float start = g.range * 0.5f;
    if (distance > start) falloff = lerpf(1.0f, 0.5f, (distance - start) / (g.range - start));
    float dmg = g.damage * rarityDamageMult(gun.rarity) * falloff;
    if (headshot) dmg *= g.headshotMult;
    return dmg;
}

bool tickGun(GunState& s, const GunStats& g, float dt) {
    s.sinceShot += dt;
    if (s.cooldown > 0) s.cooldown = std::max(0.0f, s.cooldown - dt);
    // Bloom recovers quickly once you stop shooting.
    float recover = s.sinceShot > 0.15f ? g.bloomMax * 2.5f : g.bloomMax * 0.6f;
    s.bloom = std::max(0.0f, s.bloom - recover * dt);
    if (s.reloadTimer > 0) {
        s.reloadTimer -= dt;
        if (s.reloadTimer <= 0) {
            s.reloadTimer = 0;
            return true;
        }
    }
    return false;
}

void onShotFired(GunState& s, const GunStats& g) {
    s.cooldown = 1.0f / g.fireRate;
    s.bloom = std::min(g.bloomMax, s.bloom + g.bloomPerShot);
    s.sinceShot = 0;
}

}  // namespace bf
