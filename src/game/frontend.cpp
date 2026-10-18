// Blockfall - menus: lobby, friends, settings (keybinds), pause and end-of-match screens
#include "frontend.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "map.h"
#include "skins.h"
#include <ctime>

namespace bf {

namespace {
const Vec4 WHITE(1, 1, 1, 1);
const Vec4 DIM(0.75f, 0.8f, 0.9f, 1);
const Vec4 ACCENT(1.0f, 0.82f, 0.3f, 1);
const Vec4 PANEL_COL(0.05f, 0.07f, 0.13f, 0.82f);
const Vec4 BTN(0.16f, 0.22f, 0.36f, 0.92f);
const Vec4 BTN_HOVER(0.24f, 0.34f, 0.55f, 0.95f);
const Vec4 BTN_PRIMARY(0.95f, 0.72f, 0.15f, 0.97f);
const Vec4 BTN_PRIMARY_HOVER(1.0f, 0.82f, 0.3f, 1.0f);
}  // namespace

FrontEnd::FrontEnd() : game(1337) {}

void FrontEnd::init(const std::string& settingsPath) {
    settingsPath_ = settingsPath;
    if (!settingsPath_.empty()) {
        settings.load(settingsPath_);
        size_t slash = settingsPath_.find_last_of("\\/");
        std::string dir = slash == std::string::npos ? "." : settingsPath_.substr(0, slash);
        accounts.setDirectory(dir + "/accounts");
        // Signed in last time? Pick the account back up. Otherwise ask to sign in first.
        Account a;
        std::string err;
        if (!settings.account.empty() && accounts.exists(settings.account)) {
            // Password already checked when it was signed in; just reload the account's things.
            if (accounts.signInRaw(settings.account, a)) {
                account = a;
                signedIn = true;
                settings.playerName = a.name;
                settings.kbucks = a.kbucks;
                settings.ownedSkins = a.ownedSkins;
                settings.equippedSkin = a.equippedSkin;
            }
        }
        if (!signedIn) {
            settings.account.clear();
            screen = Screen::SignIn;
            signName_ = settings.playerName == "PLAYER" ? "" : settings.playerName;
        }
    }
    nameInput_ = settings.playerName;
}

void FrontEnd::save() {
    if (signedIn) {
        account.kbucks = settings.kbucks;
        account.ownedSkins = settings.ownedSkins;
        account.equippedSkin = settings.equippedSkin;
        accounts.save(account);
    }
    if (!settingsPath_.empty()) settings.save(settingsPath_);
}

bool FrontEnd::signIn(const std::string& name, const std::string& password, std::string& error) {
    Account a;
    if (!accounts.signIn(name, password, a, error)) return false;
    account = a;
    signedIn = true;
    settings.account = a.name;
    settings.playerName = a.name;
    settings.kbucks = a.kbucks;
    settings.ownedSkins = a.ownedSkins;
    settings.equippedSkin = a.equippedSkin;
    nameInput_ = a.name;
    save();
    return true;
}

bool FrontEnd::createAccount(const std::string& name, const std::string& password, std::string& error) {
    Account a;
    if (!accounts.create(name, password, a, error)) return false;
    // A new account starts with whatever this PC had unsigned (so nothing is lost).
    a.kbucks = settings.kbucks;
    a.ownedSkins = settings.ownedSkins;
    a.equippedSkin = settings.equippedSkin;
    account = a;
    signedIn = true;
    settings.account = a.name;
    settings.playerName = a.name;
    nameInput_ = a.name;
    save();
    return true;
}

void FrontEnd::signOut() {
    save();
    signedIn = false;
    account = Account();
    settings.account.clear();
    settings.kbucks = 0;
    settings.ownedSkins = 1;
    settings.equippedSkin = 0;
    if (!settingsPath_.empty()) settings.save(settingsPath_);
    signPass_.clear();
    signError_.clear();
    screen = Screen::SignIn;
}

void FrontEnd::notify(const std::string& m) {
    message = m;
    messageTime = 3.0f;
}

void FrontEnd::setTeamSize(int n) {
    teamSize = std::max(1, std::min(4, n));
}

bool FrontEnd::invite(const std::string& raw) {
    std::string n = cleanName(raw);
    if (n.empty() || !settings.hasFriend(n)) return false;
    if (std::find(party.begin(), party.end(), n) != party.end()) return false;
    if ((int)party.size() >= MAX_PARTY) {
        notify("PARTY IS FULL (SQUADS IS 4 PLAYERS)");
        return false;
    }
    party.push_back(n);
    // Make sure the mode fits the party.
    if ((int)party.size() + 1 > teamSize) {
        setTeamSize((int)party.size() + 1);
        notify(n + " JOINED - MODE SWITCHED TO " + teamModeName(teamSize));
    } else {
        notify(n + " JOINED YOUR PARTY");
    }
    return true;
}

void FrontEnd::uninvite(const std::string& raw) {
    std::string n = cleanName(raw);
    party.erase(std::remove(party.begin(), party.end(), n), party.end());
}

void FrontEnd::startMatch() {
    MatchConfig cfg;
    cfg.teamSize = teamSize;
    cfg.playerName = settings.playerName;
    for (const std::string& f : party)
        if ((int)cfg.party.size() < teamSize - 1) cfg.party.push_back(f);
    cfg.seed = nextSeed_++;
    cfg.useBus = true;
    cfg.skin = settings.equippedSkin;
    creditedKills = 0;
    bool help = game.showHelp;
    game.startMatch(cfg);
    game.showHelp = help;
    game.mouseSensitivity = settings.sensitivity;
    screen = Screen::InGame;
    matchOverTime_ = 0;
    focus_ = -1;
}

InputState FrontEnd::gameInput(const RawInput& in) const {
    InputState s;
    const Bindings& b = settings.binds;
    auto key = [&](Action a, int slot = 0) { int k = b.get(a, slot); return (k > 0 && k < KEY_COUNT) ? k : 0; };
    auto down = [&](Action a) {
        int k0 = key(a, 0), k1 = key(a, 1);
        return (k0 && in.down[k0]) || (k1 && in.down[k1]);
    };
    auto pressed = [&](Action a) {
        int k0 = key(a, 0), k1 = key(a, 1);
        return (k0 && in.pressed[k0]) || (k1 && in.pressed[k1]);
    };
    // Released only counts once neither key is held any more.
    auto released = [&](Action a) {
        int k0 = key(a, 0), k1 = key(a, 1);
        return ((k0 && in.released[k0]) || (k1 && in.released[k1])) && !down(a);
    };
    s.forward = down(Action::MoveForward);
    s.back = down(Action::MoveBack);
    s.left = down(Action::MoveLeft);
    s.right = down(Action::MoveRight);
    s.sprint = down(Action::Sprint);
    s.crouch = down(Action::Crouch);
    s.fire = down(Action::Fire);
    s.aim = down(Action::Aim);
    s.interact = down(Action::Interact);
    s.jumpPressed = pressed(Action::Jump);
    s.firePressed = pressed(Action::Fire);
    s.fireReleased = released(Action::Fire);
    s.aimPressed = pressed(Action::Aim);
    s.reloadPressed = pressed(Action::Reload);
    s.interactPressed = pressed(Action::Interact);
    s.buildTogglePressed = pressed(Action::BuildToggle);
    s.editPressed = pressed(Action::Edit);
    s.resetEditPressed = pressed(Action::ResetEdit);
    s.pickaxePressed = pressed(Action::Pickaxe);
    if (pressed(Action::Wall)) s.buildPiecePressed = 0;
    if (pressed(Action::Floor)) s.buildPiecePressed = 1;
    if (pressed(Action::Ramp)) s.buildPiecePressed = 2;
    if (pressed(Action::Cone)) s.buildPiecePressed = 3;
    for (int i = 0; i < 5; ++i)
        if (pressed((Action)((int)Action::Slot1 + i))) s.slotPressed = i;
    // Wheel "keys" only exist as presses; let them hold fire/interact for one frame too.
    for (int slot = 0; slot < BIND_SLOTS; ++slot) {
        int k = key(Action::Fire, slot);
        if (k && in.pressed[k] && (k == KEY_WHEEL_UP || k == KEY_WHEEL_DOWN)) s.fire = true;
    }
    s.helpPressed = in.pressed[KEY_F1];
    s.mouseDx = in.mouseDx;
    s.mouseDy = in.mouseDy;
    return s;
}

// ------------------------------------------------------------------ widgets
void FrontEnd::panel(float x, float y, float w, float h) {
    hud_->rect(x, y, w, h, PANEL_COL);
    hud_->rect(x, y, w, 3 * ui_, ACCENT);
}

bool FrontEnd::button(const std::string& label, float x, float y, float w, float h, bool primary, bool enabled) {
    bool hover = enabled && in_->mouseX >= x && in_->mouseX < x + w && in_->mouseY >= y && in_->mouseY < y + h;
    Vec4 c = primary ? (hover ? BTN_PRIMARY_HOVER : BTN_PRIMARY) : (hover ? BTN_HOVER : BTN);
    if (!enabled) c = Vec4(0.2f, 0.22f, 0.26f, 0.7f);
    hud_->rect(x, y, w, h, c);
    if (hover) hud_->frame(x, y, w, h, 2 * ui_, WHITE);
    float scale = std::min(2.2f * ui_, (h * 0.5f) / CHAR_HEIGHT);
    Vec4 tc = primary ? Vec4(0.1f, 0.08f, 0.04f, 1) : (enabled ? WHITE : Vec4(0.6f, 0.6f, 0.65f, 1));
    hud_->text(x + w * 0.5f, y + (h - CHAR_HEIGHT * scale) * 0.5f, scale, tc, label, 1);
    bool clicked = hover && in_->pressed[KEY_MOUSE_LEFT];
    if (clicked) game.soundUi(Sfx::UiClick);
    return clicked;
}

bool FrontEnd::textField(int id, std::string& value, const std::string& placeholder, float x, float y, float w, float h, bool secret) {
    bool hover = in_->mouseX >= x && in_->mouseX < x + w && in_->mouseY >= y && in_->mouseY < y + h;
    if (in_->pressed[KEY_MOUSE_LEFT]) {
        if (hover) focus_ = id;
        else if (focus_ == id) focus_ = -1;
    }
    bool focused = focus_ == id;
    bool submit = false;
    if (focused) {
        for (char c : in_->text) {
            if ((int)value.size() >= (secret ? 32 : MAX_NAME_LEN)) break;
            if (secret) {
                if (c > 32 && c < 127) value += c;
                continue;
            }
            std::string one = cleanName(std::string(1, c) == " " ? "_" : std::string(1, c));
            if (!one.empty()) value += one;
        }
        if (in_->pressed[KEY_BACKSPACE] && !value.empty()) value.pop_back();
        if (in_->pressed[KEY_ENTER]) submit = true;
    }
    hud_->rect(x, y, w, h, Vec4(0.02f, 0.03f, 0.06f, 0.9f));
    hud_->frame(x, y, w, h, 2 * ui_, focused ? ACCENT : Vec4(0.35f, 0.42f, 0.55f, 1));
    float scale = std::min(2.0f * ui_, (h * 0.5f) / CHAR_HEIGHT);
    float ty = y + (h - CHAR_HEIGHT * scale) * 0.5f;
    std::string shown = secret ? std::string(value.size(), '*') : value;
    if (value.empty() && !focused) hud_->text(x + 10 * ui_, ty, scale, Vec4(0.5f, 0.55f, 0.65f, 1), placeholder);
    else hud_->text(x + 10 * ui_, ty, scale, WHITE, shown);
    if (focused && std::fmod(time_, 1.0f) < 0.6f)
        hud_->rect(x + 10 * ui_ + hud_->textWidth(shown, scale) + 2, ty, 2 * ui_, CHAR_HEIGHT * scale, WHITE);
    return submit;
}

int FrontEnd::keyButton(const std::string& label, float x, float y, float w, float h, bool waiting) {
    bool hover = in_->mouseX >= x && in_->mouseX < x + w && in_->mouseY >= y && in_->mouseY < y + h;
    Vec4 c = waiting ? BTN_PRIMARY : (hover ? BTN_HOVER : BTN);
    hud_->rect(x, y, w, h, c);
    if (hover) hud_->frame(x, y, w, h, 2 * ui_, WHITE);
    float scale = std::min(1.8f * ui_, std::min((h * 0.5f) / CHAR_HEIGHT, (w - 8 * ui_) / std::max(1.0f, hud_->textWidth(label, 1.0f))));
    hud_->text(x + w * 0.5f, y + (h - CHAR_HEIGHT * scale) * 0.5f, scale, waiting ? Vec4(0.1f, 0.08f, 0.04f, 1) : WHITE, label, 1);
    if (!hover) return 0;
    if (in_->pressed[KEY_MOUSE_LEFT]) return 1;
    if (in_->pressed[KEY_MOUSE_RIGHT]) return 2;
    return 0;
}

// ------------------------------------------------------------------ frame
void FrontEnd::frame(const RawInput& in, float dt, float W, float H, float fps, SceneList& scene, HudBuilder& hud) {
    in_ = &in;
    hud_ = &hud;
    W_ = W;
    H_ = H;
    ui_ = std::max(0.7f, H / 900.0f);
    time_ += dt;
    messageTime = std::max(0.0f, messageTime - dt);

    // Waiting for a key to bind? Take the first key or mouse button pressed.
    if (rebinding >= 0) {
        if (in.pressed[KEY_ESCAPE]) {
            rebinding = -1;
        } else if (in.pressed[KEY_DELETE] || in.pressed[KEY_BACKSPACE]) {
            // Delete (or Backspace) while waiting clears the key.
            settings.binds.clear((Action)rebinding, rebindSlot);
            notify(std::string(actionName((Action)rebinding)) + " KEY CLEARED");
            rebinding = -1;
            save();
        } else {
            for (int k = 1; k < KEY_COUNT; ++k)
                if (in.pressed[k] && keyBindable(k)) {
                    Action a = (Action)rebinding;
                    settings.binds.assign(a, k, rebindSlot);
                    Action shared[4];
                    int n = settings.binds.sharedWith(a, k, shared, 4);
                    if (n > 0) {
                        std::string msg = keyName(k) + " IS ALSO USED BY " + actionName(shared[0]);
                        if (n > 1) msg += " +" + std::to_string(n - 1) + " MORE";
                        notify(msg + " (THAT'S ALLOWED)");
                    }
                    rebinding = -1;
                    save();
                    break;
                }
        }
    } else if (in.pressed[KEY_ESCAPE]) {
        switch (screen) {
            case Screen::InGame: screen = Screen::Paused; break;
            case Screen::Inventory: screen = Screen::InGame; break;
            case Screen::Map: screen = Screen::InGame; break;
            case Screen::Paused: screen = Screen::InGame; break;
            case Screen::Friends: screen = Screen::Lobby; break;
            case Screen::Locker: screen = Screen::Lobby; break;
            case Screen::Shop: screen = Screen::Lobby; break;
            case Screen::Settings: screen = settingsFromPause_ ? Screen::Paused : Screen::Lobby; save(); break;
            default: break;
        }
    }
    if (in.focusLost && (screen == Screen::InGame || screen == Screen::Inventory || screen == Screen::Map)) screen = Screen::Paused;
    // Inventory key opens/closes the inventory during a match.
    if (rebinding < 0 && !in.pressed[KEY_ESCAPE]) {
        bool inv = false;
        for (int slot = 0; slot < BIND_SLOTS; ++slot) {
            int k = settings.binds.get(Action::Inventory, slot);
            if (k > 0 && k < KEY_COUNT && in.pressed[k]) inv = true;
        }
        if (inv && screen == Screen::InGame && game.player.alive) {
            screen = Screen::Inventory;
            inventorySel = -1;
        } else if (inv && screen == Screen::Inventory) {
            screen = Screen::InGame;
        }
        bool mapKey = false;
        for (int slot = 0; slot < BIND_SLOTS; ++slot) {
            int k = settings.binds.get(Action::Map, slot);
            if (k > 0 && k < KEY_COUNT && in.pressed[k]) mapKey = true;
        }
        if (mapKey && screen == Screen::InGame) screen = Screen::Map;
        else if (mapKey && screen == Screen::Map) screen = Screen::InGame;
    }

    // Game.
    bool inMatch = screen == Screen::InGame || screen == Screen::Paused || screen == Screen::MatchEnd ||
                   screen == Screen::Inventory || screen == Screen::Map || (screen == Screen::Settings && settingsFromPause_);
    // The match keeps going while the inventory or map is open (like the real thing), just without your input.
    if (screen == Screen::InGame || screen == Screen::MatchEnd || screen == Screen::Inventory || screen == Screen::Map) {
        game.mouseSensitivity = settings.sensitivity;
        InputState gi = screen == Screen::InGame ? gameInput(in) : InputState();
        game.update(gi, dt);
        if (screen == Screen::Inventory && !game.player.alive) screen = Screen::InGame;
        if (screen == Screen::InGame && game.matchOver()) {
            matchOverTime_ += dt;
            if (matchOverTime_ > 1.2f) {
                screen = Screen::MatchEnd;
                if (signedIn) {
                    account.matches += 1;
                    if (game.victory()) account.wins += 1;
                    save();
                }
            }
        }
    }

    creditKills();

    // 3D.
    float aspect = W / std::max(1.0f, H);
    Camera lobbyCam;
    std::vector<LobbyMember> members;
    if (inMatch) {
        buildScene(game, aspect, scene);
    } else {
        int previewSkin = (screen == Screen::Locker || screen == Screen::Shop) ? lockerSel : settings.equippedSkin;
        members.push_back({settings.playerName, skinAt(previewSkin).shirt, true, previewSkin, screen == Screen::Locker || screen == Screen::Shop});
        if (screen != Screen::Locker && screen != Screen::Shop)
            for (size_t i = 0; i < party.size() && (int)i < teamSize - 1; ++i)
                members.push_back({party[i], skinAt((int)(party[i].size() + i) % skinCount()).shirt, false, (int)(party[i].size() + i) % skinCount()});
        lobbyCam = buildLobbyScene(members, time_, aspect, scene);
    }

    // 2D.
    if (inMatch) buildHud(game, W, H, fps, settings.binds, hud);
    else hud.begin(W, H);

    switch (screen) {
        case Screen::Lobby:
            for (size_t i = 0; i < members.size(); ++i) {
                float sx, sy;
                if (hud.project(lobbyCam, lobbySpot((int)i) + Vec3(0, 2.25f, 0), sx, sy)) {
                    float tw = hud.textWidth(members[i].name, 1.8f * ui_);
                    hud.rect(sx - tw * 0.5f - 8 * ui_, sy - 6 * ui_, tw + 16 * ui_, 26 * ui_, Vec4(0, 0, 0, 0.45f));
                    hud.text(sx, sy, 1.8f * ui_, members[i].you ? ACCENT : Vec4(0.5f, 0.9f, 1, 1), members[i].name, 1);
                }
            }
            lobbyScreen();
            break;
        case Screen::Friends: friendsScreen(); break;
        case Screen::Settings:
            if (settingsFromPause_) hud.rect(0, 0, W, H, Vec4(0, 0, 0, 0.5f));
            settingsScreen();
            break;
        case Screen::Paused: pauseScreen(); break;
        case Screen::MatchEnd: matchEndScreen(); break;
        case Screen::Inventory: inventoryScreen(); break;
        case Screen::Map: mapScreen(); break;
        case Screen::Locker: lockerScreen(); break;
        case Screen::Shop: shopScreen(); break;
        case Screen::SignIn: signInScreen(); break;
        default: break;
    }

    if (messageTime > 0 && screen != Screen::InGame) {
        float a = std::min(1.0f, messageTime);
        float tw = hud.textWidth(message, 2.0f * ui_);
        hud.rect(W * 0.5f - tw * 0.5f - 16 * ui_, H - 90 * ui_, tw + 32 * ui_, 40 * ui_, Vec4(0, 0, 0, 0.6f * a));
        hud.text(W * 0.5f, H - 80 * ui_, 2.0f * ui_, Vec4(1, 0.9f, 0.6f, a), message, 1);
    }
}

// ------------------------------------------------------------------ screens
void FrontEnd::lobbyScreen() {
    float u = ui_;
    hud_->text(50 * u, 40 * u, 7.0f * u, ACCENT, "BLOCKFALL");
    // K-Bucks (top right).
    std::string kb = std::to_string(settings.kbucks) + " K-BUCKS";
    float kw = hud_->textWidth(kb, 2.2f * u);
    hud_->rect(W_ - kw - 70 * u, 30 * u, kw + 40 * u, 44 * u, Vec4(0.05f, 0.07f, 0.13f, 0.8f));
    hud_->text(W_ - 50 * u, 42 * u, 2.2f * u, Vec4(0.5f, 0.95f, 0.6f, 1), kb, 2);
    hud_->text(54 * u, 112 * u, 1.8f * u, DIM, "BUILD. EDIT. LOOT. LAST ONE STANDING.");

    float x = 50 * u, y = 190 * u, w = 380 * u;
    if (button("PLAY", x, y, w, 78 * u, true)) startMatch();
    y += 98 * u;
    // Mode picker.
    hud_->text(x, y, 1.6f * u, DIM, "MODE");
    y += 24 * u;
    if (button("<", x, y, 56 * u, 52 * u)) setTeamSize(teamSize == 1 ? 4 : teamSize - 1);
    hud_->rect(x + 64 * u, y, w - 128 * u, 52 * u, Vec4(0.08f, 0.1f, 0.18f, 0.9f));
    hud_->text(x + w * 0.5f, y + 8 * u, 2.4f * u, WHITE, teamModeName(teamSize), 1);
    char sub[64];
    std::snprintf(sub, sizeof sub, "%d TEAMS OF %d", teamCountFor(teamSize), teamSize);
    hud_->text(x + w * 0.5f, y + 32 * u, 1.3f * u, DIM, sub, 1);
    if (button(">", x + w - 56 * u, y, 56 * u, 52 * u)) setTeamSize(teamSize == 4 ? 1 : teamSize + 1);
    y += 72 * u;
    if (button("FRIENDS (" + std::to_string(settings.friends.size()) + ")", x, y, w, 56 * u)) {
        screen = Screen::Friends;
        nameInput_ = settings.playerName;
    }
    y += 66 * u;
    if (button("LOCKER", x, y, w / 2 - 4 * u, 56 * u)) {
        screen = Screen::Locker;
        lockerSel = settings.equippedSkin;
    }
    if (button("ITEM SHOP", x + w / 2 + 4 * u, y, w / 2 - 4 * u, 56 * u)) {
        screen = Screen::Shop;
        lockerSel = settings.equippedSkin;
    }
    y += 66 * u;
    if (button("SETTINGS / KEYBINDS", x, y, w, 56 * u)) {
        settingsFromPause_ = false;
        screen = Screen::Settings;
    }
    y += 66 * u;
    if (button("QUIT", x, y, w, 56 * u)) quitRequested = true;

    // Party panel (bottom right).
    float pw = 380 * u, ph = (90 + 46 * (float)std::max<size_t>(1, party.size())) * u;
    float px = W_ - pw - 40 * u, py = H_ - ph - 40 * u;
    panel(px, py, pw, ph);
    hud_->text(px + 18 * u, py + 16 * u, 2.0f * u, WHITE,
               "PARTY " + std::to_string(1 + std::min<int>((int)party.size(), teamSize - 1)) + "/" + std::to_string(teamSize));
    float ry = py + 50 * u;
    hud_->text(px + 18 * u, ry + 6 * u, 1.8f * u, ACCENT, settings.playerName + " (YOU)");
    ry += 40 * u;
    for (size_t i = 0; i < party.size(); ++i) {
        bool playing = (int)i < teamSize - 1;
        hud_->text(px + 18 * u, ry + 6 * u, 1.8f * u, playing ? Vec4(0.5f, 0.9f, 1, 1) : Vec4(0.55f, 0.55f, 0.6f, 1),
                   party[i] + (playing ? "" : " (NOT IN THIS MODE)"));
        if (button("X", px + pw - 56 * u, ry, 38 * u, 34 * u)) {
            uninvite(party[i]);
            break;
        }
        ry += 46 * u;
    }
    if (party.empty()) hud_->text(px + 18 * u, ry + 6 * u, 1.5f * u, DIM, "INVITE FRIENDS FROM THE FRIENDS MENU");
    hud_->text(W_ - 40 * u, H_ - 28 * u, 1.3f * u, Vec4(0.85f, 0.9f, 1, 0.7f),
               "OFFLINE: FRIENDS JOIN AS AI TEAMMATES WITH THEIR NAMES. EMPTY SPOTS ARE FILLED WITH BOTS.", 2);
    // Account line under the K-Bucks.
    if (signedIn) {
        hud_->text(W_ - 50 * u, 84 * u, 1.3f * u, DIM, "SIGNED IN AS " + account.name, 2);
        if (button("SIGN OUT", W_ - 170 * u, 104 * u, 120 * u, 32 * u)) signOut();
    }
}

void FrontEnd::signInScreen() {
    float u = ui_;
    float pw = 560 * u, ph = 440 * u;
    float px = (W_ - pw) * 0.5f, py = (H_ - ph) * 0.5f;
    panel(px, py, pw, ph);
    hud_->text(px + pw * 0.5f, py + 24 * u, 3.4f * u, ACCENT, "SIGN IN", 1);
    hud_->text(px + pw * 0.5f, py + 66 * u, 1.4f * u, DIM, "YOUR K-BUCKS, SKINS AND STATS ARE SAVED TO YOUR ACCOUNT.", 1);
    hud_->text(px + 40 * u, py + 110 * u, 1.5f * u, WHITE, "NAME");
    bool enter = textField(10, signName_, "YOUR NAME", px + 40 * u, py + 132 * u, pw - 80 * u, 48 * u);
    hud_->text(px + 40 * u, py + 196 * u, 1.5f * u, WHITE, "PASSWORD");
    enter = textField(11, signPass_, "AT LEAST 4 CHARACTERS", px + 40 * u, py + 218 * u, pw - 80 * u, 48 * u, true) || enter;
    if (!signError_.empty()) hud_->text(px + pw * 0.5f, py + 278 * u, 1.4f * u, Vec4(1, 0.5f, 0.4f, 1), signError_, 1);
    float by = py + 306 * u;
    if (button("SIGN IN", px + 40 * u, by, (pw - 96 * u) * 0.5f, 54 * u, true) || enter) {
        std::string err;
        if (signIn(signName_, signPass_, err)) {
            signError_.clear();
            signPass_.clear();
            screen = Screen::Lobby;
            notify("WELCOME BACK, " + account.name);
        } else {
            signError_ = err;
        }
    }
    if (button("CREATE ACCOUNT", px + 40 * u + (pw - 96 * u) * 0.5f + 16 * u, by, (pw - 96 * u) * 0.5f, 54 * u)) {
        std::string err;
        if (createAccount(signName_, signPass_, err)) {
            signError_.clear();
            signPass_.clear();
            screen = Screen::Lobby;
            notify("ACCOUNT CREATED - WELCOME, " + account.name);
        } else {
            signError_ = err;
        }
    }
    hud_->text(px + pw * 0.5f, py + ph - 52 * u, 1.2f * u, DIM, "ACCOUNTS ARE SAVED ON THIS PC (THERE IS NO ONLINE SERVER).", 1);
    std::vector<std::string> names = accounts.names();
    if (!names.empty()) {
        std::string line = "ACCOUNTS HERE: ";
        for (size_t i = 0; i < names.size() && i < 4; ++i) line += (i ? ", " : "") + names[i];
        hud_->text(px + pw * 0.5f, py + ph - 32 * u, 1.2f * u, DIM, line, 1);
    }
}

void FrontEnd::friendsScreen() {
    float u = ui_;
    float pw = 760 * u, ph = H_ - 120 * u;
    float px = (W_ - pw) * 0.5f, py = 60 * u;
    panel(px, py, pw, ph);
    hud_->text(px + 30 * u, py + 24 * u, 3.2f * u, WHITE, "FRIENDS");

    float x = px + 30 * u, y = py + 84 * u;
    hud_->text(x, y, 1.6f * u, DIM, "YOUR NAME");
    y += 24 * u;
    std::string before = nameInput_;
    bool nameSubmit = textField(1, nameInput_, "TYPE YOUR NAME", x, y, 420 * u, 48 * u);
    if (nameInput_ != before || nameSubmit) {
        std::string n = cleanName(nameInput_);
        if (!n.empty() && n != settings.playerName && !settings.hasFriend(n)) {
            settings.playerName = n;
            save();
        }
    }
    y += 72 * u;
    hud_->text(x, y, 1.6f * u, DIM, "ADD A FRIEND (THE NAME OF A REAL PLAYER ACCOUNT)");
    y += 24 * u;
    bool submit = textField(2, friendInput_, "FRIEND'S NAME", x, y, 420 * u, 48 * u);
    if (button("ADD", x + 436 * u, y, 140 * u, 48 * u, true) || submit) {
        std::string n = cleanName(friendInput_);
        if (n.empty()) notify("TYPE A NAME FIRST");
        else if (settings.hasFriend(n)) notify(n + " IS ALREADY YOUR FRIEND");
        else if (n == settings.playerName) notify("THAT'S YOU!");
        else if (!accounts.exists(n)) notify("NO PLAYER CALLED " + n + " - THEY NEED AN ACCOUNT ON THIS PC FIRST");
        else if (settings.addFriend(n)) {
            notify("ADDED " + n);
            friendInput_.clear();
            save();
        } else {
            notify("FRIENDS LIST IS FULL");
        }
    }
    y += 76 * u;

    hud_->text(x, y, 1.6f * u, DIM, "YOUR FRIENDS (" + std::to_string(settings.friends.size()) + ")");
    y += 28 * u;
    float rowH = 50 * u;
    int maxRows = (int)((py + ph - 100 * u - y) / rowH);
    std::string removeName;
    for (int i = 0; i < (int)settings.friends.size() && i < maxRows; ++i) {
        const std::string& f = settings.friends[i];
        hud_->rect(x, y, pw - 60 * u, rowH - 8 * u, Vec4(0.1f, 0.13f, 0.22f, 0.8f));
        hud_->text(x + 14 * u, y + 12 * u, 1.9f * u, WHITE, f);
        bool inParty = std::find(party.begin(), party.end(), f) != party.end();
        bool full = (int)party.size() >= MAX_PARTY;
        float bx = x + pw - 60 * u - 330 * u;
        if (inParty) {
            if (button("IN PARTY - LEAVE", bx, y + 4 * u, 190 * u, rowH - 16 * u)) uninvite(f);
        } else if (button(full ? "PARTY FULL" : "INVITE", bx, y + 4 * u, 190 * u, rowH - 16 * u, !full, !full)) {
            invite(f);
        }
        if (button("REMOVE", bx + 200 * u, y + 4 * u, 120 * u, rowH - 16 * u)) removeName = f;
        y += rowH;
    }
    if ((int)settings.friends.size() > maxRows)
        hud_->text(x, y + 4 * u, 1.4f * u, DIM, "+" + std::to_string(settings.friends.size() - maxRows) + " MORE");
    if (settings.friends.empty()) hud_->text(x, y + 6 * u, 1.6f * u, DIM, "NO FRIENDS YET - ADD ONE ABOVE");
    if (!removeName.empty()) {
        uninvite(removeName);
        settings.removeFriend(removeName);
        save();
        notify("REMOVED " + removeName);
    }

    if (button("BACK", px + 30 * u, py + ph - 76 * u, 200 * u, 52 * u)) {
        screen = Screen::Lobby;
        focus_ = -1;
    }
}

void FrontEnd::settingsScreen() {
    float u = ui_;
    float pw = std::min(W_ - 60 * u, 1180 * u), ph = H_ - 80 * u;
    float px = (W_ - pw) * 0.5f, py = 40 * u;
    panel(px, py, pw, ph);
    hud_->text(px + 30 * u, py + 22 * u, 3.0f * u, WHITE, "SETTINGS");
    // Tabs.
    float tx = px + 300 * u;
    if (button("CONTROLS", tx, py + 18 * u, 180 * u, 40 * u, settingsTab == 0)) { settingsTab = 0; rebinding = -1; }
    if (button("GRAPHICS", tx + 190 * u, py + 18 * u, 180 * u, 40 * u, settingsTab == 1)) { settingsTab = 1; rebinding = -1; }
    if (settingsTab == 0) controlsTab(px, py, pw, ph);
    else graphicsTab(px, py, pw, ph);
    if (button("BACK", px + pw - 240 * u, py + ph - 64 * u, 210 * u, 48 * u, true)) {
        rebinding = -1;
        save();
        screen = settingsFromPause_ ? Screen::Paused : Screen::Lobby;
    }
}

void FrontEnd::controlsTab(float px, float py, float pw, float ph) {
    float u = ui_;
    hud_->text(px + 30 * u, py + 68 * u, 1.4f * u, DIM,
               "LEFT CLICK A KEY, THEN PRESS THE NEW KEY OR MOUSE BUTTON. DELETE CLEARS IT. A KEY MAY BE USED BY MORE THAN ONE ACTION.");

    int n = (int)Action::Count;
    int perCol = (n + 1) / 2;
    float colW = (pw - 90 * u) * 0.5f;
    float rowH = std::min(40 * u, (ph - 250 * u) / perCol);
    float btnW = std::min(170 * u, (colW - 230 * u) * 0.5f);
    for (int col = 0; col < 2; ++col) {
        float x = px + 30 * u + col * (colW + 30 * u);
        hud_->text(x + colW - btnW * 2 - 8 * u + btnW * 0.5f, py + 92 * u, 1.4f * u, DIM, "PRIMARY", 1);
        hud_->text(x + colW - btnW * 0.5f, py + 92 * u, 1.4f * u, DIM, "SECONDARY", 1);
    }
    for (int i = 0; i < n; ++i) {
        int col = i / perCol, row = i % perCol;
        float x = px + 30 * u + col * (colW + 30 * u), y = py + 112 * u + row * rowH;
        hud_->text(x, y + (rowH - 8 * 1.6f * u) * 0.5f, 1.6f * u, WHITE, actionName((Action)i));
        for (int slot = 0; slot < BIND_SLOTS; ++slot) {
            bool waiting = rebinding == i && rebindSlot == slot;
            float bx = x + colW - btnW * (2 - slot) - (slot == 0 ? 8 * u : 0);
            int code = settings.binds.get((Action)i, slot);
            std::string label = waiting ? "PRESS A KEY" : (code == KEY_NONE ? "-" : keyName(code));
            int clickedWith = keyButton(label, bx, y + 3 * u, btnW, rowH - 6 * u, waiting);
            bool hover = in_->mouseX >= bx && in_->mouseX < bx + btnW && in_->mouseY >= y + 3 * u && in_->mouseY < y + rowH - 3 * u;
            if (rebinding < 0 && clickedWith == 1) {
                rebinding = i;
                rebindSlot = slot;
            } else if (rebinding < 0 && (clickedWith == 2 || (hover && in_->pressed[KEY_DELETE]))) {
                settings.binds.clear((Action)i, slot);  // right click or Delete clears a key
                save();
            }
        }
    }

    float y = py + ph - 130 * u;
    float x = px + 30 * u;
    char buf[32];
    std::snprintf(buf, sizeof buf, "MOUSE SENSITIVITY  %.1f", settings.sensitivity * 1000.0f);
    hud_->text(x, y + 12 * u, 1.8f * u, WHITE, buf);
    float bx = x + hud_->textWidth(buf, 1.8f * u) + 20 * u;
    if (button("-", bx, y, 48 * u, 44 * u)) {
        settings.sensitivity = std::max(0.0001f, settings.sensitivity - 0.0001f);
        save();
    }
    if (button("+", bx + 56 * u, y, 48 * u, 44 * u)) {
        settings.sensitivity = std::min(0.02f, settings.sensitivity + 0.0001f);
        save();
    }
    if (button("RESET KEYS", px + pw - 470 * u, y, 210 * u, 44 * u)) {
        settings.binds = Bindings::defaults();
        rebinding = -1;
        save();
        notify("KEYS RESET TO DEFAULT");
    }
}

void FrontEnd::graphicsTab(float px, float py, float pw, float ph) {
    float u = ui_;
    float x = px + 30 * u;
    hud_->text(x, py + 68 * u, 1.4f * u, DIM, "PICK A LEVEL FOR EACH EFFECT, OR USE A PRESET. CHANGES APPLY RIGHT AWAY.");
    // Presets.
    float y = py + 96 * u;
    hud_->text(x, y + 12 * u, 1.7f * u, WHITE, "PRESETS");
    const char* presetNames[3] = {"PERFORMANCE", "BALANCED", "EPIC"};
    for (int p = 0; p < 3; ++p) {
        bool active = settings.gfx == GraphicsSettings::preset(p);
        if (button(presetNames[p], x + 150 * u + p * 200 * u, y, 190 * u, 42 * u, active)) {
            settings.gfx = GraphicsSettings::preset(p);
            save();
        }
    }
    y += 64 * u;
    int n = (int)GfxOption::Count;
    float rowH = std::min(54 * u, (ph - 320 * u) / n);
    float btnW = 118 * u, btnX = px + pw - 30 * u - 4 * (btnW + 8 * u);
    for (int i = 0; i < n; ++i) {
        float ry = y + i * rowH;
        GfxOption o = (GfxOption)i;
        hud_->text(x, ry + 6 * u, 1.8f * u, WHITE, gfxOptionName(o));
        hud_->text(x, ry + 28 * u, 1.15f * u, DIM, gfxOptionHint(o));
        for (int lv = 0; lv < 4; ++lv) {
            bool on = settings.gfx.get(o) == lv;
            if (button(gfxLevelName(lv), btnX + lv * (btnW + 8 * u), ry + 4 * u, btnW, rowH - 12 * u, on)) {
                settings.gfx.set(o, lv);
                save();
            }
        }
    }
    y += n * rowH + 8 * u;
    hud_->text(x, y + 12 * u, 1.8f * u, WHITE, "V-SYNC");
    if (button(settings.vsync ? "ON" : "OFF", x + 110 * u, y, 90 * u, 44 * u)) {
        settings.vsync = !settings.vsync;
        save();
    }
    // Sound volume, on the same row.
    char vol[32];
    std::snprintf(vol, sizeof vol, "SOUND VOLUME  %d%%", (int)std::lround(settings.volume * 100.0f));
    float vx = x + 240 * u;
    hud_->text(vx, y + 12 * u, 1.8f * u, WHITE, vol);
    float vbx = vx + hud_->textWidth(vol, 1.8f * u) + 20 * u;
    if (button("-", vbx, y, 48 * u, 44 * u)) {
        settings.volume = std::max(0.0f, std::round(settings.volume * 10.0f - 1.0f) / 10.0f);
        save();
    }
    if (button("+", vbx + 56 * u, y, 48 * u, 44 * u)) {
        settings.volume = std::min(1.0f, std::round(settings.volume * 10.0f + 1.0f) / 10.0f);
        save();
    }
    hud_->text(x, y + 60 * u, 1.3f * u, DIM, "THESE ARE THIS GAME'S OWN VERSIONS OF THE UE5 EFFECTS, BUILT ON DIRECT3D 11.");
}

void FrontEnd::pauseScreen() {
    float u = ui_;
    hud_->rect(0, 0, W_, H_, Vec4(0, 0, 0, 0.45f));
    float pw = 420 * u, ph = 330 * u, px = (W_ - pw) * 0.5f, py = (H_ - ph) * 0.5f;
    panel(px, py, pw, ph);
    hud_->text(W_ * 0.5f, py + 26 * u, 3.0f * u, WHITE, "PAUSED", 1);
    float x = px + 40 * u, w = pw - 80 * u, y = py + 90 * u;
    if (button("RESUME", x, y, w, 56 * u, true)) screen = Screen::InGame;
    y += 70 * u;
    if (button("SETTINGS / KEYBINDS", x, y, w, 56 * u)) {
        settingsFromPause_ = true;
        screen = Screen::Settings;
    }
    y += 70 * u;
    if (button("LEAVE MATCH", x, y, w, 56 * u)) {
        screen = Screen::Lobby;
        settingsFromPause_ = false;
    }
}

void FrontEnd::inventoryScreen() {
    float u = ui_;
    Player& p = game.player;
    hud_->rect(0, 0, W_, H_, Vec4(0, 0, 0, 0.35f));
    float pw = std::min(W_ - 40 * u, 1000 * u), ph = 560 * u;
    float px = (W_ - pw) * 0.5f, py = (H_ - ph) * 0.5f;
    panel(px, py, pw, ph);
    hud_->text(px + 30 * u, py + 22 * u, 3.0f * u, WHITE, "INVENTORY");
    std::string closeKey = keyName(settings.binds.get(Action::Inventory, 0));
    hud_->text(px + 30 * u, py + 62 * u, 1.5f * u, DIM,
               "CLICK A SLOT, THEN ANOTHER TO SWAP THEM. " + closeKey + " OR ESC TO CLOSE.");

    // Hotbar slots.
    float gap = 14 * u;
    float sw = (pw - 60 * u - gap * (HOTBAR_SLOTS - 1)) / HOTBAR_SLOTS, sh = 150 * u;
    float sy = py + 96 * u;
    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
        float sx = px + 30 * u + i * (sw + gap);
        const Item& it = p.slots[i];
        bool hover = in_->mouseX >= sx && in_->mouseX < sx + sw && in_->mouseY >= sy && in_->mouseY < sy + sh;
        bool sel = inventorySel == i;
        hud_->rect(sx, sy, sw, sh, sel ? Vec4(0.22f, 0.32f, 0.55f, 0.95f) : (hover ? Vec4(0.16f, 0.22f, 0.36f, 0.95f) : Vec4(0.08f, 0.1f, 0.18f, 0.95f)));
        Vec4 border = it.kind == ItemKind::Gun || isConsumable(it.kind) ? rarityColor(it.rarity) : Vec4(0.35f, 0.4f, 0.5f, 1);
        hud_->frame(sx, sy, sw, sh, (sel ? 4 : 2) * u, sel ? WHITE : border);
        hud_->text(sx + 8 * u, sy + 6 * u, 1.4f * u, DIM, std::to_string(i + 1));
        if (!it.empty()) {
            hud_->icon(itemIcon(it), sx + 10 * u, sy + 22 * u, sw - 20 * u, 60 * u, it.kind == ItemKind::Gun ? border : WHITE);
            std::string name = it.kind == ItemKind::Gun ? gunStats(it.gun).name : it.label();
            float ns = std::min(1.6f * u, (sw - 12 * u) / std::max(1.0f, hud_->textWidth(name, 1.0f)));
            hud_->text(sx + sw * 0.5f, sy + 92 * u, ns, WHITE, name, 1);
            std::string sub = it.kind == ItemKind::Gun ? std::string(rarityName(it.rarity)) + "  " + std::to_string(it.count) + "/" +
                                                             std::to_string(gunStats(it.gun).magSize)
                                                       : "X" + std::to_string(it.count);
            float ss = std::min(1.3f * u, (sw - 12 * u) / std::max(1.0f, hud_->textWidth(sub, 1.0f)));
            hud_->text(sx + sw * 0.5f, sy + 118 * u, ss, it.kind == ItemKind::Gun ? border : DIM, sub, 1);
        } else {
            hud_->text(sx + sw * 0.5f, sy + sh * 0.5f - 8 * u, 1.5f * u, Vec4(0.4f, 0.45f, 0.55f, 1), "EMPTY", 1);
        }
        if (hover && in_->pressed[KEY_MOUSE_LEFT]) {
            if (inventorySel < 0) {
                if (!it.empty()) inventorySel = i;
            } else if (inventorySel == i) {
                inventorySel = -1;
            } else {
                game.swapSlots(inventorySel, i);
                inventorySel = -1;
            }
        }
    }

