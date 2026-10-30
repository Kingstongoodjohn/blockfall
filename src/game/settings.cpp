// Blockfall - player settings, keybinds and friends list (saved as a small text file)
#include "settings.h"
#include <algorithm>
#include <cstdlib>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace bf {

namespace {
const char* bindKey(Action a) {
    switch (a) {
        case Action::MoveForward: return "forward";
        case Action::MoveBack: return "back";
        case Action::MoveLeft: return "left";
        case Action::MoveRight: return "right";
        case Action::Jump: return "jump";
        case Action::Sprint: return "sprint";
        case Action::Crouch: return "crouch";
        case Action::Fire: return "fire";
        case Action::Aim: return "aim";
        case Action::Reload: return "reload";
        case Action::Interact: return "interact";
        case Action::Pickaxe: return "pickaxe";
        case Action::Slot1: return "slot1";
        case Action::Slot2: return "slot2";
        case Action::Slot3: return "slot3";
        case Action::Slot4: return "slot4";
        case Action::Slot5: return "slot5";
        case Action::BuildToggle: return "build";
        case Action::Wall: return "wall";
        case Action::Floor: return "floor";
        case Action::Ramp: return "ramp";
        case Action::Cone: return "cone";
        case Action::Edit: return "edit";
        case Action::ResetEdit: return "resetedit";
        case Action::Inventory: return "inventory";
        case Action::Map: return "map";
        default: return "";
    }
}
}  // namespace

const char* gfxOptionName(GfxOption o) {
    switch (o) {
        case GfxOption::MeshDetail: return "MESH DETAIL";
        case GfxOption::SmoothShading: return "SMOOTH SHADING";
        case GfxOption::NormalMaps: return "NORMAL MAPS";
        case GfxOption::Materials: return "MATERIALS (PBR)";
        case GfxOption::Lighting: return "LIGHTING";
        case GfxOption::Shadows: return "SHADOWS";
        case GfxOption::AntiAliasing: return "ANTI-ALIASING";
        case GfxOption::PostFx: return "POST-PROCESSING";
        default: return "?";
    }
}

const char* gfxOptionHint(GfxOption o) {
    switch (o) {
        case GfxOption::MeshDetail: return "MORE TRIANGLES ON ROUND SHAPES UP CLOSE, FEWER FAR AWAY. HIGH = FINER TERRAIN TOO.";
        case GfxOption::SmoothShading: return "OFF SHOWS EVERY FLAT FACE. ON BLENDS LIGHT ACROSS EDGES SO CURVES LOOK ROUND.";
        case GfxOption::NormalMaps: return "FAKE BUMPS, GRAIN AND CRACKS THAT REACT TO LIGHT ON WOOD, STONE AND GRASS.";
        case GfxOption::Materials: return "SURFACES SAY HOW SHINY, ROUGH OR METALLIC THEY ARE. HIGH ADDS REFLECTIONS.";
        case GfxOption::Lighting: return "SKY LIGHT, CORNER SHADING (AO) AND, ON HIGH, COLOUR BOUNCING OFF NEARBY SURFACES.";
        case GfxOption::Shadows: return "SUN SHADOWS FROM A SHADOW MAP. HIGHER = SHARPER, AND THEY SOFTEN WITH DISTANCE.";
        case GfxOption::AntiAliasing: return "SMOOTHS JAGGED EDGES. LOW = FXAA, MEDIUM = TEMPORAL, HIGH = TEMPORAL + SHARPEN.";
        case GfxOption::PostFx: return "FOG AND TONEMAPPING, THEN BLOOM AND COLOUR GRADING, THEN DEPTH OF FIELD WHEN AIMING.";
        default: return "";
    }
}

const char* gfxLevelName(int level) {
    switch (level) {
        case 0: return "OFF";
        case 1: return "LOW";
        case 2: return "MEDIUM";
        default: return "HIGH";
    }
}

GraphicsSettings GraphicsSettings::preset(int p) {
    GraphicsSettings g;
    int v = p <= 0 ? 0 : (p == 1 ? 2 : 3);
    for (int i = 0; i < (int)GfxOption::Count; ++i) g.level[i] = v;
    if (p <= 0) {
        g.set(GfxOption::SmoothShading, 1);  // performance mode still keeps the round look
        g.set(GfxOption::MeshDetail, 1);
    }
    return g;
}

