// Blockfall - logic tests for every stage. Runs on any OS (no graphics needed).
//   g++ -std=c++17 -O2 -Isrc src/game/*.cpp tests/tests.cpp -o blockfall_tests && ./blockfall_tests
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "game/game.h"
#include "game/hud.h"
#include "game/loot.h"
#include "game/scene.h"
#include "game/occlusion.h"
#include "game/frontend.h"
#include "game/keybinds.h"
#include "game/settings.h"
#include "game/atlas.h"
#include "game/map.h"
#include "game/skins.h"
#include "game/model.h"
#include "game/charanim.h"
#include "game/charface.h"
#include "game/charphys.h"
#include "game/account.h"
#include <cstdlib>
#include <fstream>

using namespace bf;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (cond) { ++g_pass; }                                            \
        else {                                                             \
            ++g_fail;                                                      \
            std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);  \
            std::printf(__VA_ARGS__);                                      \
            std::printf("\n");                                             \
        }                                                                  \
    } while (0)

static const float DT = 1.0f / 60.0f;

// A game with every bot parked far away so it doesn't interfere.
static Game quietGame(uint64_t seed = 1337) {
    Game g(seed);
    for (Bot& e : g.bots) {
        e.alive = false;
    }
    return g;
}

// Clears everything near a point so movement tests are not blocked by props, and levels the
// ground there to just above a story line (the old flat map was like that everywhere).
static void clearArea(Game& g, const Vec3& c, float r) {
    g.world.flattenTerrain(c.x, c.z, r, 0.25f);
    auto& s = g.world.statics;
    s.erase(std::remove_if(s.begin(), s.end(), [&](const StaticBox& b) { return lengthXZ(b.box.center() - c) < r; }), s.end());
    auto& ch = g.world.chests;
    ch.erase(std::remove_if(ch.begin(), ch.end(), [&](const Chest& b) { return lengthXZ(b.pos - c) < r; }), ch.end());
}

static void run(Game& g, int frames, const std::function<InputState(int)>& f) {
    for (int i = 0; i < frames; ++i) g.update(f ? f(i) : InputState(), DT);
}

static InputState none(int) { return InputState(); }

// Puts the player on a flat cell centre facing +z.
static void standAtCell(Game& g, int i, int j) {
    g.player.body.pos = Vec3((i + 0.5f) * TILE, 0, (j + 0.5f) * TILE);
    g.player.body.pos.y = g.world.terrainHeight(g.player.body.pos.x, g.player.body.pos.z);
    g.player.body.vel = Vec3();
    g.player.yaw = 0;
    g.player.pitch = 0;
    run(g, 5, none);
}

static void section(const char* name) { std::printf("\n== %s\n", name); }

// ------------------------------------------------------------------ stage 1
static void testStage1() {
    section("Stage 1: window/camera/movement (logic side)");
    Game g = quietGame();
    CHECK(g.world.chests.size() >= 25, "chests=%zu", g.world.chests.size());
    CHECK(g.world.houseCenters.size() >= 6, "houses=%zu", g.world.houseCenters.size());
    CHECK(g.world.enemySpawns.size() >= 10, "spawns=%zu", g.world.enemySpawns.size());
    CHECK(std::fabs(MAP_HALF * 2 - 800.0f) < 1e-3f, "map size 800 m (room for 100 players)");

    // Gravity: drop from 10 m.
    Vec3 start = g.world.playerSpawn;
    clearArea(g, start, 30);
    g.player.body.pos = start + Vec3(0, 10, 0);
    run(g, 120, none);
    float ground = g.world.terrainHeight(g.player.body.pos.x, g.player.body.pos.z);
    CHECK(g.player.body.grounded && std::fabs(g.player.body.pos.y - ground) < 0.05f, "y=%.2f ground=%.2f",
          g.player.body.pos.y, ground);

    auto measure = [&](InputState held, float seconds) {
        g.player.body.pos = start;
        g.player.body.pos.y = g.world.terrainHeight(start.x, start.z);
        g.player.body.vel = Vec3();
        g.player.yaw = 0;
        run(g, 10, none);
        Vec3 a = g.player.body.pos;
        run(g, (int)(seconds * 60), [&](int) { return held; });
        return lengthXZ(g.player.body.pos - a) / seconds;
    };
    InputState walk; walk.forward = true;
    InputState sprint = walk; sprint.sprint = true;
    InputState crouch = walk; crouch.crouch = true;
    InputState sprintAim = sprint; sprintAim.aim = true;
    InputState sprintFire = sprint; sprintFire.fire = true;
    float vw = measure(walk, 1.5f), vs = measure(sprint, 1.5f), vc = measure(crouch, 1.5f);
    g.player.ammo[(int)AmmoType::Light] = 999;
    g.player.pickaxeOut = false;  // holding the pistol (you can't aim a pickaxe)
    float vsa = measure(sprintAim, 1.5f);
    float vsf = measure(sprintFire, 1.5f);
    std::printf("  walk %.2f m/s, sprint %.2f m/s (x%.2f), crouch %.2f, sprint+aim %.2f, sprint+fire %.2f\n", vw, vs, vs / vw,
                vc, vsa, vsf);
    CHECK(std::fabs(vs / vw - 1.5f) < 0.1f, "sprint ratio %.2f", vs / vw);
    CHECK(vc < vw * 0.7f, "crouch slower");
    CHECK(vsa < vw, "can't sprint while aiming (%.2f)", vsa);
    CHECK(vsf <= vw * 1.02f, "can't sprint while shooting (%.2f)", vsf);

    // Jump.
    standAtCell(g, 0, 0);
    float y0 = g.player.body.pos.y, peak = y0;
    run(g, 70, [&](int i) {
        InputState in;
        in.jumpPressed = (i == 0);
        peak = std::max(peak, g.player.body.pos.y);
        return in;
    });
    CHECK(peak - y0 > 0.9f && peak - y0 < 1.6f, "jump height %.2f", peak - y0);
    CHECK(g.player.body.grounded, "landed");

    // Collision with a wall-like prop.
    Vec3 p = g.player.body.pos;
    g.world.statics.push_back({AABB({p.x - 3, p.y - 1, p.z + 2}, {p.x + 3, p.y + 4, p.z + 2.5f}),
                               Vec4(1, 1, 1, 1), PropKind::Rock, true, true});
    run(g, 90, [](int) { InputState in; in.forward = true; return in; });
    CHECK(g.player.body.pos.z < p.z + 2 - 0.39f, "blocked by box (z=%.2f limit=%.2f)", g.player.body.pos.z, p.z + 2);
    g.world.statics.pop_back();

    // Camera: shoulder view, player left of centre.
    standAtCell(g, 0, 0);
    g.updateCamera();
    Mat4 vp = cameraView(g.camera) * cameraProj(g.camera, 16.0f / 9.0f);
    Vec3 chest = g.player.body.pos + Vec3(0, 1.2f, 0);
    Vec4 c = transform(Vec4(chest.x, chest.y, chest.z, 1), vp);
    float ndcX = c.x / c.w;
    std::printf("  player chest on screen at ndc x=%.3f (negative = left of centre)\n", ndcX);
    CHECK(ndcX < -0.05f && ndcX > -0.6f, "player sits left of centre");
    CHECK(dot(g.camera.forward, g.player.forward()) > 0.999f, "camera looks where player looks");
}

// ------------------------------------------------------------------ stage 2
static void testStage2() {
    section("Stage 2: shooting + crosshair");
    float cx, cy;
    crosshairPosition(1920, 1080, cx, cy);
    CHECK(cx == 960 && cy == 540, "crosshair at exact centre");

    Game g = quietGame();
    standAtCell(g, 0, 0);
    clearArea(g, g.player.body.pos, 40);
    g.updateCamera();
    // Aim ray goes through the exact centre of the screen.
    Mat4 vp = cameraView(g.camera) * cameraProj(g.camera, 16.0f / 9.0f);
    Vec3 far = g.camera.aimRay.at(50);
    Vec4 c = transform(Vec4(far.x, far.y, far.z, 1), vp);
    CHECK(std::fabs(c.x / c.w) < 1e-4f && std::fabs(c.y / c.w) < 1e-4f, "aim ray hits screen centre (%.5f, %.5f)",
          c.x / c.w, c.y / c.w);

    // HUD crosshair quads are centred on the screen.
    HudBuilder hud;
    g.showHelp = false;
    g.player.pickaxeOut = false;  // gun crosshair
    buildHud(g, 1920, 1080, 60, Bindings::defaults(), hud);
    float sx = 0, sy = 0;
    int n = 0;
    for (const HudQuad& q : hud.quads) {
        float qx = q.x + q.w * 0.5f, qy = q.y + q.h * 0.5f;
        bool white = q.color.x > 0.99f && q.color.y > 0.99f && q.color.z > 0.99f && std::fabs(q.color.w - 0.95f) < 1e-3f;
        if (white && std::fabs(qx - 960) < 40 && std::fabs(qy - 540) < 40) { sx += qx; sy += qy; ++n; }  // the crosshair lines only
    }
    CHECK(n >= 5 && std::fabs(sx / n - 960) < 1.0f && std::fabs(sy / n - 540) < 1.0f, "crosshair quads n=%d centre=(%.1f,%.1f)",
          n, n ? sx / n : 0, n ? sy / n : 0);

    // Shoot an enemy standing in front of us.
    Game g2(1337);
    for (size_t i = 1; i < g2.bots.size(); ++i) g2.bots[i].alive = false;
    standAtCell(g2, 0, 0);
    clearArea(g2, g2.player.body.pos, 40);
    Bot& e = g2.bots[0];
    e.body.pos = g2.player.body.pos + Vec3(0, 0, 12);
    e.body.pos.y = g2.world.terrainHeight(e.body.pos.x, e.body.pos.z);
    e.state = AIState::Spot;
    e.reaction = 1e9f;
    g2.player.slots[0] = Item::makeGun(GunType::AssaultRifle, Rarity::Legendary);
    g2.player.pickaxeOut = false;
    g2.player.ads = true;
    g2.updateCamera();
    Vec3 target = e.body.pos + Vec3(0, 1.0f, 0);
    Vec3 d = normalize(target - g2.camera.eye);
    g2.player.yaw = std::atan2(d.x, d.z);
    g2.player.pitch = std::asin(d.y);
    float before = e.health;
    InputState fire;
    fire.fire = fire.firePressed = fire.aim = true;
    g2.update(fire, DT);
    CHECK(!g2.tracers.empty(), "tracer spawned");
    if (!g2.tracers.empty()) {
        Vec3 muzzle = g2.tracers[0].from;
        Vec3 pivot = g2.player.eyePivot();
        CHECK(length(muzzle - pivot) < 1.0f && length(muzzle - g2.camera.eye) > 0.5f,
              "the tracer is drawn from the gun");
        CHECK(length(g2.tracers[0].to - target) < 1.0f, "bullet lands exactly where the crosshair points (miss %.2f m)",
              length(g2.tracers[0].to - target));
    }
    CHECK(e.health < before, "enemy took damage (%.0f -> %.0f)", before, e.health);
    float expected = 30.0f * rarityDamageMult(Rarity::Legendary);
    CHECK(std::fabs((before - e.health) - expected) < 0.5f || std::fabs((before - e.health) - expected * 1.5f) < 0.5f,
          "legendary AR damage %.1f", before - e.health);
    CHECK(g2.hitMarker > 0 && !g2.damageNumbers.empty(), "hit marker + damage number");
    CHECK(g2.player.slots[0].count == 29, "mag 30 -> 29");

    // Reload takes ammo from reserve.
    g2.player.ammo[(int)AmmoType::Medium] = 100;
    InputState rl;
    rl.reloadPressed = true;
    g2.update(rl, DT);
    run(g2, (int)(2.5f * 60), none);
    CHECK(g2.player.slots[0].count == 30 && g2.player.ammo[(int)AmmoType::Medium] == 99, "reload mag=%d reserve=%d",
          g2.player.slots[0].count, g2.player.ammo[(int)AmmoType::Medium]);

    // Damage falloff and range.
    Item sg = Item::makeGun(GunType::Shotgun, Rarity::Common);
    CHECK(bulletDamage(sg, 5, false) > bulletDamage(sg, 22, false), "shotgun falls off");
    CHECK(bulletDamage(sg, 40, false) == 0, "shotgun out of range");
}

// ------------------------------------------------------------------ stage 3
static void testStage3() {
    section("Stage 3: building");
    Game g = quietGame();
    standAtCell(g, 2, 2);
    clearArea(g, g.player.body.pos, 30);
    standAtCell(g, 2, 2);
    int ck = (int)std::floor((g.player.body.pos.y + 0.5f) / STORY);

    InputState wallKey;
    wallKey.buildPiecePressed = 0;
    g.update(wallKey, DT);
    CHECK(g.player.mode == Mode::Build && g.player.buildPiece == PieceType::Wall, "Z enters build mode with wall");
    const Slot& s = g.buildTarget.slot;
    CHECK(s.type == PieceType::Wall && s.side == 1 && s.i == 2 && s.j == 3 && s.k == ck, "wall in front: side=%d i=%d j=%d k=%d",
          s.side, s.i, s.j, s.k);
    int mats = g.player.matCount[0];
    InputState click;
    click.fire = click.firePressed = true;
    g.update(click, DT);
    CHECK(g.world.build.aliveCount() == 1 && g.player.matCount[0] == mats - BUILD_COST, "placed, mats %d", g.player.matCount[0]);
    const Piece& w = g.world.build.pieces()[0];
    g.world.build.update(BUILD_UP_TIME + 0.1f);  // let it build up
    CHECK(w.hp == 150 && w.maxHp == 150, "wall hp 150 (after building up)");
    g.update(InputState(), DT);
    g.update(click, DT);
    CHECK(g.world.build.aliveCount() == 1, "same slot can't be built twice");

    // Wall blocks movement.
    run(g, 120, [](int) { InputState in; in.forward = true; return in; });
    CHECK(g.player.body.pos.z < 3 * TILE, "wall blocks walking (z=%.2f)", g.player.body.pos.z);

    // Ramp: follows facing; R rotates.
    standAtCell(g, 2, 2);
    InputState rampKey;
    rampKey.buildPiecePressed = 2;
    g.player.yaw = PI * 0.5f;  // face +x
    g.update(rampKey, DT);
    CHECK(g.buildTarget.slot.type == PieceType::Ramp && g.buildTarget.slot.dir == 1, "ramp faces +x (dir=%d)",
          g.buildTarget.slot.dir);
    InputState rot;
    rot.reloadPressed = true;
    g.update(rot, DT);
    CHECK(g.buildTarget.slot.dir == 2, "R rotates ramp (dir=%d)", g.buildTarget.slot.dir);
    g.player.rampRotation = 0;

    // Walk up a ramp.
    Game g2 = quietGame();
    standAtCell(g2, 4, 4);
    clearArea(g2, g2.player.body.pos, 30);
    standAtCell(g2, 4, 4);
    float base = g2.player.body.pos.y;
    Slot rs;
    rs.type = PieceType::Ramp;
    rs.i = 4; rs.j = 5; rs.k = (int)std::floor((base + 0.5f) / STORY); rs.dir = 0;
    g2.world.build.place(rs, true);
    Slot fs;
    fs.type = PieceType::Floor;
    fs.i = 4; fs.j = 6; fs.k = rs.k + 1;
    g2.world.build.place(fs, true);
    run(g2, 150, [](int) { InputState in; in.forward = true; return in; });
    std::printf("  ramp climb: feet y %.2f -> %.2f (floor at %.2f)\n", base, g2.player.body.pos.y, fs.k * STORY);
    CHECK(std::fabs(g2.player.body.pos.y - fs.k * STORY) < 0.1f, "walked up ramp onto floor");

    // Floor, cone and their health.
    Slot cs = fs;
    cs.type = PieceType::Cone;
    cs.k = fs.k + 1;
    int coneId = g2.world.build.place(cs, true);
    g2.world.build.update(BUILD_UP_TIME + 0.1f);
    CHECK(g2.world.build.get(coneId)->maxHp == 140, "cone hp 140");
    // Shooting a piece damages and eventually destroys it.
    int hits = 0;
    while (g2.world.build.get(coneId)->alive && hits < 20) {
        g2.world.build.damage(coneId, 30);
        ++hits;
    }
    CHECK(hits == 5, "140 hp cone breaks on 5th 30-damage hit (hits=%d)", hits);
    CHECK(g2.world.build.isFree(cs), "slot frees up after destruction");

    // Not enough materials.
    g.player.matCount[0] = 5;
    g.player.mode = Mode::Combat;
    InputState floorKey;
    floorKey.buildPiecePressed = 1;
    standAtCell(g, 6, 6);
    g.update(floorKey, DT);
    int before = g.world.build.aliveCount();
    g.update(click, DT);
    CHECK(g.world.build.aliveCount() == before && !g.buildTarget.valid, "can't build with 5 mats");
    CHECK(g.toast.text == "NOT ENOUGH WOOD", "toast '%s'", g.toast.text.c_str());

    // Looking up places the floor one level higher; looking down places it at your level.
    Game g3 = quietGame();
    standAtCell(g3, 3, 3);
    clearArea(g3, g3.player.body.pos, 30);
    standAtCell(g3, 3, 3);
    g3.update(floorKey, DT);
    int k0 = g3.buildTarget.slot.k;
    g3.player.pitch = deg2rad(40);
    g3.update(InputState(), DT);
    int kUp = g3.buildTarget.slot.k;
    CHECK(kUp == k0 + 1, "looking up raises the floor (k0=%d up=%d)", k0, kUp);

    // Hold-to-build places several pieces while moving.
    Game g4 = quietGame();
    standAtCell(g4, 0, 0);
    clearArea(g4, g4.player.body.pos, 40);
    standAtCell(g4, 0, 0);
    g4.update(rampKey, DT);
    g4.player.yaw = 0;
    run(g4, 180, [](int i) { InputState in; in.forward = true; in.fire = true; in.firePressed = (i == 0); return in; });
    std::printf("  turbo ramp rush: %d ramps, feet y=%.2f\n", g4.world.build.aliveCount(), g4.player.body.pos.y);
    CHECK(g4.world.build.aliveCount() >= 2 && g4.player.body.pos.y > 3.0f, "ramp rush climbs");
}

// ------------------------------------------------------------------ stage 4
static void aimAt(Game& g, const Vec3& p) {
    g.updateCamera();
    for (int it = 0; it < 4; ++it) {  // camera moves with pitch; iterate
        Vec3 d = normalize(p - g.camera.eye);
        g.player.yaw = std::atan2(d.x, d.z);
        g.player.pitch = std::asin(d.y);
        g.updateCamera();
    }
}

