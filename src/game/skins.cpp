// Blockfall - skins (outfits) for the locker
#include "skins.h"

namespace bf {

namespace {
const Skin kSkins[] = {
#include "skins_data.inc"
};
}  // namespace

int skinCount() { return (int)(sizeof(kSkins) / sizeof(kSkins[0])); }

int skinPrice(int i) {
    static const int prices[SKIN_RARITIES] = {800, 1200, 1500, 2000};
    int r = skinAt(i).rarity;
    return prices[r < 0 ? 0 : (r >= SKIN_RARITIES ? SKIN_RARITIES - 1 : r)];
}

const char* skinRarityName(int rarity) {
    static const char* names[SKIN_RARITIES] = {"UNCOMMON", "RARE", "EPIC", "LEGENDARY"};
    return names[rarity < 0 ? 0 : (rarity >= SKIN_RARITIES ? SKIN_RARITIES - 1 : rarity)];
}

Vec4 skinRarityColor(int rarity) {
    static const Vec4 cols[SKIN_RARITIES] = {{0.4f, 0.85f, 0.4f, 1}, {0.35f, 0.65f, 1.0f, 1}, {0.75f, 0.4f, 1.0f, 1}, {1.0f, 0.75f, 0.25f, 1}};
    return cols[rarity < 0 ? 0 : (rarity >= SKIN_RARITIES ? SKIN_RARITIES - 1 : rarity)];
}
const Skin& skinAt(int i) { return kSkins[(i < 0 || i >= skinCount()) ? 0 : i]; }

Vec4 skinShirt(int i, int team, bool isPlayer) {
    (void)isPlayer;
    Vec4 c = skinAt(i).shirt;
    if (team != 0) return c;  // enemies wear their own skin colours
    return c;
}

}  // namespace bf
