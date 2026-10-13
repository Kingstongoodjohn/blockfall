// Blockfall - skins (outfits) for the locker
#pragma once
#include "../core/math.h"

namespace bf {

constexpr int SKIN_PRICE = 1200;      // K-Bucks, the RARE price (see skinPrice for the others)
constexpr int KBUCKS_PER_KILL = 25;
constexpr int SKIN_RARITIES = 4;

struct Skin {
    const char* name;
    Vec4 shirt, pants, hair, accent, hat;
    int hairStyle;    // 0 spiky, 1 flat cap of hair, 2 mohawk, 3 long/back
    int hatStyle;     // 0 none, 1 cap, 2 beanie, 3 helmet
    bool headband;
    int pattern;      // 0 plain, 1 stripes, 2 camo, 3 checker, 4 hex, 5 stars, 6 flames, 7 sash
    int skinTone;
    int rarity;       // 0 uncommon, 1 rare, 2 epic, 3 legendary
};

int skinCount();
const Skin& skinAt(int i);   // 0 is the free default
int skinPrice(int i);        // full price in K-Bucks by rarity: 800 / 1200 / 1500 / 2000
const char* skinRarityName(int rarity);
Vec4 skinRarityColor(int rarity);
Vec4 skinShirt(int i, int team, bool isPlayer);  // team-tinted shirt (teammates share the hue)

}  // namespace bf
