// Blockfall - shared game types: rarity, guns, ammo, items
#pragma once
#include "../core/math.h"
#include "atlas.h"
#include <string>

namespace bf {

// ---------------------------------------------------------------- rarity
enum class Rarity : int { Common = 0, Uncommon, Rare, Epic, Legendary, Count };

inline const char* rarityName(Rarity r) {
    switch (r) {
        case Rarity::Common: return "COMMON";
        case Rarity::Uncommon: return "UNCOMMON";
        case Rarity::Rare: return "RARE";
        case Rarity::Epic: return "EPIC";
        case Rarity::Legendary: return "LEGENDARY";
        default: return "?";
    }
}
inline Vec4 rarityColor(Rarity r) {
    switch (r) {
        case Rarity::Common: return {0.62f, 0.64f, 0.66f, 1};
        case Rarity::Uncommon: return {0.30f, 0.80f, 0.25f, 1};
        case Rarity::Rare: return {0.20f, 0.55f, 1.00f, 1};
        case Rarity::Epic: return {0.70f, 0.30f, 0.95f, 1};
        case Rarity::Legendary: return {1.00f, 0.68f, 0.15f, 1};
        default: return {1, 1, 1, 1};
    }
}
// Each rarity step adds 5% damage.
inline float rarityDamageMult(Rarity r) { return 1.0f + 0.05f * (float)(int)r; }

// ---------------------------------------------------------------- ammo
enum class AmmoType : int { Light = 0, Medium, Heavy, Shells, Count };

inline const char* ammoName(AmmoType a) {
    switch (a) {
        case AmmoType::Light: return "LIGHT";
        case AmmoType::Medium: return "MEDIUM";
        case AmmoType::Heavy: return "HEAVY";
        case AmmoType::Shells: return "SHELLS";
        default: return "?";
    }
}

// ---------------------------------------------------------------- guns
enum class GunType : int { AssaultRifle = 0, Shotgun, SMG, Pistol, Sniper, Count };

struct GunStats {
    const char* name;
    float damage;        // per bullet / pellet, at Common
    int pellets;         // bullets per shot
    float fireRate;      // shots per second
    bool automatic;      // hold to fire
    int magSize;
    float reloadTime;    // seconds
    float spreadHip;     // degrees, base cone (hip fire)
    float spreadAds;     // degrees, base cone (aiming)
    float bloomPerShot;  // degrees added per shot
    float bloomMax;      // degrees
    float range;         // metres (full damage falls off after half)
    AmmoType ammo;
    int ammoPerChest;    // stack size dropped from chests
    float headshotMult;
};

inline const GunStats& gunStats(GunType t) {
    static const GunStats table[(int)GunType::Count] = {
        //  name            dmg  pel rate  auto  mag reload hip  ads  bloom max  range  ammo               stack hs
        {"ASSAULT RIFLE",  30.f, 1, 5.5f, true,  30, 2.3f, 1.6f, 0.35f, 0.45f, 4.5f, 160.f, AmmoType::Medium, 30, 1.5f},
        {"SHOTGUN",         9.f, 10, 0.9f, false, 5, 4.4f, 5.5f, 4.0f, 0.0f, 5.5f, 25.f, AmmoType::Shells, 10, 1.5f},
        {"SMG",            17.f, 1, 12.f, true,  30, 2.2f, 2.4f, 1.2f, 0.25f, 5.0f, 70.f, AmmoType::Light, 36, 1.5f},
        {"PISTOL",         24.f, 1, 6.75f, false, 16, 1.5f, 1.4f, 0.4f, 0.6f, 3.5f, 90.f, AmmoType::Light, 36, 1.5f},
        {"SNIPER",        105.f, 1, 0.35f, false, 1, 2.8f, 6.0f, 0.0f, 0.0f, 6.0f, 400.f, AmmoType::Heavy, 6, 2.5f},
    };
    return table[(int)t];
}

// ---------------------------------------------------------------- items
enum class ItemKind : int { None = 0, Gun, MedKit, Ammo, Pickaxe, BigPot, Mini, Mats, Shockwave };
constexpr int SHOCKWAVE_MAX_STACK = 6;
constexpr float SHOCKWAVE_RADIUS = 6.5f;   // metres
constexpr float SHOCKWAVE_PUSH = 17.0f;    // m/s outward
constexpr float SHOCKWAVE_LIFT = 12.0f;    // m/s upward
constexpr int DEATH_MATS_DROP = 50;   // materials an eliminated player drops

// Pickaxe: always carried, never dropped.
constexpr float PICKAXE_SWING_TIME = 0.5f;     // seconds between swings
constexpr float PICKAXE_REACH = 3.2f;          // metres from the player's head
constexpr float PICKAXE_PLAYER_DAMAGE = 20.0f;
constexpr float PICKAXE_BUILD_DAMAGE = 50.0f;  // to builds and props

constexpr int MEDKIT_MAX_STACK = 3;
constexpr float MEDKIT_HEAL = 50.0f;
constexpr float MEDKIT_USE_TIME = 3.0f;
// Shield: Big Pot gives 50 (to 100) in 5 s, stacks to 2. Minis give 25 (up to 50) in 2 s, stack to 6.
constexpr int BIGPOT_MAX_STACK = 2;
constexpr float BIGPOT_SHIELD = 50.0f;
constexpr float BIGPOT_USE_TIME = 5.0f;
constexpr int MINI_MAX_STACK = 6;
constexpr float MINI_SHIELD = 25.0f;
constexpr float MINI_SHIELD_CAP = 50.0f;
constexpr float MINI_USE_TIME = 2.0f;
inline bool isConsumable(ItemKind k) { return k == ItemKind::MedKit || k == ItemKind::BigPot || k == ItemKind::Mini || k == ItemKind::Shockwave; }
inline int maxStack(ItemKind k) {
    return k == ItemKind::MedKit ? MEDKIT_MAX_STACK : (k == ItemKind::BigPot ? BIGPOT_MAX_STACK : (k == ItemKind::Mini ? MINI_MAX_STACK : (k == ItemKind::Shockwave ? SHOCKWAVE_MAX_STACK : 1)));
}
inline float useTime(ItemKind k) {
    return k == ItemKind::MedKit ? MEDKIT_USE_TIME : (k == ItemKind::BigPot ? BIGPOT_USE_TIME : MINI_USE_TIME);
}

struct Item {
    ItemKind kind = ItemKind::None;
    GunType gun = GunType::AssaultRifle;
    Rarity rarity = Rarity::Common;
    AmmoType ammoType = AmmoType::Light;
    int count = 0;   // MedKit: stack count. Ammo: amount. Gun: rounds in magazine. Mats: amount.
    int mat = 0;     // Mats: 0 wood, 1 brick, 2 metal

