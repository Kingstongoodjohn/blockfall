// Blockfall - collects keyboard, mouse and typed text from Win32 messages
#include "input_win32.h"
#include <cstring>
#include <windowsx.h>

namespace bf {

void Win32Input::init(HWND hwnd) {
    hwnd_ = hwnd;
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;  // generic desktop
    rid.usUsage = 0x02;      // mouse
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

void Win32Input::setLocked(bool on) {
    if (on == locked_) return;
    if (on && GetForegroundWindow() != hwnd_) return;  // only lock while we have focus
    locked_ = on;
    if (on) {
        RECT r;
        GetClientRect(hwnd_, &r);
        POINT tl = {r.left, r.top}, br = {r.right, r.bottom};
        ClientToScreen(hwnd_, &tl);
        ClientToScreen(hwnd_, &br);
        RECT clip = {tl.x, tl.y, br.x, br.y};
        ClipCursor(&clip);
        SetCapture(hwnd_);
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        ClipCursor(nullptr);
        ReleaseCapture();
        while (ShowCursor(TRUE) < 0) {}
        // Put the cursor in the middle so menus are easy to reach.
        RECT r;
        GetClientRect(hwnd_, &r);
        POINT c = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
        ClientToScreen(hwnd_, &c);
        SetCursorPos(c.x, c.y);
        cur_.mouseX = (float)((r.left + r.right) / 2);
        cur_.mouseY = (float)((r.top + r.bottom) / 2);
    }
}

void Win32Input::keyEvent(int code, bool down) {
    if (code <= 0 || code >= KEY_COUNT) return;
    if (down && !cur_.down[code]) cur_.pressed[code] = true;
    if (!down && cur_.down[code]) cur_.released[code] = true;
    cur_.down[code] = down;
}

bool Win32Input::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INPUT: {
            if (!locked_) return false;
            RAWINPUT ri;
            UINT size = sizeof(ri);
            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                cur_.mouseDx += (float)ri.data.mouse.lLastX;
                cur_.mouseDy += (float)ri.data.mouse.lLastY;
            }
            return false;  // let DefWindowProc clean up
        }
        case WM_MOUSEMOVE:
            cur_.mouseX = (float)GET_X_LPARAM(lp);
            cur_.mouseY = (float)GET_Y_LPARAM(lp);
            return true;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            keyEvent((int)wp, true);
            return true;  // also stops Alt / F10 from opening the window menu
        case WM_KEYUP:
        case WM_SYSKEYUP:
            keyEvent((int)wp, false);
            return true;
        case WM_CHAR:
            if (wp >= 32 && wp < 127) cur_.text += (char)wp;
            return true;
        case WM_LBUTTONDOWN: keyEvent(KEY_MOUSE_LEFT, true); return true;
        case WM_LBUTTONUP: keyEvent(KEY_MOUSE_LEFT, false); return true;
        case WM_RBUTTONDOWN: keyEvent(KEY_MOUSE_RIGHT, true); return true;
        case WM_RBUTTONUP: keyEvent(KEY_MOUSE_RIGHT, false); return true;
        case WM_MBUTTONDOWN: keyEvent(KEY_MOUSE_MIDDLE, true); return true;
        case WM_MBUTTONUP: keyEvent(KEY_MOUSE_MIDDLE, false); return true;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            int code = GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? KEY_MOUSE_X1 : KEY_MOUSE_X2;
            keyEvent(code, msg == WM_XBUTTONDOWN);
            return false;  // XBUTTON messages want DefWindowProc / TRUE; harmless either way
        }
        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wp);
            cur_.pressed[delta > 0 ? KEY_WHEEL_UP : KEY_WHEEL_DOWN] = true;
            return true;
        }
        case WM_KILLFOCUS:
            setLocked(false);
            std::memset(cur_.down, 0, sizeof cur_.down);
            cur_.focusLost = true;
            return false;
        default:
            return false;
    }
}

RawInput Win32Input::takeFrame() {
    RawInput out = cur_;
    std::memset(cur_.pressed, 0, sizeof cur_.pressed);
    std::memset(cur_.released, 0, sizeof cur_.released);
    cur_.mouseDx = cur_.mouseDy = 0;
    cur_.text.clear();
    cur_.focusLost = false;
    return out;
}

}  // namespace bf