    // Selected item: drop it.
    float y = sy + sh + 20 * u;
    if (inventorySel >= 0 && !p.slots[inventorySel].empty()) {
        const Item& it = p.slots[inventorySel];
        hud_->text(px + 30 * u, y + 12 * u, 1.9f * u, it.kind == ItemKind::Gun ? rarityColor(it.rarity) : WHITE, it.label());
        if (button("DROP", px + pw - 230 * u, y, 200 * u, 46 * u)) {
            game.dropSlot(inventorySel);
            inventorySel = -1;
        }
    } else {
        hud_->text(px + 30 * u, y + 12 * u, 1.5f * u, DIM, "SELECT A SLOT TO MOVE OR DROP IT");
    }

    // Ammo and materials.
    y += 70 * u;
    hud_->text(px + 30 * u, y, 1.6f * u, DIM, "AMMO");
    hud_->text(px + pw * 0.5f + 20 * u, y, 1.6f * u, DIM, "MATERIALS");
    y += 26 * u;
    static const int dropAmount[(int)AmmoType::Count] = {36, 30, 6, 10};
    for (int t = 0; t < (int)AmmoType::Count; ++t) {
        float ry = y + t * 50 * u;
        hud_->icon(ICON_AMMO, px + 30 * u, ry, 56 * u, 40 * u, Vec4(1, 0.9f, 0.6f, 1));
        hud_->text(px + 96 * u, ry + 10 * u, 1.8f * u, WHITE, std::string(ammoName((AmmoType)t)) + "  " + std::to_string(p.ammo[t]));
        if (button("DROP " + std::to_string(dropAmount[t]), px + pw * 0.5f - 190 * u, ry, 170 * u, 40 * u, false, p.ammo[t] > 0))
            game.dropAmmo((AmmoType)t, dropAmount[t]);
    }
    hud_->icon(ICON_MATS, px + pw * 0.5f + 20 * u, y, 70 * u, 40 * u, Vec4(0.95f, 0.75f, 0.5f, 1));
    hud_->text(px + pw * 0.5f + 100 * u, y + 10 * u, 1.8f * u, Vec4(0.95f, 0.75f, 0.5f, 1), "WOOD " + std::to_string(p.matCount[0]) + "   BRICK " + std::to_string(p.matCount[1]) + "   METAL " + std::to_string(p.matCount[2]));
    hud_->text(px + pw * 0.5f + 20 * u, y + 60 * u, 1.4f * u, DIM, "HARVEST MORE WITH THE PICKAXE");
    if (button("CLOSE", px + pw - 230 * u, py + ph - 70 * u, 200 * u, 50 * u, true)) screen = Screen::InGame;
}

