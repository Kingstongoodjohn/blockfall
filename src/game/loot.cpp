// Blockfall - random loot: rarity, gun type, ammo, Med Kits
#include "loot.h"

namespace bf {

Rarity rollRarity(Rng& rng) {
    int total = 0;
    for (int w : RARITY_WEIGHTS) total += w;
    int roll = rng.rangeInt(0, total - 1);
    for (int i = 0; i < (int)Rarity::Count; ++i) {
        if (roll < RARITY_WEIGHTS[i]) return (Rarity)i;
        roll -= RARITY_WEIGHTS[i];
    }
    return Rarity::Common;
}

GunType rollGunType(Rng& rng) { return (GunType)rng.rangeInt(0, (int)GunType::Count - 1); }

std::vector<Item> rollChest(Rng& rng) {
    std::vector<Item> out;
    GunType g = rollGunType(rng);
    out.push_back(Item::makeGun(g, rollRarity(rng)));
    const GunStats& gs = gunStats(g);
    out.push_back(Item::makeAmmo(gs.ammo, gs.ammoPerChest));
    if (rng.chance(CHEST_MEDKIT_CHANCE)) out.push_back(Item::makeMedKit(1));
    // Shield: 40% minis (x3), 20% a big pot.
    float r = rng.uniform();
    if (r < 0.4f) out.push_back(Item::makeMini(3));
    else if (r < 0.6f) out.push_back(Item::makeBigPot(1));
    // Shockwave grenades: one chest in four.
    if (rng.chance(CHEST_SHOCKWAVE_CHANCE)) out.push_back(Item::makeShockwave(2));
    return out;
}

}  // namespace bf
