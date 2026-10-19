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

const char* kitPieceName(KitPiece k) {
    static const char* names[(int)KitPiece::Count] = {"jacket", "hood", "vest", "pauldrons", "gauntlets", "shinguards", "boots",
                                                      "belt", "cape", "scarf", "backpack", "goggles", "mask", "holster"};
    int i = (int)k;
    return i >= 0 && i < (int)KitPiece::Count ? names[i] : "?";
}

const char* bodyBuildName(int build) {
    static const char* names[(int)BodyBuild::Count] = {"ATHLETIC", "HEAVY", "SLIM"};
    return build >= 0 && build < (int)BodyBuild::Count ? names[build] : "?";
}

const char* kitMaterialName(int m) {
    static const char* names[(int)KitMaterial::Count] = {"CLOTH", "LEATHER", "METAL", "RUBBER"};
    return m >= 0 && m < (int)KitMaterial::Count ? names[m] : "?";
}

void kitMaterialParams(int m, float& rough, float& metal) {
    static const float params[(int)KitMaterial::Count][2] = {{0.85f, 0.0f}, {0.42f, 0.0f}, {0.28f, 1.0f}, {0.6f, 0.0f}};
    m = m < 0 || m >= (int)KitMaterial::Count ? 0 : m;
    rough = params[m][0];
    metal = params[m][1];
}

Vec4 skinToneColor(int tone) {
    // Same values as SKIN_TONES in tools/character/skins_catalog.py.
    static const Vec4 tones[8] = {{0.93f, 0.72f, 0.58f, 1}, {0.80f, 0.56f, 0.40f, 1}, {0.55f, 0.36f, 0.25f, 1}, {0.98f, 0.82f, 0.70f, 1},
                                  {0.68f, 0.48f, 0.34f, 1}, {0.90f, 0.68f, 0.52f, 1}, {0.84f, 0.62f, 0.50f, 1}, {0.45f, 0.28f, 0.20f, 1}};
    return tones[tone < 0 ? 0 : (tone > 7 ? 7 : tone)];
}

Vec4 skinShirt(int i, int team, bool isPlayer) {
    (void)isPlayer;
    Vec4 c = skinAt(i).shirt;
    if (team != 0) return c;  // enemies wear their own skin colours
    return c;
}

}  // namespace bf