void FrontEnd::mapScreen() {
    float u = ui_;
    const Game& g = game;
    hud_->rect(0, 0, W_, H_, Vec4(0, 0, 0, 0.55f));
    float size = std::min(H_ - 120 * u, W_ - 420 * u);
    float mx = (W_ - size) * 0.5f + 90 * u, my = (H_ - size) * 0.5f + 10 * u;
    // Side panel with the legend.
    float lx = mx - 300 * u;
    panel(lx, my, 270 * u, 360 * u);
    hud_->text(lx + 18 * u, my + 18 * u, 2.4f * u, WHITE, "MAP");
    std::string closeKey = keyName(settings.binds.get(Action::Map, 0));
    hud_->text(lx + 18 * u, my + 52 * u, 1.3f * u, DIM, closeKey + " OR ESC CLOSES");
    float ly = my + 90 * u;
    hud_->arrow(lx + 30 * u, ly + 8 * u, 16 * u, 0, WHITE);
    hud_->text(lx + 52 * u, ly, 1.5f * u, WHITE, "YOU");
    ly += 30 * u;
    hud_->rect(lx + 25 * u, ly + 3 * u, 10 * u, 10 * u, Vec4(0.45f, 0.9f, 1, 1));
    hud_->text(lx + 52 * u, ly, 1.5f * u, WHITE, "TEAMMATES");
    ly += 30 * u;
    hud_->rect(lx + 25 * u, ly + 3 * u, 10 * u, 10 * u, Vec4(0.62f, 0.26f, 0.20f, 1));
    hud_->text(lx + 52 * u, ly, 1.5f * u, WHITE, "HOUSES");
    ly += 30 * u;
    hud_->text(lx + 18 * u, ly, 1.5f * u, Vec4(1, 0.85f, 0.4f, 1), "NAMED PLACES");
    ly += 24 * u;
    int found = 0;
    for (const Poi& p : g.world.pois) found += p.discovered ? 1 : 0;
    hud_->text(lx + 18 * u, ly, 1.3f * u, DIM, "DISCOVERED " + std::to_string(found) + " / " + std::to_string(g.world.pois.size()));
    Vec3 me = g.player.alive ? g.player.body.pos : (g.spectating >= 0 ? g.bots[g.spectating].body.pos : g.player.body.pos);
    hud_->text(lx + 18 * u, ly + 28 * u, 1.3f * u, DIM, "YOU'RE IN " + mapGridName(me.x, me.z));

    // The map itself.
    hud_->rect(mx - 4 * u, my - 4 * u, size + 8 * u, size + 8 * u, Vec4(0.05f, 0.07f, 0.12f, 0.9f));
    hud_->image(HUD_TEX_MAP, mx, my, size, size, 0, 0, 1, 1, WHITE);
    // Grid A-F / 1-6.
    for (int i = 1; i < 6; ++i) {
        hud_->rect(mx + i * size / 6, my, 1, size, Vec4(1, 1, 1, 0.18f));
        hud_->rect(mx, my + i * size / 6, size, 1, Vec4(1, 1, 1, 0.18f));
    }
    for (int i = 0; i < 6; ++i) {
        hud_->text(mx + (i + 0.5f) * size / 6, my - 18 * u, 1.3f * u, DIM, std::string(1, (char)('A' + i)), 1);
        hud_->text(mx - 10 * u, my + (i + 0.5f) * size / 6 - 5 * u, 1.3f * u, DIM, std::to_string(i + 1), 2);
    }
    auto toScreen = [&](const Vec3& p, float& sx, float& sy) {
        float uu, vv;
        worldToMapUV(p.x, p.z, uu, vv);
        sx = mx + uu * size;
        sy = my + vv * size;
    };
    float sx, sy;
    for (const Poi& poi : g.world.pois) {
        toScreen(poi.center, sx, sy);
        Vec4 c = poi.discovered ? Vec4(1, 1, 1, 1) : Vec4(1, 1, 1, 0.55f);
        float tw = hud_->textWidth(poi.name, 1.4f * u);
        hud_->rect(sx - tw * 0.5f - 6 * u, sy - 4 * u, tw + 12 * u, 20 * u, Vec4(0, 0, 0, 0.45f));
        hud_->text(sx, sy, 1.4f * u, c, poi.name, 1);
    }
    for (const Bot& b : g.bots) {
        if (!b.alive || b.team != 0) continue;
        toScreen(b.body.pos, sx, sy);
        hud_->rect(sx - 5 * u, sy - 5 * u, 10 * u, 10 * u, Vec4(0.45f, 0.9f, 1, 1));
        hud_->text(sx, sy - 18 * u, 1.1f * u, Vec4(0.45f, 0.9f, 1, 1), b.name, 1);
    }
    {
        const Storm& st = g.storm;
        float scx, scy;
        if (st.radius < 300) {
            toScreen(st.center, scx, scy);
            hud_->circle(scx, scy, st.radius / (2 * MAP_HALF) * size, 3 * u, Vec4(0.8f, 0.45f, 1, 1), 160);
        }
        if (st.nextRadius < st.radius) {
            toScreen(st.nextCenter, scx, scy);
            hud_->circle(scx, scy, st.nextRadius / (2 * MAP_HALF) * size, 3 * u, WHITE, 160);
        }
    }
    hud_->rect(lx + 25 * u, ly + 58 * u, 10 * u, 10 * u, Vec4(0.8f, 0.45f, 1, 1));
    hud_->text(lx + 52 * u, ly + 55 * u, 1.5f * u, WHITE, "STORM EDGE");
    hud_->rect(lx + 25 * u, ly + 86 * u, 10 * u, 10 * u, WHITE);
    hud_->text(lx + 52 * u, ly + 83 * u, 1.5f * u, WHITE, "NEXT CIRCLE");
    toScreen(me, sx, sy);
    hud_->arrow(sx, sy, 22 * u, g.player.alive ? g.player.yaw : 0.0f, WHITE);
}