static void testStage4() {
    section("Stage 4: editing");
    Game g = quietGame();
    standAtCell(g, 2, 2);
    clearArea(g, g.player.body.pos, 30);
    standAtCell(g, 2, 2);
    Slot s;
    s.type = PieceType::Wall;
    s.side = 1; s.i = 2; s.j = 3;
    s.k = (int)std::floor((g.player.body.pos.y + 0.5f) / STORY);
    int id = g.world.build.place(s, true);
    Piece* w = g.world.build.get(id);

    aimAt(g, pieceTileBounds(*w, 4).center());
    InputState gk;
    gk.editPressed = true;
    g.update(gk, DT);
    CHECK(g.player.mode == Mode::Edit && g.player.edit.pieceId == id, "G enters edit mode");

    // Drag from middle tile (4) down to bottom-middle (1), release.
    aimAt(g, pieceTileBounds(*w, 4).center());
    InputState press;
    press.fire = press.firePressed = true;
    g.update(press, DT);
    CHECK(g.player.edit.selected == (1 << 4), "tile 4 selected (mask=0x%x)", g.player.edit.selected);
    aimAt(g, pieceTileBounds(*w, 1).center());
    InputState hold;
    hold.fire = true;
    g.update(hold, DT);
    CHECK(g.player.edit.selected == ((1 << 4) | (1 << 1)), "drag adds tile 1 (mask=0x%x)", g.player.edit.selected);
    InputState rel;
    rel.fireReleased = true;
    g.update(rel, DT);
    CHECK(g.player.mode == Mode::Combat, "release confirms and exits edit");
    CHECK(w->removed == ((1 << 4) | (1 << 1)), "door cut (removed=0x%x)", w->removed);
    std::vector<AABB> boxes;
    pieceBoxes(*w, boxes);
    CHECK(boxes.size() == 7, "7 tiles remain (%zu)", boxes.size());

    // Walk through the new door.
    g.player.yaw = 0; g.player.pitch = 0;
    g.player.body.pos.x = (s.i + 0.5f) * TILE;
    run(g, 150, [](int) { InputState in; in.forward = true; return in; });
    CHECK(g.player.body.pos.z > s.j * TILE + 1.0f, "walked through the door (z=%.2f wall=%.2f)", g.player.body.pos.z,
          s.j * TILE);

    // Reset with right click.
    standAtCell(g, 2, 2);
    aimAt(g, pieceTileBounds(*w, 7).center());
    g.update(gk, DT);
    CHECK(g.player.mode == Mode::Edit && g.player.edit.selected == w->removed, "re-enter edit with current shape");
    InputState rmb;
    rmb.aim = rmb.aimPressed = true;
    g.update(rmb, DT);
    CHECK(w->removed == 0 && g.player.mode == Mode::Combat, "RMB resets the piece");

    // G again also confirms; can't remove every tile.
    aimAt(g, pieceTileBounds(*w, 4).center());
    g.update(gk, DT);
    g.player.edit.selected = 0x1FF;
    g.update(gk, DT);
    CHECK(w->removed == 0, "can't cut out all 9 tiles");

    // Door / window shape rules. Tiles: row*3+col, row 0 at the bottom.
    //   6 7 8
    //   3 4 5
    //   0 1 2
    auto mask = [](std::initializer_list<int> tiles) {
        uint16_t m = 0;
        for (int t : tiles) m |= (uint16_t)(1u << t);
        return m;
    };
    CHECK(validWallEdit(0), "uncut wall is valid");
    CHECK(validWallEdit(mask({1, 4})), "door (bottom middle, two tall)");
    CHECK(validWallEdit(mask({0, 3})), "side door");
    CHECK(validWallEdit(mask({4})), "window (middle)");
    CHECK(validWallEdit(mask({0, 1, 3, 4})), "double door");
    CHECK(validWallEdit(mask({0, 1, 2})) == false, "whole bottom row: the rest would float");
    CHECK(validWallEdit(mask({3, 4, 5})) == false, "whole middle row: the top row would float");
    CHECK(validWallEdit(mask({6, 7, 8})), "half wall (top row cut)");
    CHECK(validWallEdit(mask({1, 4, 7})), "full-height opening");
    CHECK(validWallEdit(mask({0, 2})) == false, "two separate holes");
    CHECK(validWallEdit(mask({1, 4, 5})) == false, "L shape isn't a rectangle");
    CHECK(validWallEdit(mask({0, 4})) == false, "diagonal tiles aren't one opening");
    CHECK(validWallEdit(0x1FF) == false, "all nine tiles");
    CHECK(validEdit(PieceType::Floor, mask({0, 3})), "floors have no shape rules (diagonal ok)");

    // An invalid shape is refused in game and the wall stays as it was.
    aimAt(g, pieceTileBounds(*w, 4).center());
    g.update(gk, DT);
    g.player.edit.selected = mask({3, 4, 5});
    g.update(gk, DT);
    CHECK(w->removed == 0 && g.player.mode == Mode::Combat, "invalid wall edit isn't applied (0x%x)", w->removed);
    aimAt(g, pieceTileBounds(*w, 4).center());
    g.update(gk, DT);
    g.player.edit.selected = mask({4});
    g.update(gk, DT);
    CHECK(w->removed == mask({4}), "window edit applied (0x%x)", w->removed);
    w->removed = 0;

    // Floor is a 2x2 grid; cone too; ramps can't be edited.
    CHECK(editGridSize(PieceType::Wall) == 3 && editGridSize(PieceType::Floor) == 2 &&
              editGridSize(PieceType::Cone) == 2 && editGridSize(PieceType::Ramp) == 2,
          "grid sizes");
    Slot fl;
    fl.type = PieceType::Floor;
    fl.i = 2; fl.j = 2; fl.k = s.k + 1;
    int fid = g.world.build.place(fl, true);
    Piece* f = g.world.build.get(fid);
    standAtCell(g, 2, 2);
    aimAt(g, pieceTileBounds(*f, 3).center());
    g.update(gk, DT);
    CHECK(g.player.mode == Mode::Edit && g.player.edit.pieceId == fid, "edit floor from below");
    g.update(press, DT);
    g.update(rel, DT);
    boxes.clear();
    pieceBoxes(*f, boxes);
    CHECK(boxes.size() == 3 && f->removed != 0, "floor tile cut (%zu left)", boxes.size());

    Slot cs = fl;
    cs.type = PieceType::Cone;
    cs.i = 3;
    int cid = g.world.build.place(cs, true);
    Piece* cone = g.world.build.get(cid);
    cone->removed = 1u << 3;
    std::vector<Tri> tris;
    pieceTris(*cone, tris);
    CHECK(tris.size() == 128, "edited cone is tessellated (%zu tris)", tris.size());
    float y;
    float cx = (cs.i + 1.0f) * TILE, cz = (cs.j + 1.0f) * TILE;
    CHECK(pieceSurfaceHeight(*cone, cx, cz, y) && std::fabs(y - (cs.k * STORY + CONE_EDIT_HEIGHT)) < 0.05f,
          "the selected corner rises to the edit height (y=%.2f)", y);
    float y2;
    CHECK(pieceSurfaceHeight(*cone, cs.i * TILE, cs.j * TILE, y2) && std::fabs(y2 - cs.k * STORY) < 0.05f,
          "the far corner drops to the base (y=%.2f)", y2);
    // Every quadrant may be selected on a cone (it stays a plain cone).
    standAtCell(g, 3, 1);
    {
        Vec3 tc = pieceTileBounds(*cone, 0).center();
        float sy;
        if (pieceSurfaceHeight(*cone, tc.x, tc.z, sy)) tc.y = sy;
        aimAt(g, tc);
    }
    g.update(gk, DT);
    if (g.player.mode == Mode::Edit) {
        g.player.edit.selected = 0xF;
        g.update(gk, DT);
    }
    CHECK(cone->removed == 0xF, "all four cone quadrants can be edited (0x%x)", cone->removed);
    cone->removed = 1u << 3;

    // Ramps (stairs) edit too: two tiles on a side turn the ramp that way, an L makes turn stairs.
    Slot rs;
    rs.type = PieceType::Ramp;
    rs.i = 2; rs.j = 1; rs.k = s.k; rs.dir = 0;
    int rid = g.world.build.place(rs, true);
    Piece* rp = g.world.build.get(rid);
    standAtCell(g, 2, 2);
    aimAt(g, pieceBounds(rs).center());
    g.update(gk, DT);
    CHECK(g.player.mode == Mode::Edit && g.player.edit.pieceId == rid, "ramps can be edited");
    // Fortnite style: swipe from tile 0 to tile 1 (toward +x) and the stairs rise toward +x.
    g.player.edit.selected = 0x3;
    g.player.edit.firstTile = 0;
    g.player.edit.lastTile = 1;
    g.update(gk, DT);
    CHECK(rp->slot.dir == 1 && std::fabs(rampHeightAt(*rp, TILE, TILE * 0.5f) - STORY) < 1e-3f && rampHeightAt(*rp, 0, TILE * 0.5f) < 1e-3f,
          "swipe toward +x: stairs now rise toward +x (dir %d)", rp->slot.dir);
    applyStairEdit(*rp, 0xC, 3, 2);  // swipe from tile 3 to tile 2: toward -x
    CHECK(rp->slot.dir == 3 && rampHeightAt(*rp, 0, TILE * 0.5f) > STORY - 1e-3f, "swipe toward -x: rise toward -x");
    // An L of three: turn stairs climbing from the first tile round to the last.
    applyStairEdit(*rp, 0x7, 1, 2);  // tiles 1 -> 0 -> 2, round the (+x,+z) corner
    float low = rampHeightAt(*rp, TILE * 0.75f, TILE * 0.25f), high = rampHeightAt(*rp, TILE * 0.25f, TILE * 0.75f);
    CHECK(low < STORY * 0.3f && high > STORY * 0.7f && rampHeightAt(*rp, TILE * 0.1f, TILE * 0.1f) > 0.3f * STORY && rampHeightAt(*rp, TILE * 0.1f, TILE * 0.1f) < 0.7f * STORY,
          "L swipe 1->0->2: turn stairs, low at the first tile (%.1f), high at the last (%.1f)", low, high);
    applyStairEdit(*rp, 0x7, 2, 1);  // the other way round
    CHECK(rampHeightAt(*rp, TILE * 0.75f, TILE * 0.25f) > STORY * 0.7f && rampHeightAt(*rp, TILE * 0.25f, TILE * 0.75f) < STORY * 0.3f,
          "swiping the other way flips the turn stairs");
    applyStairEdit(*rp, 0xF, 0, 3);
    CHECK(rp->removed == 0, "all four tiles: plain stairs again");
}

// ------------------------------------------------------------------ stage 5
static void testStage5() {
    section("Stage 5: loot + chests");
    Rng rng(42);
    int counts[(int)Rarity::Count] = {};
    int guns[(int)GunType::Count] = {};
    int medkits = 0;
    const int N = 100000;
    bool ammoMatches = true;
    for (int i = 0; i < N; ++i) {
        std::vector<Item> items = rollChest(rng);
        counts[(int)items[0].rarity]++;
        guns[(int)items[0].gun]++;
        if (items[0].kind != ItemKind::Gun || items[1].kind != ItemKind::Ammo || items[1].ammoType != gunStats(items[0].gun).ammo)
            ammoMatches = false;
        for (const Item& it : items) if (it.kind == ItemKind::MedKit) ++medkits;
    }
    std::printf("  rarity %%: ");
    for (int r = 0; r < (int)Rarity::Count; ++r) {
        float pct = 100.0f * counts[r] / N;
        std::printf("%s %.1f  ", rarityName((Rarity)r), pct);
        CHECK(std::fabs(pct - RARITY_WEIGHTS[r]) < 1.0f, "rarity %d at %.1f%%", r, pct);
    }
    std::printf("\n  med kit in %.1f%% of chests\n", 100.0f * medkits / N);
    CHECK(ammoMatches, "every chest: 1 gun + matching ammo");
    CHECK(std::fabs(100.0f * medkits / N - 50.0f) < 1.0f, "50%% med kit");
    for (int t = 0; t < (int)GunType::Count; ++t) CHECK(guns[t] > N / 6, "gun type %d drops", t);
    CHECK(rarityDamageMult(Rarity::Uncommon) == 1.05f && std::fabs(rarityDamageMult(Rarity::Legendary) - 1.2f) < 1e-5f,
          "+5%% damage per rarity step");

    // Hold E for half a second to open a chest.
    Game g = quietGame();
    const Chest* target = nullptr;
    int ci = -1;
    for (size_t i = 0; i < g.world.chests.size(); ++i) {
        bool open = true;
        for (const Vec3& h : g.world.houseCenters) if (lengthXZ(h - g.world.chests[i].pos) < 9) open = false;
        if (open) { ci = (int)i; break; }
    }
    target = &g.world.chests[ci];
    g.player.body.pos = target->pos + Vec3(0, 0.3f, -2.0f);
    g.player.body.vel = Vec3();
    g.player.yaw = 0;
    run(g, 20, none);
    aimAt(g, target->pos + Vec3(0, 0.4f, 0));
    g.update(InputState(), DT);
    CHECK(g.prompt.kind == InteractPrompt::OpenChest, "prompt shows OPEN CHEST (%d)", (int)g.prompt.kind);
    InputState e;
    e.interact = true;
    e.interactPressed = true;
    g.update(e, DT);
    e.interactPressed = false;
    run(g, 20, [&](int) { return e; });  // ~0.35 s
    CHECK(!g.world.chests[ci].opened, "not open before 0.5 s");
    run(g, 12, [&](int) { return e; });
    CHECK(g.world.chests[ci].opened, "open after holding 0.5 s");
    int pk = 0;
    bool hasGun = false, hasAmmo = false;
    for (const Pickup& p : g.world.pickups) {
        if (!p.alive) continue;
        ++pk;
        hasGun |= p.item.kind == ItemKind::Gun;
        hasAmmo |= p.item.kind == ItemKind::Ammo;
    }
    CHECK(pk >= 2 && hasGun && hasAmmo, "chest dropped %d items", pk);
    run(g, 120, none);
    bool settledFloating = true;
    for (const Pickup& p : g.world.pickups)
        if (p.alive) {
            float gr = g.world.groundHeight(p.pos.x, p.pos.z, 0.2f, p.pos.y, 0.0f);
            if (!p.settled || std::fabs(p.pos.y - gr - 0.5f) > 0.05f) settledFloating = false;
        }
    CHECK(settledFloating, "items land and float above the ground");

    // Pick up everything with E.
    for (size_t i = 0; i < g.world.pickups.size(); ++i) {
        Pickup& p = g.world.pickups[i];
        if (!p.alive) continue;
        g.player.body.pos = p.pos - Vec3(0, 0.5f, 0.8f);
        g.player.body.vel = Vec3();
        g.player.yaw = 0;
        run(g, 3, none);
        InputState pe;
        pe.interact = pe.interactPressed = true;
        g.update(pe, DT);
        CHECK(!g.world.pickups[i].alive, "picked up %s", g.world.pickups[i].item.label().c_str());
    }
    CHECK(g.player.slots[1].kind == ItemKind::Gun, "new gun in slot 2");

    // Full hotbar: swap with selected slot and drop the old item.
    Game g2 = quietGame();
    for (int i = 0; i < HOTBAR_SLOTS; ++i) g2.player.slots[i] = Item::makeGun(GunType::Pistol, Rarity::Common);
    g2.player.selected = 2;
    size_t before = g2.world.pickups.size();
    g2.giveItem(Item::makeGun(GunType::Sniper, Rarity::Epic), true);
    CHECK(g2.player.slots[2].gun == GunType::Sniper && g2.world.pickups.size() == before + 1, "swap + drop");

    // Med Kit: 3 s standing still heals 50.
    Game g3 = quietGame();
    standAtCell(g3, 0, 0);
    g3.player.health = 40;
    g3.player.slots[1] = Item::makeMedKit(2);
    InputState s2;
    s2.slotPressed = 1;
    g3.update(s2, DT);
    InputState use;
    use.fire = use.firePressed = true;
    g3.update(use, DT);
    run(g3, 150, none);  // 2.5 s
    CHECK(g3.player.health == 40, "no heal before 3 s");
    run(g3, 40, none);
    CHECK(g3.player.health == 90 && g3.player.slots[1].count == 1, "healed to %.0f, kits left %d", g3.player.health,
          g3.player.slots[1].count);
    g3.update(use, DT);
    run(g3, 60, [](int) { InputState in; in.forward = true; return in; });
    CHECK(g3.player.medkitTimer < 0 && g3.player.slots[1].count == 1 && g3.player.health == 90, "moving cancels healing");
    g3.player.health = 100;
    g3.update(use, DT);
    CHECK(g3.player.medkitTimer < 0, "can't heal at full health");
}

// ------------------------------------------------------------------ stage 6
static void testStage6() {
    section("Stage 6: enemies");
    Game g(1337);
    CHECK((int)g.bots.size() >= 8 && g.enemiesAlive() == (int)g.bots.size(), "%zu enemies", g.bots.size());
    for (size_t i = 1; i < g.bots.size(); ++i) g.bots[i].alive = false;
    standAtCell(g, 0, 0);
    clearArea(g, g.player.body.pos, 50);
    standAtCell(g, 0, 0);
    Bot& e = g.bots[0];
    e.body.pos = g.player.body.pos + Vec3(0, 0, 25);
    e.body.pos.y = g.world.terrainHeight(e.body.pos.x, e.body.pos.z);
    e.yaw = PI;  // facing the player
    e.gun = Item::makeGun(GunType::AssaultRifle, Rarity::Common);
    e.state = AIState::Wander;
    e.moveTarget = e.body.pos;
    std::vector<AIState> seen = {AIState::Wander};
    float hp0 = g.player.health + g.player.shield;
    float spotAt = -1, shootAt = -1;
    for (int i = 0; i < 60 * 6; ++i) {
        g.update(InputState(), DT);
        if (seen.empty() || seen.back() != e.state) {
            seen.push_back(e.state);
            if (e.state == AIState::Spot && spotAt < 0) spotAt = g.time;
            if (e.state == AIState::Shoot && shootAt < 0) shootAt = g.time;
        }
    }
    std::printf("  states:");
    for (AIState s : seen) std::printf(" %s", aiStateName(s));
    std::printf("\n  reaction delay %.2fs, player hp+shield %.0f -> %.0f in 6 s standing still\n", shootAt - spotAt, hp0,
                g.player.health + g.player.shield);
    CHECK(seen.size() >= 3 && seen[1] == AIState::Spot, "Wander -> Spot");
    CHECK(shootAt > spotAt && shootAt - spotAt >= 0.4f, "reaction delay before shooting");
    float lost = hp0 - (g.player.health + g.player.shield);
    CHECK(lost > 0 && lost < hp0, "enemy hurts but doesn't instantly kill (lost %.0f in 6 s)", lost);

    // A wall between us hides the player.
    Game g2(1337);
    for (size_t i = 1; i < g2.bots.size(); ++i) g2.bots[i].alive = false;
    standAtCell(g2, 0, 0);
    clearArea(g2, g2.player.body.pos, 50);
    standAtCell(g2, 0, 0);
    Bot& e2 = g2.bots[0];
    e2.body.pos = g2.player.body.pos + Vec3(0, 0, 20);
    e2.body.pos.y = g2.world.terrainHeight(e2.body.pos.x, e2.body.pos.z);
    e2.yaw = PI;
    e2.state = AIState::Wander;
    e2.moveTarget = e2.body.pos;
    for (int k = -1; k <= 1; ++k) {
        Slot s;
        s.type = PieceType::Wall;
        s.side = 1; s.i = k; s.j = 1; s.k = (int)std::floor((g2.player.body.pos.y + 0.5f) / STORY);
        g2.world.build.place(s, true);
        s.k += 1;
        g2.world.build.place(s, true);
    }
    run(g2, 60, none);
    CHECK(e2.state == AIState::Wander && !e2.canSee, "can't see through walls (state %s)", aiStateName(e2.state));

    // Low health -> take cover behind something.
    Game g3(1337);
    for (size_t i = 1; i < g3.bots.size(); ++i) g3.bots[i].alive = false;
    Vec3 hc = g3.world.houseCenters[0];
    g3.player.body.pos = hc + Vec3(0, 0, -22);
    g3.player.body.pos.y = g3.world.terrainHeight(g3.player.body.pos.x, g3.player.body.pos.z);
    g3.player.yaw = 0;
    Bot& e3 = g3.bots[0];
    e3.body.pos = hc + Vec3(4, 0, -9);
    e3.body.pos.y = g3.world.terrainHeight(e3.body.pos.x, e3.body.pos.z);
    e3.state = AIState::Shoot;
    e3.canSee = true;
    e3.targetId = PLAYER_ACTOR_ID;
    e3.lastKnown = g3.player.body.pos;
    e3.health = 30;
    e3.yaw = PI;
    bool tookCover = false, hidden = false;
    for (int i = 0; i < 60 * 6; ++i) {
        g3.update(InputState(), DT);
        if (e3.state == AIState::TakeCover) tookCover = true;
        if (tookCover && e3.coverTime > 0.7f) {
            hidden = !e3.canSee;
            break;
        }
    }
    std::printf("  cover: took=%d hidden=%d at (%.1f, %.1f), house (%.1f, %.1f)\n", tookCover, hidden, e3.coverPos.x,
                e3.coverPos.z, hc.x, hc.z);
    CHECK(tookCover, "low health -> take cover");
    CHECK(hidden, "cover breaks line of sight");

    // Death drops the gun.
    Game g4(1337);
    size_t before = g4.world.pickups.size();
    GunType gt = g4.bots[3].gun.gun;
    g4.damageActor(3, 500, false, g4.bots[3].body.pos, PLAYER_ACTOR_ID, "TEST");
    CHECK(!g4.bots[3].alive && g4.world.pickups.size() == before + 2 && g4.world.pickups[before].item.gun == gt,
          "dead bot drops its gun");
    const Item& matsDrop = g4.world.pickups.back().item;
    CHECK(matsDrop.kind == ItemKind::Mats && matsDrop.count == DEATH_MATS_DROP && matsDrop.mat >= 0 && matsDrop.mat <= 2,
          "...and 50 of a random material (%s)", matsDrop.label().c_str());
    {
        int wood = g4.player.matCount[matsDrop.mat];
        g4.giveItem(matsDrop, false);
        CHECK(g4.player.matCount[matsDrop.mat] == wood + DEATH_MATS_DROP, "picking it up adds the mats");
    }
    CHECK(g4.enemiesAlive() == (int)g4.bots.size() - 1 && g4.player.kills == 1, "kill counted");

    // All dead -> victory.
    for (size_t i = 0; i < g4.bots.size(); ++i) g4.damageActor((int)i, 500, false, Vec3(), PLAYER_ACTOR_ID, "TEST");
    CHECK(g4.victory(), "victory when everyone is down");
}

