// Blockfall - random loot: rarity, gun type, ammo, Med Kits
#pragma once
#include "../core/rng.h"
#include "types.h"
#include <vector>

namespace bf {

// Rarity odds out of 100: Common 40, Uncommon 30, Rare 18, Epic 9, Legendary 3.
constexpr int RARITY_WEIGHTS[(int)Rarity::Count] = {40, 30, 18, 9, 3};
constexpr float CHEST_SHOCKWAVE_CHANCE = 0.25f;
constexpr float CHEST_MEDKIT_CHANCE = 0.5f;

Rarity rollRarity(Rng& rng);
GunType rollGunType(Rng& rng);
// One chest: 1 gun, 1 stack of the matching ammo, 50% chance of a Med Kit.
std::vector<Item> rollChest(Rng& rng);

}  // namespace bf