void FrontEnd::creditKills() {
    int kills = game.player.kills;
    if (kills > creditedKills) {
        settings.kbucks += (kills - creditedKills) * KBUCKS_PER_KILL;
        account.kills += kills - creditedKills;
        creditedKills = kills;
        save();
    }
}

std::vector<int> FrontEnd::shopLineup(long day) const {
    // A deterministic shuffle of every skin except the free one, re-dealt each day.
    std::vector<int> ids;
    for (int i = 1; i < skinCount(); ++i) ids.push_back(i);
    uint64_t x = (uint64_t)day * 0x9E3779B97F4A7C15ull + 12345;
    for (size_t i = ids.size(); i > 1; --i) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        size_t j = (size_t)(x % i);
        std::swap(ids[i - 1], ids[j]);
    }
    std::vector<int> out;
    for (size_t i = 0; i < ids.size() && out.size() < 8; ++i) out.push_back(ids[i]);
    return out;
}

bool FrontEnd::shopFeatured(int skin, long day) const {
    std::vector<int> l = shopLineup(day);
    return l.size() > 1 && (skin == l[0] || skin == l[1]);
}

bool FrontEnd::shopDaily(int skin, long day) const {
    std::vector<int> l = shopLineup(day);
    for (size_t i = 2; i < l.size(); ++i)
        if (l[i] == skin) return true;
    return false;
}

