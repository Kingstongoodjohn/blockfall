// Blockfall - skins (outfits) for the locker
#pragma once
#include "../core/math.h"
#include <cstdint>

namespace bf {

// Outfit kit pieces (tools/character/kit.py). Bit i of Skin::kit = KIT_NAMES[i] is worn.
enum class KitPiece : int { Jacket = 0, Hood, Vest, Pauldrons, Gauntlets, ShinGuards, Boots, Belt, Cape, Scarf,
                            Backpack, Goggles, Mask, Holster, Count };
const char* kitPieceName(KitPiece k);   // mesh name in character.glb ("jacket", "hood", ...)
enum class BodyBuild : int { Athletic = 0, Heavy, Slim, Count };
const char* bodyBuildName(int build);
// What the main kit pieces are made of (sets shine and metalness in the Realistic style).
enum class KitMaterial : int { Cloth = 0, Leather, Metal, Rubber, Count };
const char* kitMaterialName(int m);
// Roughness (0 mirror .. 1 matte) and metalness for a KitMaterial.
void kitMaterialParams(int m, float& rough, float& metal);

constexpr int SKIN_PRICE = 1200;      // K-Bucks, the RARE price (see skinPrice for the others)
constexpr int KBUCKS_PER_KILL = 25;
constexpr int SKIN_RARITIES = 4;
constexpr int SKIN_FX_TRIM = 1;
constexpr int SKIN_FX_EYES = 2;

struct Skin {
    const char* name;
    Vec4 shirt, pants, hair, accent, hat;
    int hairStyle;    // 0 spiky, 1 flat cap of hair, 2 mohawk, 3 long/back
    int hatStyle;     // 0 none, 1 cap, 2 beanie, 3 helmet
    bool headband;
    int pattern;      // 0 plain .. 13 panelled, 14 cracks, 15 circuit, 16 tiger, 17 gradient, 18 pixels (PATTERN_NAME in skins_catalog.py)
    int skinTone;
    int rarity;       // 0 uncommon, 1 rare, 2 epic, 3 legendary
    int build;        // BodyBuild
    uint32_t kit;     // bit per KitPiece
    int kitMaterial;  // KitMaterial of the main kit pieces
    int fx;           // emission: SKIN_FX_TRIM (trim/accent paint glows), SKIN_FX_EYES (eyes and lenses glow)
    bool wears(KitPiece k) const { return (kit >> (int)k) & 1u; }
};
Vec4 skinToneColor(int tone);   // SKIN_TONES in skins_catalog.py

int skinCount();
const Skin& skinAt(int i);   // 0 is the free default
int skinPrice(int i);        // full price in K-Bucks by rarity: 800 / 1200 / 1500 / 2000
const char* skinRarityName(int rarity);
Vec4 skinRarityColor(int rarity);
Vec4 skinShirt(int i, int team, bool isPlayer);  // team-tinted shirt (teammates share the hue)

}  // namespace bf
