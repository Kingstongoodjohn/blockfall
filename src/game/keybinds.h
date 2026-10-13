// Blockfall - rebindable controls. Key codes are Windows virtual-key codes (plain numbers),
// plus a few extra codes for the mouse wheel.
#pragma once
#include <string>

namespace bf {

enum class Action : int {
    MoveForward = 0, MoveBack, MoveLeft, MoveRight,
    Jump, Sprint, Crouch,
    Fire, Aim, Reload, Interact,
    Pickaxe, Slot1, Slot2, Slot3, Slot4, Slot5,
    BuildToggle, Wall, Floor, Ramp, Cone, Edit, ResetEdit,
    Inventory, Map,
    Count
};

constexpr int KEY_NONE = 0;
constexpr int KEY_MOUSE_LEFT = 0x01;
constexpr int KEY_MOUSE_RIGHT = 0x02;
constexpr int KEY_MOUSE_MIDDLE = 0x04;
constexpr int KEY_MOUSE_X1 = 0x05;
constexpr int KEY_MOUSE_X2 = 0x06;
constexpr int KEY_BACKSPACE = 0x08;
constexpr int KEY_TAB = 0x09;
constexpr int KEY_ENTER = 0x0D;
constexpr int KEY_SHIFT = 0x10;
constexpr int KEY_CONTROL = 0x11;
constexpr int KEY_ALT = 0x12;
constexpr int KEY_CAPS = 0x14;
constexpr int KEY_ESCAPE = 0x1B;
constexpr int KEY_SPACE = 0x20;
constexpr int KEY_DELETE = 0x2E;
constexpr int KEY_F1 = 0x70;
constexpr int KEY_F2 = 0x71;
constexpr int KEY_WHEEL_UP = 0x100;
constexpr int KEY_WHEEL_DOWN = 0x101;
constexpr int KEY_COUNT = 0x102;

const char* actionName(Action a);
std::string keyName(int code);
// Esc, F1 and Delete are reserved (menus, help, clearing a key) and can't be bound.
bool keyBindable(int code);

// Every action has a primary and a secondary key (either one works).
constexpr int BIND_SLOTS = 2;

struct Bindings {
    int key[(int)Action::Count][BIND_SLOTS];

    static Bindings defaults();
    int get(Action a, int slot = 0) const { return key[(int)a][slot]; }
    bool uses(Action a, int code) const {
        return code > 0 && (key[(int)a][0] == code || key[(int)a][1] == code);
    }
    // Binds `code` to one slot of `a`. The same key may be used by several actions (like a
    // scroll-wheel reset on wall + edit); the first other action sharing it is returned so the
    // UI can mention it, or Action::Count if none. Binding a key that the other slot of the same
    // action already has just clears that slot.
    Action assign(Action a, int code, int slot = 0);
    // Other actions (not `a`) that use `code`.
    int sharedWith(Action a, int code, Action* out, int maxOut) const;
    void clear(Action a, int slot) { key[(int)a][slot] = KEY_NONE; }
};

}  // namespace bf