int FrontEnd::shopPrice(int skin, long day) const {
    if (skin == 0) return 0;
    int full = skinPrice(skin);
    return shopFeatured(skin, day) ? full * 3 / 4 : full;  // featured: 25% off
}

// One row of the locker / shop list. Returns true if the row was clicked (to preview it).
static bool skinRow(HudBuilder* hud, const RawInput* in, float x, float y, float w, float h, int i, bool selected,
                    const char* status, const Vec4& statusColor) {
    const Skin& sk = skinAt(i);
    bool hover = in->mouseX >= x && in->mouseX < x + w && in->mouseY >= y && in->mouseY < y + h;
    hud->rect(x, y, w, h, selected ? Vec4(0.2f, 0.3f, 0.5f, 0.95f) : (hover ? Vec4(0.16f, 0.22f, 0.36f, 0.9f) : Vec4(0.08f, 0.1f, 0.18f, 0.9f)));
    float u = h / 48.0f;
    hud->rect(x + 10 * u, y + 10 * u, 28 * u, 28 * u, sk.shirt);        // colour swatches
    hud->rect(x + 10 * u, y + 30 * u, 28 * u, 8 * u, sk.pants);
    hud->rect(x + 32 * u, y + 10 * u, 6 * u, 28 * u, sk.accent);
    hud->text(x + 52 * u, y + 10 * u, 1.9f * u, Vec4(1, 1, 1, 1), sk.name);
    hud->text(x + 52 * u, y + 30 * u, 1.1f * u, skinRarityColor(sk.rarity), skinRarityName(sk.rarity));
    hud->text(x + w - 156 * u, y + 16 * u, 1.4f * u, statusColor, status, 2);
    return hover && in->pressed[KEY_MOUSE_LEFT];
}

