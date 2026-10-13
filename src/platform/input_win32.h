// Blockfall - collects keyboard, mouse and typed text from Win32 messages
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../game/frontend.h"

namespace bf {

class Win32Input {
public:
    void init(HWND hwnd);
    // Returns true if the message was fully handled.
    bool handleMessage(UINT msg, WPARAM wp, LPARAM lp);
    // This frame's input; clears the per-frame "pressed" flags.
    RawInput takeFrame();
    // Locks (hides + confines) the mouse for playing, or frees it for menus.
    void setLocked(bool on);
    bool locked() const { return locked_; }

private:
    void keyEvent(int code, bool down);

    HWND hwnd_ = nullptr;
    bool locked_ = false;
    RawInput cur_;
};

}  // namespace bf
