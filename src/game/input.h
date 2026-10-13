// Blockfall - platform-independent input snapshot for one frame
#pragma once

namespace bf {

struct InputState {
    // Held keys
    bool forward = false, back = false, left = false, right = false;
    bool sprint = false;   // Shift
    bool crouch = false;   // Ctrl
    bool fire = false;     // Left mouse
    bool aim = false;      // Right mouse
    bool interact = false; // E

    // Pressed this frame
    bool jumpPressed = false;      // Space
    bool firePressed = false;
    bool fireReleased = false;
    bool aimPressed = false;
    bool reloadPressed = false;    // R
    bool interactPressed = false;
    bool buildTogglePressed = false;  // Q
    int buildPiecePressed = -1;       // Z/X/C/V -> 0..3 (Wall, Floor, Ramp, Cone)
    int slotPressed = -1;             // 1..5 -> 0..4
    bool pickaxePressed = false;      // F
    bool editPressed = false;         // G
    bool resetEditPressed = false;    // T: put an edited build back to full
    bool restartPressed = false;      // Enter
    bool helpPressed = false;         // F1
    bool mapPressed = false;          // M (handled by the front end)

    // Mouse movement in pixels since last frame
    float mouseDx = 0, mouseDy = 0;
};

}  // namespace bf
