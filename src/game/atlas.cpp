// Blockfall - HUD texture atlas: text glyphs, a white cell, and weapon/item icons
#include "atlas.h"
#include "font.h"
#include <cmath>

namespace bf {

namespace {

struct P { float x, y; };
using Poly = std::vector<P>;

// Icon outlines in a 64 x 32 box (y down), all facing right. Overlapping polygons are combined
// with the even-odd rule, so a polygon drawn inside another one cuts a hole.
std::vector<Poly> iconShapes(IconId id) {
    switch (id) {
        case ICON_AR:
            return {
                {{2, 11}, {14, 11}, {16, 14}, {16, 19}, {6, 22}, {2, 22}},      // stock
                {{16, 10}, {44, 10}, {44, 17}, {16, 17}},                         // body
                {{44, 11}, {54, 11}, {54, 16}, {44, 16}},                         // handguard
                {{54, 12.5f}, {62, 12.5f}, {62, 14.5f}, {54, 14.5f}},             // barrel
                {{24, 7}, {38, 7}, {38, 10}, {24, 10}},                           // sight rail
                {{30, 17}, {36, 17}, {39, 27}, {33, 28}},                         // curved magazine
                {{20, 17}, {25, 17}, {23, 25}, {18, 25}},                         // grip
            };
        case ICON_SHOTGUN:
            return {
                {{2, 12}, {14, 13}, {14, 19}, {2, 23}},                           // stock
                {{14, 11}, {30, 11}, {30, 18}, {14, 18}},                         // body
                {{30, 11}, {62, 11}, {62, 14}, {30, 14}},                         // barrel
                {{30, 15}, {56, 15}, {56, 17}, {30, 17}},                         // tube
                {{38, 14.5f}, {50, 14.5f}, {50, 20}, {38, 20}},                   // pump
                {{17, 18}, {22, 18}, {20, 25}, {15, 25}},                         // grip
            };
        case ICON_SMG:
            return {
                {{2, 11}, {13, 11}, {13, 13}, {5, 13}, {5, 17}, {13, 17}, {13, 19}, {2, 19}},  // wire stock
                {{13, 10}, {40, 10}, {40, 18}, {13, 18}},                         // body
                {{40, 12}, {52, 12}, {52, 15}, {40, 15}},                         // barrel
                {{30, 7}, {36, 7}, {36, 10}, {30, 10}},                           // sight
                {{26, 18}, {31, 18}, {31, 30}, {26, 30}},                         // long magazine
                {{16, 18}, {21, 18}, {19, 26}, {14, 26}},                         // grip
            };
        case ICON_PISTOL:
            return {
                {{16, 8}, {50, 8}, {50, 15}, {16, 15}},                           // slide
                {{18, 15}, {46, 15}, {46, 17}, {18, 17}},                         // frame
                {{18, 17}, {28, 17}, {26, 29}, {16, 29}},                         // grip
                {{28, 17}, {37, 17}, {37, 22}, {28, 22}},                         // trigger guard
                {{30, 17}, {35, 17}, {35, 20}, {30, 20}},                         //   (hole)
            };
        case ICON_SNIPER:
            return {
                {{2, 13}, {16, 13}, {18, 16}, {18, 20}, {6, 23}, {2, 23}},      // stock
                {{16, 13}, {36, 13}, {36, 19}, {16, 19}},                         // body
                {{36, 14.5f}, {62, 14.5f}, {62, 16.5f}, {36, 16.5f}},             // long barrel
                {{20, 7}, {36, 7}, {36, 11}, {20, 11}},                           // scope tube
                {{17, 6}, {20, 6}, {20, 12}, {17, 12}},                           // scope ends
                {{36, 6}, {40, 6}, {40, 12}, {36, 12}},
                {{24, 11}, {26, 11}, {26, 13}, {24, 13}},                         // mounts
                {{31, 11}, {33, 11}, {33, 13}, {31, 13}},
                {{20, 19}, {25, 19}, {23, 27}, {18, 27}},                         // grip
                {{28, 19}, {33, 19}, {33, 24}, {28, 24}},                         // magazine
            };
        case ICON_PICKAXE:
            return {
                {{12, 28}, {16, 31}, {42, 7}, {38, 4}},                           // handle
                {{20, 9}, {27, 4.5f}, {35, 2}, {44, 1.5f}, {52, 3.5f}, {59, 8}, {61, 13},  // curved head (outer)
                 {56, 11}, {50, 8}, {44, 7}, {36, 7.5f}, {29, 9.5f}, {23, 13}},    // (inner)
            };
        case ICON_MEDKIT:
            return {
                {{10, 8}, {54, 8}, {54, 29}, {10, 29}},                           // box
                {{26, 3}, {38, 3}, {38, 8}, {26, 8}},                             // handle
                {{29, 5}, {35, 5}, {35, 8}, {29, 8}},                             //   (hole)
                {{29, 11}, {35, 11}, {35, 15.5f}, {42, 15.5f}, {42, 21.5f}, {35, 21.5f},
                 {35, 26}, {29, 26}, {29, 21.5f}, {22, 21.5f}, {22, 15.5f}, {29, 15.5f}},  // cross (hole)
            };
        case ICON_AMMO: {
            std::vector<Poly> v;
            for (int i = 0; i < 3; ++i) {
                float x0 = 15.0f + i * 13.0f;
                v.push_back({{x0, 14}, {x0 + 8, 14}, {x0 + 8, 29}, {x0, 29}});          // case
                v.push_back({{x0, 13}, {x0 + 2, 7}, {x0 + 4, 4}, {x0 + 6, 7}, {x0 + 8, 13}});  // tip
            }
            return v;
        }
        case ICON_MATS:
            return {
                {{6, 4}, {58, 4}, {58, 11}, {6, 11}},
                {{10, 13}, {54, 13}, {54, 20}, {10, 20}},
                {{6, 22}, {58, 22}, {58, 29}, {6, 29}},
            };
        case ICON_BIGPOT:
            return {
                {{26, 2}, {38, 2}, {38, 8}, {26, 8}},                              // cap
                {{28, 8}, {36, 8}, {36, 12}, {28, 12}},                            // neck
                {{20, 12}, {44, 12}, {46, 16}, {46, 29}, {18, 29}, {18, 16}},      // bottle
                {{24, 18}, {40, 18}, {40, 26}, {24, 26}},                          // label (hole)
                {{28, 20}, {36, 20}, {36, 24}, {28, 24}},                          // label fill
            };
        case ICON_MINI: {
            std::vector<Poly> v;
            for (int i = 0; i < 2; ++i) {
                float x0 = 16.0f + i * 20.0f;
                v.push_back({{x0 + 4, 6}, {x0 + 10, 6}, {x0 + 10, 10}, {x0 + 4, 10}});                // cap
                v.push_back({{x0, 10}, {x0 + 14, 10}, {x0 + 14, 28}, {x0, 28}});                      // bottle
                v.push_back({{x0 + 3, 15}, {x0 + 11, 15}, {x0 + 11, 24}, {x0 + 3, 24}});              // label (hole)
            }
            return v;
        }
        case ICON_SHOCKWAVE: {
            // A round grenade with rings coming off it.
            std::vector<Poly> v;
            auto ring = [&](float cx, float cy, float r, int n) {
                Poly p;
                for (int i = 0; i < n; ++i) {
                    float a = (float)i / n * 6.2831853f;
                    p.push_back(P{cx + std::cos(a) * r, cy + std::sin(a) * r});
                }
                return p;
            };
            v.push_back(ring(32, 16, 9, 24));
            v.push_back(ring(32, 16, 5, 24));   // hole
            v.push_back(ring(32, 16, 3, 24));   // core
            v.push_back(ring(32, 16, 14, 32));  // outer ring
            v.push_back(ring(32, 16, 12, 32));  // hole
            return v;
        }
        default:
            return {};
    }
}

bool insidePoly(const Poly& p, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        if ((p[i].y > y) != (p[j].y > y)) {
            float xc = p[j].x + (y - p[j].y) * (p[i].x - p[j].x) / (p[i].y - p[j].y);
            if (x < xc) in = !in;
        }
    }
    return in;
}

void iconCellOrigin(IconId id, int& px, int& py) {
    px = ((int)id % ICON_COLS) * ICON_W;
    py = ATLAS_TEXT_H + ((int)id / ICON_COLS) * ICON_H;
}

}  // namespace

