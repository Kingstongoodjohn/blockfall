// Blockfall - menus: lobby, friends, settings (keybinds), pause and end-of-match screens.
// Owns the Game and turns raw keys into game input using the player's keybinds.
#pragma once
#include <string>
#include <vector>
#include "game.h"
#include "hud.h"
#include "keybinds.h"
#include "scene.h"
#include "settings.h"
#include "account.h"

namespace bf {

// Everything the platform layer collected this frame.
struct RawInput {
    bool down[KEY_COUNT] = {};
    bool pressed[KEY_COUNT] = {};
    bool released[KEY_COUNT] = {};
    float mouseDx = 0, mouseDy = 0;  // raw mouse movement (while the mouse is locked)
    float mouseX = 0, mouseY = 0;    // cursor position in pixels (while it's free)
    std::string text;                // characters typed this frame
    bool focusLost = false;
};

enum class Screen : int { Lobby = 0, Friends, Settings, InGame, Paused, MatchEnd, Inventory, Map, Locker, Shop, SignIn };

constexpr int MAX_PARTY = 3;  // you + 3 friends = a squad

class FrontEnd {
public:
    FrontEnd();
    // Loads settings from `path` (if it exists). Changes are saved back there.
    void init(const std::string& settingsPath);
    // Runs one frame: menus, game update, and fills the scene + HUD for drawing.
    void frame(const RawInput& in, float dt, float width, float height, float fps, SceneList& scene, HudBuilder& hud);

    bool wantsMouseLocked() const { return screen == Screen::InGame; }
    bool quitRequested = false;

    // Actions (also used by tests).
    void startMatch();
    bool invite(const std::string& name);
    void uninvite(const std::string& name);
    void setTeamSize(int n);
    InputState gameInput(const RawInput& in) const;

    Settings settings;
    Game game;
    Screen screen = Screen::Lobby;
    int teamSize = 1;
    std::vector<std::string> party;  // invited friends
    int rebinding = -1;              // action waiting for a key, or -1
    int rebindSlot = 0;              // 0 = primary, 1 = secondary
    int inventorySel = -1;           // slot picked on the inventory screen
    int settingsTab = 0;             // 0 = controls, 1 = graphics
    int lockerSel = 0;               // skin being looked at in the locker
    int creditedKills = 0;           // kills already paid out this match
    std::string message;             // short notice on menu screens
    float messageTime = 0;

    // Accounts: everything you own lives in the signed-in account's file.
    AccountStore accounts;
    Account account;
    bool signedIn = false;
    bool signIn(const std::string& name, const std::string& password, std::string& error);
    bool createAccount(const std::string& name, const std::string& password, std::string& error);
    void signOut();
    std::string signName_, signPass_, signError_;
    // The shop rotates daily: 2 featured skins (25% off) and 6 daily ones. day = days since 1970.
    int shopPrice(int skin, long day) const;
    bool shopFeatured(int skin, long day) const;
    bool shopDaily(int skin, long day) const;
    std::vector<int> shopLineup(long day) const;   // featured first, then daily
    long shopDay = 0;   // which day's rotation to show (0 = use the real clock)
    int lockerPage = 0;

private:
    // Immediate-mode widgets (draw and handle the click in one call).
    bool button(const std::string& label, float x, float y, float w, float h, bool primary = false, bool enabled = true);
    bool textField(int id, std::string& value, const std::string& placeholder, float x, float y, float w, float h, bool secret = false);
    void panel(float x, float y, float w, float h);
    void notify(const std::string& m);
    void save();

    void lobbyScreen();
    void friendsScreen();
    void settingsScreen();
    void pauseScreen();
    void matchEndScreen();
    void inventoryScreen();
    void mapScreen();
    void lockerScreen();
    void shopScreen();
    void signInScreen();
    void creditKills();
    void graphicsTab(float px, float py, float pw, float ph);
    void controlsTab(float px, float py, float pw, float ph);
    // 0 = not clicked, 1 = left click, 2 = right click.
    int keyButton(const std::string& label, float x, float y, float w, float h, bool waiting);

    std::string settingsPath_;
    const RawInput* in_ = nullptr;
    HudBuilder* hud_ = nullptr;
    float W_ = 1280, H_ = 720, ui_ = 1;
    float time_ = 0;
    float matchOverTime_ = 0;
    bool settingsFromPause_ = false;
    int focus_ = -1;  // text field with keyboard focus
    std::string friendInput_;
    std::string nameInput_;
    uint64_t nextSeed_ = 1337;
};

}  // namespace bf