    static Item makeGun(GunType g, Rarity r) {
        Item it;
        it.kind = ItemKind::Gun;
        it.gun = g;
        it.rarity = r;
        it.count = gunStats(g).magSize;
        return it;
    }
    static Item makePickaxe() {
        Item it;
        it.kind = ItemKind::Pickaxe;
        return it;
    }
    static Item makeBigPot(int n = 1) {
        Item it;
        it.kind = ItemKind::BigPot;
        it.rarity = Rarity::Rare;
        it.count = n;
        return it;
    }
    static Item makeMini(int n = 1) {
        Item it;
        it.kind = ItemKind::Mini;
        it.rarity = Rarity::Uncommon;
        it.count = n;
        return it;
    }
    static Item makeMedKit(int n = 1) {
        Item it;
        it.kind = ItemKind::MedKit;
        it.rarity = Rarity::Uncommon;
        it.count = n;
        return it;
    }
    static Item makeAmmo(AmmoType a, int n) {
        Item it;
        it.kind = ItemKind::Ammo;
        it.ammoType = a;
        it.count = n;
        return it;
    }
    static Item makeShockwave(int n = 2) {
        Item it;
        it.kind = ItemKind::Shockwave;
        it.rarity = Rarity::Epic;
        it.count = n;
        return it;
    }
    static Item makeMats(int matType, int n) {
        Item it;
        it.kind = ItemKind::Mats;
        it.mat = matType;
        it.count = n;
        return it;
    }
    bool empty() const { return kind == ItemKind::None; }
    std::string label() const {
        switch (kind) {
            case ItemKind::Gun: return std::string(rarityName(rarity)) + " " + gunStats(gun).name;
            case ItemKind::MedKit: return "MED KIT";
            case ItemKind::Ammo: return std::string(ammoName(ammoType)) + " AMMO";
            case ItemKind::Mats: return mat == 0 ? "WOOD" : (mat == 1 ? "BRICK" : "METAL");
            case ItemKind::Shockwave: return "SHOCKWAVE GRENADE";
            case ItemKind::Pickaxe: return "PICKAXE";
            case ItemKind::BigPot: return "BIG SHIELD POTION";
            case ItemKind::Mini: return "SMALL SHIELD POTION";
            default: return "";
        }
    }
};

inline IconId itemIcon(const Item& it) {
    switch (it.kind) {
        case ItemKind::Gun:
            switch (it.gun) {
                case GunType::AssaultRifle: return ICON_AR;
                case GunType::Shotgun: return ICON_SHOTGUN;
                case GunType::SMG: return ICON_SMG;
                case GunType::Pistol: return ICON_PISTOL;
                default: return ICON_SNIPER;
            }
        case ItemKind::MedKit: return ICON_MEDKIT;
        case ItemKind::BigPot: return ICON_BIGPOT;
        case ItemKind::Mini: return ICON_MINI;
        case ItemKind::Pickaxe: return ICON_PICKAXE;
        case ItemKind::Ammo: return ICON_AMMO;
        case ItemKind::Mats: return ICON_MATS;
        case ItemKind::Shockwave: return ICON_SHOCKWAVE;
        default: return ICON_COUNT;
    }
}

}  // namespace bf
