// Blockfall - builds the 2D HUD as textured quads (crosshair, bars, hotbar, icons, text)
#include "hud.h"
#include "map.h"
#include "scene.h"
#include <cmath>
#include <cstdio>

namespace bf {

namespace {
const Vec4 WHITE(1, 1, 1, 1);
const Vec4 SHADOW(0, 0, 0, 0.55f);
const Vec4 PANEL(0.05f, 0.07f, 0.12f, 0.55f);
const Vec4 HEALTH_COL(0.35f, 0.85f, 0.35f, 1);
const Vec4 SHIELD_COL(0.30f, 0.65f, 1.0f, 1);
const Vec4 STAMINA_COL(1.0f, 0.82f, 0.25f, 1);
const Vec4 MATS_COL(0.95f, 0.75f, 0.5f, 1);
const Vec4 DIM_TEXT(0.8f, 0.85f, 0.95f, 0.9f);

Vec4 withAlpha(Vec4 c, float a) {
    c.w *= a;
    return c;
}
std::string key(const Bindings& b, Action a) { return keyName(b.get(a)); }
// Gun icons are tinted with a lighter version of their rarity colour.
Vec4 mixIcon(const Vec4& c) { return {c.x + (1 - c.x) * 0.35f, c.y + (1 - c.y) * 0.35f, c.z + (1 - c.z) * 0.35f, 1}; }
}  // namespace

void HudBuilder::begin(float w, float h) {
    quads.clear();
    width = w;
    height = h;
}

void HudBuilder::rect(float x, float y, float w, float h, const Vec4& c) {
    float u, v;
    whiteUV(u, v);
    quads.push_back({x, y, w, h, u, v, u, v, c});
}

void HudBuilder::frame(float x, float y, float w, float h, float t, const Vec4& c) {
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y + t, t, h - 2 * t, c);
    rect(x + w - t, y + t, t, h - 2 * t, c);
}

float HudBuilder::textWidth(const std::string& s, float scale) const {
    float w = 0;
    for (char c : s) w += glyphAdvance(c) * scale;
    return w;
}

void HudBuilder::text(float x, float y, float scale, const Vec4& c, const std::string& s, int align) {
    float w = textWidth(s, scale);
    if (align == 1) x -= w * 0.5f;
    else if (align == 2) x -= w;
    x = std::floor(x);
    y = std::floor(y);
    // Drop shadow then glyphs.
    for (int pass = 0; pass < 2; ++pass) {
        float o = pass == 0 ? std::max(1.0f, scale * 0.5f) : 0;
        Vec4 col = pass == 0 ? Vec4(0, 0, 0, 0.55f * c.w) : c;
        float cx = x;
        for (char ch : s) {
            float adv = glyphAdvance(ch) * scale;
            if (ch != ' ') {
                HudQuad q;
                glyphUV(ch, q.u0, q.v0, q.u1, q.v1);
                // The glyph is centred in its cell; line it up with its advance box.
                q.x = cx + o + (adv - CHAR_CELL * scale) * 0.5f;
                q.y = y + o;
                q.w = CHAR_CELL * scale;
                q.h = CHAR_HEIGHT * scale;
                q.color = col;
                quads.push_back(q);
            }
            cx += adv;
        }
    }
}

void HudBuilder::icon(IconId id, float x, float y, float w, float h, const Vec4& c) {
    if (id >= ICON_COUNT) return;
    float iw = w, ih = w * 0.5f;
    if (ih > h) { ih = h; iw = h * 2.0f; }
    HudQuad q;
    iconUV(id, q.u0, q.v0, q.u1, q.v1);
    q.x = x + (w - iw) * 0.5f;
    q.y = y + (h - ih) * 0.5f;
    q.w = iw;
    q.h = ih;
    q.color = c;
    quads.push_back(q);
}

void HudBuilder::image(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, const Vec4& c) {
    HudQuad q{x, y, w, h, u0, v0, u1, v1, c};
    q.tex = tex;
    quads.push_back(q);
}

void HudBuilder::circle(float cx, float cy, float r, float thickness, const Vec4& c, int segments) {
    for (int i = 0; i < segments; ++i) {
        float a = 2 * PI * i / segments;
        rect(cx + std::cos(a) * r - thickness * 0.5f, cy + std::sin(a) * r - thickness * 0.5f, thickness, thickness, c);
    }
}

void HudBuilder::arrow(float cx, float cy, float size, float angle, const Vec4& c) {
    // A chevron: head square plus two trailing squares on each side.
    float s = std::max(2.0f, size * 0.28f);
    float dx = std::sin(angle), dy = -std::cos(angle);
    float px = -dy, py = dx;
    auto dot = [&](float ax, float ay) { rect(cx + ax - s * 0.5f, cy + ay - s * 0.5f, s, s, c); };
    dot(dx * size * 0.5f, dy * size * 0.5f);
    for (int i = 1; i <= 2; ++i) {
        float back = size * 0.5f - i * s * 0.9f;
        dot(dx * back + px * i * s * 0.8f, dy * back + py * i * s * 0.8f);
        dot(dx * back - px * i * s * 0.8f, dy * back - py * i * s * 0.8f);
    }
}