// ------------------------------------------------------------------ stage 7
static void testStage7() {
    section("Stage 7: HUD + scene");
    Game g(1337);
    g.player.slots[1] = Item::makeGun(GunType::Shotgun, Rarity::Epic);
    g.player.slots[2] = Item::makeMedKit(2);
    g.showHelp = true;
    HudBuilder hud;
    buildHud(g, 1600, 900, 144, Bindings::defaults(), hud);
    std::printf("  HUD quads: %zu\n", hud.quads.size());
    CHECK(hud.quads.size() > 300, "HUD has content");
    bool inBounds = true;
    for (const HudQuad& q : hud.quads)
        if (q.x < -1 || q.y < -1 || q.x + q.w > 1601 || q.y + q.h > 901) inBounds = false;
    CHECK(inBounds, "all HUD quads on screen at 1600x900");
    // Epic-coloured hotbar border present.
    Vec4 epic = rarityColor(Rarity::Epic);
    bool found = false;
    for (const HudQuad& q : hud.quads)
        if (q.color.x == epic.x && q.color.y == epic.y && q.color.z == epic.z) found = true;
    CHECK(found, "rarity-coloured hotbar border");
    buildHud(g, 1280, 720, 60, Bindings::defaults(), hud);
    inBounds = true;
    for (const HudQuad& q : hud.quads)
        if (q.x < -1 || q.y < -1 || q.x + q.w > 1281 || q.y + q.h > 721) inBounds = false;
    CHECK(inBounds, "all HUD quads on screen at 1280x720");

    SceneList scene;
    buildScene(g, 16.0f / 9.0f, scene);
    std::printf("  scene instances: %zu (cubes %zu)\n", scene.totalInstances(), scene.opaque[MESH_CUBE].size());
    CHECK(scene.opaque[MESH_TERRAIN].size() == 1 && scene.totalInstances() > 800, "scene filled");
    CHECK(scene.opaque[MESH_SPHERE].size() > 200 && scene.opaque[MESH_ROCK].size() >= 50 && scene.opaque[MESH_CYLINDER].size() > 100,
          "round shapes used (spheres %zu, rocks %zu, cylinders %zu)", scene.opaque[MESH_SPHERE].size(),
          scene.opaque[MESH_ROCK].size(), scene.opaque[MESH_CYLINDER].size());

    // Humanoid: player + each enemy = 9-11 boxes each.
    std::vector<MeshVertex> v;
    std::vector<uint32_t> idx;
    makeCubeMesh(v, idx);
    CHECK(v.size() == 24 && idx.size() == 36, "cube mesh");
    makeTerrainMesh(g.world, v, idx);
    CHECK(v.size() == 401 * 401, "terrain mesh %zu verts", v.size());

    // Every mesh must wind its triangles so the outside is Direct3D's front face (clockwise on
    // screen). In our left-handed space that means cross(b-a, c-a) points along the normal.
    auto windingOk = [](const std::vector<MeshVertex>& vv, const std::vector<uint32_t>& ii) {
        for (size_t t = 0; t + 2 < ii.size(); t += 3) {
            const MeshVertex &a = vv[ii[t]], &b = vv[ii[t + 1]], &c = vv[ii[t + 2]];
            Vec3 e1(b.px - a.px, b.py - a.py, b.pz - a.pz), e2(c.px - a.px, c.py - a.py, c.pz - a.pz);
            if (dot(cross(e1, e2), Vec3(a.nx, a.ny, a.nz)) <= 0) return false;
        }
        return true;
    };
    CHECK(windingOk(v, idx), "terrain winding");
    makeCubeMesh(v, idx);
    CHECK(windingOk(v, idx), "cube winding");
    makeConeQuadMesh(v, idx);
    CHECK(windingOk(v, idx), "cone winding");
    makeSphereMesh(v, idx);
    CHECK(windingOk(v, idx) && v.size() > 400, "sphere winding (%zu verts)", v.size());
    makeCylinderMesh(v, idx);
    CHECK(windingOk(v, idx), "cylinder winding");
    makeRockMesh(v, idx);
    CHECK(windingOk(v, idx), "rock winding");
}


// ------------------------------------------------------------------ pickaxe
static void testPickaxe() {
    section("Pickaxe + harvesting");
    Game g = quietGame();
    CHECK(g.player.pickaxeOut && g.heldItem().kind == ItemKind::Pickaxe, "you start holding the pickaxe");
    CHECK(g.player.matCount[0] == START_MATS, "start with %d mats", g.player.matCount[0]);

    // Find a tree and stand next to it.
    int trunk = -1;
    for (size_t i = 0; i < g.world.statics.size(); ++i)
        if (g.world.statics[i].kind == PropKind::Trunk) { trunk = (int)i; break; }
    Vec3 tc = g.world.statics[trunk].box.center();
    g.player.body.pos = Vec3(tc.x, 0, tc.z - 1.6f);
    g.player.body.pos.y = g.world.terrainHeight(g.player.body.pos.x, g.player.body.pos.z);
    g.player.body.vel = Vec3();
    run(g, 3, none);
    aimAt(g, Vec3(tc.x, g.player.body.pos.y + 1.3f, tc.z));
    int mats0 = g.player.matCount[0];
    int grp = g.world.statics[trunk].group;
    // Hold fire for 2 s: a swing every 0.5 s.
    int swings = 0;
    float prevCd = 0;
    for (int i = 0; i < 120; ++i) {
        aimAt(g, Vec3(tc.x, g.player.body.pos.y + 1.3f, tc.z));
        InputState in;
        in.fire = true;
        in.firePressed = i == 0;
        g.update(in, DT);
        if (g.player.swingCooldown > prevCd + 0.1f) ++swings;
        prevCd = g.player.swingCooldown;
    }
    std::printf("  %d swings in 2 s, mats %d -> %d, tree hp %.0f\n", swings, mats0, g.player.matCount[0], g.world.groups[grp].hp);
    CHECK(swings == 4, "one swing every 0.5 s (%d)", swings);
    CHECK(g.player.matCount[0] == mats0 + 4 * 8, "8 mats per tree hit");
    CHECK(std::fabs(g.world.groups[grp].hp - (200 - 4 * 50)) < 1e-3f || !g.world.groups[grp].alive, "50 damage per hit");
    // Keep going until it breaks.
    for (int i = 0; i < 120 && g.world.groups[grp].alive; ++i) {
        aimAt(g, Vec3(tc.x, g.player.body.pos.y + 1.3f, tc.z));
        InputState in;
        in.fire = true;
        g.update(in, DT);
    }
    CHECK(!g.world.groups[grp].alive && !g.world.statics[trunk].alive, "tree breaks");
    HitInfo h;
    Vec3 eye = g.player.eyePivot();
    bool blocked = g.world.raycast(Ray{eye, normalize(Vec3(tc.x, eye.y, tc.z) - eye)}, 3.0f, h, false) && h.kind == HitKind::Static;
    CHECK(!blocked, "broken tree no longer blocks");

    // Pickaxe on a build and on a bot.
    Game g2(1337);
    for (size_t i = 1; i < g2.bots.size(); ++i) g2.bots[i].alive = false;
    standAtCell(g2, 0, 0);
    clearArea(g2, g2.player.body.pos, 30);
    standAtCell(g2, 0, 0);
    Slot s;
    s.type = PieceType::Wall; s.side = 1; s.i = 0; s.j = 1; s.k = (int)std::floor((g2.player.body.pos.y + 0.5f) / STORY);
    int wid = g2.world.build.place(s, true);
    g2.world.build.update(BUILD_UP_TIME + 0.1f);
    aimAt(g2, pieceBounds(s).center() + Vec3(0, -0.6f, 0));
    g2.swingPickaxe();
    CHECK(std::fabs(g2.world.build.get(wid)->hp - 100) < 1e-3f, "pickaxe does 50 to builds (hp %.0f)", g2.world.build.get(wid)->hp);
    g2.world.build.destroy(wid);
    Bot& b = g2.bots[0];
    b.body.pos = g2.player.body.pos + Vec3(0, 0, 2.0f);
    b.state = AIState::Spot;
    b.reaction = 99;
    aimAt(g2, b.body.pos + Vec3(0, 1.0f, 0));
    float hp = b.health;
    g2.swingPickaxe();
    CHECK(std::fabs(hp - b.health - PICKAXE_PLAYER_DAMAGE) < 1e-3f, "pickaxe does 20 to players (%.0f)", hp - b.health);

    // Switching: number key takes out the item, pickaxe key puts it back.
    InputState one;
    one.slotPressed = 0;
    g2.update(one, DT);
    CHECK(!g2.player.pickaxeOut && g2.heldItem().kind == ItemKind::Gun, "1 takes out the pistol");
    InputState pk;
    pk.pickaxePressed = true;
    g2.update(pk, DT);
    CHECK(g2.player.pickaxeOut, "pickaxe key goes back to the pickaxe");
}

// ------------------------------------------------------------------ stamina
static void testStamina() {
    section("Stamina");
    Game g = quietGame();
    standAtCell(g, 0, 0);
    clearArea(g, g.player.body.pos, 80);
    standAtCell(g, 0, 0);
    InputState sprint;
    sprint.forward = sprint.sprint = true;
    run(g, 180, [&](int) { return sprint; });  // 3 s
    std::printf("  after 3 s of sprinting: %.0f stamina\n", g.player.stamina);
    CHECK(std::fabs(g.player.stamina - (STAMINA_MAX - 3 * STAMINA_DRAIN)) < 2.0f, "drains %.0f/s", STAMINA_DRAIN);
    int guard = 0;
    while (g.player.stamina > 0 && guard++ < 600) g.update(sprint, DT);
    CHECK(g.player.stamina == 0 && g.player.exhausted, "runs out after %.1f s", 3.0f + guard * DT);
    // Rest: nothing for the delay, then it refills.
    run(g, (int)(STAMINA_REGEN_DELAY * 60) - 5, none);
    CHECK(g.player.stamina == 0, "waits before refilling");
    Vec3 a = g.player.body.pos;
    run(g, 6, [&](int) { return sprint; });
    CHECK(!g.player.sprinting, "can't sprint while exhausted");
    run(g, 54, [&](int) { return sprint; });
    float v = lengthXZ(g.player.body.pos - a);
    CHECK(v < WALK_SPEED * 1.05f, "walk speed while exhausted (%.2f m/s)", v);
    run(g, 120, none);
    CHECK(g.player.stamina > 30 && !g.player.exhausted, "refills (%.0f)", g.player.stamina);
    g.update(sprint, DT);
    CHECK(g.player.sprinting, "can sprint again");
    // HUD shows the stamina bar.
    HudBuilder hud;
    buildHud(g, 1600, 900, 60, Bindings::defaults(), hud);
    bool bar = false;
    for (const HudQuad& q : hud.quads)
        if (std::fabs(q.color.x - 1.0f) < 1e-3f && std::fabs(q.color.y - 0.82f) < 1e-3f && q.w > 20) bar = true;
    CHECK(bar, "stamina bar drawn");
}

// ------------------------------------------------------------------ keybinds + settings
static void testKeybindsAndSettings() {
    section("Keybinds + settings + friends");
    Bindings b = Bindings::defaults();
    CHECK(b.get(Action::Pickaxe) == 'F' && b.get(Action::Jump) == KEY_SPACE && b.get(Action::Fire) == KEY_MOUSE_LEFT, "defaults");
    bool unique = true;
    for (int i = 0; i < (int)Action::Count; ++i)
        for (int j = i + 1; j < (int)Action::Count; ++j)
            if (b.key[i][0] == b.key[j][0]) unique = false;
    CHECK(unique, "no default key used twice");
    CHECK(b.get(Action::ResetEdit) == 'T' && b.get(Action::Inventory) == KEY_TAB, "reset edit = T, inventory = Tab");
    bool noSecondary = true;
    for (int i = 0; i < (int)Action::Count; ++i) noSecondary = noSecondary && b.key[i][1] == KEY_NONE;
    CHECK(noSecondary, "secondary keys start empty");
    Action sw = b.assign(Action::Jump, 'F');
    CHECK(b.get(Action::Jump) == 'F' && b.get(Action::Pickaxe) == 'F' && sw == Action::Pickaxe, "a used key can be shared (reported)");
    CHECK(b.assign(Action::Jump, KEY_ESCAPE) == Action::Count && b.get(Action::Jump) == 'F', "Esc can't be bound");
    // Secondary keys.
    CHECK(b.assign(Action::Jump, KEY_WHEEL_DOWN, 1) == Action::Count && b.get(Action::Jump, 1) == KEY_WHEEL_DOWN &&
              b.get(Action::Jump, 0) == 'F',
          "secondary key added, primary kept");
    CHECK(b.uses(Action::Jump, 'F') && b.uses(Action::Jump, KEY_WHEEL_DOWN), "both keys count");
    Action sw2 = b.assign(Action::Crouch, KEY_WHEEL_DOWN, 1);
    CHECK(sw2 == Action::Jump && b.get(Action::Crouch, 1) == KEY_WHEEL_DOWN && b.get(Action::Jump, 1) == KEY_WHEEL_DOWN,
          "another action's secondary key can be shared too");
    b.assign(Action::Jump, KEY_WHEEL_DOWN, 0);
    CHECK(b.get(Action::Jump, 0) == KEY_WHEEL_DOWN && b.get(Action::Jump, 1) == KEY_NONE, "moving a key between slots of one action");
    b.clear(Action::Crouch, 1);
    CHECK(b.get(Action::Crouch, 1) == KEY_NONE, "clear a key");
    b.assign(Action::Crouch, KEY_WHEEL_DOWN);
    CHECK(keyName(KEY_WHEEL_DOWN) == "WHEEL DOWN" && keyName('Q') == "Q" && keyName(KEY_SHIFT) == "SHIFT" && keyName(0x73) == "F4",
          "key names");

    Settings s;
    s.playerName = "KINGSLAYER";
    s.sensitivity = 0.0031f;
    s.vsync = false;
    s.binds = b;
    s.binds.assign(Action::Fire, KEY_MOUSE_X1, 1);
    CHECK(s.addFriend("river"), "add friend");
    CHECK(!s.addFriend("RIVER") && !s.addFriend("   ") && !s.addFriend("KINGSLAYER"), "no duplicates, blanks or yourself");
    CHECK(s.addFriend("Maple Leaf!!"), "add friend 2");
    CHECK(s.friends.size() == 2 && s.friends[1] == "MAPLE LEAF", "names cleaned (%s)", s.friends[1].c_str());
    CHECK(cleanName("abcdefghijklmnopqrstuvwxyz").size() == MAX_NAME_LEN, "name length limit");

    const char* path = "blockfall_test_settings.ini";
    CHECK(s.save(path), "save");
    Settings l;
    CHECK(l.load(path), "load");
    std::remove(path);
    CHECK(l.playerName == "KINGSLAYER" && std::fabs(l.sensitivity - 0.0031f) < 1e-6f && !l.vsync, "settings round-trip");
    bool same = true;
    for (int i = 0; i < (int)Action::Count; ++i)
        for (int j = 0; j < BIND_SLOTS; ++j) same = same && l.binds.key[i][j] == s.binds.key[i][j];
    CHECK(same, "keybinds round-trip");
    CHECK(l.friends == s.friends, "friends round-trip");
    Settings junk;
    junk.parse("name=\nsensitivity=banana\nbind.jump=27\nbind.fire=999999\nrandom line\n");
    CHECK(junk.playerName == "PLAYER" && junk.binds.get(Action::Jump) == KEY_SPACE && junk.binds.get(Action::Fire) == KEY_MOUSE_LEFT,
          "bad lines are ignored");
    CHECK(l.removeFriend("river") && !l.hasFriend("RIVER"), "remove friend");
}

// ------------------------------------------------------------------ lobby / menus
static RawInput click(float x, float y) {
    RawInput r;
    r.mouseX = x;
    r.mouseY = y;
    r.down[KEY_MOUSE_LEFT] = r.pressed[KEY_MOUSE_LEFT] = true;
    return r;
}

static void testFrontEnd() {
    section("Lobby, friends, modes, keybind screen");
    FrontEnd fe;
    fe.init("");
    SceneList scene;
    HudBuilder hud;
    RawInput idle;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::Lobby && !fe.wantsMouseLocked(), "starts in the lobby with a free mouse");
    CHECK(!scene.drawTerrain && scene.opaque[MESH_SPHERE].size() > 10, "lobby stage drawn with your character");

    CHECK(!fe.invite("STRANGER"), "can't invite someone who isn't a friend");
    fe.settings.addFriend("RIVER");
    fe.settings.addFriend("MAPLE");
    fe.settings.addFriend("JUNO");
    fe.settings.addFriend("OAK");
    CHECK(fe.invite("RIVER") && fe.teamSize == 2, "inviting a friend switches Solo -> Duos");
    CHECK(fe.invite("MAPLE") && fe.teamSize == 3, "-> Trios");
    CHECK(fe.invite("JUNO") && fe.teamSize == 4, "-> Squads");
    CHECK(!fe.invite("OAK") && fe.party.size() == 3, "party max is 3 friends");
    CHECK(!fe.invite("RIVER"), "can't invite twice");

    // Click PLAY (the big button at the top left of the lobby).
    float u = 900.0f / 900.0f;
    RawInput c = click(50 * u + 100, 190 * u + 30);
    fe.frame(c, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::InGame && fe.wantsMouseLocked(), "PLAY starts a match and locks the mouse");
    const Game& g = fe.game;
    int mates = 0, friends = 0, teams = 0;
    std::vector<int> perTeam(128, 0);
    for (const Bot& b : g.bots) {
        perTeam[b.team]++;
        teams = std::max(teams, b.team + 1);
        if (b.team == 0) { ++mates; friends += b.isFriend ? 1 : 0; }
    }
    std::printf("  squads: %d teams, %zu bots, teammates %d (%d friends: %s, %s, %s)\n", teams, g.bots.size(), mates, friends,
                g.bots[0].name.c_str(), g.bots[1].name.c_str(), g.bots[2].name.c_str());
    CHECK(teams == 25 && g.bots.size() == 99 && mates == 3 && friends == 3, "squads = 25 teams of 4 (100 players)");
    CHECK(g.bots[0].name == "RIVER" && g.bots[1].name == "MAPLE" && g.bots[2].name == "JUNO", "friends keep their names");
    bool fours = true;
    for (int t = 1; t < 25; ++t) fours = fours && perTeam[t] == 4;
    CHECK(fours, "every enemy team has 4");
    for (int ts = 1; ts <= 4; ++ts) {
        fe.setTeamSize(ts);
        fe.startMatch();
        int expectBots = teamCountFor(ts) * ts - 1;
        CHECK((int)fe.game.bots.size() == expectBots, "%s: %zu bots (want %d)", teamModeName(ts), fe.game.bots.size(), expectBots);
    }

    // Keybinds: rebind Jump to K, then K jumps and Space doesn't.
    fe.screen = Screen::Settings;
    fe.rebinding = (int)Action::Jump;
    RawInput k;
    k.down['K'] = k.pressed['K'] = true;
    fe.frame(k, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.rebinding == -1 && fe.settings.binds.get(Action::Jump) == 'K', "pressing a key while waiting binds it");
    RawInput space;
    space.down[KEY_SPACE] = space.pressed[KEY_SPACE] = true;
    CHECK(!fe.gameInput(space).jumpPressed, "Space no longer jumps");
    RawInput kk;
    kk.down['K'] = kk.pressed['K'] = true;
    CHECK(fe.gameInput(kk).jumpPressed, "K jumps");
    RawInput f;
    f.down['F'] = f.pressed['F'] = true;
    CHECK(fe.gameInput(f).pickaxePressed, "F = pickaxe");

    // Esc pauses and resumes; leaving goes back to the lobby.
    fe.screen = Screen::InGame;
    RawInput esc;
    esc.down[KEY_ESCAPE] = esc.pressed[KEY_ESCAPE] = true;
    fe.frame(esc, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::Paused && !fe.wantsMouseLocked(), "Esc pauses");
    float t0 = fe.game.time;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.game.time == t0, "game is frozen while paused");
    fe.frame(esc, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::InGame, "Esc resumes");
    RawInput lost;
    lost.focusLost = true;
    fe.frame(lost, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::Paused, "alt-tab pauses");

    // Friends screen: type a name and press Enter to add.
    fe.screen = Screen::Friends;
    fe.frame(click(550, 285), DT, 1600, 900, 60, scene, hud);  // focus the ADD A FRIEND field
    RawInput typed;
    typed.text = "pixel_pal";
    fe.frame(typed, DT, 1600, 900, 60, scene, hud);
    RawInput enter;
    enter.down[KEY_ENTER] = enter.pressed[KEY_ENTER] = true;
    fe.frame(enter, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.settings.hasFriend("PIXEL_PAL"), "typed friend added (friends: %zu)", fe.settings.friends.size());

    // Every screen draws inside the window.
    for (Screen sc : {Screen::Lobby, Screen::Friends, Screen::Settings, Screen::Paused}) {
        fe.screen = sc;
        fe.frame(idle, DT, 1280, 720, 60, scene, hud);
        bool in = true;
        for (const HudQuad& q : hud.quads)
            if (q.x < -1 || q.y < -1 || q.x + q.w > 1281 || q.y + q.h > 721) in = false;
        CHECK(in, "screen %d fits in 1280x720", (int)sc);
    }
}