std::string cleanName(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
        if (ok) out += c;
    }
    size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = out.find_last_not_of(' ');
    out = out.substr(a, b - a + 1);
    if ((int)out.size() > MAX_NAME_LEN) out.resize(MAX_NAME_LEN);
    return out;
}

std::string Settings::serialize() const {
    std::ostringstream o;
    o << "# Blockfall settings\n";
    o << "name=" << playerName << "\n";
    o << "sensitivity=" << sensitivity << "\n";
    o << "vsync=" << (vsync ? 1 : 0) << "\n";
    o << "volume=" << volume << "\n";
    o << "charstyle=" << characterStyle << "\n";
    o << "kbucks=" << kbucks << "\n";
    o << "skins=" << ownedSkins.low() << "\n";
    o << "skins2=" << ownedSkins.high() << "\n";
    o << "skin=" << equippedSkin << "\n";
    o << "account=" << account << "\n";
    if (!storeUrl.empty()) o << "store=" << storeUrl << "\n";
    for (int i = 0; i < (int)GfxOption::Count; ++i) o << "gfx." << i << "=" << gfx.level[i] << "\n";
    for (int i = 0; i < (int)Action::Count; ++i) {
        o << "bind." << bindKey((Action)i) << "=" << binds.key[i][0] << "\n";
        o << "bind2." << bindKey((Action)i) << "=" << binds.key[i][1] << "\n";
    }
    for (const std::string& f : friends) o << "friend=" << f << "\n";
    return o.str();
}

void Settings::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    bool friendsReset = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "name") {
            std::string n = cleanName(v);
            if (!n.empty()) playerName = n;
        } else if (k == "sensitivity") {
            float s = (float)std::atof(v.c_str());
            if (s > 0.0001f && s < 0.05f) sensitivity = s;
        } else if (k == "vsync") {
            vsync = v != "0";
        } else if (k == "volume") {
            float vol = (float)std::atof(v.c_str());
            if (vol >= 0.0f && vol <= 1.0f) volume = vol;
        } else if (k == "charstyle") {
            characterStyle = v == "1" ? 1 : 0;
        } else if (k.compare(0, 5, "bind.") == 0 || k.compare(0, 6, "bind2.") == 0) {
            int slot = k[4] == '2' ? 1 : 0;
            std::string which = k.substr(slot ? 6 : 5);
            int code = std::atoi(v.c_str());
            for (int i = 0; i < (int)Action::Count; ++i)
                if (which == bindKey((Action)i) && (keyBindable(code) || code == KEY_NONE))
                    binds.key[i][slot] = code;
        } else if (k == "kbucks") {
            kbucks = std::max(0, std::atoi(v.c_str()));
        } else if (k == "skins") {
            ownedSkins.setLow(v);
        } else if (k == "skins2") {
            ownedSkins.setHigh(v);
        } else if (k == "skin") {
            equippedSkin = std::max(0, std::atoi(v.c_str()));
        } else if (k == "store") {
            storeUrl = v;
        } else if (k == "account") {
            account = cleanName(v);
        } else if (k.compare(0, 4, "gfx.") == 0) {
            int idx = std::atoi(k.c_str() + 4), lv = std::atoi(v.c_str());
            if (idx >= 0 && idx < (int)GfxOption::Count && lv >= 0 && lv <= 3) gfx.level[idx] = lv;
        } else if (k == "friend") {
            if (!friendsReset) { friends.clear(); friendsReset = true; }
            addFriend(v);
        }
    }
}

bool Settings::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    parse(ss.str());
    return true;
}

bool Settings::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << serialize();
    return (bool)f;
}

bool Settings::addFriend(const std::string& raw) {
    std::string n = cleanName(raw);
    if (n.empty() || n == playerName || hasFriend(n) || (int)friends.size() >= MAX_FRIENDS) return false;
    friends.push_back(n);
    return true;
}

bool Settings::removeFriend(const std::string& raw) {
    std::string n = cleanName(raw);
    auto it = std::find(friends.begin(), friends.end(), n);
    if (it == friends.end()) return false;
    friends.erase(it);
    return true;
}

bool Settings::hasFriend(const std::string& raw) const {
    std::string n = cleanName(raw);
    return std::find(friends.begin(), friends.end(), n) != friends.end();
}

}  // namespace bf
