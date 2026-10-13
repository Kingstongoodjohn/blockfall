// Blockfall - HUD texture atlas: text glyphs, a white cell, and weapon/item icons
#pragma once
#include <cstdint>
#include <vector>

namespace bf {

// Text: 16 x 6 cells for ASCII 32..127. Cell 127 is solid white (plain rectangles).
constexpr int TEXT_CELL_W = 32;
constexpr int TEXT_CELL_H = 32;
constexpr int TEXT_COLS = 16;
constexpr int TEXT_ROWS = 6;
// Icons: 64 x 32 cells below the text.
constexpr int ICON_W = 64;
constexpr int ICON_H = 32;
constexpr int ICON_COLS = 6;
constexpr int ICON_ROWS = 2;
constexpr int ATLAS_W = TEXT_COLS * TEXT_CELL_W;                   // 512
constexpr int ATLAS_TEXT_H = TEXT_ROWS * TEXT_CELL_H;              // 192
constexpr int ATLAS_H = ATLAS_TEXT_H + ICON_ROWS * ICON_H;         // 256
// Layout units: a cell is 8 x 8 "points" (the 32 x 32 cell); 4 texture pixels per point.
// The built-in pixel font advances 6 points per character; a system font sets its own widths.
constexpr float CHAR_CELL = 8.0f;
constexpr float CHAR_ADVANCE = 6.0f;
constexpr float CHAR_HEIGHT = 8.0f;
// How far to move after drawing a character (points). Glyphs are centred in their cells.
float glyphAdvance(char c);
// Installs per-character advances (128 entries, points), e.g. from a proportional system font.
void setGlyphAdvances(const float* advances);
void resetGlyphAdvances();

enum IconId : int {
    ICON_AR = 0,
    ICON_SHOTGUN,
    ICON_SMG,
    ICON_PISTOL,
    ICON_SNIPER,
    ICON_PICKAXE,
    ICON_MEDKIT,
    ICON_AMMO,
    ICON_MATS,
    ICON_BIGPOT,
    ICON_MINI,
    ICON_SHOCKWAVE,
    ICON_COUNT
};

// RGBA8 pixels (0xAABBGGRR, white with coverage in alpha). The text cells use the built-in
// pixel font; the Windows renderer may replace them with a smooth system font.
std::vector<uint32_t> buildAtlas();
// Fraction (0..1) of an icon's cell that is covered; used by tests.
float iconCoverage(const std::vector<uint32_t>& atlas, IconId id);

void textCellOrigin(char c, int& px, int& py);
void glyphUV(char c, float& u0, float& v0, float& u1, float& v1);
void iconUV(IconId id, float& u0, float& v0, float& u1, float& v1);
void whiteUV(float& u, float& v);

}  // namespace bf