// ------------------------------------------------------------------ teams
static void testTeams() {
    section("Teams, friendly fire, spectating, winning");
    MatchConfig cfg;
    cfg.teamSize = 2;
    cfg.party = {"RIVER"};
    Game g;
    g.startMatch(cfg);
    Bot& mate = g.bots[0];
    CHECK(mate.team == 0 && mate.name == "RIVER", "duo partner");
    CHECK(lengthXZ(mate.body.pos - g.player.body.pos) < 6.0f, "teammate drops next to you");

    // Friendly fire is off.
    float hp = mate.health;
    CHECK(g.damageActor(mate.index, 50, false, Vec3(), PLAYER_ACTOR_ID, "AR") == 0 && mate.health == hp, "can't hurt your teammate");
    // Enemies can.
    int enemy = -1;
    for (const Bot& b : g.bots)
        if (b.team != 0) { enemy = b.index; break; }
    CHECK(g.damageActor(mate.index, 30, false, Vec3(), enemy, "AR") == 30, "enemies hurt your teammate");

    // Teammates follow you around.
    for (Bot& b : g.bots)
        if (b.team != 0) b.alive = false;
    g.bots[enemy].alive = false;
    Vec3 start = g.player.body.pos;
    clearArea(g, start, 60);
    g.player.yaw = PI * 0.5f;
    run(g, 60 * 5, [](int) { InputState in; in.forward = true; return in; });  // walk ~23 m
    run(g, 60 * 4, none);
    float d = lengthXZ(mate.body.pos - g.player.body.pos);
    std::printf("  walked %.0f m, teammate is %.1f m away\n", lengthXZ(g.player.body.pos - start), d);
    CHECK(d < 10.0f, "teammate keeps up");

    // Bots fight each other.
    Game g2(1337);  // solo: 15 bots, all on different teams
    for (Bot& b : g2.bots) b.alive = false;
    Bot& a = g2.bots[0];
    Bot& c = g2.bots[1];
    a.alive = c.alive = true;
    a.style = c.style = BotStyle::Fighter;  // no running or boxing up in this test
    for (int k = 0; k < 3; ++k) a.mats[k] = c.mats[k] = 0;
    Vec3 mid = g2.world.playerSpawn;
    clearArea(g2, mid, 40);
    a.body.pos = mid + Vec3(-10, 0, 0);
    c.body.pos = mid + Vec3(10, 0, 0);
    a.body.pos.y = g2.world.terrainHeight(a.body.pos.x, a.body.pos.z);
    c.body.pos.y = g2.world.terrainHeight(c.body.pos.x, c.body.pos.z);
    a.yaw = PI * 0.5f;
    c.yaw = -PI * 0.5f;
    g2.player.body.pos = mid + Vec3(0, 0, 120);
    g2.player.alive = false;  // out of the way
    for (int i = 0; i < 60 * 20 && a.alive && c.alive; ++i) g2.update(InputState(), DT);
    std::printf("  bot vs bot: %s %.0f hp, %s %.0f hp after %.1f s\n", a.name.c_str(), a.health, c.name.c_str(), c.health, g2.time);
    CHECK(a.health + a.shield < 100 || c.health + c.shield < 100 || !a.alive || !c.alive, "bots shoot each other");

    // Knocked phase (team modes): the first "death" knocks you while a teammate is standing.
    Game g3;
    g3.startMatch(cfg);
    for (Bot& b : g3.bots)   // keep the enemies out of this
        if (b.team != 0) { b.body.pos = b.body.pos + Vec3(0, 0, -250); b.body.pos.y = g3.world.terrainHeight(b.body.pos.x, b.body.pos.z); }
    g3.damagePlayer(500);
    g3.update(InputState(), DT);
    CHECK(g3.player.alive && g3.player.down && g3.player.downHealth > 90, "knocked instead of eliminated (down hp %.0f)", g3.player.downHealth);
    {
        // Your teammate comes over and picks you up.
        Bot& mate = g3.bots[0];
        mate.body.pos = g3.player.body.pos + Vec3(1.5f, 0, 0);
        mate.state = AIState::Wander;
        mate.hurtTimer = 0;
        float t = 0;
        for (int i = 0; i < 60 * 12 && g3.player.down; ++i, t += DT) g3.update(InputState(), DT);
        CHECK(!g3.player.down && g3.player.alive && g3.player.health >= REVIVE_HEALTH - 1, "teammate revived you after %.1f s (hp %.0f)", t, g3.player.health);
        CHECK(t > REVIVE_TIME - 0.5f, "a revive takes about %.0f s", REVIVE_TIME);
        // You pick your teammate up: hold Interact next to them.
        g3.damageActor(mate.index, 500, false, mate.body.pos, NO_TARGET, "");
        CHECK(mate.alive && mate.down, "teammate knocked, not eliminated");
        mate.body.pos = g3.player.body.pos + Vec3(1.2f, 0, 0);
        InputState hold; hold.interact = true;
        t = 0;
        for (int i = 0; i < 60 * 8 && mate.down; ++i, t += DT) g3.update(hold, DT);
        CHECK(!mate.down && mate.alive, "you revived your teammate after %.1f s", t);
        // Bleeding out ends it. And when nobody on the team is standing, the knocked are out too.
        g3.damagePlayer(500);
        CHECK(g3.player.down, "knocked again");
        g3.player.downHealth = 0.01f;
        g3.update(InputState(), DT);
        CHECK(!g3.player.alive, "bled out");
    }
    CHECK(!g3.player.alive && g3.playerTeamAlive() && !g3.defeat() && g3.spectating == 0, "you're out, spectating RIVER");
    CHECK(length(g3.camera.eye - g3.bots[0].body.pos) < 5.0f, "camera follows the teammate");
    int enemyId = -1;
    for (const Bot& b : g3.bots)
        if (b.team != 0 && b.alive) { enemyId = b.index; break; }
    g3.damageActor(0, 500, false, Vec3(), enemyId, "SMG");
    g3.update(InputState(), DT);
    CHECK(g3.defeat() && g3.placement == g3.teamsAlive() + 1, "team out -> placed #%d", g3.placement);

    // Last team standing wins, even if you're down and your teammate isn't.
    Game g4;
    g4.startMatch(cfg);
    g4.damagePlayer(500);
    for (Bot& b : g4.bots)
        if (b.team != 0) g4.damageActor(b.index, 999, false, Vec3(), 0, "AR");
    g4.update(InputState(), DT);
    CHECK(g4.victory() && g4.placement == 1, "your team wins");
    CHECK(g4.bots[0].kills == (int)g4.bots.size() - 1, "teammate credited with kills");
}

// ------------------------------------------------------------------ icons
static void testIcons() {
    section("Weapon icons");
    std::vector<uint32_t> atlas = buildAtlas();
    CHECK((int)atlas.size() == ATLAS_W * ATLAS_H, "atlas %dx%d", ATLAS_W, ATLAS_H);
    for (int i = 0; i < ICON_COUNT; ++i) {
        float cov = iconCoverage(atlas, (IconId)i);
        CHECK(cov > 0.08f && cov < 0.75f, "icon %d covers %.0f%% of its box", i, cov * 100);
    }
    bool distinct = true;
    for (int a = 0; a < (int)GunType::Count; ++a)
        for (int b2 = a + 1; b2 < (int)GunType::Count; ++b2)
            if (itemIcon(Item::makeGun((GunType)a, Rarity::Common)) == itemIcon(Item::makeGun((GunType)b2, Rarity::Common)))
                distinct = false;
    CHECK(distinct, "every gun type has its own icon");
    CHECK(itemIcon(Item::makePickaxe()) == ICON_PICKAXE && itemIcon(Item::makeMedKit()) == ICON_MEDKIT, "pickaxe + med kit icons");
    // Icons differ from each other pixel-wise.
    float u0, v0, u1, v1, a0, b0, a1, b1;
    iconUV(ICON_AR, u0, v0, u1, v1);
    iconUV(ICON_SNIPER, a0, b0, a1, b1);
    CHECK(u0 != a0 || v0 != b0, "icons live in different atlas cells");
    // Hotbar draws an icon quad for each filled slot (+ the pickaxe).
    Game g = quietGame();
    g.player.slots[1] = Item::makeGun(GunType::Sniper, Rarity::Legendary);
    g.player.slots[2] = Item::makeMedKit(1);
    HudBuilder hud;
    buildHud(g, 1600, 900, 60, Bindings::defaults(), hud);
    int iconQuads = 0;
    for (const HudQuad& q : hud.quads)
        if (q.v0 >= (float)ATLAS_TEXT_H / ATLAS_H - 1e-4f && q.v1 > q.v0) ++iconQuads;
    CHECK(iconQuads >= 4, "hotbar icons drawn (%d)", iconQuads);
}


// ------------------------------------------------------------------ inventory, reset edit, secondary keys
static void testInventoryResetSecondary() {
    section("Inventory, reset-edit key, secondary keybinds");
    FrontEnd fe;
    fe.init("");
    SceneList scene;
    HudBuilder hud;
    fe.startMatch();
    Game& g = fe.game;
    for (Bot& b : g.bots) b.alive = false;
    g.bots[0].alive = true;  // keep the match going
    g.bots[0].body.pos = Vec3(140, 0, 140);
    g.player.slots[1] = Item::makeGun(GunType::Shotgun, Rarity::Epic);
    g.player.slots[2] = Item::makeMedKit(2);
    g.player.ammo[(int)AmmoType::Shells] = 20;

    g.landEveryone();
    RawInput tab;
    tab.down[KEY_TAB] = tab.pressed[KEY_TAB] = true;
    fe.frame(tab, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::Inventory && !fe.wantsMouseLocked(), "Tab opens the inventory and frees the mouse");
    float t0 = g.time;
    RawInput idle;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    CHECK(g.time > t0, "the match keeps running while it's open");

    // Slot boxes: same maths as the screen (panel 1000 wide, centred).
    float u = 1.0f, pw = 1000, px = (1600 - pw) * 0.5f, py = (900 - 560) * 0.5f;
    float gap = 14, sw = (pw - 60 - gap * 4) / 5, sy = py + 96;
    auto slotCenterX = [&](int i) { return px + 30 * u + i * (sw + gap) + sw * 0.5f; };
    fe.frame(click(slotCenterX(1), sy + 70), DT, 1600, 900, 60, scene, hud);
    CHECK(fe.inventorySel == 1, "click picks the shotgun");
    fe.frame(click(slotCenterX(4), sy + 70), DT, 1600, 900, 60, scene, hud);
    CHECK(g.player.slots[4].gun == GunType::Shotgun && g.player.slots[1].empty() && fe.inventorySel == -1,
          "click another slot to move it there");
    fe.frame(click(slotCenterX(2), sy + 70), DT, 1600, 900, 60, scene, hud);
    fe.frame(click(slotCenterX(0), sy + 70), DT, 1600, 900, 60, scene, hud);
    CHECK(g.player.slots[0].kind == ItemKind::MedKit && g.player.slots[2].kind == ItemKind::Gun, "swap two full slots");
    // Drop the pistol (now in slot 2).
    fe.frame(click(slotCenterX(2), sy + 70), DT, 1600, 900, 60, scene, hud);
    size_t before = g.world.pickups.size();
    float dropY = sy + 150 + 20;
    fe.frame(click(px + pw - 230 + 100, dropY + 20), DT, 1600, 900, 60, scene, hud);
    CHECK(g.player.slots[2].empty() && g.world.pickups.size() == before + 1, "DROP throws the item on the ground");
    CHECK(g.dropAmmo(AmmoType::Shells, 10) && g.player.ammo[(int)AmmoType::Shells] == 10, "drop ammo");
    CHECK(!g.dropSlot(2), "can't drop an empty slot");
    bool inBounds = true;
    for (const HudQuad& q : hud.quads)
        if (q.x < -1 || q.y < -1 || q.x + q.w > 1601 || q.y + q.h > 901) inBounds = false;
    CHECK(inBounds, "inventory fits on screen");
    fe.frame(tab, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::InGame && fe.wantsMouseLocked(), "Tab closes it again");
    fe.frame(tab, DT, 1600, 900, 60, scene, hud);
    RawInput esc;
    esc.down[KEY_ESCAPE] = esc.pressed[KEY_ESCAPE] = true;
    fe.frame(esc, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::InGame, "Esc also closes it");

    // Reset edit: T puts a piece back to full, from edit mode or just by looking at it.
    Game e = quietGame();
    standAtCell(e, 2, 2);
    clearArea(e, e.player.body.pos, 30);
    standAtCell(e, 2, 2);
    Slot ws;
    ws.type = PieceType::Wall; ws.side = 1; ws.i = 2; ws.j = 3; ws.k = (int)std::floor((e.player.body.pos.y + 0.5f) / STORY);
    int id = e.world.build.place(ws, true);
    Piece* w = e.world.build.get(id);
    w->removed = (1 << 1) | (1 << 4);
    aimAt(e, pieceTileBounds(*w, 7).center());
    InputState t;
    t.resetEditPressed = true;
    e.update(t, DT);
    CHECK(w->removed == 0 && e.player.mode == Mode::Combat, "T resets the build you look at");
    w->removed = 1 << 4;
    aimAt(e, pieceTileBounds(*w, 7).center());
    InputState gk;
    gk.editPressed = true;
    e.update(gk, DT);
    CHECK(e.player.mode == Mode::Edit, "in edit mode");
    e.update(t, DT);
    CHECK(w->removed == 0 && e.player.mode == Mode::Combat, "T resets and leaves edit mode");
    aimAt(e, Vec3(e.player.body.pos.x, e.player.body.pos.y, e.player.body.pos.z - 20));
    CHECK(!e.resetEdit(), "nothing to reset when not looking at a build");

    // Secondary keys work in game.
    fe.settings.binds.assign(Action::Jump, KEY_MOUSE_X1, 1);
    RawInput m4;
    m4.down[KEY_MOUSE_X1] = m4.pressed[KEY_MOUSE_X1] = true;
    RawInput sp;
    sp.down[KEY_SPACE] = sp.pressed[KEY_SPACE] = true;
    CHECK(fe.gameInput(m4).jumpPressed && fe.gameInput(sp).jumpPressed, "Space and Mouse 4 both jump");
    fe.settings.binds.assign(Action::Fire, 'J', 1);
    RawInput holdBoth;
    holdBoth.down[KEY_MOUSE_LEFT] = true;
    holdBoth.down['J'] = true;
    holdBoth.released['J'] = false;
    RawInput relOne = holdBoth;
    relOne.down['J'] = false;
    relOne.released['J'] = true;
    CHECK(!fe.gameInput(relOne).fireReleased && fe.gameInput(relOne).fire, "letting go of one fire key while holding the other keeps firing");

    // Settings screen: right-click clears, left-click on the secondary column waits for a key.
    fe.screen = Screen::Settings;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    // Find the JUMP secondary button: second column of row 4 in the left half.
    float spw = std::min(1600 - 60.0f, 1180.0f), spx = (1600 - spw) * 0.5f, spy = 40;
    float colW = (spw - 90) * 0.5f;
    int perCol = ((int)Action::Count + 1) / 2;
    float rowH = std::min(42.0f, (900 - 80 - 250.0f) / perCol);
    float btnW = std::min(170.0f, (colW - 230) * 0.5f);
    float jumpY = spy + 116 + (int)Action::Jump * rowH + rowH * 0.5f;
    float secX = spx + 30 + colW - btnW * 0.5f;
    RawInput rc;
    rc.mouseX = secX; rc.mouseY = jumpY;
    rc.down[KEY_MOUSE_RIGHT] = rc.pressed[KEY_MOUSE_RIGHT] = true;
    fe.frame(rc, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.settings.binds.get(Action::Jump, 1) == KEY_NONE, "right click clears the secondary key");
    fe.frame(click(secX, jumpY), DT, 1600, 900, 60, scene, hud);
    CHECK(fe.rebinding == (int)Action::Jump && fe.rebindSlot == 1, "left click waits for a secondary key");
    RawInput k;
    k.down['K'] = k.pressed['K'] = true;
    fe.frame(k, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.settings.binds.get(Action::Jump, 1) == 'K' && fe.settings.binds.get(Action::Jump, 0) == KEY_SPACE,
          "secondary set to K, primary still Space");
    inBounds = true;
    for (const HudQuad& q : hud.quads)
        if (q.x < -1 || q.y < -1 || q.x + q.w > 1601 || q.y + q.h > 901) inBounds = false;
    CHECK(inBounds, "settings with two key columns fits on screen");
    fe.frame(idle, DT, 1280, 720, 60, scene, hud);
    inBounds = true;
    for (const HudQuad& q : hud.quads)
        if (q.x < -1 || q.y < -1 || q.x + q.w > 1281 || q.y + q.h > 721) inBounds = false;
    CHECK(inBounds, "and at 1280x720");
}


// ------------------------------------------------------------------ POIs, maps, animations, graphics settings
static void testPoisMapsAnims() {
    section("POIs, mini map, full map, animations, graphics settings");
    Game g = quietGame();
    CHECK(g.world.pois.size() == 12, "12 named places (%zu)", g.world.pois.size());
    CHECK(g.world.houseCenters.size() >= 14, "houses clustered into places (%zu)", g.world.houseCenters.size());
    bool clustered = true;
    for (const Vec3& h : g.world.houseCenters) {
        bool near = false;
        for (const Poi& p : g.world.pois)
            if (lengthXZ(h - p.center) < p.radius) near = true;
        clustered = clustered && near;
    }
    CHECK(clustered, "every house belongs to a place");
    bool unique = true;
    for (size_t i = 0; i < g.world.pois.size(); ++i)
        for (size_t j = i + 1; j < g.world.pois.size(); ++j)
            if (lengthXZ(g.world.pois[i].center - g.world.pois[j].center) < 40.0f) unique = false;
    CHECK(unique, "places are spread out");
    CHECK(g.world.poiAt(g.player.body.pos.x, g.player.body.pos.z) == -1, "you spawn just outside a place");
    // Walk into the middle POI: banner shows DISCOVERED + name.
    const Poi& mid = g.world.pois.back();
    g.player.body.pos = mid.center;
    g.player.body.pos.y = g.world.terrainHeight(mid.center.x, mid.center.z) + 0.2f;
    g.update(InputState(), DT);
    CHECK(g.currentPoi == (int)g.world.pois.size() - 1 && g.poiBanner == mid.name && g.poiBannerNew && g.poiBannerTime > 3.0f,
          "entering a place shows its name (%s)", g.poiBanner.c_str());
    CHECK(g.world.pois.back().discovered, "marked discovered");
    g.player.body.pos = mid.center + Vec3(60, 0, 0);
    g.update(InputState(), DT);
    g.player.body.pos = mid.center;
    g.update(InputState(), DT);
    CHECK(!g.poiBannerNew, "second visit isn't 'discovered'");

    // Map image.
    std::vector<uint32_t> img;
    buildMapImage(g.world, 128, img);
    CHECK(img.size() == 128 * 128, "map image size");
    int reds = 0, greens = 0;
    for (uint32_t c : img) {
        int r = c & 0xFF, gg = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
        if (r > 120 && gg < 90 && b < 80) ++reds;
        if (gg > r && gg > b) ++greens;
    }
    CHECK(reds > 50 && greens > 128 * 128 / 2, "map shows green ground and red roofs (%d roof px)", reds);
    float u, v;
    worldToMapUV(-MAP_HALF, MAP_HALF, u, v);
    CHECK(u == 0 && v == 0, "north-west corner maps to the top-left");
    CHECK(mapGridName(-MAP_HALF + 10, MAP_HALF - 10) == "A1" && mapGridName(MAP_HALF - 10, -MAP_HALF + 10) == "F6" && mapGridName(0, 0) == "D4", "grid names");

    // Mini map quads: a map-texture quad in the top right, with the player arrow inside it.
    HudBuilder hud;
    buildHud(g, 1600, 900, 60, Bindings::defaults(), hud);
    float mx, my, msz;
    HudBuilder mm;
    mm.begin(1600, 900);
    buildMiniMap(g, 1600, 900, mm, mx, my, msz);
    int mapQuads = 0;
    bool arrowInside = false;
    for (const HudQuad& q : mm.quads) {
        if (q.tex == HUD_TEX_MAP) ++mapQuads;
        if (q.tex == HUD_TEX_ATLAS && q.color.x == 1 && q.color.y == 1 && q.color.z == 1 && q.w < 8 &&
            q.x > mx && q.x < mx + msz && q.y > my && q.y < my + msz)
            arrowInside = true;
    }
    CHECK(mapQuads == 1 && mx > 1600 * 0.7f && my < 60, "mini map drawn top right");
    CHECK(arrowInside, "player arrow inside the mini map");

    // Full map screen via M.
    FrontEnd fe;
    fe.init("");
    SceneList scene;
    fe.startMatch();
    RawInput m;
    m.down['M'] = m.pressed['M'] = true;
    fe.frame(m, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::Map && !fe.wantsMouseLocked(), "M opens the map");
    int big = 0, small = 0;
    for (const HudQuad& q : hud.quads) {
        if (q.tex == HUD_TEX_MAP && q.w > 500) ++big;
        if (q.tex == HUD_TEX_MAP && q.w < 300) ++small;
    }
    CHECK(big == 1 && small == 1 && !fe.game.world.pois.empty(), "full map drawn (big %d, mini %d)", big, small);
    fe.frame(m, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.screen == Screen::InGame, "M closes it");

    // Animations: the right state for each action.
    Game a = quietGame();
    standAtCell(a, 0, 0);
    clearArea(a, a.player.body.pos, 50);
    standAtCell(a, 0, 0);
    CHECK(playerAnim(a) == Anim::Pickaxe, "pickaxe out when idle with it");
    InputState one; one.slotPressed = 0; a.update(one, DT);
    run(a, 10, none);
    CHECK(playerAnim(a) == Anim::Idle, "idle with a gun");
    run(a, 20, [](int) { InputState in; in.forward = true; return in; });
    CHECK(playerAnim(a) == Anim::Walk, "walk");
    run(a, 20, [](int) { InputState in; in.forward = in.sprint = true; return in; });
    CHECK(playerAnim(a) == Anim::Sprint, "sprint");
    run(a, 10, none);
    InputState cr; cr.crouch = true; a.update(cr, DT);
    CHECK(playerAnim(a) == Anim::Crouch, "crouch");
    InputState jp; jp.jumpPressed = true; a.update(jp, DT); a.update(InputState(), DT);
    CHECK(playerAnim(a) == Anim::Jump, "jump");
    run(a, 80, none);
    InputState aim; aim.aim = true; a.update(aim, DT);
    CHECK(playerAnim(a) == Anim::Aim, "aim");
    InputState fire; fire.fire = fire.firePressed = true; a.update(fire, DT);
    CHECK(playerAnim(a) == Anim::Shoot && a.player.shotAnim > 0, "shoot (recoil)");
    run(a, 10, none);
    InputState rl; rl.reloadPressed = true; a.update(rl, DT);
    CHECK(playerAnim(a) == Anim::Reload, "reload");
    run(a, 120, none);
    InputState bq; bq.buildTogglePressed = true; a.update(bq, DT);
    CHECK(playerAnim(a) == Anim::Build, "build");
    a.update(bq, DT);
    a.player.slots[1] = Item::makeMedKit(1);
    a.player.health = 50;
    InputState two; two.slotPressed = 1; a.update(two, DT);
    a.update(fire, DT);
    CHECK(playerAnim(a) == Anim::Heal, "heal");
    // Poses differ: the same figure in different animations gives different instance lists.
    SceneList s1, s2;
    buildScene(a, 16.0f / 9.0f, s1);
    a.player.medkitTimer = -1;
    a.player.pickaxeOut = true;
    a.player.swingAnim = 0.2f;
    buildScene(a, 16.0f / 9.0f, s2);
    bool differs = s1.opaque[MESH_CYLINDER].size() != s2.opaque[MESH_CYLINDER].size();
    if (!differs)
        for (size_t i = 0; i < s1.opaque[MESH_CYLINDER].size() && !differs; ++i)
            if (std::fabs(s1.opaque[MESH_CYLINDER][i].world.m[3][1] - s2.opaque[MESH_CYLINDER][i].world.m[3][1]) > 1e-4f) differs = true;
    CHECK(differs, "different animations move the limbs");

    // Graphics settings: presets, levels, save/load, mesh LODs.
    GraphicsSettings perf = GraphicsSettings::preset(0), epic = GraphicsSettings::preset(2);
    CHECK(epic.get(GfxOption::Shadows) == 3 && perf.get(GfxOption::Shadows) == 0 && perf.get(GfxOption::SmoothShading) == 1, "presets");
    Settings st;
    st.gfx.set(GfxOption::AntiAliasing, 1);
    st.gfx.set(GfxOption::PostFx, 3);
    st.gfx.set(GfxOption::Lighting, 9);
    CHECK(st.gfx.get(GfxOption::Lighting) == 3, "levels clamp to 0..3");
    Settings ld;
    ld.parse(st.serialize());
    CHECK(ld.gfx == st.gfx, "graphics settings save and load");
    for (int o = 0; o < (int)GfxOption::Count; ++o)
        CHECK(std::string(gfxOptionName((GfxOption)o)).size() > 3 && std::string(gfxOptionHint((GfxOption)o)).size() > 10, "option %d has a name and a hint", o);
    std::vector<MeshVertex> v0, v1, v2;
    std::vector<uint32_t> i0, i1, i2;
    makeSphereMesh(v0, i0, 0); makeSphereMesh(v1, i1, 1); makeSphereMesh(v2, i2, 2);
    CHECK(i0.size() < i1.size() && i1.size() < i2.size() && i2.size() / 3 > 4000, "sphere LODs: %zu / %zu / %zu tris", i0.size() / 3, i1.size() / 3, i2.size() / 3);
    makeTerrainMesh(a.world, v0, i0, 4); makeTerrainMesh(a.world, v2, i2, 1);
    CHECK(v2.size() == 801 * 801 && v0.size() < v2.size() / 10, "terrain detail levels (%zu vs %zu verts)", v0.size(), v2.size());
    bool mats = true;
    buildScene(a, 16.0f / 9.0f, s1);
    int woods = 0, grass = 0;
    for (const Instance& in : s1.opaque[MESH_CUBE]) if ((int)in.params.y == MAT_WOOD) ++woods;
    for (const Instance& in : s1.opaque[MESH_TERRAIN]) if ((int)in.params.y == MAT_GRASS) ++grass;
    for (const Instance& in : s1.opaque[MESH_SPHERE]) if (in.params.y < 0 || in.params.y >= MAT_COUNT) mats = false;
    CHECK(mats && grass == 1 && woods > 0, "materials tagged on instances (wood %d)", woods);
}