void textCellOrigin(char ch, int& px, int& py) {
    int c = (unsigned char)ch;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 32 || c > 127) c = '?';
    int code = c - 32;
    px = (code % TEXT_COLS) * TEXT_CELL_W;
    py = (code / TEXT_COLS) * TEXT_CELL_H;
}

namespace {
float g_adv[128];
bool g_advInit = false;
void initAdv() {
    if (g_advInit) return;
    for (float& a : g_adv) a = CHAR_ADVANCE;
    g_advInit = true;
}
}  // namespace

float glyphAdvance(char c) {
    initAdv();
    int i = (unsigned char)c;
    return i < 128 ? g_adv[i] : CHAR_ADVANCE;
}

void setGlyphAdvances(const float* advances) {
    initAdv();
    for (int i = 0; i < 128; ++i) g_adv[i] = advances[i];
}

void resetGlyphAdvances() {
    g_advInit = false;
    initAdv();
}

std::vector<uint32_t> buildAtlas() {
    std::vector<uint32_t> px((size_t)ATLAS_W * ATLAS_H, 0x00FFFFFFu);
    auto put = [&](int x, int y, float a) {
        uint32_t al = (uint32_t)(a * 255.0f + 0.5f);
        px[(size_t)y * ATLAS_W + x] = (al << 24) | 0x00FFFFFFu;
    };

    // Pixel font, 4x scaled and centred in the 32 x 32 cells.
    int count = 0;
    const GlyphDef* g = fontGlyphs(count);
    for (int i = 0; i < count; ++i) {
        int cx, cy;
        textCellOrigin(g[i].c, cx, cy);
        for (int y = 0; y < GLYPH_H; ++y)
            for (int x = 0; x < GLYPH_W; ++x)
                if (g[i].rows[y][x] == '#')
                    for (int sy = 0; sy < 4; ++sy)
                        for (int sx = 0; sx < 4; ++sx) put(cx + 6 + x * 4 + sx, cy + 2 + y * 4 + sy, 1.0f);
    }
    // Solid white cell.
    {
        int cx, cy;
        textCellOrigin((char)127, cx, cy);
        for (int y = 0; y < TEXT_CELL_H; ++y)
            for (int x = 0; x < TEXT_CELL_W; ++x) put(cx + x, cy + y, 1.0f);
    }
    // Icons: 4x4 supersampled polygons for smooth edges.
    for (int id = 0; id < ICON_COUNT; ++id) {
        std::vector<Poly> shapes = iconShapes((IconId)id);
        int ox, oy;
        iconCellOrigin((IconId)id, ox, oy);
        for (int y = 0; y < ICON_H; ++y)
            for (int x = 0; x < ICON_W; ++x) {
                int hits = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        float fx = x + (sx + 0.5f) / 4.0f, fy = y + (sy + 0.5f) / 4.0f;
                        bool in = false;
                        for (const Poly& p : shapes)
                            if (insidePoly(p, fx, fy)) in = !in;
                        hits += in ? 1 : 0;
                    }
                if (hits) put(ox + x, oy + y, hits / 16.0f);
            }
    }
    return px;
}

