// Blockfall - builds the 2D HUD as textured quads (crosshair, bars, hotbar, icons, text)
#pragma once
#include "../core/math.h"
#include "atlas.h"
#include "game.h"
#include "keybinds.h"
#include <string>
#include <vector>

namespace bf {

struct HudQuad {
    float x, y, w, h;      // pixels, top-left origin
    float u0, v0, u1, v1;  // texture UVs
    Vec4 color;
    int tex = 0;           // 0 = HUD atlas (text/icons), 1 = map picture
};
constexpr int HUD_TEX_ATLAS = 0;
constexpr int HUD_TEX_MAP = 1;

class HudBuilder {
public:
    std::vector<HudQuad> quads;
    float width = 1280, height = 720;

    void begin(float w, float h);
    void rect(float x, float y, float w, float h, const Vec4& c);
    void frame(float x, float y, float w, float h, float t, const Vec4& c);
    // align: 0 = left, 1 = centre, 2 = right. Text is CHAR_HEIGHT * scale pixels tall.
    void text(float x, float y, float scale, const Vec4& c, const std::string& s, int align = 0);
    float textWidth(const std::string& s, float scale) const;
    // Draws an icon fitted inside the box, keeping its 2:1 shape.
    void icon(IconId id, float x, float y, float w, float h, const Vec4& c);
    // Part of a texture (uv0..uv1) stretched over a box.
    void image(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, const Vec4& c);
    // A circle outline made of small squares.
    void circle(float cx, float cy, float r, float thickness, const Vec4& c, int segments = 64);
    // A small arrow pointing along `angle` (radians, 0 = up, clockwise), built from squares.
    void arrow(float cx, float cy, float size, float angle, const Vec4& c);
    bool project(const Camera& cam, const Vec3& p, float& sx, float& sy) const;
};

// Screen-space centre of the crosshair. Always the exact middle of the screen.
inline void crosshairPosition(float w, float h, float& x, float& y) {
    x = w * 0.5f;
    y = h * 0.5f;
}

void buildHud(const Game& g, float width, float height, float fps, const Bindings& binds, HudBuilder& hud);
// The mini map (top right). Also used by tests.
void buildMiniMap(const Game& g, float width, float height, HudBuilder& hud, float& outX, float& outY, float& outSize);

}  // namespace bf