// ------------------------------------------------------------------ keybinds round 3
static void testKeybindsShared() {
    section("Shared keys and Delete");
    Bindings b = Bindings::defaults();
    CHECK(b.get(Action::Map) == 'M', "map on M");
    // The scroll wheel on two actions at once (a wheel reset): both keep it.
    b.assign(Action::Wall, KEY_WHEEL_UP, 1);
    Action other = b.assign(Action::Edit, KEY_WHEEL_UP, 1);
    CHECK(b.get(Action::Wall, 1) == KEY_WHEEL_UP && b.get(Action::Edit, 1) == KEY_WHEEL_UP && other == Action::Wall,
          "same key on two actions is allowed");
    Action shared[4];
    CHECK(b.sharedWith(Action::Edit, KEY_WHEEL_UP, shared, 4) == 1 && shared[0] == Action::Wall, "sharedWith lists the other");
    b.assign(Action::Wall, 'Z', 1);
    CHECK(b.get(Action::Wall, 0) == KEY_NONE && b.get(Action::Wall, 1) == 'Z', "same key on both slots of one action collapses");
    CHECK(!keyBindable(KEY_DELETE), "Delete can't be bound");

    FrontEnd fe;
    fe.init("");
    SceneList scene;
    HudBuilder hud;
    fe.screen = Screen::Settings;
    fe.rebinding = (int)Action::Jump;
    fe.rebindSlot = 0;
    RawInput del;
    del.down[KEY_DELETE] = del.pressed[KEY_DELETE] = true;
    fe.frame(del, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.rebinding == -1 && fe.settings.binds.get(Action::Jump, 0) == KEY_NONE, "Delete while waiting clears the key");
    RawInput sp;
    sp.down[KEY_SPACE] = sp.pressed[KEY_SPACE] = true;
    CHECK(!fe.gameInput(sp).jumpPressed, "cleared key does nothing");
    // Wheel up on Fire and on Wall at the same time, both work.
    fe.settings.binds.assign(Action::Fire, KEY_WHEEL_UP, 1);
    fe.settings.binds.assign(Action::Wall, KEY_WHEEL_UP, 1);
    RawInput wh;
    wh.pressed[KEY_WHEEL_UP] = true;
    InputState gi = fe.gameInput(wh);
    CHECK(gi.firePressed && gi.buildPiecePressed == 0, "wheel up fires AND picks wall");
    // Rebinding a key that's already in use just reports it (no swap).
    fe.rebinding = (int)Action::Crouch;
    fe.rebindSlot = 1;
    RawInput f;
    f.down['F'] = f.pressed['F'] = true;
    fe.frame(f, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.settings.binds.get(Action::Crouch, 1) == 'F' && fe.settings.binds.get(Action::Pickaxe, 0) == 'F' &&
              fe.message.find("ALSO USED BY") != std::string::npos,
          "shared key kept on both, with a notice");
}


// ------------------------------------------------------------------ cone peaks, support, health bars, potions, storm
static void testRound4() {
    section("Cone edits move the peak");
    Piece c;
    c.alive = true;
    c.slot.type = PieceType::Cone;
    float H = CONE_HEIGHT, HE = CONE_EDIT_HEIGHT, T = TILE;
    c.removed = 0;
    CHECK(std::fabs(coneHeightAt(c, T * 0.5f, T * 0.5f) - H) < 1e-4f && coneHeightAt(c, 0, 0) < 1e-4f, "plain cone: peak in the middle");
    c.removed = 1 << 3;  // the (+x,+z) tile
    CHECK(std::fabs(coneHeightAt(c, T, T) - HE) < 1e-4f && coneHeightAt(c, 0, 0) < 1e-4f &&
              coneHeightAt(c, T, 0) < 1e-4f && coneHeightAt(c, 0, T) < 1e-4f,
          "one tile: that corner rises to %.1f m, the rest slopes to the floor", HE);
    CHECK(std::fabs(coneHeightAt(c, T * 0.5f, T * 0.5f) - HE * 0.5f) < 1e-4f, "...halfway up in the middle");
    c.removed = (1 << 2) | (1 << 3);  // the +z side
    bool ramp = true;
    for (int i = 0; i <= 4; ++i) ramp = ramp && std::fabs(coneHeightAt(c, i * T / 4, T) - HE) < 1e-4f && coneHeightAt(c, i * T / 4, 0) < 1e-4f;
    CHECK(ramp && std::fabs(coneHeightAt(c, T * 0.3f, T * 0.5f) - HE * 0.5f) < 1e-4f, "two tiles on a side: a ramp up to that side");
    c.removed = 0x7;  // only (+x,+z) left out
    CHECK(coneHeightAt(c, T, T) < 1e-4f && std::fabs(coneHeightAt(c, 0, 0) - HE) < 1e-4f && coneHeightAt(c, T, 0) < 1e-4f,
          "three tiles: the one left out is the low corner");
    c.removed = 0xF;
    CHECK(std::fabs(coneHeightAt(c, T * 0.5f, T * 0.5f) - H) < 1e-4f, "all four: still a plain cone (never disappears)");
    std::vector<Tri> tris;
    c.removed = 1 << 3;
    pieceTris(c, tris);
    CHECK(tris.size() == 128, "edited cone is tessellated (%zu tris)", tris.size());
    float y;
    CHECK(pieceSurfaceHeight(c, T * 0.1f, T * 0.1f, y) && y < 0.4f, "can stand on the lowered corner (y=%.2f)", y);
    // Walk down an edited cone's ramp side.
    Game g = quietGame();
    standAtCell(g, 4, 4);
    clearArea(g, g.player.body.pos, 30);
    standAtCell(g, 4, 4);
    Slot cs;
    cs.type = PieceType::Cone; cs.i = 4; cs.j = 5; cs.k = (int)std::floor((g.player.body.pos.y + 0.8f) / STORY);
    int cid = g.world.build.place(cs, true);
    g.world.build.get(cid)->removed = 0xC;  // +z side raised: ramp rising toward +z
    float peak = -99;
    run(g, 120, [&](int) { peak = std::max(peak, g.player.body.pos.y); InputState in; in.forward = true; return in; });
    CHECK(peak > cs.k * STORY + 1.5f, "walked up the edited cone's ramp (peak y=%.2f)", peak);

    // Looking at the near face of a cone picks the near tile (not where the aim line crosses the far side).
    {
        Game cg = quietGame();
        standAtCell(cg, 6, 6);
        clearArea(cg, cg.player.body.pos, 30);
        standAtCell(cg, 6, 6);
        Slot c2; c2.type = PieceType::Cone; c2.i = 6; c2.j = 7; c2.k = (int)std::floor((cg.player.body.pos.y + 0.8f) / STORY);
        int id2 = cg.world.build.place(c2, true);
        const Piece* cp = cg.world.build.get(id2);
        // Aim at the middle of the near (-z) face, low down, from the player's eye.
        Vec3 target((c2.i + 0.5f) * TILE, c2.k * STORY + 0.5f, c2.j * TILE + 0.9f);
        aimAt(cg, target);
        int tile = pieceTileUnderRay(*cp, cg.camera.aimRay, 60.0f);
        CHECK(tile == 0 || tile == 1, "aiming at the near face of a cone picks a near tile (%d)", tile);
    }
    // Storm ticks don't cancel a heal.
    {
        Game hg = quietGame();
        standAtCell(hg, 0, 0);
        hg.player.health = 40;
        hg.player.slots[1] = Item::makeMedKit(1);
        InputState two; two.slotPressed = 1; hg.update(two, DT);
        InputState use; use.fire = use.firePressed = true; hg.update(use, DT);
        hg.damageActor(PLAYER_ACTOR_ID, 1, false, Vec3(), NO_TARGET, "STORM");
        CHECK(hg.player.medkitTimer >= 0, "storm damage keeps the heal going");
        hg.damageActor(PLAYER_ACTOR_ID, 1, false, Vec3(), 0, "AR");
        CHECK(hg.player.medkitTimer < 0, "getting shot still cancels it");
    }

    section("No building in the air, floating builds break");
    Game b = quietGame();
    standAtCell(b, 2, 2);
    clearArea(b, b.player.body.pos, 40);
    standAtCell(b, 2, 2);
    int k0 = (int)std::floor((b.player.body.pos.y + 0.5f) / STORY);
    Slot air;
    air.type = PieceType::Floor; air.i = 2; air.j = 3; air.k = k0 + 2;
    CHECK(!b.world.wouldBeSupported(air), "a floor two stories up with nothing under it isn't supported");
    Slot ground;
    ground.type = PieceType::Wall; ground.side = 1; ground.i = 2; ground.j = 3; ground.k = k0;
    CHECK(b.world.wouldBeSupported(ground), "a wall on the ground is");
    int wid = b.world.build.place(ground, true);
    Slot above = ground; above.k = k0 + 1;
    CHECK(b.world.wouldBeSupported(above), "a wall on top of a wall is");
    int w2 = b.world.build.place(above, true);
    Slot floorTop; floorTop.type = PieceType::Floor; floorTop.i = 2; floorTop.j = 3; floorTop.k = k0 + 2;
    CHECK(b.world.wouldBeSupported(floorTop), "a floor resting on that stack is");
    int fid = b.world.build.place(floorTop, true);
    run(b, 2, none);
    CHECK(b.world.build.aliveCount() == 3, "all three stand");
    // Build mode refuses an air placement.
    b.player.pitch = deg2rad(60);
    InputState fk; fk.buildPiecePressed = 1; b.update(fk, DT);
    b.buildTarget.slot = air;  // force the aimed slot
    b.buildTarget.unsupported = true; b.buildTarget.valid = false;
    int before = b.world.build.aliveCount();
    CHECK(!b.tryPlaceBuild() && b.world.build.aliveCount() == before, "can't place in the air");
    // Shoot out the bottom wall: everything above breaks.
    b.world.build.destroy(wid);
    run(b, 2, none);
    CHECK(!b.world.build.get(w2)->alive && !b.world.build.get(fid)->alive, "pieces above a destroyed wall fall apart");
    CHECK(b.toast.text.find("FLOATING") != std::string::npos, "toast says so");

    section("Build health bars");
    Game h = quietGame();
    standAtCell(h, 2, 2);
    clearArea(h, h.player.body.pos, 30);
    standAtCell(h, 2, 2);
    Slot ws; ws.type = PieceType::Wall; ws.side = 1; ws.i = 2; ws.j = 3; ws.k = (int)std::floor((h.player.body.pos.y + 0.5f) / STORY);
    int hw = h.world.build.place(ws, true);
    h.world.build.update(BUILD_UP_TIME + 0.1f);
    h.world.build.damage(hw, 60);
    aimAt(h, pieceBounds(ws).center());
    h.update(InputState(), DT);
    CHECK(h.aimedPiece == hw, "the wall under the crosshair is tracked");
    HudBuilder hud;
    buildHud(h, 1600, 900, 60, Bindings::defaults(), hud);
    bool bar = false, text = false;
    for (const HudQuad& q : hud.quads) {
        if (std::fabs(q.color.x - 0.55f) < 1e-3f && std::fabs(q.color.y - 0.85f) < 1e-3f && q.w > 50 && q.w < 180) bar = true;
    }
    for (size_t i = 0; i + 2 < hud.quads.size(); ++i) text = text || hud.quads[i].h < 20;  // some text drawn
    CHECK(bar && text, "health bar for the aimed wall (90/150)");

    section("Shield potions");
    Game pz = quietGame();
    standAtCell(pz, 0, 0);
    pz.player.shield = 0;
    pz.player.slots[1] = Item::makeMini(3);
    pz.player.slots[2] = Item::makeBigPot(1);
    InputState two; two.slotPressed = 1; pz.update(two, DT);
    InputState use; use.fire = use.firePressed = true;
    pz.update(use, DT);
    run(pz, (int)(MINI_USE_TIME * 60) + 10, none);
    CHECK(pz.player.shield == 25 && pz.player.slots[1].count == 2, "mini: +25 shield in 2 s (%.0f, %d left)", pz.player.shield, pz.player.slots[1].count);
    pz.update(use, DT); run(pz, 130, none);
    pz.update(use, DT); run(pz, 130, none);
    CHECK(pz.player.shield == 50 && pz.player.slots[1].count == 1, "minis stop at 50 (%.0f)", pz.player.shield);
    pz.update(use, DT);
    CHECK(pz.player.medkitTimer < 0 && pz.toast.text.find("50") != std::string::npos, "can't drink a mini at 50");
    InputState three; three.slotPressed = 2; pz.update(three, DT);
    pz.update(use, DT);
    run(pz, (int)(BIGPOT_USE_TIME * 60) - 10, none);
    CHECK(pz.player.shield == 50, "big pot takes 5 s");
    run(pz, 20, none);
    CHECK(pz.player.shield == 100 && pz.player.slots[2].empty(), "big pot: +50 shield");
    CHECK(pz.giveItem(Item::makeMini(6), false) && pz.giveItem(Item::makeMini(1), false), "minis stack to 6 then a new slot");
    Rng rng(7);
    int pots = 0, minis = 0;
    for (int i = 0; i < 2000; ++i)
        for (const Item& it : rollChest(rng)) { pots += it.kind == ItemKind::BigPot; minis += it.kind == ItemKind::Mini; }
    CHECK(pots > 300 && pots < 500 && minis > 700 && minis < 900, "chests drop shields (%d pots, %d minis)", pots, minis);

    section("Storm");
    Game st(1337);
    CHECK(st.storm.radius >= 300 && !st.storm.shrinking && st.storm.timer > 40, "starts open with a wait");
    st.player.body.pos = Vec3(0, 0, 0);
    st.storm.timer = 0.5f;
    for (Bot& bb : st.bots) bb.alive = false;
    run(st, 60, none);
    CHECK(st.storm.shrinking, "then shrinks");
    run(st, 90 * 60, none);  // let it finish stage 1
    CHECK(st.storm.radius < 310 && st.storm.stage >= 1, "first circle ~300 m (r=%.0f, stage %d)", st.storm.radius, st.storm.stage);
    // Stand outside: 1 damage per second.
    Vec3 out = st.storm.center + Vec3(st.storm.radius + 20, 0, 0);
    st.player.body.pos = Vec3(clampf(out.x, -140, 140), 0, clampf(out.z, -140, 140));
    if (st.storm.inside(st.player.body.pos.x, st.player.body.pos.z)) st.player.body.pos = st.storm.center - Vec3(st.storm.radius + 20, 0, 0);
    st.player.shield = 0; st.player.health = 100;
    run(st, 60 * 5 + 5, none);
    CHECK(st.playerInStorm && st.player.health <= 95 && st.player.health >= 94, "5 s in the storm = 5 damage (%.0f)", st.player.health);
    // A bot outside heads back in.
    Bot& bot = st.bots[0];
    bot.alive = true;
    bot.body.pos = st.storm.center - Vec3(st.storm.radius + 25, 0, 0);
    bot.body.pos.y = st.world.terrainHeight(bot.body.pos.x, bot.body.pos.z);
    float d0 = lengthXZ(bot.body.pos - st.storm.center);
    run(st, 60 * 6, none);
    CHECK(lengthXZ(bot.body.pos - st.storm.center) < d0 - 10, "bot runs toward the safe zone");
    HudBuilder sh;
    buildHud(st, 1600, 900, 60, Bindings::defaults(), sh);
    bool purple = false;
    for (const HudQuad& q : sh.quads) if (std::fabs(q.color.x - 0.55f) < 1e-3f && std::fabs(q.color.z - 0.8f) < 1e-3f && q.w > 1000) purple = true;
    CHECK(purple, "purple edges while in the storm");

    section("Solid ramps + scope + no help box");
    Game rr = quietGame();
    standAtCell(rr, 4, 4);
    clearArea(rr, rr.player.body.pos, 30);
    standAtCell(rr, 4, 4);
    Slot rs; rs.type = PieceType::Ramp; rs.i = 4; rs.j = 5; rs.k = (int)std::floor((rr.player.body.pos.y + 0.5f) / STORY);
    rs.dir = 1;  // rises toward +x, so walking in from -z hits its side
    rr.world.build.place(rs, true);
    rr.player.body.pos.x = 4 * TILE + 1.7f;  // where the ramp's side is about 1.4 m high
    run(rr, 120, [](int) { InputState in; in.forward = true; return in; });
    CHECK(rr.player.body.pos.z < 5 * TILE + 0.2f && rr.player.body.pos.y < rs.k * STORY + 0.9f,
          "the side of a ramp blocks you (z=%.2f y=%.2f)", rr.player.body.pos.z, rr.player.body.pos.y);
    rr.player.body.pos.x = 4 * TILE + 4.5f;  // under the tall end: you can walk beneath it
    rr.player.body.pos.z = 4 * TILE + 2.5f;
    run(rr, 100, [](int) { InputState in; in.forward = true; return in; });
    CHECK(rr.player.body.pos.z > 5 * TILE + 1.0f, "...but you can walk under its tall end (z=%.2f)", rr.player.body.pos.z);
    Game sc = quietGame();
    sc.player.slots[1] = Item::makeGun(GunType::Sniper, Rarity::Rare);
    InputState s2; s2.slotPressed = 1; sc.update(s2, DT);
    InputState ads; ads.aim = true; sc.update(ads, DT);
    HudBuilder scope;
    buildHud(sc, 1600, 900, 60, Bindings::defaults(), scope);
    int dark = 0;
    for (const HudQuad& q : scope.quads) if (q.color.x < 0.05f && q.color.w > 0.9f) ++dark;
    CHECK(dark > 40 && sc.camera.fovY < deg2rad(35), "scope overlay + zoom when aiming a sniper (%d dark quads)", dark);
    CHECK(!Game(1337).showHelp, "controls box is off by default");
}