void FrontEnd::lockerScreen() {
    float u = ui_;
    float pw = 520 * u, ph = H_ - 100 * u;
    float px = 40 * u, py = 50 * u;
    panel(px, py, pw, ph);
    hud_->text(px + 24 * u, py + 20 * u, 3.0f * u, WHITE, "LOCKER");
    std::string kb = std::to_string(settings.kbucks) + " K-BUCKS";
    hud_->text(px + pw - 24 * u, py + 26 * u, 2.0f * u, Vec4(0.5f, 0.95f, 0.6f, 1), kb, 2);
    std::vector<int> owned;
    for (int i = 0; i < skinCount(); ++i)
        if (settings.ownsSkin(i)) owned.push_back(i);
    const int perPage = 7;
    int pages = std::max(1, ((int)owned.size() + perPage - 1) / perPage);
    lockerPage = std::max(0, std::min(pages - 1, lockerPage));
    hud_->text(px + 24 * u, py + 58 * u, 1.3f * u, DIM, "YOUR SKINS (" + std::to_string(owned.size()) + " OF " + std::to_string(skinCount()) + "). CLICK ONE TO PREVIEW IT, THEN EQUIP.");
    float y = py + 92 * u, rowH = 56 * u;
    int row = 0;
    for (int n = lockerPage * perPage; n < (int)owned.size() && n < (lockerPage + 1) * perPage; ++n) {
        int i = owned[n];
        const Skin& sk = skinAt(i);
        bool equipped = settings.equippedSkin == i;
        float ry = y + row * rowH;
        if (skinRow(hud_, in_, px + 24 * u, ry, pw - 48 * u, rowH - 8 * u, i, lockerSel == i, equipped ? "EQUIPPED" : "OWNED",
                    equipped ? ACCENT : DIM))
            lockerSel = i;
        if (!equipped && button("EQUIP", px + pw - 170 * u, ry + 6 * u, 136 * u, rowH - 20 * u, true)) {
            settings.equippedSkin = i;
            save();
            notify(std::string(sk.name) + " EQUIPPED");
        }
        ++row;
    }
    float by = y + perPage * rowH + 4 * u;
    if (pages > 1) {
        if (button("<", px + 24 * u, by, 48 * u, 36 * u, false, lockerPage > 0)) lockerPage--;
        hud_->text(px + 140 * u, by + 9 * u, 1.4f * u, DIM, "PAGE " + std::to_string(lockerPage + 1) + " / " + std::to_string(pages), 1);
        if (button(">", px + 208 * u, by, 48 * u, 36 * u, false, lockerPage < pages - 1)) lockerPage++;
    }
    hud_->text(px + 24 * u, py + ph - 128 * u, 1.3f * u, DIM, "WANT MORE? THE ITEM SHOP SELLS NEW SKINS EVERY DAY.");
    if (button("ITEM SHOP", px + 24 * u, py + ph - 104 * u, 200 * u, 40 * u)) screen = Screen::Shop;
    if (button("BACK", px + 24 * u, py + ph - 56 * u, 200 * u, 44 * u)) screen = Screen::Lobby;
}