bool HudBuilder::project(const Camera& cam, const Vec3& p, float& sx, float& sy) const {
    Mat4 vp = cameraView(cam) * cameraProj(cam, width / height);
    Vec4 c = transform(Vec4(p.x, p.y, p.z, 1), vp);
    if (c.w < 0.1f) return false;
    float nx = c.x / c.w, ny = c.y / c.w;
    if (nx < -1.2f || nx > 1.2f || ny < -1.2f || ny > 1.2f) return false;
    sx = (nx * 0.5f + 0.5f) * width;
    sy = (1.0f - (ny * 0.5f + 0.5f)) * height;
    return true;
}

void buildHud(const Game& g, float W, float H, float fps, const Bindings& binds, HudBuilder& hud) {
    hud.begin(W, H);
    const Player& p = g.player;
    float ui = std::max(0.75f, H / 900.0f);  // UI scale
    float cx, cy;
    crosshairPosition(W, H, cx, cy);

    // Red vignette when hurt.
    if (p.hurtTimer > 0) {
        Vec4 red(0.9f, 0.05f, 0.05f, 0.35f * p.hurtTimer / 0.35f);
        float t = 60 * ui;
        hud.rect(0, 0, W, t, red);
        hud.rect(0, H - t, W, t, red);
        hud.rect(0, t, t, H - 2 * t, red);
        hud.rect(W - t, t, t, H - 2 * t, red);
    }

    // Floating damage numbers and "+materials".
    for (const DamageNumber& d : g.damageNumbers) {
        float sx, sy;
        if (!hud.project(g.camera, d.pos, sx, sy)) continue;
        float a = 1.0f - d.age;
        std::string txt = d.mats ? "+" + std::to_string(d.amount) : std::to_string(d.amount);
        hud.text(sx, sy, 2.8f * ui, withAlpha(d.color, a), txt, 1);
    }

    // Running damage totals over the target you're shooting (reset after 1 s without a hit).
    for (const DamageStack& d : g.damageStacks) {
        float sx, sy;
        if (!hud.project(g.camera, d.pos, sx, sy)) continue;
        float a = d.sinceHit > 0.7f ? (1.0f - d.sinceHit) / 0.3f : 1.0f;
        float pop = d.sinceHit < 0.08f ? 1.0f + (0.08f - d.sinceHit) * 4.0f : 1.0f;
        Vec4 col = d.head ? Vec4(1, 0.8f, 0.2f, a) : Vec4(1, 1, 1, a);
        hud.text(sx, sy - 10 * ui, 3.6f * ui * pop, col, std::to_string((int)std::lround(d.total)), 1);
    }

    // Teammate name tags and health. Enemies never show a health bar - you only see the damage numbers.
    for (const Bot& b : g.bots) {
        if (!b.alive || b.team != 0) continue;
        float sx, sy;
        if (!hud.project(g.camera, b.body.pos + Vec3(0, 2.3f, 0), sx, sy)) continue;
        float bw = 60 * ui, bh = 6 * ui;
        if (b.team == 0) {
            float dist = length(b.body.pos - g.camera.eye);
            float s = clampf(1.8f - dist * 0.01f, 1.2f, 1.8f) * ui;
            hud.text(sx, sy - 18 * ui, s, b.down ? Vec4(1, 0.6f, 0.35f, 1) : Vec4(0.45f, 0.9f, 1, 0.95f), b.down ? b.name + " - KNOCKED" : b.name, 1);
            hud.rect(sx - bw * 0.5f - 1, sy - 1, bw + 2, bh + 2, SHADOW);
            if (b.down) hud.rect(sx - bw * 0.5f, sy, bw * clampf(b.downHealth / KNOCK_HEALTH, 0, 1), bh, Vec4(0.95f, 0.45f, 0.25f, 1));
            else hud.rect(sx - bw * 0.5f, sy, bw * clampf(b.health / 100.0f, 0, 1), bh, HEALTH_COL);
        }
    }

    const Item& held = g.heldItem();
    bool scoped = p.alive && p.ads && p.mode == Mode::Combat && held.kind == ItemKind::Gun && held.gun == GunType::Sniper;
    if (scoped) {
        // Scope view: everything outside a circle goes black, with fine cross lines and a range ring.
        float r = std::min(W, H) * 0.42f;
        const int N = 48;
        Vec4 black(0.02f, 0.02f, 0.03f, 0.97f);
        // Approximate the round window with horizontal strips.
        for (int i = 0; i < N; ++i) {
            float y0 = cy - r + (2 * r) * i / N, y1 = cy - r + (2 * r) * (i + 1) / N;
            float ym = (y0 + y1) * 0.5f;
            float half = std::sqrt(std::max(0.0f, r * r - (ym - cy) * (ym - cy)));
            hud.rect(0, y0, cx - half, y1 - y0, black);
            hud.rect(cx + half, y0, W - (cx + half), y1 - y0, black);
        }
        hud.rect(0, 0, W, cy - r, black);
        hud.rect(0, cy + r, W, H - (cy + r), black);
        Vec4 line(0.05f, 0.05f, 0.06f, 0.9f);
        hud.rect(cx - r, cy - 1, r - 14 * ui, 2, line);
        hud.rect(cx + 14 * ui, cy - 1, r - 14 * ui, 2, line);
        hud.rect(cx - 1, cy - r, 2, r - 14 * ui, line);
        hud.rect(cx - 1, cy + 14 * ui, 2, r - 14 * ui, line);
        for (int i = 1; i <= 3; ++i) {  // range ticks
            hud.rect(cx - 6 * ui, cy + i * 40 * ui - 1, 12 * ui, 2, line);
            hud.rect(cx + i * 40 * ui - 1, cy - 6 * ui, 2, 12 * ui, line);
            hud.rect(cx - i * 40 * ui - 1, cy - 6 * ui, 2, 12 * ui, line);
        }
        hud.text(cx, cy + r - 26 * ui, 1.4f * ui, Vec4(1, 1, 1, 0.6f), std::to_string((int)g.aimDistanceWorld()) + " M", 1);
    }
    if (p.alive) {
        // ---- crosshair: always the exact centre of the screen
        Vec4 ch(1, 1, 1, 0.95f);
        if (p.mode == Mode::Combat && held.kind == ItemKind::Gun) {
            float spread = g.currentSpread();
            float gap = std::tan(deg2rad(spread * 0.5f)) / std::tan(g.camera.fovY * 0.5f) * (H * 0.5f);
            gap = std::max(gap, 5.0f * ui);
            float len = 9 * ui, th = std::max(2.0f, 2 * ui);
            hud.rect(cx - th * 0.5f, cy - gap - len, th, len, ch);
            hud.rect(cx - th * 0.5f, cy + gap, th, len, ch);
            hud.rect(cx - gap - len, cy - th * 0.5f, len, th, ch);
            hud.rect(cx + gap, cy - th * 0.5f, len, th, ch);
            hud.rect(cx - th * 0.5f, cy - th * 0.5f, th, th, ch);
        } else {
            float d = std::max(4.0f, 4 * ui);
            hud.rect(cx - d * 0.5f, cy - d * 0.5f, d, d, ch);
        }
        // Magazine count next to the crosshair, and a reload ring while reloading.
        if (p.mode == Mode::Combat && held.kind == ItemKind::Gun) {
            const GunStats& gs = gunStats(held.gun);
            const GunState& gst = p.gunStates[p.selected];
            Vec4 col = held.count == 0 ? Vec4(1, 0.4f, 0.3f, 1) : (held.count <= gs.magSize / 4 ? Vec4(1, 0.8f, 0.3f, 1) : Vec4(1, 1, 1, 0.9f));
            hud.text(cx + 26 * ui, cy - 6 * ui, 2.0f * ui, col, std::to_string(held.count) + "/" + std::to_string(gs.magSize));
            if (gst.reloadTimer > 0) {
                float f = 1.0f - gst.reloadTimer / gs.reloadTime;
                int segs = 40;
                for (int i = 0; i < (int)(segs * f); ++i) {
                    float a = -PI * 0.5f + 2 * PI * i / segs;
                    hud.rect(cx + std::cos(a) * 24 * ui - 2 * ui, cy + std::sin(a) * 24 * ui - 2 * ui, 4 * ui, 4 * ui, Vec4(1, 0.85f, 0.4f, 1));
                }
                hud.text(cx, cy + 32 * ui, 1.5f * ui, Vec4(1, 0.85f, 0.4f, 1), "RELOADING", 1);
            } else if (held.count == 0) {
                hud.text(cx, cy + 32 * ui, 1.5f * ui, Vec4(1, 0.4f, 0.3f, 1),
                         p.ammo[(int)gs.ammo] > 0 ? "EMPTY - " + key(binds, Action::Reload) + " TO RELOAD" : "NO AMMO", 1);
            }
        }
        if (g.hitMarker > 0) {
            Vec4 hm = g.hitMarkerHead ? Vec4(1, 0.85f, 0.2f, 1) : WHITE;
            float s = 3 * ui;
            for (int i = 3; i <= 6; ++i) {
                float o = i * s;
                hud.rect(cx - o - s * 0.5f, cy - o - s * 0.5f, s, s, hm);
                hud.rect(cx + o - s * 0.5f, cy - o - s * 0.5f, s, s, hm);
                hud.rect(cx - o - s * 0.5f, cy + o - s * 0.5f, s, s, hm);
                hud.rect(cx + o - s * 0.5f, cy + o - s * 0.5f, s, s, hm);
            }
        }

        // ---- health, shield and stamina (bottom left)
        float bx = 30 * ui, bw = 320 * ui, bh = 22 * ui;
        float by = H - 40 * ui - bh;
        hud.rect(bx - 3, by - 3, bw + 6, bh + 6, PANEL);
        if (p.down) {
            // Knocked: the health bar becomes the bleed-out bar.
            hud.rect(bx, by, bw * clampf(p.downHealth / KNOCK_HEALTH, 0, 1), bh, Vec4(0.95f, 0.45f, 0.25f, 1));
            hud.text(bx + bw + 12 * ui, by + 1 * ui, 2.4f * ui, WHITE, std::to_string((int)std::ceil(p.downHealth)));
            hud.text(W * 0.5f, H * 0.62f, 3.0f * ui, Vec4(1, 0.5f, 0.3f, 1), "KNOCKED", 1);
            if (p.reviveProgress > 0) {
                hud.text(W * 0.5f, H * 0.62f + 36 * ui, 1.8f * ui, Vec4(0.5f, 1, 0.6f, 1), "A TEAMMATE IS REVIVING YOU", 1);
                hud.rect(W * 0.5f - 120 * ui, H * 0.62f + 62 * ui, 240 * ui, 8 * ui, SHADOW);
                hud.rect(W * 0.5f - 120 * ui, H * 0.62f + 62 * ui, 240 * ui * clampf(p.reviveProgress, 0, 1), 8 * ui, Vec4(0.5f, 1, 0.6f, 1));
            } else {
                hud.text(W * 0.5f, H * 0.62f + 36 * ui, 1.8f * ui, Vec4(1, 0.8f, 0.6f, 1), "CRAWL TO YOUR TEAM - THEY CAN PICK YOU UP", 1);
            }
        } else {
            hud.rect(bx, by, bw * clampf(p.health / PLAYER_MAX_HEALTH, 0, 1), bh, HEALTH_COL);
            hud.text(bx + bw + 12 * ui, by + 1 * ui, 2.4f * ui, WHITE, std::to_string((int)std::ceil(p.health)));
        }
        float sy = by - bh - 12 * ui;
        hud.rect(bx - 3, sy - 3, bw + 6, bh + 6, PANEL);
        hud.rect(bx, sy, bw * clampf(p.shield / PLAYER_MAX_SHIELD, 0, 1), bh, SHIELD_COL);
        hud.text(bx + bw + 12 * ui, sy + 1 * ui, 2.4f * ui, WHITE, std::to_string((int)std::ceil(p.shield)));
        float sth = 9 * ui, sty = sy - sth - 14 * ui;
        Vec4 stCol = p.exhausted ? Vec4(0.9f, 0.4f, 0.3f, 1) : STAMINA_COL;
        hud.rect(bx - 3, sty - 3, bw * 0.75f + 6, sth + 6, PANEL);
        hud.rect(bx, sty, bw * 0.75f * clampf(p.stamina / STAMINA_MAX, 0, 1), sth, stCol);
        hud.text(bx + bw * 0.75f + 12 * ui, sty - 3 * ui, 1.6f * ui, withAlpha(stCol, 0.9f), "STAMINA");
        hud.text(bx, sty - 26 * ui, 1.7f * ui, Vec4(0.85f, 0.9f, 1, 0.9f), p.name);

        // ---- hotbar (bottom right): pickaxe + 5 slots, each with an icon
        float slot = 74 * ui, gapS = 8 * ui;
        float hx = W - 30 * ui - (HOTBAR_SLOTS + 1) * slot - HOTBAR_SLOTS * gapS;
        float hy = H - 30 * ui - slot;
        for (int i = -1; i < HOTBAR_SLOTS; ++i) {
            static const Item pick = Item::makePickaxe();
            const Item& it = i < 0 ? pick : p.slots[i];
            bool sel = p.mode == Mode::Combat && (i < 0 ? p.pickaxeOut : (!p.pickaxeOut && i == p.selected));
            float x = hx + (i + 1) * (slot + gapS), y = hy - (sel ? 10 * ui : 0);
            hud.rect(x, y, slot, slot, sel ? Vec4(0.15f, 0.2f, 0.32f, 0.85f) : PANEL);
            Vec4 border = it.kind == ItemKind::Gun ? rarityColor(it.rarity)
                          : isConsumable(it.kind) ? rarityColor(it.rarity)
                          : it.kind == ItemKind::Pickaxe ? Vec4(0.75f, 0.78f, 0.82f, 1)
                                                         : Vec4(0.4f, 0.45f, 0.5f, 0.8f);
            hud.frame(x, y, slot, slot, 3 * ui, border);
            if (sel) hud.frame(x - 3 * ui, y - 3 * ui, slot + 6 * ui, slot + 6 * ui, 2 * ui, WHITE);
            std::string k = key(binds, i < 0 ? Action::Pickaxe : (Action)((int)Action::Slot1 + i));
            hud.text(x + 5 * ui, y + 4 * ui, 1.4f * ui, Vec4(1, 1, 1, 0.6f), k);
            if (!it.empty()) {
                Vec4 ic = it.kind == ItemKind::Gun ? mixIcon(border)
                          : it.kind == ItemKind::BigPot || it.kind == ItemKind::Mini ? Vec4(0.45f, 0.75f, 1.0f, 1)
                                                                                     : Vec4(0.95f, 0.95f, 0.95f, 1);
                hud.icon(itemIcon(it), x + 6 * ui, y + 16 * ui, slot - 12 * ui, slot - 34 * ui, ic);
            }
            if (it.kind == ItemKind::Gun) {
                // Total ammo for this gun: what's loaded plus the reserve of its ammo type.
                int total = it.count + p.ammo[(int)gunStats(it.gun).ammo];
                hud.text(x + slot - 5 * ui, y + slot - 17 * ui, 1.6f * ui, WHITE, std::to_string(total), 2);
            }
            else if (isConsumable(it.kind))
                hud.text(x + slot - 5 * ui, y + slot - 17 * ui, 1.6f * ui, WHITE, "X" + std::to_string(it.count), 2);
        }
        // Selected item name + ammo above the hotbar.
        float rx = W - 30 * ui;
        float ay = hy - 50 * ui;
        if (p.mode == Mode::Combat && held.kind == ItemKind::Gun) {
            const GunStats& gs = gunStats(held.gun);
            std::string ammoTxt = std::to_string(held.count) + " / " + std::to_string(p.ammo[(int)gs.ammo]);
            hud.text(rx, ay, 3.2f * ui, WHITE, ammoTxt, 2);
            hud.icon(ICON_AMMO, rx - hud.textWidth(ammoTxt, 3.2f * ui) - 52 * ui, ay, 44 * ui, 26 * ui, Vec4(1, 0.9f, 0.6f, 1));
            hud.text(rx, ay - 26 * ui, 1.9f * ui, rarityColor(held.rarity), held.label(), 2);
        } else if (p.mode == Mode::Combat && isConsumable(held.kind)) {
            hud.text(rx, ay, 2.1f * ui, WHITE, held.label() + " - " + key(binds, Action::Fire) + " TO USE (STAND STILL)", 2);
        } else if (p.mode == Mode::Combat && held.kind == ItemKind::Pickaxe) {
            hud.text(rx, ay, 2.1f * ui, WHITE, "PICKAXE - HIT TREES, ROCKS AND WALLS FOR MATS", 2);
        }
        // Reserve ammo + materials.
        char buf[64];
        std::snprintf(buf, sizeof buf, "LIGHT %d  MED %d  HEAVY %d  SHELLS %d", p.ammo[0], p.ammo[1], p.ammo[2], p.ammo[3]);
        hud.text(rx, ay - 52 * ui, 1.5f * ui, Vec4(0.85f, 0.85f, 0.85f, 0.9f), buf, 2);
        {
            static const Vec4 mcols[3] = {{0.95f, 0.75f, 0.45f, 1}, {0.95f, 0.55f, 0.45f, 1}, {0.75f, 0.8f, 0.9f, 1}};
            float mxp = rx;
            for (int m = 2; m >= 0; --m) {
                std::string txt = std::to_string(p.matCount[m]);
                bool sel = (int)p.buildMat == m;
                Vec4 c = sel ? mcols[m] : withAlpha(mcols[m], 0.6f);
                hud.text(mxp, ay - 82 * ui, sel ? 2.4f * ui : 1.9f * ui, c, txt, 2);
                mxp -= hud.textWidth(txt, sel ? 2.4f * ui : 1.9f * ui) + 8 * ui;
                hud.icon(ICON_MATS, mxp - 34 * ui, ay - 82 * ui, 32 * ui, 18 * ui, c);
                mxp -= 48 * ui;
            }
            hud.text(rx, ay - 104 * ui, 1.3f * ui, Vec4(0.85f, 0.85f, 0.9f, 0.8f), "METAL  BRICK  WOOD", 2);
        }

        // ---- mode bar (bottom centre)
        if (p.mode == Mode::Build) {
            const Action pieces[4] = {Action::Wall, Action::Floor, Action::Ramp, Action::Cone};
            float bw2 = 116 * ui, bx2 = W * 0.5f - bw2 * 2 - 15 * ui, by2 = H - 70 * ui;
            for (int i = 0; i < 4; ++i) {
                bool sel = (int)p.buildPiece == i;
                float x = bx2 + i * (bw2 + 10 * ui);
                hud.rect(x, by2, bw2, 40 * ui, sel ? Vec4(0.25f, 0.5f, 0.9f, 0.85f) : PANEL);
                hud.text(x + bw2 * 0.5f, by2 + 12 * ui, 1.8f * ui, WHITE,
                         "[" + key(binds, pieces[i]) + "] " + pieceName((PieceType)i), 1);
            }
            std::string tip = "BUILD MODE  " + key(binds, Action::Fire) + " PLACE (HOLD TO KEEP BUILDING)  " +
                              key(binds, Action::BuildToggle) + " EXIT";
            if (g.buildTarget.unsupported) tip = "NEEDS SUPPORT - BUILD FROM THE GROUND OR OFF ANOTHER PIECE";
            if (p.buildPiece == PieceType::Ramp) tip += "  " + key(binds, Action::Reload) + " ROTATE";
            tip += "  " + key(binds, Action::Aim) + " MATERIAL: " + buildMatName(p.buildMat);
            hud.text(W * 0.5f, by2 - 26 * ui, 1.6f * ui, Vec4(1, 1, 1, 0.85f), tip, 1);
        } else if (p.mode == Mode::Edit) {
            hud.text(W * 0.5f, H - 60 * ui, 1.8f * ui, Vec4(0.6f, 0.8f, 1, 1),
                     "EDIT  DRAG " + key(binds, Action::Fire) + " OVER TILES, RELEASE OR " + key(binds, Action::Edit) +
                         " TO CONFIRM, " + key(binds, Action::Aim) + " RESET",
                     1);
        }

        // ---- progress bars (med kit / reload)
        float pbw = 220 * ui, pbh = 10 * ui, pby = cy + 70 * ui;
        auto progress = [&](float f, const std::string& label, const Vec4& c) {
            hud.rect(cx - pbw * 0.5f - 2, pby - 2, pbw + 4, pbh + 4, PANEL);
            hud.rect(cx - pbw * 0.5f, pby, pbw * clampf(f, 0, 1), pbh, c);
            hud.text(cx, pby + pbh + 8 * ui, 1.7f * ui, WHITE, label, 1);
        };
        const GunState& gst = p.gunStates[p.selected];
        if (p.medkitTimer >= 0 && isConsumable(held.kind))
            progress(p.medkitTimer / useTime(held.kind), held.kind == ItemKind::MedKit ? "HEALING..." : "DRINKING...",
                     held.kind == ItemKind::MedKit ? HEALTH_COL : SHIELD_COL);
        else if (held.kind == ItemKind::Gun && gst.reloadTimer > 0 && p.mode == Mode::Combat)
            progress(1.0f - gst.reloadTimer / gunStats(held.gun).reloadTime, "RELOADING", WHITE);
        // Health of the build under the crosshair.
        if (g.aimedPiece >= 0) {
            const Piece* pc = g.world.build.get(g.aimedPiece);
            if (pc && pc->alive) {
                float bw2 = 180 * ui, bh2 = 8 * ui, by2 = cy + 40 * ui;
                std::string label = std::string(pieceName(pc->slot.type)) + "  " + std::to_string((int)std::ceil(pc->hp)) + " / " +
                                    std::to_string((int)pc->maxHp);
                hud.rect(cx - bw2 * 0.5f - 2, by2 - 2, bw2 + 4, bh2 + 4, PANEL);
                float f = clampf(pc->hp / pc->maxHp, 0, 1);
                Vec4 c = f > 0.5f ? Vec4(0.55f, 0.85f, 1, 1) : (f > 0.25f ? Vec4(1, 0.8f, 0.3f, 1) : Vec4(1, 0.4f, 0.3f, 1));
                hud.rect(cx - bw2 * 0.5f, by2, bw2 * f, bh2, c);
                hud.text(cx, by2 + bh2 + 6 * ui, 1.5f * ui, WHITE, label, 1);
            }
        }
        // Small bars on builds that were hit recently.
        for (const Piece& pc : g.world.build.pieces()) {
            if (!pc.alive || pc.hp >= pc.maxHp || pc.id == g.aimedPiece) continue;
            float px3, py3;
            Vec3 c3 = pieceBounds(pc.slot).center();
            if (length(c3 - g.camera.eye) > 30.0f || !hud.project(g.camera, c3, px3, py3)) continue;
            float bw3 = 40 * ui, bh3 = 4 * ui;
            hud.rect(px3 - bw3 * 0.5f - 1, py3 - 1, bw3 + 2, bh3 + 2, SHADOW);
            hud.rect(px3 - bw3 * 0.5f, py3, bw3 * clampf(pc.hp / pc.maxHp, 0, 1), bh3, Vec4(0.55f, 0.85f, 1, 1));
        }

        // ---- interact prompt
        if (g.prompt.kind != InteractPrompt::None) {
            float py = cy + 110 * ui;
            bool hold = g.prompt.kind == InteractPrompt::OpenChest || g.prompt.kind == InteractPrompt::Revive;
            std::string txt = (hold ? "HOLD [" : "[") + key(binds, Action::Interact) +
                              "] " + g.prompt.text;
            float tw = hud.textWidth(txt, 2.0f * ui);
            hud.rect(cx - tw * 0.5f - 10 * ui, py - 8 * ui, tw + 20 * ui, 32 * ui, PANEL);
            hud.text(cx, py, 2.0f * ui, WHITE, txt, 1);
            if (hold && g.prompt.progress > 0) {
                hud.rect(cx - tw * 0.5f - 10 * ui, py + 26 * ui, (tw + 20 * ui) * clampf(g.prompt.progress, 0, 1), 4 * ui,
                         Vec4(1, 0.8f, 0.3f, 1));
            }
        }
    } else if (g.spectating >= 0 && !g.matchOver()) {
        const Bot& b = g.bots[g.spectating];
        hud.rect(W * 0.5f - 220 * ui, 70 * ui, 440 * ui, 64 * ui, PANEL);
        hud.text(W * 0.5f, 80 * ui, 1.8f * ui, Vec4(1, 0.5f, 0.45f, 1), "YOU'RE OUT - YOUR TEAM IS STILL IN", 1);
        hud.text(W * 0.5f, 104 * ui, 2.2f * ui, Vec4(0.45f, 0.9f, 1, 1), "SPECTATING " + b.name, 1);
    }

    // ---- mini map (top right) with the match status under it
    float mmX, mmY, mmSize;
    buildMiniMap(g, W, H, hud, mmX, mmY, mmSize);
    hud.text(W - 30 * ui, mmY + mmSize + 12 * ui, 2.2f * ui, WHITE, "PLAYERS LEFT " + std::to_string(g.playersAlive()), 2);
    float tr = mmY + mmSize + 38 * ui;
    if (g.config.teamSize > 1) {
        hud.text(W - 30 * ui, tr, 1.8f * ui, Vec4(0.8f, 0.9f, 1, 1),
                 std::string(teamModeName(g.config.teamSize)) + "  TEAMS LEFT " + std::to_string(g.teamsAlive()), 2);
        tr += 24 * ui;
    }
    hud.text(W - 30 * ui, tr, 1.8f * ui, Vec4(1, 0.85f, 0.4f, 1), "KILLS " + std::to_string(p.kills), 2);
    hud.text(W - 30 * ui, tr + 22 * ui, 1.4f * ui, Vec4(1, 1, 1, 0.6f), std::to_string((int)(fps + 0.5f)) + " FPS", 2);

    // ---- kill feed (top left)
    for (size_t i = 0; i < g.killFeed.size(); ++i) {
        const Message& m = g.killFeed[i];
        float a = m.age > 5.0f ? 1.0f - (m.age - 5.0f) : 1.0f;
        hud.text(30 * ui, (22 + 24 * (float)i) * ui, 1.7f * ui, withAlpha(m.color, a), m.text);
    }

    // ---- teammates (left side)
    if (g.config.teamSize > 1) {
        float ty = H * 0.30f;
        hud.text(30 * ui, ty, 1.6f * ui, Vec4(0.6f, 0.75f, 0.9f, 1), "YOUR TEAM");
        ty += 22 * ui;
        for (const Bot& b : g.bots) {
            if (b.team != 0) continue;
            Vec4 nc = b.alive ? Vec4(0.45f, 0.9f, 1, 1) : Vec4(0.5f, 0.5f, 0.55f, 0.8f);
            hud.text(30 * ui, ty, 1.6f * ui, nc, b.name + (b.alive ? "" : "  - OUT"));
            if (b.alive) {
                hud.rect(30 * ui, ty + 16 * ui, 150 * ui, 6 * ui, SHADOW);
                hud.rect(30 * ui, ty + 16 * ui, 150 * ui * clampf(b.health / 100.0f, 0, 1), 6 * ui, HEALTH_COL);
            }
            ty += 34 * ui;
        }
    }

    // ---- battle bus prompt
    if (p.alive && g.dropPhase == DropPhase::OnBus) {
        hud.rect(W * 0.5f - 220 * ui, H * 0.72f, 440 * ui, 54 * ui, PANEL);
        hud.text(W * 0.5f, H * 0.72f + 10 * ui, 2.2f * ui, Vec4(1, 0.85f, 0.4f, 1), "PRESS [" + key(binds, Action::Jump) + "] TO JUMP", 1);
        hud.text(W * 0.5f, H * 0.72f + 34 * ui, 1.4f * ui, DIM_TEXT, "BATTLE BUS  " + std::to_string((int)((1.0f - g.bus.t) * g.bus.duration)) + " S LEFT ON THE ROUTE", 1);
    } else if (p.alive && g.dropPhase != DropPhase::Landed) {
        float ground = g.world.terrainHeight(p.body.pos.x, p.body.pos.z);
        hud.text(W * 0.5f, H * 0.78f, 1.8f * ui, Vec4(1, 1, 1, 0.9f),
                 (g.dropPhase == DropPhase::Glide ? "GLIDING  " : "SKYDIVING  ") + std::to_string((int)(p.body.pos.y - ground)) + " M", 1);
    }

    // ---- storm timer (under the mini map) and purple edges when you're in it
    {
        const Storm& st = g.storm;
        int secs = (int)std::ceil(std::max(0.0f, std::min(st.timer, 9999.0f)));
        char tbuf[32];
        std::snprintf(tbuf, sizeof tbuf, "%d:%02d", secs / 60, secs % 60);
        std::string label = st.timer > 1e8f ? "STORM CLOSED" : (st.shrinking ? "STORM SHRINKING " : "STORM SHRINKS IN ") + std::string(tbuf);
        float ty = mmY + mmSize + 60 * ui + (g.config.teamSize > 1 ? 24 * ui : 0) + 22 * ui;
        hud.text(W - 30 * ui, ty, 1.7f * ui, st.shrinking ? Vec4(0.85f, 0.55f, 1, 1) : Vec4(0.8f, 0.8f, 0.9f, 1), label, 2);
        if (g.playerInStorm) {
            Vec4 purple(0.55f, 0.2f, 0.8f, 0.3f + 0.1f * std::sin(g.time * 6.0f));
            float t = 50 * ui;
            hud.rect(0, 0, W, t, purple);
            hud.rect(0, H - t, W, t, purple);
            hud.rect(0, t, t, H - 2 * t, purple);
            hud.rect(W - t, t, t, H - 2 * t, purple);
            hud.text(W * 0.5f, H * 0.2f, 2.0f * ui, Vec4(0.9f, 0.7f, 1, 1), "YOU'RE IN THE STORM - " + std::to_string((int)st.damagePerSecond) + " DAMAGE / S", 1);
        }
    }

    // ---- place name when you walk into a POI
    if (g.poiBannerTime > 0 && !g.poiBanner.empty()) {
        float a = std::min(1.0f, g.poiBannerTime * 2.0f) * std::min(1.0f, (3.5f - g.poiBannerTime) * 3.0f);
        if (g.poiBannerNew) hud.text(W * 0.5f, H * 0.12f, 1.6f * ui, Vec4(1, 0.85f, 0.4f, a), "DISCOVERED", 1);
        hud.text(W * 0.5f, H * 0.12f + 22 * ui, 3.6f * ui, Vec4(1, 1, 1, a), g.poiBanner, 1);
    }

    // ---- toast
    if (g.toast.age < 2.0f && !g.toast.text.empty()) {
        float a = g.toast.age > 1.5f ? (2.0f - g.toast.age) * 2.0f : 1.0f;
        hud.text(W * 0.5f, H * 0.3f, 2.4f * ui, withAlpha(g.toast.color, a), g.toast.text, 1);
    }

    // ---- help
    if (g.showHelp && p.alive && !g.matchOver()) {
        std::vector<std::string> lines = {
            "CONTROLS  (F1 HIDES THIS)",
            key(binds, Action::MoveForward) + key(binds, Action::MoveLeft) + key(binds, Action::MoveBack) +
                key(binds, Action::MoveRight) + " MOVE   " + key(binds, Action::Jump) + " JUMP",
            key(binds, Action::Sprint) + " SPRINT (USES STAMINA)   " + key(binds, Action::Crouch) + " CROUCH",
            key(binds, Action::Fire) + " SHOOT   " + key(binds, Action::Aim) + " AIM   " + key(binds, Action::Reload) + " RELOAD",
            key(binds, Action::Pickaxe) + " PICKAXE   " + key(binds, Action::Slot1) + "-" + key(binds, Action::Slot5) + " HOTBAR",
            key(binds, Action::Interact) + " OPEN CHEST / PICK UP",
            key(binds, Action::BuildToggle) + " BUILD   " + key(binds, Action::Wall) + " WALL  " + key(binds, Action::Floor) +
                " FLOOR  " + key(binds, Action::Ramp) + " RAMP  " + key(binds, Action::Cone) + " CONE",
            key(binds, Action::Edit) + " EDIT A BUILD (DRAG TILES, RELEASE)   " + key(binds, Action::ResetEdit) + " RESET EDIT",
            key(binds, Action::Inventory) + " INVENTORY",
            key(binds, Action::Map) + " MAP   ESC MENU (SETTINGS / KEYBINDS)",
        };
        float lx = 30 * ui, ly = H * 0.30f + (g.config.teamSize > 1 ? 160 * ui : 0);
        float maxW = 0;
        for (auto& l : lines) maxW = std::max(maxW, hud.textWidth(l, 1.7f * ui));
        hud.rect(lx - 12 * ui, ly - 12 * ui, maxW + 24 * ui, (lines.size() * 22 + 14) * ui, PANEL);
        for (size_t i = 0; i < lines.size(); ++i)
            hud.text(lx, ly + i * 22 * ui, 1.7f * ui, i == 0 ? Vec4(1, 0.85f, 0.4f, 1) : WHITE, lines[i]);
    }
}