// ------------------------------------------------------------------ locker + K-Bucks
static void testLocker() {
    section("Locker, skins, K-Bucks");
    CHECK(skinCount() >= 8, "%d skins", skinCount());
    FrontEnd fe;
    fe.init("");
    SceneList scene;
    HudBuilder hud;
    CHECK(fe.settings.kbucks == 0 && fe.settings.ownsSkin(0) && !fe.settings.ownsSkin(1), "start with 0 K-Bucks and the free skin");
    fe.startMatch();
    fe.game.landEveryone();
    int enemy = -1;
    for (const Bot& b : fe.game.bots) if (b.team != 0) { enemy = b.index; break; }
    fe.game.damageActor(enemy, 999, false, Vec3(), PLAYER_ACTOR_ID, "AR");
    RawInput idle;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    CHECK(fe.settings.kbucks == KBUCKS_PER_KILL, "25 K-Bucks per kill (%d)", fe.settings.kbucks);
    fe.screen = Screen::Lobby;
    fe.screen = Screen::Shop;
    fe.shopDay = 1;
    std::vector<int> lineup = fe.shopLineup(1);
    CHECK(lineup.size() == 8 && fe.shopFeatured(lineup[0], 1) && fe.shopDaily(lineup[2], 1) && !fe.shopFeatured(lineup[2], 1), "2 featured + 6 daily skins rotate by day");
    CHECK(fe.shopLineup(2) != lineup, "a different lineup tomorrow");
    int pick = lineup[2];
    int price = skinPrice(pick);
    CHECK(fe.shopPrice(pick, 1) == price && fe.shopPrice(lineup[0], 1) == skinPrice(lineup[0]) * 3 / 4, "featured skins are 25%% off");
    CHECK(skinPrice(0) == 800 && skinPrice(7) == 2000 && skinCount() >= 40, "%d skins, priced by rarity (800..2000)", skinCount());
    fe.lockerSel = pick;
    fe.settings.kbucks = price - 1;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    // Click BUY on the first DAILY row (it's disabled, one K-Buck short).
    float u = 1.0f, px = 40, py = 50, pw = 520, rowH = 56, ry = py + 92 + 26 + 2 * rowH + 8 + 26;
    fe.frame(click(px + pw - 170 * u + 60, ry + 20), DT, 1600, 900, 60, scene, hud);
    CHECK(!fe.settings.ownsSkin(pick) && fe.settings.kbucks == price - 1, "can't buy one K-Buck short");
    fe.settings.kbucks = price;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    fe.frame(click(px + pw - 170 * u + 60, ry + 20), DT, 1600, 900, 60, scene, hud);
    CHECK(fe.settings.ownsSkin(pick) && fe.settings.kbucks == 0 && fe.settings.equippedSkin == pick, "bought and equipped for %d", price);
    // The locker lists only owned skins (2 now) and can equip the free one again.
    fe.screen = Screen::Locker;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    fe.frame(click(px + pw - 170 * u + 60, py + 92 + 20), DT, 1600, 900, 60, scene, hud);  // EQUIP on row 0 (RECRUIT)
    CHECK(fe.settings.equippedSkin == 0, "locker equips an owned skin");
    fe.settings.equippedSkin = pick;
    fe.settings.ownedSkins |= 1ull << 1;  // for the checks below
    fe.settings.equippedSkin = 1;
    Settings ld;
    ld.parse(fe.settings.serialize());
    CHECK(ld.ownsSkin(1) && ld.equippedSkin == 1 && ld.kbucks == 0, "locker saves and loads");
    fe.screen = Screen::Lobby;
    fe.startMatch();
    CHECK(fe.game.player.skin == 1, "you wear the equipped skin in a match");
    SceneList s1, s2;
    fe.game.landEveryone();
    buildScene(fe.game, 16.0f / 9.0f, s1);
    fe.game.player.skin = 0;
    buildScene(fe.game, 16.0f / 9.0f, s2);
    CHECK(s1.opaque[MESH_SPHERE].size() != s2.opaque[MESH_SPHERE].size() || s1.opaque[MESH_CYLINDER].size() != s2.opaque[MESH_CYLINDER].size(),
          "different skins draw differently (hair styles)");
}



// ------------------------------------------------------------------ builds build up
static void testBuildUp() {
    section("Builds build up their health");
    BuildSystem b;
    Slot s;
    s.type = PieceType::Wall;
    int id = b.place(s, true, BuildMat::Wood);
    Piece* p = b.get(id);
    float maxHp = p->maxHp;
    CHECK(std::fabs(p->hp - maxHp * BUILD_UP_START) < 1e-3f, "a fresh wall starts at %.0f%% health", BUILD_UP_START * 100);
    for (int i = 0; i < 60; ++i) b.update(1.0f / 60.0f);
    CHECK(p->hp > maxHp * 0.5f && p->hp < maxHp, "after 1 s it's part way (%.0f/%.0f)", p->hp, maxHp);
    for (int i = 0; i < 120; ++i) b.update(1.0f / 60.0f);
    CHECK(std::fabs(p->hp - maxHp) < 1e-3f, "full health after %.1f s", BUILD_UP_TIME);
    b.damage(id, 30);
    CHECK(std::fabs(p->hp - (maxHp - 30)) < 1e-3f && std::fabs(p->targetHp - (maxHp - 30)) < 1e-3f, "damage lowers the target too");
    // Editing restarts the build-up, toward the damaged health (not full).
    Game g = quietGame();
    standAtCell(g, 4, 4);
    clearArea(g, g.player.body.pos, 30);
    standAtCell(g, 4, 4);
    Slot ws;
    ws.type = PieceType::Wall; ws.i = 5; ws.j = 4; ws.k = (int)std::floor((g.player.body.pos.y + 0.8f) / STORY); ws.side = 0;
    int wid = g.world.build.place(ws, true);
    Piece* w = g.world.build.get(wid);
    for (int i = 0; i < 200; ++i) g.world.build.update(1.0f / 60.0f);
    g.world.build.damage(wid, 30);
    float before = w->hp;
    g.player.edit.pieceId = wid;
    g.player.edit.returnMode = Mode::Build;
    g.player.mode = Mode::Edit;
    g.player.edit.selected = 1u << 4;
    g.confirmEdit();
    CHECK(w->hp < before * 0.5f, "an edit drops the health to build back up (%.0f -> %.0f)", before, w->hp);
    for (int i = 0; i < 240; ++i) g.world.build.update(1.0f / 60.0f);
    CHECK(std::fabs(w->hp - (w->maxHp - 30)) < 1e-3f, "...and it comes back to the damaged health, %.0f, not full", w->hp);
}


// ------------------------------------------------------------------ accounts
static void testAccounts() {
    section("Accounts (sign in saves what you own)");
    AccountStore store;  // in memory
    Account a;
    std::string err;
    CHECK(!store.create("", "pass1234", a, err), "needs a name: %s", err.c_str());
    CHECK(!store.create("KINGSTON", "ab", a, err), "needs a password: %s", err.c_str());
    CHECK(store.create("kingston", "secret1", a, err) && a.name == "KINGSTON", "account created (name cleaned to %s)", a.name.c_str());
    CHECK(!store.create("KINGSTON", "other12", a, err), "name taken: %s", err.c_str());
    Account b;
    CHECK(!store.signIn("KINGSTON", "wrong", b, err), "wrong password refused: %s", err.c_str());
    CHECK(!store.signIn("NOBODY", "secret1", b, err), "unknown name refused");
    CHECK(store.signIn("KINGSTON", "secret1", b, err) && b.name == "KINGSTON", "sign in works");
    CHECK(hashPassword("KINGSTON", "secret1") != hashPassword("KINGSTON", "secret2") && a.serialize().find("secret1") == std::string::npos,
          "the password itself is never stored");
    a.kbucks = 2500;
    a.ownedSkins = 1ull | (1ull << 4);
    a.equippedSkin = 4;
    a.kills = 7;
    store.save(a);
    Account c;
    c.parse(a.serialize());
    CHECK(c.kbucks == 2500 && c.ownedSkins == a.ownedSkins && c.equippedSkin == 4 && c.kills == 7 && c.passHash == a.passHash, "account file round-trips");
    CHECK(store.signIn("KINGSTON", "secret1", b, err) && b.kbucks == 2500 && ((b.ownedSkins >> 4) & 1u), "signing in brings the K-Bucks and skins back");

    // Through the front end: a fresh install asks you to sign in; buying saves to the account;
    // signing out and back in keeps everything.
    FrontEnd fe;
    fe.init("");
    SceneList scene;
    HudBuilder hud;
    RawInput idle;
    fe.screen = Screen::SignIn;
    CHECK(fe.createAccount("PLAYER ONE", "hunter2", err) && fe.signedIn && fe.settings.playerName == "PLAYER ONE", "create account from the sign-in screen");
    fe.screen = Screen::Shop;
    fe.shopDay = 1;
    int pick = fe.shopLineup(1)[2];
    fe.settings.kbucks = skinPrice(pick);
    float px = 40, py = 50, pw = 520, rowH = 56, ry = py + 92 + 26 + 2 * rowH + 8 + 26;
    fe.frame(idle, DT, 1600, 900, 60, scene, hud);
    fe.frame(click(px + pw - 170 + 60, ry + 20), DT, 1600, 900, 60, scene, hud);
    CHECK(fe.settings.ownsSkin(pick) && fe.account.ownedSkins == fe.settings.ownedSkins && fe.account.kbucks == 0, "a purchase is saved to the account");
    fe.signOut();
    CHECK(fe.screen == Screen::SignIn && !fe.signedIn && fe.settings.kbucks == 0 && !fe.settings.ownsSkin(pick), "signing out clears the local copy");
    CHECK(!fe.signIn("PLAYER ONE", "nope", err), "wrong password");
    CHECK(fe.signIn("player one", "hunter2", err) && fe.settings.ownsSkin(pick) && fe.settings.equippedSkin == pick && fe.settings.kbucks == 0,
          "signing back in restores the skins and K-Bucks");
}


// ------------------------------------------------------------------ big map, distinct places
static void testBigMap() {
    section("Big map with 12 different places and 100 players");
    Game g(2024);
    const World& w = g.world;
    CHECK(w.pois.size() == 12, "12 places");
    bool kinds[(int)PoiKind::Count] = {};
    for (const Poi& p : w.pois) kinds[(int)p.kind] = true;
    bool all = true;
    for (bool k : kinds) all = all && k;
    CHECK(all, "every kind of place appears once (town, farm, tower, lumber yard, quarry, gas station, camp, ruins, market, depot, village, lookout)");
    auto countKind = [&](PropKind k, const Poi& p) {
        int n = 0;
        for (const StaticBox& b : w.statics)
            if (b.kind == k && lengthXZ(b.box.center() - p.center) < p.radius + 10) ++n;
        return n;
    };
    for (const Poi& p : w.pois) {
        switch (p.kind) {
            case PoiKind::Farm: CHECK(countKind(PropKind::Silo, p) == 2 && countKind(PropKind::Fence, p) > 20, "%s: silos and fences", p.name.c_str()); break;
            case PoiKind::Tower: CHECK(countKind(PropKind::Step, p) >= 24, "%s: stairs inside the tower", p.name.c_str()); break;
            case PoiKind::Lumber: CHECK(countKind(PropKind::Log, p) >= 12, "%s: log piles", p.name.c_str()); break;
            case PoiKind::Quarry: CHECK(countKind(PropKind::Rock, p) >= 15, "%s: lots of rocks", p.name.c_str()); break;
            case PoiKind::GasStation: CHECK(countKind(PropKind::Canopy, p) == 1 && countKind(PropKind::Post, p) == 4, "%s: canopy on posts", p.name.c_str()); break;
            case PoiKind::Ruins: CHECK(countKind(PropKind::Stone, p) >= 8, "%s: broken stone walls", p.name.c_str()); break;
            case PoiKind::Market: CHECK(countKind(PropKind::Canopy, p) == 8, "%s: eight stalls", p.name.c_str()); break;
            case PoiKind::Depot: CHECK(countKind(PropKind::Crate, p) >= 10, "%s: crate stacks", p.name.c_str()); break;
            case PoiKind::Village: CHECK(countKind(PropKind::Well, p) == 1, "%s: a well", p.name.c_str()); break;
            case PoiKind::Town: CHECK(countKind(PropKind::Road, p) == 2 && countKind(PropKind::HouseRoof, p) >= 8, "%s: roads and many houses", p.name.c_str()); break;
            default: break;
        }
    }
    CHECK(g.bots.size() == 99 && w.enemySpawns.size() >= 99, "100 players with their own spawn spots (%zu spawns)", w.enemySpawns.size());
    CHECK(w.statics.size() > 3000 && w.chests.size() > 100, "%zu props, %zu chests", w.statics.size(), w.chests.size());
    // The tower can be climbed: the roof chest is reachable by walking up the stairs.
    const Poi* tower = nullptr;
    for (const Poi& p : w.pois) if (p.kind == PoiKind::Tower) tower = &p;
    float top = -1;
    for (const Chest& c : w.chests)
        if (tower && lengthXZ(c.pos - tower->center) < 9) top = std::max(top, c.pos.y);
    CHECK(tower && top > tower->center.y + 10, "tower roof chest is %.1f m up", top - (tower ? tower->center.y : 0));
}


// ------------------------------------------------------------------ fall damage + shockwaves + bot styles
static void testFallShockBots() {
    section("Fall damage, shockwaves, bot play styles");
    Game g = quietGame();
    standAtCell(g, 6, 6);
    clearArea(g, g.player.body.pos, 40);
    standAtCell(g, 6, 6);
    float ground = g.player.body.pos.y;
    g.player.shield = 0;
    g.player.health = 100;
    g.player.body.pos.y = ground + 4.0f;  // a short drop
    g.player.body.grounded = false;
    run(g, 120, none);
    CHECK(std::fabs(g.player.health - 100) < 1e-3f, "a 4 m drop is free (hp %.0f)", g.player.health);
    g.player.body.pos.y = ground + 14.0f;  // a long one
    g.player.body.grounded = false;
    run(g, 180, none);
    float expect = std::min(FALL_MAX_DAMAGE, (14.0f - FALL_SAFE_DROP) * FALL_DAMAGE_PER_M);
    CHECK(g.player.health < 100 - expect * 0.6f && g.player.health > 100 - expect - 5, "a 14 m drop hurts (hp %.0f, expected about %.0f off)", g.player.health, expect);
    // Shockwave: launches you, and the landing is free.
    g.player.health = 100;
    g.player.slots[1] = Item::makeShockwave(2);
    g.player.selected = 1;
    g.player.pickaxeOut = false;
    g.player.pitch = deg2rad(-80);  // throw at your feet
    g.player.yaw = 0;
    run(g, 5, none);
    InputState fire;
    fire.firePressed = fire.fire = true;
    g.update(fire, DT);
    CHECK(g.grenades.size() == 1 && g.player.slots[1].count == 1, "throwing a shockwave uses one");
    // It lands, then sits there for a second before it goes off.
    run(g, 20, none);
    CHECK(g.grenades.size() == 1 && g.grenades[0].fuse > 0 && g.grenades[0].fuse <= SHOCKWAVE_FUSE && length(g.grenades[0].vel) < 1e-3f,
          "landed and armed (fuse %.2f s)", g.grenades.empty() ? 0.0f : g.grenades[0].fuse);
    run(g, 30, none);
    CHECK(g.grenades.size() == 1 && g.player.body.pos.y < ground + 0.5f, "still on the ground half a second later");
    float peak = g.player.body.pos.y;
    run(g, 120, [&](int) { peak = std::max(peak, g.player.body.pos.y); return InputState(); });
    CHECK(g.grenades.empty() && peak > ground + 3.0f, "it went off and launched you %.1f m up", peak - ground);
    run(g, 240, none);
    CHECK(g.player.body.grounded && std::fabs(g.player.health - 100) < 1e-3f, "no fall damage after a shockwave (hp %.0f)", g.player.health);
    bool shockInChests = false;
    Rng rng(9);
    for (int i = 0; i < 200 && !shockInChests; ++i)
        for (const Item& it : rollChest(rng)) shockInChests = shockInChests || it.kind == ItemKind::Shockwave;
    CHECK(shockInChests, "shockwaves show up in chests");

    // Bot styles are all present and bots build.
    Game m(77);
    int styles[3] = {0, 0, 0};
    for (const Bot& b : m.bots) styles[(int)b.style]++;
    CHECK(styles[0] > 10 && styles[1] > 10 && styles[2] > 10, "fighters %d, hiders %d, mixed %d", styles[0], styles[1], styles[2]);
    m.landEveryone();
    Bot* hider = nullptr;
    for (Bot& b : m.bots) if (b.style == BotStyle::Hider && b.team != 0) { hider = &b; break; }
    CHECK(hider != nullptr, "found a hider");
    if (hider) {
        int before = m.world.build.aliveCount();
        m.damageActor(hider->index, 30, false, hider->body.pos, PLAYER_ACTOR_ID, "AR");
        for (int i = 0; i < 30; ++i) m.updateBot(*hider, DT);
        CHECK(m.world.build.aliveCount() > before && hider->builds > 0, "a shot hider throws up a wall (%d builds)", hider->builds);
    }
    // Bots loot: a med kit on the ground gets picked up and used when hurt; a better gun is taken.
    {
        Bot& lb = m.bots[5];
        lb.state = AIState::Wander;
        lb.targetId = NO_TARGET;
        lb.canSee = false;
        lb.hurtTimer = 0;
        lb.health = 40;
        lb.gun = Item::makeGun(GunType::Pistol, Rarity::Common);
        m.world.spawnPickup(Item::makeMedKit(1), lb.body.pos + Vec3(0.5f, 0.2f, 0), Vec3());
        m.world.spawnPickup(Item::makeGun(GunType::AssaultRifle, Rarity::Epic), lb.body.pos + Vec3(-0.5f, 0.2f, 0), Vec3());
        for (int i = 0; i < 30; ++i) m.updateBot(lb, DT);
        CHECK(lb.gun.gun == GunType::AssaultRifle && lb.gun.rarity == Rarity::Epic, "bot swapped its pistol for an epic AR");
        CHECK(lb.medkits == 1 || lb.healTimer >= 0 || lb.health > 40, "bot picked up the med kit (kits %d, healing %d)", lb.medkits, lb.healTimer >= 0);
        for (int i = 0; i < 60 * 7; ++i) m.updateBot(lb, DT);
        CHECK(lb.health > 85 && lb.medkits == 0, "...and used it when safe (hp %.0f)", lb.health);
        lb.shield = 0;
        m.world.spawnPickup(Item::makeMini(3), lb.body.pos + Vec3(0.3f, 0.2f, 0.3f), Vec3());
        for (int i = 0; i < 60 * 6; ++i) m.updateBot(lb, DT);
        CHECK(lb.shield > 20, "minis give bots shield (%.0f)", lb.shield);
        // Shield takes the hit first.
        float hp = lb.health, sh = lb.shield;
        m.damageActor(lb.index, 10, false, lb.body.pos, PLAYER_ACTOR_ID, "AR");
        CHECK(std::fabs(lb.health - hp) < 1e-3f && std::fabs(lb.shield - (sh - 10)) < 1e-3f, "bot shield absorbs damage (hp %.0f->%.0f, shield %.0f->%.0f)", hp, lb.health, sh, lb.shield);
    }
    Bot* fighter = nullptr;
    for (Bot& b : m.bots) if (b.style == BotStyle::Fighter && b.team != 0) { fighter = &b; break; }
    if (fighter) {
        // Target high above: the fighter ramps up toward it.
        fighter->targetId = PLAYER_ACTOR_ID;
        fighter->canSee = true;
        fighter->state = AIState::Chase;
        m.player.body.pos = fighter->body.pos + Vec3(6, 6, 0);
        fighter->buildCooldown = 0;
        int before = m.world.build.aliveCount();
        for (int i = 0; i < 5; ++i) m.updateBot(*fighter, DT);
        CHECK(m.world.build.aliveCount() > before, "a fighter builds a ramp toward a high target");
    }
}