float iconCoverage(const std::vector<uint32_t>& atlas, IconId id) {
    int ox, oy;
    iconCellOrigin(id, ox, oy);
    double sum = 0;
    for (int y = 0; y < ICON_H; ++y)
        for (int x = 0; x < ICON_W; ++x) sum += (atlas[(size_t)(oy + y) * ATLAS_W + ox + x] >> 24) / 255.0;
    return (float)(sum / (ICON_W * ICON_H));
}

void glyphUV(char c, float& u0, float& v0, float& u1, float& v1) {
    int x, y;
    textCellOrigin(c, x, y);
    if ((unsigned char)c == 127) {
        // The solid cell: stay well inside it so mipmaps never blend in the neighbours.
        u0 = (float)(x + 8) / ATLAS_W;
        v0 = (float)(y + 8) / ATLAS_H;
        u1 = (float)(x + TEXT_CELL_W - 8) / ATLAS_W;
        v1 = (float)(y + TEXT_CELL_H - 8) / ATLAS_H;
        return;
    }
    u0 = (float)x / ATLAS_W;
    v0 = (float)y / ATLAS_H;
    u1 = (float)(x + TEXT_CELL_W) / ATLAS_W;
    v1 = (float)(y + TEXT_CELL_H) / ATLAS_H;
}

void iconUV(IconId id, float& u0, float& v0, float& u1, float& v1) {
    int x, y;
    iconCellOrigin(id, x, y);
    u0 = (float)x / ATLAS_W;
    v0 = (float)y / ATLAS_H;
    u1 = (float)(x + ICON_W) / ATLAS_W;
    v1 = (float)(y + ICON_H) / ATLAS_H;
}

void whiteUV(float& u, float& v) {
    int x, y;
    textCellOrigin((char)127, x, y);
    u = (x + TEXT_CELL_W * 0.5f) / ATLAS_W;
    v = (y + TEXT_CELL_H * 0.5f) / ATLAS_H;
}

}  // namespace bf