void FrontEnd::shopScreen() {
    float u = ui_;
    float pw = 520 * u, ph = H_ - 100 * u;
    float px = 40 * u, py = 50 * u;
    long day = shopDay > 0 ? shopDay : (long)(std::time(nullptr) / 86400);
    panel(px, py, pw, ph);
    hud_->text(px + 24 * u, py + 20 * u, 3.0f * u, WHITE, "ITEM SHOP");
    std::string kb = std::to_string(settings.kbucks) + " K-BUCKS";
    hud_->text(px + pw - 24 * u, py + 26 * u, 2.0f * u, Vec4(0.5f, 0.95f, 0.6f, 1), kb, 2);
    hud_->text(px + 24 * u, py + 58 * u, 1.3f * u, DIM, "EARN " + std::to_string(KBUCKS_PER_KILL) + " K-BUCKS PER ELIMINATION. NEW SKINS EVERY DAY; FEATURED ONES ARE 25% OFF.");
    float rowH = 56 * u;
    float y = py + 92 * u;
    auto buyRow = [&](int i, float ry) {
        const Skin& sk = skinAt(i);
        bool owned = settings.ownsSkin(i);
        int price = shopPrice(i, day);
        std::string status = owned ? "OWNED" : std::to_string(price) + " K-BUCKS";
        if (skinRow(hud_, in_, px + 24 * u, ry, pw - 48 * u, rowH - 8 * u, i, lockerSel == i, status.c_str(), owned ? ACCENT : DIM))
            lockerSel = i;
        if (!owned) {
            bool canBuy = settings.kbucks >= price;
            if (button("BUY", px + pw - 170 * u, ry + 6 * u, 136 * u, rowH - 20 * u, canBuy, canBuy)) {
                settings.kbucks -= price;
                settings.ownedSkins |= 1ull << i;
                settings.equippedSkin = i;
                save();
                notify(std::string("BOUGHT ") + sk.name + " FOR " + std::to_string(price) + " K-BUCKS");
            }
        }
    };
    std::vector<int> lineup = shopLineup(day);
    hud_->text(px + 24 * u, y, 1.6f * u, ACCENT, "FEATURED");
    y += 26 * u;
    for (size_t n = 0; n < lineup.size() && n < 2; ++n) { buyRow(lineup[n], y); y += rowH; }
    y += 8 * u;
    hud_->text(px + 24 * u, y, 1.6f * u, ACCENT, "DAILY");
    y += 26 * u;
    for (size_t n = 2; n < lineup.size(); ++n) { buyRow(lineup[n], y); y += rowH; }
    hud_->text(px + 24 * u, py + ph - 76 * u, 1.3f * u, DIM, "CLICK A SKIN TO PREVIEW IT ON THE STAGE.");
    if (button("BACK", px + 24 * u, py + ph - 52 * u, 200 * u, 44 * u)) screen = Screen::Lobby;
}