// ------------------------------------------------------------------ rigged character
static void testCharacterModel() {
    section("Rigged character (assets/character.glb)");
    static CharacterModel model;
    std::string err;
    bool ok = model.load("assets/character.glb", err);
    CHECK(ok, "loads: %s", err.c_str());
    if (!ok) return;
    CHECK(model.bones.size() == 40 && (int)model.bones.size() <= MAX_BONES, "40 bones: body 22, face 7, swing chains 11 (%d)", (int)model.bones.size());
    CHECK(model.clips.size() >= 17, "%d animation clips", (int)model.clips.size());

    // Outfits: every build and kit piece is in the file, plus the face parts and atlas UVs.
    bool builds = true, kit = true;
    for (int b = 0; b < MAX_BUILDS; ++b)
        for (int l = 0; l < 3; ++l) builds = builds && model.bodyMesh(b, l) >= 0;
    builds = builds && model.bodyMesh(1, 0) != model.bodyMesh(0, 0) && model.bodyMesh(2, 0) != model.bodyMesh(0, 0);
    for (int k = 0; k < (int)KitPiece::Count; ++k)
        for (int b = 0; b < MAX_BUILDS; ++b) kit = kit && model.kitMesh(k, b) >= 0;
    CHECK(builds, "three body builds, three LODs each");
    CHECK(kit, "all 14 kit pieces for every build");
    CHECK(model.face >= 0 && model.mouth >= 0, "face parts and lips");
    bool atlasUv = false;
    for (const SkinnedVertex& v : model.meshes[model.bodyLod[0]].verts)
        atlasUv = atlasUv || std::fabs(v.u2 - v.u) > 1e-3f || std::fabs(v.v2 - v.v) > 1e-3f;
    CHECK(atlasUv, "the body has its own atlas UVs (second UV set)");
    SwingRig swing;
    swing.build(model);
    FaceRig faceRig;
    faceRig.build(model);
    CHECK(swing.count == 3 && faceRig.valid(), "hair, cape and scarf chains and the face bones (%d chains)", swing.count);
    // The face bones move the real face parts the right way.
    {
        auto meanY = [&](int mesh, int bone, const Pose& p) {
            Mat4 w[MAX_BONES], sk[MAX_BONES];
            model.boneWorld(p, w);
            model.skinMatrices(w, sk);
            double sum = 0;
            int n = 0;
            for (const SkinnedVertex& v : model.meshes[mesh].verts)
                if (v.joints[0] == bone && v.weights[0] > 0.99f) {
                    sum += transformPoint(Vec3(v.px, v.py, v.pz), sk[bone]).y;
                    ++n;
                }
            return n ? (float)(sum / n) : 0.0f;
        };
        Pose rest, p;
        model.restPose(rest);
        p = rest;
        model.rotateInCharacterFrame(faceRig.lidL, quatAxisAngle(Vec3(1, 0, 0), FACE_LID_ANGLE), p);
        float lid0 = meanY(model.face, faceRig.lidL, rest), lid1 = meanY(model.face, faceRig.lidL, p);
        CHECK(lid1 < lid0 - 0.005f, "closing the eyelid moves it down (%.4f m)", lid0 - lid1);
        p = rest;
        model.rotateInCharacterFrame(faceRig.jaw, quatAxisAngle(Vec3(1, 0, 0), FACE_JAW_ANGLE), p);
        float jaw0 = meanY(model.mouth, faceRig.jaw, rest), jaw1 = meanY(model.mouth, faceRig.jaw, p);
        CHECK(jaw1 < jaw0 - 0.002f, "opening the jaw drops the lower lip (%.4f m)", jaw0 - jaw1);
        p = rest;
        model.offsetInCharacterFrame(faceRig.browL, Vec3(0, FACE_BROW_LIFT, 0), p);
        float b0 = meanY(model.face, faceRig.browL, rest), b1 = meanY(model.face, faceRig.browL, p);
        CHECK(std::fabs(b1 - b0 - FACE_BROW_LIFT) < 0.001f, "raising a brow lifts it %.4f m", b1 - b0);
    }
    for (const char* name : {"idle", "walk", "sprint", "crouch", "crouch_walk", "jump", "skydive", "glide", "aim", "shoot", "reload",
                             "build", "heal", "swing", "pick_idle", "wave", "dance"})
        CHECK(model.clip(name) != nullptr, "has clip %s", name);
    CHECK(model.bodyLod[0] >= 0 && model.bodyLod[1] >= 0 && model.bodyLod[2] >= 0, "three body LODs");
    size_t t0 = model.meshes[model.bodyLod[0]].idx.size(), t1 = model.meshes[model.bodyLod[1]].idx.size(), t2 = model.meshes[model.bodyLod[2]].idx.size();
    CHECK(t0 > t1 && t1 > t2 && t2 > 100, "LODs get simpler: %zu > %zu > %zu indices", t0, t1, t2);
    CHECK(model.hair[0] >= 0 && model.hair[1] >= 0 && model.hair[2] >= 0 && model.hair[3] >= 0 && model.headband >= 0, "4 hair styles + headband");
    int handR = model.boneIndex("Hand_R"), handL = model.boneIndex("Hand_L"), thighL = model.boneIndex("Thigh_L");
    CHECK(handR >= 0 && handL >= 0 && thighL >= 0, "bones found by name");

    // Rest pose skinning leaves the mesh where it was modelled.
    Pose rest;
    model.restPose(rest);
    Mat4 world[MAX_BONES], skin[MAX_BONES];
    model.boneWorld(rest, world);
    model.skinMatrices(world, skin);
    float worst = 0;
    float top = -1, bottom = 1e9f;
    for (const SkinnedVertex& v : model.meshes[model.bodyLod[0]].verts) {
        Vec3 p(v.px, v.py, v.pz), acc(0, 0, 0);
        for (int k = 0; k < 4; ++k) acc = acc + transformPoint(p, skin[v.joints[k]]) * v.weights[k];
        worst = std::max(worst, length(acc - p));
        top = std::max(top, p.y);
        bottom = std::min(bottom, p.y);
    }
    CHECK(worst < 1e-3f, "rest pose skinning is exact (worst %.5f m)", worst);
    CHECK(top > 1.8f && top < 2.3f && bottom > -0.05f && bottom < 0.1f, "character stands on the ground about 2 m tall (%.2f..%.2f)", bottom, top);
    Vec3 hr = model.bonePosition(world, handR), hl = model.bonePosition(world, handL);
    CHECK(hr.x > 0.2f && hl.x < -0.2f, "right hand is on the +x side after mirroring (%.2f, %.2f)", hr.x, hl.x);

    // Aiming brings both hands forward and close to the middle.
    Pose aim;
    model.restPose(aim);
    model.sampleClip(model.clipIndex("aim"), 0.0f, true, aim);
    model.boneWorld(aim, world);
    hr = model.bonePosition(world, handR);
    hl = model.bonePosition(world, handL);
    CHECK(hr.z > 0.25f && hl.z > 0.3f && std::fabs(hr.x) < 0.25f && std::fabs(hl.x) < 0.2f,
          "aim pose: hands forward near the centre R(%.2f %.2f %.2f) L(%.2f %.2f %.2f)", hr.x, hr.y, hr.z, hl.x, hl.y, hl.z);

    // Walking moves the legs, and the loop joins up.
    Pose w0, w1, wEnd;
    model.restPose(w0); model.restPose(w1); model.restPose(wEnd);
    const AnimClip* walk = model.clip("walk");
    // Left heel strikes at the start of the cycle (foot out front) and pushes off half a cycle later.
    model.sampleClip(model.clipIndex("walk"), 0.0f, true, w0);
    model.sampleClip(model.clipIndex("walk"), walk->duration * 0.5f, true, w1);
    model.sampleClip(model.clipIndex("walk"), walk->duration, true, wEnd);
    Mat4 wa[MAX_BONES], wb[MAX_BONES];
    model.boneWorld(w0, wa);
    model.boneWorld(w1, wb);
    int footL = model.boneIndex("Foot_L");
    Vec3 fa = model.bonePosition(wa, footL), fb = model.bonePosition(wb, footL);
    CHECK(std::fabs(fa.z - fb.z) > 0.6f, "walk cycle swings the left foot %.2f m", std::fabs(fa.z - fb.z));
    // The planted foot moves back exactly as far as the body moves forward in that half cycle
    // (STRIDE_WALK radians of walkPhase per metre), so it doesn't skate over the ground.
    {
        Pose q0, q1;
        model.restPose(q0);
        model.restPose(q1);
        float t0 = walk->duration * 0.05f, t1 = walk->duration * 0.30f;   // both inside the left foot's stance
        model.sampleClip(model.clipIndex("walk"), t0, true, q0);
        model.sampleClip(model.clipIndex("walk"), t1, true, q1);
        Mat4 m0[MAX_BONES], m1[MAX_BONES];
        model.boneWorld(q0, m0);
        model.boneWorld(q1, m1);
        float footMoved = std::fabs(model.bonePosition(m0, footL).z - model.bonePosition(m1, footL).z);
        float bodyMoved = (0.30f - 0.05f) * 2.0f * PI / STRIDE_WALK;
        CHECK(std::fabs(footMoved - bodyMoved) < 0.03f, "planted foot keeps pace with the ground (%.3f vs %.3f m)", footMoved, bodyMoved);
    }
    Pose wStart;
    model.restPose(wStart);
    model.sampleClip(model.clipIndex("walk"), 0.0f, true, wStart);
    float loopErr = 0;
    for (int i = 0; i < (int)model.bones.size(); ++i) loopErr = std::max(loopErr, length(wStart.t[i] - wEnd.t[i]));
    CHECK(loopErr < 1e-3f, "walk loops seamlessly (%.4f)", loopErr);

    // The animation state system: blends, layers the upper body, follows the aim pitch.
    CharAnimState st;
    CharAnimInput in;
    Pose out;
    in.anim = Anim::Idle;
    in.clock = 0;
    CHECK(evalCharacterPose(model, st, in, out), "state system produces a pose");
    in.anim = Anim::Walk;
    in.moveAmount = 1;
    in.walkPhase = 1.0f;
    in.clock = 0.016f;
    evalCharacterPose(model, st, in, out);
    CHECK(st.lowerBlend < 1.0f && st.lower == model.clipIndex("walk"), "switching idle->walk starts a cross-fade (%.2f)", st.lowerBlend);
    for (int i = 0; i < 20; ++i) { in.clock += 0.016f; evalCharacterPose(model, st, in, out); }
    CHECK(st.lowerBlend >= 1.0f, "cross-fade finishes");
    // Walking with a gun: legs from walk, arms from aim.
    in.gun = true;
    in.walkPhase = 0.0f;   // left heel strike: the left foot is out in front
    in.clock += 0.5f;
    for (int i = 0; i < 20; ++i) { in.clock += 0.016f; evalCharacterPose(model, st, in, out); }
    model.boneWorld(out, world);
    hr = model.bonePosition(world, handR);
    Vec3 foot = model.bonePosition(world, footL);
    CHECK(st.upper == model.clipIndex("aim") && hr.z > 0.25f, "gun + walking: arms aim (hand z %.2f)", hr.z);
    CHECK(std::fabs(foot.z) > 0.1f, "gun + walking: legs still walk (foot z %.2f)", foot.z);
    float handY0 = hr.y;
    in.aimPitch = 0.6f;
    evalCharacterPose(model, st, in, out);
    model.boneWorld(out, world);
    hr = model.bonePosition(world, handR);
    CHECK(hr.y > handY0 + 0.1f, "looking up lifts the gun hand (%.2f -> %.2f)", handY0, hr.y);
    in.aimPitch = 0;
    // Strafing: the legs turn toward the movement, the chest stays on the aim.
    {
        int chest = model.boneIndex("Chest");
        model.boneWorld(out, world);
        auto dirOf = [](const Vec3& v, const Mat4& m) { return transformPoint(v, m) - transformPoint(Vec3(0, 0, 0), m); };
        Vec3 chestDir0 = dirOf(Vec3(1, 0, 0), world[chest]);
        in.moveAngle = PI * 0.5f;   // moving to the right
        for (int i = 0; i < 40; ++i) { in.clock += 0.016f; evalCharacterPose(model, st, in, out); }
        CHECK(std::fabs(st.legYaw - LEG_YAW_MAX) < 0.02f && !st.backward, "strafing right turns the legs right (%.2f rad)", st.legYaw);
        // The scene turns the whole body by legYaw; the chest must come back to the aim.
        model.boneWorld(out, world);
        Vec3 chestDir = dirOf(dirOf(Vec3(1, 0, 0), world[chest]), Mat4::rotationY(st.legYaw));
        CHECK(dot(normalize(chestDir), normalize(chestDir0)) > 0.97f, "strafing: chest still faces the aim (%.3f)",
              dot(normalize(chestDir), normalize(chestDir0)));
        in.moveAngle = PI;          // backing up
        for (int i = 0; i < 40; ++i) { in.clock += 0.016f; evalCharacterPose(model, st, in, out); }
        CHECK(st.backward && std::fabs(st.legYaw) < 0.02f, "backing up: legs face forward, cycle reversed (%.2f)", st.legYaw);
        in.moveAngle = PI * 0.75f;  // back and to the right: legs turn the other way
        for (int i = 0; i < 40; ++i) { in.clock += 0.016f; evalCharacterPose(model, st, in, out); }
        CHECK(st.backward && st.legYaw < -0.6f, "backing up to the right turns the legs left (%.2f)", st.legYaw);
        in.moveAngle = 0;
        in.moveAmount = 0;
        for (int i = 0; i < 40; ++i) { in.clock += 0.016f; evalCharacterPose(model, st, in, out); }
        CHECK(!st.backward && std::fabs(st.legYaw) < 0.02f, "standing still: legs straight again");
        in.moveAmount = 1;
    }
    in.gun = false;
    in.pickaxe = true;
    in.swing = 0.5f;
    in.clock += 0.016f;
    evalCharacterPose(model, st, in, out);
    CHECK(st.upper == model.clipIndex("swing"), "pickaxe swing plays the swing clip");
    in.swing = -1;
    in.clock += 0.016f;
    evalCharacterPose(model, st, in, out);
    CHECK(st.upper == model.clipIndex("pick_idle"), "pickaxe at rest is held low");

    // The scene uses it: every character becomes a skinned draw with a full set of bones and a
    // gun or pickaxe attached to the hand.
    setCharacterModel(&model);
    Game g;
    MatchConfig cfg;
    cfg.teamSize = 2;
    cfg.seed = 5;
    g.startMatch(cfg);
    g.landEveryone();
    SceneList sc;
    buildScene(g, 16.0f / 9.0f, sc);
    int alive = g.player.alive ? 1 : 0;
    for (const Bot& b : g.bots) alive += (b.alive && lengthXZ(b.body.pos - sc.eye) <= 420.0f) ? 1 : 0;
    CHECK((int)sc.characters.size() == alive && alive > 50, "%d characters drawn as rigged models (%d alive in view range)", (int)sc.characters.size(), alive);
    CHECK(sc.boneMats.size() == sc.characters.size() * model.bones.size(), "one bone set per character");
    bool skinsVary = false, farLod = false;
    for (const CharacterDraw& c : sc.characters) {
        if (c.skin != sc.characters[0].skin) skinsVary = true;
        if (c.distance > 40) farLod = true;
    }
    CHECK(skinsVary, "players wear different skins");
    CHECK(farLod, "distances are recorded for the level of detail");
    SceneList lobby;
    std::vector<LobbyMember> party = {{"YOU", Vec4(1, 1, 1, 1), true, 2, false}, {"PAL", Vec4(1, 1, 1, 1), false, 3, false}};
    buildLobbyScene(party, 1.0f, 16.0f / 9.0f, lobby);
    CHECK(lobby.characters.size() == 2 && lobby.characters[0].hair == skinAt(2).hairStyle, "lobby draws rigged members with their hair style");
    setCharacterModel(nullptr);
    SceneList sc2;
    buildScene(g, 16.0f / 9.0f, sc2);
    CHECK(sc2.characters.empty() && sc2.opaque[MESH_SPHERE].size() > sc.opaque[MESH_SPHERE].size(), "without the model, the built-in figures are used");
    setCharacterModel(&model);
}

// ------------------------------------------------------------------ performance
static void testPathfinding() {
    section("Bot pathfinding");
    Game g = quietGame();
    Vec3 mid = g.world.playerSpawn;
    clearArea(g, mid, 50);
    int ci = (int)std::floor(mid.x / TILE), cj = (int)std::floor(mid.z / TILE);
    auto wall = [&](int i, int j, int side) {
        Slot s;
        s.type = PieceType::Wall;
        s.i = i; s.j = j; s.k = 0; s.side = side;
        return g.world.build.place(s, true);
    };
    auto insideSolid = [&](const Vec3& p) {
        std::vector<AABB> solids;
        AABB probe{{p.x - 0.3f, p.y + 0.8f, p.z - 0.3f}, {p.x + 0.3f, p.y + 1.6f, p.z + 0.3f}};
        g.world.collectSolids(probe, solids);
        return !solids.empty();
    };
    Vec3 cellC((ci + 0.5f) * TILE, 0, (cj + 0.5f) * TILE);
    cellC.y = g.world.terrainHeight(cellC.x, cellC.z);

    // A U of walls open to the south; the goal is straight north, behind the closed side.
    wall(ci, cj + 1, 1);       // north
    wall(ci, cj, 0);           // west
    wall(ci + 1, cj, 0);       // east
    Vec3 goal = cellC + Vec3(0, 0, 15);
    std::vector<Vec3> path;
    bool ok = findPath(g.world, cellC, goal, path);
    CHECK(ok && !path.empty() && lengthXZ(path.back() - goal) < 0.01f, "path out of a U-shaped trap (%zu corners)", path.size());
    bool clear = true, wentSouth = false;
    for (const Vec3& p : path) {
        if (insideSolid(p)) clear = false;
        if (p.z < cj * TILE + 0.5f) wentSouth = true;
    }
    CHECK(clear, "no corner of the path is inside a wall");
    CHECK(wentSouth, "the path leaves through the open side first");

    // A bot in the trap gets out and reaches the goal (straight steering would push into the wall).
    Bot& b = g.bots[0];
    b.alive = true;
    b.team = 1;
    b.style = BotStyle::Mixed;
    b.body = Body();
    b.body.pos = cellC;
    b.drop = DropPhase::Landed;
    b.state = AIState::Wander;
    b.stateTime = 0;
    b.moveTarget = goal;
    b.targetId = NO_TARGET;
    b.lootTimer = 100;
    g.player.body.pos = mid + Vec3(150, 0, 150);   // out of sight, so the bot doesn't start a fight
    g.player.body.pos.y = g.world.terrainHeight(g.player.body.pos.x, g.player.body.pos.z);
    float closest = 1e9f;
    for (int f = 0; f < 60 * 20 && closest > 1.5f; ++f) {   // ~26 m around the walls at wander speed
        g.pathPlansLeft = NAV_PLANS_PER_FRAME;
        b.moveTarget = goal;
        b.lootTimer = 100;
        g.updateBot(b, DT);
        closest = std::min(closest, lengthXZ(b.body.pos - goal));
    }
    CHECK(closest <= 1.5f && b.paths >= 1, "bot walks out of the trap to the goal (%.1f m away, %d paths)", closest, b.paths);

    // A closed box with a door cut in its south wall: the path goes in through the door.
    int bi = ci + 4, bj = cj;
    wall(bi, bj, 1);           // south (gets the door)
    wall(bi, bj + 1, 1);       // north
    wall(bi, bj, 0);           // west
    int eastId = wall(bi + 1, bj, 0);
    Piece* south = nullptr;
    for (Piece& p : g.world.build.pieces())
        if (p.alive && p.slot.type == PieceType::Wall && p.slot.side == 1 && p.slot.i == bi && p.slot.j == bj) south = &p;
    CHECK(south != nullptr, "door wall placed");
    if (south) south->removed = (1u << 1) | (1u << 4);   // door: bottom middle, two tiles tall
    Vec3 inside((bi + 0.5f) * TILE, 0, (bj + 0.5f) * TILE);
    inside.y = g.world.terrainHeight(inside.x, inside.z);
    Vec3 outside = inside + Vec3(0, 0, 14);   // north of the box
    ok = findPath(g.world, outside, inside, path);
    bool viaDoor = false;
    for (const Vec3& p : path)
        if (p.z < bj * TILE + 1.5f && std::fabs(p.x - inside.x) < 1.2f) viaDoor = true;
    CHECK(ok && lengthXZ(path.back() - inside) < 0.01f && viaDoor, "path into a closed box goes through its door (%zu corners)", path.size());

    // Without the door there's no way in: the path stops outside, next to the box.
    if (south) south->removed = 0;
    ok = findPath(g.world, outside, inside, path);
    CHECK(!ok || lengthXZ(path.back() - inside) > 2.5f, "no route into a sealed box");
    (void)eastId;

    // Cost: one search across 40 m with obstacles stays cheap.
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int k = 0; k < 20; ++k) findPath(g.world, cellC, goal, path);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count() / 20.0;
    std::printf("  %.3f ms per path search\n", ms);
    CHECK(ms < 2.0, "path search is fast (%.3f ms)", ms);
}

