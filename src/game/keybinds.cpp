// Blockfall - rebindable controls
#include "keybinds.h"
#include <cstdio>

namespace bf {

const char* actionName(Action a) {
    switch (a) {
        case Action::MoveForward: return "MOVE FORWARD";
        case Action::MoveBack: return "MOVE BACK";
        case Action::MoveLeft: return "MOVE LEFT";
        case Action::MoveRight: return "MOVE RIGHT";
        case Action::Jump: return "JUMP";
        case Action::Sprint: return "SPRINT";
        case Action::Crouch: return "CROUCH";
        case Action::Fire: return "SHOOT / USE";
        case Action::Aim: return "AIM";
        case Action::Reload: return "RELOAD / ROTATE";
        case Action::Interact: return "OPEN / PICK UP";
        case Action::Pickaxe: return "PICKAXE";
        case Action::Slot1: return "SLOT 1";
        case Action::Slot2: return "SLOT 2";
        case Action::Slot3: return "SLOT 3";
        case Action::Slot4: return "SLOT 4";
        case Action::Slot5: return "SLOT 5";
        case Action::BuildToggle: return "BUILD MODE";
        case Action::Wall: return "WALL";
        case Action::Floor: return "FLOOR";
        case Action::Ramp: return "RAMP";
        case Action::Cone: return "CONE";
        case Action::Edit: return "EDIT";
        case Action::ResetEdit: return "RESET EDIT";
        case Action::Inventory: return "INVENTORY";
        case Action::Map: return "MAP";
        default: return "?";
    }
}

std::string keyName(int c) {
    if (c >= 'A' && c <= 'Z') return std::string(1, (char)c);
    if (c >= '0' && c <= '9') return std::string(1, (char)c);
    if (c >= 0x70 && c <= 0x7B) return "F" + std::to_string(c - 0x70 + 1);
    if (c >= 0x60 && c <= 0x69) return "NUM " + std::to_string(c - 0x60);
    switch (c) {
        case KEY_NONE: return "-";
        case KEY_MOUSE_LEFT: return "LMB";
        case KEY_MOUSE_RIGHT: return "RMB";
        case KEY_MOUSE_MIDDLE: return "MMB";
        case KEY_MOUSE_X1: return "MOUSE 4";
        case KEY_MOUSE_X2: return "MOUSE 5";
        case KEY_WHEEL_UP: return "WHEEL UP";
        case KEY_WHEEL_DOWN: return "WHEEL DOWN";
        case KEY_BACKSPACE: return "BACKSPACE";
        case KEY_TAB: return "TAB";
        case KEY_ENTER: return "ENTER";
        case KEY_SHIFT: return "SHIFT";
        case KEY_CONTROL: return "CTRL";
        case KEY_ALT: return "ALT";
        case KEY_CAPS: return "CAPS";
        case KEY_SPACE: return "SPACE";
        case 0x25: return "LEFT";
        case 0x26: return "UP";
        case 0x27: return "RIGHT";
        case 0x28: return "DOWN";
        case 0x2D: return "INSERT";
        case 0x2E: return "DELETE";
        case 0x24: return "HOME";
        case 0x23: return "END";
        case 0x21: return "PAGE UP";
        case 0x22: return "PAGE DOWN";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: {
            char buf[16];
            std::snprintf(buf, sizeof buf, "KEY %02X", c);
            return buf;
        }
    }
}

bool keyBindable(int code) {
    return code > 0 && code < KEY_COUNT && code != KEY_ESCAPE && code != KEY_F1 && code != KEY_DELETE;
}

Bindings Bindings::defaults() {
    Bindings b;
    for (int i = 0; i < (int)Action::Count; ++i)
        for (int j = 0; j < BIND_SLOTS; ++j) b.key[i][j] = KEY_NONE;
    auto set = [&](Action a, int k) { b.key[(int)a][0] = k; };
    set(Action::MoveForward, 'W');
    set(Action::MoveBack, 'S');
    set(Action::MoveLeft, 'A');
    set(Action::MoveRight, 'D');
    set(Action::Jump, KEY_SPACE);
    set(Action::Sprint, KEY_SHIFT);
    set(Action::Crouch, KEY_CONTROL);
    set(Action::Fire, KEY_MOUSE_LEFT);
    set(Action::Aim, KEY_MOUSE_RIGHT);
    set(Action::Reload, 'R');
    set(Action::Interact, 'E');
    set(Action::Pickaxe, 'F');
    set(Action::Slot1, '1');
    set(Action::Slot2, '2');
    set(Action::Slot3, '3');
    set(Action::Slot4, '4');
    set(Action::Slot5, '5');
    set(Action::BuildToggle, 'Q');
    set(Action::Wall, 'Z');
    set(Action::Floor, 'X');
    set(Action::Ramp, 'C');
    set(Action::Cone, 'V');
    set(Action::Edit, 'G');
    set(Action::ResetEdit, 'T');
    set(Action::Inventory, KEY_TAB);
    set(Action::Map, 'M');
    return b;
}

Action Bindings::assign(Action a, int code, int slot) {
    if (!keyBindable(code) || slot < 0 || slot >= BIND_SLOTS) return Action::Count;
    for (int j = 0; j < BIND_SLOTS; ++j)
        if (j != slot && key[(int)a][j] == code) key[(int)a][j] = KEY_NONE;
    key[(int)a][slot] = code;
    Action other;
    return sharedWith(a, code, &other, 1) > 0 ? other : Action::Count;
}

int Bindings::sharedWith(Action a, int code, Action* out, int maxOut) const {
    int n = 0;
    if (code == KEY_NONE) return 0;
    for (int i = 0; i < (int)Action::Count; ++i) {
        if (i == (int)a) continue;
        for (int j = 0; j < BIND_SLOTS; ++j)
            if (key[i][j] == code) {
                if (n < maxOut) out[n] = (Action)i;
                ++n;
                break;
            }
    }
    return n;
}

}  // namespace bf