void FrontEnd::matchEndScreen() {
    float u = ui_;
    hud_->rect(0, 0, W_, H_, Vec4(0, 0, 0, 0.5f));
    bool won = game.victory();
    std::string title = won ? "#1 LAST ONE STANDING" : "ELIMINATED";
    hud_->text(W_ * 0.5f, H_ * 0.26f, 7.0f * u, won ? ACCENT : Vec4(1, 0.35f, 0.3f, 1), title, 1);
    std::string place = "PLACED #" + std::to_string(std::max(1, game.placement)) + " OF " +
                        std::to_string(teamCountFor(game.config.teamSize)) + (game.config.teamSize > 1 ? " TEAMS" : "");
    hud_->text(W_ * 0.5f, H_ * 0.26f + 76 * u, 2.4f * u, WHITE, place, 1);
    int teamKills = game.player.kills;
    for (const Bot& b : game.bots)
        if (b.team == 0) teamKills += b.kills;
    std::string stats = "YOUR KILLS " + std::to_string(game.player.kills);
    if (game.config.teamSize > 1) stats += "   TEAM KILLS " + std::to_string(teamKills);
    hud_->text(W_ * 0.5f, H_ * 0.26f + 112 * u, 2.0f * u, DIM, stats, 1);
    hud_->text(W_ * 0.5f, H_ * 0.26f + 140 * u, 1.8f * u, Vec4(0.5f, 0.95f, 0.6f, 1),
               "+" + std::to_string(game.player.kills * KBUCKS_PER_KILL) + " K-BUCKS  (" + std::to_string(settings.kbucks) + " TOTAL)", 1);
    float w = 300 * u, y = H_ * 0.26f + 180 * u;
    if (button("PLAY AGAIN", W_ * 0.5f - w - 10 * u, y, w, 60 * u, true) || in_->pressed[KEY_ENTER]) startMatch();
    else if (button("BACK TO LOBBY", W_ * 0.5f + 10 * u, y, w, 60 * u)) screen = Screen::Lobby;
}

}  // namespace bf