static void testSound() {
    section("Sound effects");
    // Every sound synthesises to a short, non-silent, repeatable clip with no click at the end.
    bool allOk = true;
    for (int i = 0; i < (int)Sfx::Count; ++i) {
        std::vector<int16_t> a = synthSfx((Sfx)i), b = synthSfx((Sfx)i);
        int peak = 0;
        for (int16_t s : a) peak = std::max(peak, std::abs((int)s));
        float secs = (float)a.size() / SFX_SAMPLE_RATE;
        bool ok = !a.empty() && secs <= 2.0f && peak > 3000 && a == b && std::abs((int)a.back()) < 200;
        if (!ok) {
            allOk = false;
            std::printf("  bad sound %s: %.2f s, peak %d, repeatable %d\n", sfxName((Sfx)i), secs, peak, (int)(a == b));
        }
    }
    CHECK(allOk, "all %d sounds synthesise cleanly", (int)Sfx::Count);
    CHECK(std::string(sfxName(Sfx::UiClick)) == "UI_CLICK" && shotSfx(GunType::Sniper) == Sfx::ShotSniper, "names and gun sounds");

    // Distance and direction.
    float gNear, pNear, gFar, pFar, gOut, pOut, gLeft, pLeft;
    Vec3 ear(0, 0, 0), right(1, 0, 0);
    sfxMix(ear, right, Vec3(3, 0, 0), 150, gNear, pNear);
    sfxMix(ear, right, Vec3(80, 0, 0), 150, gFar, pFar);
    sfxMix(ear, right, Vec3(200, 0, 0), 150, gOut, pOut);
    sfxMix(ear, right, Vec3(-5, 0, 0), 150, gLeft, pLeft);
    CHECK(gNear > 0.6f && gFar > 0 && gFar < gNear * 0.3f && gOut == 0, "quieter with distance (%.2f, %.3f, %.2f)", gNear, gFar, gOut);
    CHECK(pNear > 0.5f && pLeft < -0.5f, "panned to the side it comes from (%.2f, %.2f)", pNear, pLeft);

    // The game makes the right sounds.
    auto has = [](const Game& g, Sfx id) {
        for (const SoundEvent& e : g.sounds)
            if (e.id == id) return true;
        return false;
    };
    Game g = quietGame();
    standAtCell(g, 0, 0);
    clearArea(g, g.player.body.pos, 40);
    g.sounds.clear();
    g.player.slots[0] = Item::makeGun(GunType::Shotgun, Rarity::Rare);
    g.player.selected = 0;
    g.player.pickaxeOut = false;
    InputState fire;
    fire.fire = fire.firePressed = true;
    g.update(fire, DT);
    CHECK(has(g, Sfx::ShotShotgun), "shooting a shotgun makes a shotgun sound");
    g.sounds.clear();
    Vec3 walkStart = g.player.body.pos;
    run(g, 120, [](int) { InputState in; in.forward = true; return in; });
    float walked = lengthXZ(g.player.body.pos - walkStart);
    int steps = 0;
    for (const SoundEvent& e : g.sounds) steps += e.id == Sfx::Footstep;
    // The walk animation puts a foot down every PI / 2.4 = ~1.3 m.
    int expected = (int)(walked * 2.4f / PI);
    CHECK(steps >= 4 && std::abs(steps - expected) <= 1, "footsteps match the walk cycle (%d over %.1f m)", steps, walked);
    g.sounds.clear();
    g.player.mode = Mode::Build;
    g.player.buildPiece = PieceType::Wall;
    run(g, 2, none);
    g.update(fire, DT);
    CHECK(has(g, Sfx::BuildPlace), "placing a build makes a sound");
    g.player.mode = Mode::Combat;
    // A gunshot is heard from far away, a footstep isn't.
    CHECK(sfxRange(Sfx::ShotAR) > 100 && sfxRange(Sfx::Footstep) < 30, "gunshots carry further than footsteps");
    // Nobody drains the list in tests: it stops growing at the cap.
    g.sounds.clear();
    for (int i = 0; i < MAX_SOUND_EVENTS + 50; ++i) g.soundUi(Sfx::UiClick);
    CHECK((int)g.sounds.size() == MAX_SOUND_EVENTS, "sound list is capped (%zu)", g.sounds.size());

    // Volume is saved with the settings.
    Settings s;
    s.volume = 0.3f;
    Settings l;
    l.parse(s.serialize());
    CHECK(std::fabs(l.volume - 0.3f) < 1e-4f, "volume saved and loaded (%.2f)", l.volume);
    l.parse("volume=7\n");
    CHECK(std::fabs(l.volume - 0.3f) < 1e-4f, "bad volume ignored");
}

// A tiny skeleton for the swing and face tests: a "Chest" root with a scarf chain and the
// seven face bones. Every bone gets an odd rest rotation (like real glTF bones), and its offset
// is given in model space, so the joints sit where the numbers say.
static CharacterModel tinyRig(bool horizontalChain) {
    CharacterModel m;
    Rng rng(3);
    auto add = [&](const char* name, int parent, Vec3 modelOffset) {
        Bone b;
        b.name = name;
        b.parent = parent;
        b.restR = quatAxisAngle(normalize(Vec3(rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1))), rng.range(0.3f, 2.5f));
        Quat parentW = parent >= 0 ? m.bones[parent].restWorldR : Quat();
        b.restT = quatRotate(quatConj(parentW), modelOffset);
        b.restWorldR = quatMul(b.restR, parentW);   // parent's rotation, then the bone's own
        m.bones.push_back(b);
        return (int)m.bones.size() - 1;
    };
    int chest = add("Chest", -1, Vec3(0, 1.5f, 0));
    Vec3 step = horizontalChain ? Vec3(0, 0, -0.15f) : Vec3(0, -0.15f, 0);
    int s1 = add("Scarf_1", chest, Vec3(0, 0, -0.1f));
    int s2 = add("Scarf_2", s1, step);
    add("Scarf_end", s2, step);
    int head = add("Head", chest, Vec3(0, 0.3f, 0));
    add("Jaw", head, Vec3(0, -0.03f, 0.05f));
    add("Brow_L", head, Vec3(-0.06f, 0.06f, 0.1f));
    add("Brow_R", head, Vec3(0.06f, 0.06f, 0.1f));
    add("Lid_L", head, Vec3(-0.06f, 0.02f, 0.1f));
    add("Lid_R", head, Vec3(0.06f, 0.02f, 0.1f));
    add("Mouth_L", head, Vec3(-0.03f, -0.06f, 0.11f));
    add("Mouth_R", head, Vec3(0.03f, -0.06f, 0.11f));
    m.loaded = true;
    return m;
}

static void testSecondaryMotion() {
    section("Swinging capes and scarves, faces");
    // quatFromMat undoes quatToMat; quatFromTo turns one direction into another.
    Quat q = quatAxisAngle(normalize(Vec3(0.3f, 1, -0.4f)), 1.1f);
    Quat back = quatFromMat(quatToMat(q));
    float qd = std::fabs(q.x * back.x + q.y * back.y + q.z * back.z + q.w * back.w);
    CHECK(qd > 0.9999f, "quatFromMat round trip (%.5f)", qd);
    Vec3 to = quatRotate(quatFromTo(Vec3(0, 0, 1), Vec3(1, 1, 0)), Vec3(0, 0, 1));
    CHECK(length(to - normalize(Vec3(1, 1, 0))) < 1e-4f, "quatFromTo turns z toward (1,1,0)");

    auto chainPos = [](const CharacterModel& m, const Pose& p, const Mat4& root, Vec3* out) {
        Mat4 w[MAX_BONES];
        m.boneWorld(p, w);
        for (int k = 0; k < 3; ++k) out[k] = transformPoint(m.bonePosition(w, m.boneIndex(k == 0 ? "Scarf_1" : (k == 1 ? "Scarf_2" : "Scarf_end"))), root);
    };

    // A chain sticking straight out backward sags under gravity (it must not swing up).
    {
        CharacterModel m = tinyRig(true);
        SwingRig rig;
        rig.build(m);
        CHECK(rig.count == 1 && rig.chains[0].count == 3, "finds the scarf chain (%d chains)", rig.count);
        SwingState st;
        Mat4 root = Mat4::identity();
        Pose p;
        Vec3 c[3];
        for (int f = 0; f < 120; ++f) {
            m.restPose(p);
            applySwing(m, rig, st, root, DT, 1u, p);
        }
        chainPos(m, p, root, c);
        // The spring toward the animated pose holds most of the shape; it must sag down, not up.
        CHECK(c[2].y < 1.5f - 0.02f && std::fabs(c[2].x) < 0.01f, "a horizontal chain sags under gravity (end y %.3f, was 1.500)", c[2].y);
        float l1 = length(c[1] - c[0]), l2 = length(c[2] - c[1]);
        CHECK(std::fabs(l1 - 0.15f) < 0.01f && std::fabs(l2 - 0.15f) < 0.01f, "segments keep their length (%.3f, %.3f)", l1, l2);
        bool finite = std::isfinite(c[2].x) && std::isfinite(c[2].y) && std::isfinite(c[2].z);
        CHECK(finite, "no NaNs");
    }
    // A hanging chain at rest stays put; running drags it behind, falling lifts it.
    {
        CharacterModel m = tinyRig(false);
        SwingRig rig;
        rig.build(m);
        SwingState st;
        Pose p;
        Vec3 c[3];
        Mat4 root = Mat4::identity();
        for (int f = 0; f < 120; ++f) {
            m.restPose(p);
            applySwing(m, rig, st, root, DT, 1u, p);
        }
        chainPos(m, p, root, c);
        CHECK(std::fabs(c[2].z - (-0.1f)) < 0.02f && c[2].y < 1.25f, "a hanging chain stays hanging (end %.2f, %.2f)", c[2].y, c[2].z);
        float z = 0;
        for (int f = 0; f < 60; ++f) {
            z += 5.0f * DT;   // run forward (+z) at 5 m/s
            root = Mat4::translation(Vec3(0, 0, z));
            m.restPose(p);
            applySwing(m, rig, st, root, DT, 1u, p);
        }
        chainPos(m, p, root, c);
        CHECK(c[2].z - z < -0.15f, "running forward drags it back (tip %.2f m behind)", z - c[2].z);
        float y = 0;
        for (int f = 0; f < 60; ++f) {
            y -= 25.0f * DT;   // skydive
            root = Mat4::translation(Vec3(0, y, z));
            m.restPose(p);
            applySwing(m, rig, st, root, DT, 1u, p);
        }
        chainPos(m, p, root, c);
        CHECK(c[2].y - y > c[0].y - y - 0.1f, "falling fast lifts it (tip %.2f vs root %.2f)", c[2].y - y, c[0].y - y);
        root = Mat4::translation(Vec3(100, 0, 0));   // teleport: starts again from the animated pose
        m.restPose(p);
        applySwing(m, rig, st, root, DT, 1u, p);
        chainPos(m, p, root, c);
        CHECK(std::fabs(c[2].y - 1.2f) < 0.02f, "a teleport resets the chain (%.2f)", c[2].y);
    }
    // Faces: blinking, and expressions moving the right bones the right way.
    {
        CharacterModel m = tinyRig(false);
        FaceRig rig;
        rig.build(m);
        CHECK(rig.valid(), "finds the seven face bones");
        FaceState st;
        Pose p;
        float most = 0;
        for (int f = 0; f < 60 * 8; ++f) {
            m.restPose(p);
            applyFace(m, rig, st, Expression::Neutral, DT, p);
            most = std::max(most, faceLidClosure(st));
        }
        CHECK(most > 0.9f, "the eyes blink (lids reach %.2f)", most);
        Pose rest;
        m.restPose(rest);
        FaceState happy;
        for (int f = 0; f < 60; ++f) {
            m.restPose(p);
            applyFace(m, rig, happy, Expression::Happy, DT, p);
        }
        int ml = m.boneIndex("Mouth_L"), bl = m.boneIndex("Brow_L");
        Mat4 wr[MAX_BONES], wp[MAX_BONES];
        m.boneWorld(rest, wr);
        m.boneWorld(p, wp);
        float lift = m.bonePosition(wp, ml).y - m.bonePosition(wr, ml).y;
        CHECK(lift > 0.002f, "smiling lifts the mouth corners (%.4f m)", lift);
        CHECK(m.bonePosition(wp, bl).y > m.bonePosition(wr, bl).y + 0.001f, "happy raises the brows");
        FaceState pain;
        for (int f = 0; f < 60; ++f) {
            m.restPose(p);
            applyFace(m, rig, pain, Expression::Pain, DT, p);
        }
        CHECK(pain.now.lids > 0.5f && pain.now.mouthOpen > 0.4f, "pain squints and opens the mouth");
        CHECK(std::string(expressionName(Expression::Focused)) == "FOCUSED", "expression names");
    }
}

static void testOutfits() {
    section("Outfits: builds, kit pieces, character style");
    bool valid = true, capeAndPack = false;
    int wearers[(int)KitPiece::Count] = {};
    int builds[(int)BodyBuild::Count] = {};
    for (int i = 0; i < skinCount(); ++i) {
        const Skin& s = skinAt(i);
        valid = valid && s.build >= 0 && s.build < (int)BodyBuild::Count && s.kitMaterial >= 0 &&
                s.kitMaterial < (int)KitMaterial::Count && (s.kit >> (int)KitPiece::Count) == 0;
        if (s.build >= 0 && s.build < (int)BodyBuild::Count) builds[s.build]++;
        for (int k = 0; k < (int)KitPiece::Count; ++k) wearers[k] += s.wears((KitPiece)k);
        capeAndPack = capeAndPack || (s.wears(KitPiece::Cape) && s.wears(KitPiece::Backpack));
    }
    CHECK(valid, "every skin has a valid build, kit and material");
    int unused = 0;
    for (int k = 0; k < (int)KitPiece::Count; ++k) unused += wearers[k] == 0;
    CHECK(unused == 0, "every kit piece is worn by someone (%d unused)", unused);
    CHECK(builds[0] > 5 && builds[1] > 5 && builds[2] > 5, "all three builds are used (%d/%d/%d)", builds[0], builds[1], builds[2]);
    CHECK(!capeAndPack, "nobody wears a cape over a backpack");
    CHECK(skinAt(10).wears(KitPiece::Pauldrons) && skinAt(10).kitMaterial == (int)KitMaterial::Metal, "STEEL KNIGHT wears metal pauldrons");
    float r, mtl;
    kitMaterialParams((int)KitMaterial::Metal, r, mtl);
    float rc, mc;
    kitMaterialParams((int)KitMaterial::Cloth, rc, mc);
    CHECK(mtl == 1.0f && mc == 0.0f && r < rc, "metal is shiny and metallic, cloth matte");
    CHECK(std::string(kitPieceName(KitPiece::Holster)) == "holster" && std::string(bodyBuildName(1)) == "HEAVY", "names");
    Settings s;
    s.characterStyle = 1;
    Settings l;
    l.parse(s.serialize());
    CHECK(l.characterStyle == 1, "character style is saved");
    // Each character's draw carries its build, kit and palette.
    Game g(7);
    SceneList sc;
    buildScene(g, 16.0f / 9.0f, sc);
    bool carries = !sc.characters.empty();
    for (const CharacterDraw& c : sc.characters)
        carries = carries && c.build >= 0 && c.build < 3 && c.skinTone.w > 0;
    if (characterModel()) CHECK(carries, "character draws carry build and palette");
}

static void testOcclusion() {
    section("Occlusion culling");
    // A camera at the origin looking down +z.
    Mat4 view = Mat4::lookAtLH(Vec3(0, 2, 0), Vec3(0, 2, 10), Vec3(0, 1, 0));
    Mat4 proj = Mat4::perspectiveLH(deg2rad(70.0f), 16.0f / 9.0f, 0.1f, 600.0f);
    OcclusionCuller occ;
    occ.begin(view, proj, Vec3(0, 2, 0));
    occ.addBox(AABB({-4, 0, 10}, {4, 5, 10.3f}));   // a wall 10 m ahead, 8 m wide, 5 m tall
    occ.finish();
    CHECK(occ.occluderTriangles > 0, "the wall is drawn into the depth picture (%d triangles)", occ.occluderTriangles);
    CHECK(!occ.sphereVisible(Vec3(0, 2, 20), 0.8f), "a barrel right behind the wall is hidden");
    CHECK(occ.sphereVisible(Vec3(0, 2, 6), 0.8f), "a barrel in front of the wall is drawn");
    CHECK(occ.sphereVisible(Vec3(0, 7, 20), 0.8f), "something sticking up over the wall is drawn");
    CHECK(occ.sphereVisible(Vec3(10.0f, 2, 20), 0.8f), "something past the end of the wall is drawn");
    CHECK(!occ.sphereVisible(Vec3(0, 2, -10), 0.8f), "something behind the camera is skipped");
    CHECK(!occ.sphereVisible(Vec3(60, 2, 10), 0.8f), "something far off to the side is skipped");
    CHECK(occ.sphereVisible(Vec3(0, 2, 0.5f), 0.8f), "something touching the camera is always drawn");
    // Big things behind a small wall stay visible (only fully covered things are hidden).
    CHECK(occ.sphereVisible(Vec3(0, 2, 30), 8.0f), "a house-sized thing behind the small wall is drawn");

    // Hills: a raised plateau between the camera and a target hides it; one on top is drawn.
    Game g;
    g.reset(7);
    World& w = g.world;
    w.flattenTerrain(0, 0, 400, 0);            // flat map
    w.flattenTerrain(0, 80, 25, 30);           // a 30 m high plateau 80 m ahead
    Vec3 eye(0, 2, 0);
    Mat4 v2 = Mat4::lookAtLH(eye, Vec3(0, 2, 100), Vec3(0, 1, 0));
    occ.begin(v2, proj, eye);
    occ.addGround(w);
    occ.finish();
    CHECK(!occ.sphereVisible(Vec3(0, 3, 150), 2.0f), "a player behind the hill is hidden");
    CHECK(occ.sphereVisible(Vec3(0, w.terrainHeight(0, 80) + 1.0f, 80), 1.0f), "a player standing on the hill is drawn");
    CHECK(occ.sphereVisible(Vec3(0, 95, 150), 2.0f), "a glider high above the hill is drawn");

    // Never hides what a straight line from the eye can reach (checked against the world raycast).
    {
        // Stand 14 m from a house and look at it: plenty is hidden behind its walls.
        g.reset(11);
        g.landEveryone();
        Vec3 house = g.world.houseCenters[g.world.houseCenters.size() / 2];
        Vec3 eye = house + Vec3(-14, 0, 0);
        eye.y = g.world.terrainHeight(eye.x, eye.z) + 2.0f;
        g.camera.eye = eye;
        g.camera.forward = normalize(Vec3(house.x, eye.y, house.z) - eye);
        SceneList scene;
        setOcclusionCulling(false);
        buildScene(g, 16.0f / 9.0f, scene);
        OcclusionCuller c;
        c.begin(scene.view, scene.proj, scene.eye);
        c.addGround(g.world);
        for (const StaticBox& b : g.world.statics)
            if (b.alive && b.collide && (b.kind == PropKind::HouseWall || b.kind == PropKind::HouseFloor)) c.addBox(b.box);
        c.finish();
        int hidden = 0, wrong = 0;
        Rng rng(5);
        for (int i = 0; i < 4000; ++i) {
            Vec3 p(house.x + rng.range(0.0f, 120.0f), 0, house.z + rng.range(-40.0f, 40.0f));
            p.y = g.world.terrainHeight(p.x, p.z) + rng.range(0.5f, 3.0f);
            if (!c.inFrustum(p, 0.5f) || c.sphereVisible(p, 0.5f)) continue;
            ++hidden;
            // Hidden: every line from the eye to the point must be blocked by something solid.
            Vec3 d = p - scene.eye;
            float len = length(d);
            HitInfo h;
            bool blocked = g.world.raycast(Ray{scene.eye, d / len}, len - 0.6f, h, false);
            if (!blocked) {
                // The world raycast may not include the ground; check the line against the terrain.
                for (float t = 0.02f; t < 0.98f && !blocked; t += 0.01f) {
                    Vec3 q = scene.eye + d * t;
                    blocked = q.y < g.world.terrainHeight(q.x, q.z);
                }
            }
            if (!blocked) ++wrong;
        }
        CHECK(hidden > 30 && wrong == 0, "hidden points are really out of sight (%d hidden, %d wrongly)", hidden, wrong);
        setOcclusionCulling(true);
    }

    // The scene: hidden instances move to the shadow-only lists (they still cast shadows).
    {
        SceneList scene;
        setOcclusionCulling(false);
        buildScene(g, 16.0f / 9.0f, scene);
        size_t all = scene.totalInstances();
        setOcclusionCulling(true);
        buildScene(g, 16.0f / 9.0f, scene);
        size_t shadows = 0;
        for (int m = 0; m < MESH_COUNT; ++m) shadows += scene.shadowOnly[m].size();
        OcclusionStats st = lastOcclusionStats();
        std::printf("  %zu instances: %d outside the view, %d behind something; %d occluder triangles\n", all,
                    st.culledFrustum, st.culledOccluded, st.occluderTriangles);
        CHECK(scene.totalInstances() < all / 2, "most of the map isn't drawn from one spot (%zu of %zu)", scene.totalInstances(), all);
        CHECK(shadows > 0 && !scene.opaque[MESH_TERRAIN].empty(), "hidden things still cast shadows; the ground is never culled");
    }
}

static void testPerformance() {
    section("Performance (CPU side of one frame)");
    Game g;
    MatchConfig cfg;
    cfg.teamSize = 4;  // squads: 20 players
    g.startMatch(cfg);
    // Give the bots something to do: player in the middle of them, with some builds around.
    g.player.body.pos = g.world.enemySpawns[0] + Vec3(15, 0, 0);
    for (int i = 0; i < 60; ++i) {
        Slot s;
        s.type = (PieceType)(i % 4);
        s.i = i % 8; s.j = i / 8; s.k = 0; s.side = i % 2; s.dir = i % 4;
        g.world.build.place(s, true);
    }
    SceneList scene;
    HudBuilder hud;
    auto t0 = std::chrono::high_resolution_clock::now();
    const int frames = 600;
    for (int i = 0; i < frames; ++i) {
        InputState in;
        in.forward = (i / 60) % 2 == 0;
        in.mouseDx = 2;
        g.update(in, DT);
        buildScene(g, 16.0f / 9.0f, scene);
        buildHud(g, 1920, 1080, 60, Bindings::defaults(), hud);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / frames;
    std::printf("  %.3f ms per frame for game update + scene + HUD (budget for 60 FPS: 16.7 ms)\n", ms);
    CHECK(ms < 4.0, "CPU frame time %.3f ms", ms);
}

int main() {
    testStage1();
    testStage2();
    testStage3();
    testStage4();
    testStage5();
    testStage6();
    testStage7();
    testPickaxe();
    testStamina();
    testKeybindsAndSettings();
    testFrontEnd();
    testTeams();
    testIcons();
    testInventoryResetSecondary();
    testPoisMapsAnims();
    testKeybindsShared();
    testRound4();
    testLocker();
    testCharacterModel();
    testBuildUp();
    testAccounts();
    testBigMap();
    testFallShockBots();
    testPathfinding();
    testSound();
    testSecondaryMotion();
    testOutfits();
    testOcclusion();
    testPerformance();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