void buildMiniMap(const Game& g, float W, float H, HudBuilder& hud, float& outX, float& outY, float& outSize) {
    float ui = std::max(0.75f, H / 900.0f);
    float size = 190 * ui;
    float x = W - 30 * ui - size, y = 24 * ui;
    outX = x; outY = y; outSize = size;
    Vec3 me = g.player.alive ? g.player.body.pos : (g.spectating >= 0 ? g.bots[g.spectating].body.pos : g.player.body.pos);
    const float span = 130.0f;  // metres shown edge to edge
    // Keep the window inside the map so the edge doesn't stretch.
    float cx = clampf(me.x, -MAP_HALF + span * 0.5f, MAP_HALF - span * 0.5f);
    float cz = clampf(me.z, -MAP_HALF + span * 0.5f, MAP_HALF - span * 0.5f);
    float u0, v0, u1, v1;
    worldToMapUV(cx - span * 0.5f, cz + span * 0.5f, u0, v0);
    worldToMapUV(cx + span * 0.5f, cz - span * 0.5f, u1, v1);
    hud.rect(x - 3 * ui, y - 3 * ui, size + 6 * ui, size + 6 * ui, Vec4(0.05f, 0.07f, 0.12f, 0.8f));
    hud.image(HUD_TEX_MAP, x, y, size, size, u0, v0, u1, v1, Vec4(1, 1, 1, 0.92f));
    auto toScreen = [&](const Vec3& p, float& sx, float& sy) {
        sx = x + (p.x - (cx - span * 0.5f)) / span * size;
        sy = y + ((cz + span * 0.5f) - p.z) / span * size;
        return sx >= x && sx <= x + size && sy >= y && sy <= y + size;
    };
    float sx, sy;
    for (const Poi& poi : g.world.pois)
        if (toScreen(poi.center, sx, sy)) {
            std::string n = poi.name.substr(0, poi.name.find(' '));
            hud.text(sx, sy - 6 * ui, 1.1f * ui, Vec4(1, 1, 1, 0.9f), n, 1);
        }
    for (const Bot& b : g.bots) {
        if (!b.alive || b.team != 0) continue;
        if (toScreen(b.body.pos, sx, sy)) hud.rect(sx - 4 * ui, sy - 4 * ui, 8 * ui, 8 * ui, Vec4(0.45f, 0.9f, 1, 1));
    }
    // Storm: current edge in white-purple, where it's going in white.
    {
        const Storm& st = g.storm;
        auto ring = [&](const Vec3& c, float r, const Vec4& col) {
            float scx, scy;
            scx = x + (c.x - (cx - span * 0.5f)) / span * size;
            scy = y + ((cz + span * 0.5f) - c.z) / span * size;
            float pr = r / span * size;
            const int N = 96;
            for (int i = 0; i < N; ++i) {
                float a = 2 * PI * i / N;
                float px = scx + std::cos(a) * pr, py = scy + std::sin(a) * pr;
                if (px >= x && px <= x + size && py >= y && py <= y + size) hud.rect(px - 1.5f * ui, py - 1.5f * ui, 3 * ui, 3 * ui, col);
            }
        };
        if (st.radius < 300) ring(st.center, st.radius, Vec4(0.8f, 0.45f, 1, 1));
        if (st.nextRadius < st.radius) ring(st.nextCenter, st.nextRadius, Vec4(1, 1, 1, 0.9f));
    }
    if (toScreen(me, sx, sy)) hud.arrow(sx, sy, 16 * ui, g.player.alive ? g.player.yaw : 0.0f, Vec4(1, 1, 1, 1));
    hud.text(x + 6 * ui, y + 4 * ui, 1.2f * ui, Vec4(1, 1, 1, 0.75f), "N");
    hud.text(x + size - 6 * ui, y + size - 16 * ui, 1.2f * ui, Vec4(1, 1, 1, 0.75f), mapGridName(me.x, me.z), 2);
}

}  // namespace bf
