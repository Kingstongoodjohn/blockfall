// Blockfall - Windows entry point: window, main loop, and an optional scripted demo for testing
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../game/frontend.h"
#include "../game/model.h"
#include "../game/map.h"
#include "audio_xaudio2.h"
#include "input_win32.h"
#include "renderer_d3d11.h"

using namespace bf;

namespace {

struct App {
    HWND hwnd = nullptr;
    RendererD3D11 renderer;
    Win32Input input;
    bool running = true;
    bool resized = false;
    int width = 1600, height = 900;
};

App* g_app = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = g_app;
    if (app && app->input.handleMessage(msg, wp, lp)) return 0;
    switch (msg) {
        case WM_SIZE:
            if (app && wp != SIZE_MINIMIZED) {
                app->width = LOWORD(lp);
                app->height = HIWORD(lp);
                app->resized = true;
            }
            return 0;
        case WM_CLOSE:
            if (app) app->running = false;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

double nowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

// %APPDATA%\Blockfall\settings.ini
std::string settingsPath() {
    char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "blockfall_settings.ini";
    std::string dir = std::string(buf) + "\\Blockfall";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\settings.ini";
}

// Points the player's view at a world position (used by the scripted demo).
void lookAt(Game& g, const Vec3& target) {
    for (int i = 0; i < 3; ++i) {
        g.updateCamera();
        Vec3 d = normalize(target - g.camera.eye);
        g.player.yaw = std::atan2(d.x, d.z);
        g.player.pitch = std::asin(clampf(d.y, -0.99f, 0.99f));
    }
}

// ------------------------------------------------------------------ scripted demo (--shots DIR)
// Clicks through the menus and plays a squads match, saving screenshots along the way, so the
// build can be checked without anyone at the keyboard.
struct Demo {
    std::string dir;
    FILE* log = nullptr;
    int frame = 0;
    int stopAt = 650;   // --until N stops the demo early
    bool done = false;
    int tree = -1;
    int soundEvents = 0;   // sounds the game asked for during the demo (logged at the end)
    const char* pendingShot = nullptr;

    static void hold(RawInput& r, int k) { r.down[k] = true; }
    static void press(RawInput& r, int k) { r.down[k] = r.pressed[k] = true; }

    void park(Game& g, Bot& b, const Vec3& p) {
        b.body.pos = p;
        b.body.pos.y = g.world.terrainHeight(p.x, p.z);
        b.body.vel = Vec3();
    }

    RawInput step(FrontEnd& fe, const RawInput& real) {
        RawInput r;
        r.mouseX = real.mouseX;
        r.mouseY = real.mouseY;
        Game& g = fe.game;
        int f = frame;
        pendingShot = nullptr;

        if (f == 1) fe.screen = Screen::SignIn;
        if (f == 3) pendingShot = "00_sign_in.bmp";
        if (f == 4) {
            std::string err;
            fe.createAccount("PLAYER", "demo1234", err);
            fe.screen = Screen::Lobby;
        }
        if (f == 1) {
            fe.settings.gfx = GraphicsSettings::preset(2);  // EPIC for the photos
            fe.settings.addFriend("RIVER");
            fe.settings.addFriend("MAPLE");
            fe.settings.addFriend("JUNO");
            fe.settings.addFriend("PIXELPAL");
            fe.invite("RIVER");
            fe.invite("MAPLE");
            r.mouseX = 240; r.mouseY = 240;  // hover PLAY
        }
        if (f == 20) pendingShot = "01_lobby.bmp";
        if (f == 22) fe.screen = Screen::Friends;
        if (f == 32) pendingShot = "02_friends.bmp";
        if (f == 34) { fe.screen = Screen::Settings; fe.settingsTab = 0; fe.rebinding = (int)Action::Pickaxe; }
        if (f == 40) pendingShot = "03_settings_controls.bmp";
        if (f == 41) { fe.rebinding = -1; fe.settingsTab = 1; }
        if (f == 44) pendingShot = "03b_settings_graphics.bmp";
        if (f == 45) {
            fe.settings.kbucks = 2500;                 // show off the locker with a couple of skins
            fe.settings.ownedSkins |= 1u << 2;
            fe.settings.equippedSkin = 2;
            fe.screen = Screen::Locker; fe.lockerSel = 4;
        }
        if (f == 48) pendingShot = "03c_locker.bmp";
        if (f == 49) { fe.screen = Screen::Lobby; fe.invite("JUNO"); }
        if (f == 50) fe.startMatch();
        if (f > 50 && f < 60) { g.player.pitch = -1.0f; g.player.yaw += 0.03f; }  // looking down at the roof deck
        if (f == 57) pendingShot = "04a_battle_bus.bmp";
        if (f >= 58 && f < 76) {
            // Jump: hang in the air skydiving (held in place) long enough for the pose to blend in.
            g.dropPhase = DropPhase::Skydive;
            g.player.body.pos = g.bus.pos() + Vec3(0, -8.0f, 0);
            g.player.body.vel = Vec3(0, -30.0f, 0);
            g.player.pitch = -0.9f;
            for (Bot& b : g.bots)
                if (b.team == 0) { b.drop = DropPhase::Skydive; b.body.pos = g.player.body.pos + Vec3(3.0f + b.index * 2.0f, -2.0f, 4.0f); b.body.vel = Vec3(0, -30.0f, 0); }
        }
        if (f == 74) pendingShot = "04b_skydive.bmp";
        if (f == 76) {
            g.landEveryone();
            // Keep the other squads busy far away for the photos.
            for (Bot& b : g.bots)
                if (b.team != 0) park(g, b, Vec3(120.0f - b.team * 3.0f, 0, 130.0f));
            Vec3 mate = g.bots[0].body.pos;
            lookAt(g, mate + Vec3(0, 1.2f, 0) + (mate - g.player.body.pos) * 2.0f);
            g.player.pitch = -0.12f;
        }
        if (f == 86) pendingShot = "04_squad_drop.bmp";
        // Pickaxe on a tree.
        if (f == 88) {
            float best = 1e9f;
            for (size_t i = 0; i < g.world.statics.size(); ++i) {
                const StaticBox& s = g.world.statics[i];
                if (s.kind != PropKind::Trunk || !s.alive) continue;
                float d = lengthXZ(s.box.center() - g.player.body.pos);
                if (d < best) { best = d; tree = (int)i; }
            }
            Vec3 t = g.world.statics[tree].box.center();
            g.player.body.pos = Vec3(t.x, 0, t.z - 1.6f);
            g.player.body.pos.y = g.world.terrainHeight(g.player.body.pos.x, g.player.body.pos.z);
            for (Bot& b : g.bots)
                if (b.team == 0) park(g, b, g.player.body.pos + Vec3(-3.0f - b.index, 0, -2.0f));
        }
        if (f > 88 && f < 150 && tree >= 0) {
            const StaticBox& s = g.world.statics[tree];
            lookAt(g, Vec3(s.box.center().x, g.player.body.pos.y + 1.3f, s.box.center().z));
            hold(r, KEY_MOUSE_LEFT);
            if (f == 89) press(r, KEY_MOUSE_LEFT);
        }
        if (f == 128) pendingShot = "05_pickaxe.bmp";
        // Sprint until stamina is low.
        if (f >= 152 && f < 336) {
            hold(r, 'W');
            hold(r, KEY_SHIFT);
            g.player.pitch = -0.1f;
        }
        if (f == 152) g.player.yaw += 2.6f;
        if (f == 300) pendingShot = "06_sprint_stamina_epic.bmp";
        if (f == 301) fe.settings.gfx = GraphicsSettings::preset(0);
        if (f == 330) pendingShot = "06b_sprint_performance_preset.bmp";
        if (f == 331) fe.settings.gfx = GraphicsSettings::preset(2);
        if (f == 340) press(r, 'M');
        if (f == 348) pendingShot = "06c_map.bmp";
        if (f == 350) press(r, 'M');
        // Cone edit: place a cone, flatten one quadrant, show it from above.
        if (f == 352) {
            // A cone 8 m ahead, sitting on the ground, with one quadrant flattened by an edit.
            g.player.body.pos = g.world.playerSpawn + Vec3(0, 0, -10.0f);  // open ground near the spawn
            g.player.body.pos.y = g.world.terrainHeight(g.player.body.pos.x, g.player.body.pos.z);
            g.player.yaw = 0;
            Vec3 fwd(0, 0, 1);
            Vec3 spot = g.player.body.pos + fwd * 8.0f;
            Slot cs;
            cs.type = PieceType::Cone;
            cs.i = (int)std::floor(spot.x / TILE);
            cs.j = (int)std::floor(spot.z / TILE);
            float ground = g.world.terrainHeight((cs.i + 0.5f) * TILE, (cs.j + 0.5f) * TILE);
            cs.k = (int)std::floor((ground + 1.0f) / STORY);  // base within a metre of the ground
            int id = g.world.build.place(cs, true);
            if (Piece* pc = g.world.build.get(id)) pc->removed = 1 << 3;  // flatten the +x,+z quadrant
            AABB b = pieceBounds(cs);
            g.player.body.pos = b.center() + Vec3(-7.0f, 0, -7.0f);
            g.player.body.pos.y = g.world.terrainHeight(g.player.body.pos.x, g.player.body.pos.z) + 1.0f;
            g.player.body.vel = Vec3();
            press(r, 'F');
        }
        if (f > 352 && f < 380) {
            const Piece* pc = nullptr;
            for (const Piece& q : g.world.build.pieces()) if (q.alive && q.slot.type == PieceType::Cone) pc = &q;
            if (pc) {
                AABB b = pieceBounds(pc->slot);
                g.player.body.pos = b.center() + Vec3(-7.0f, 0, -7.0f);
                g.player.body.pos.y = g.world.terrainHeight(g.player.body.pos.x, g.player.body.pos.z) + 0.5f;
                g.player.body.vel = Vec3();
                lookAt(g, b.center() + Vec3(0, 0.3f, 0));
            }
        }
        if (f == 372) pendingShot = "06d_cone_edit.bmp";
        // Weapon icons in the hotbar.
        if (f == 382) {
            g.player.slots[0] = Item::makeGun(GunType::AssaultRifle, Rarity::Epic);
            g.player.slots[1] = Item::makeGun(GunType::Shotgun, Rarity::Rare);
            g.player.slots[2] = Item::makeGun(GunType::SMG, Rarity::Uncommon);
            g.player.slots[3] = Item::makeGun(GunType::Sniper, Rarity::Legendary);
            g.player.slots[4] = Item::makeMedKit(2);
            g.player.ammo[(int)AmmoType::Medium] = 120;
            press(r, '1');
        }
        // Team fight: bring one enemy squad in.
        if (f == 384) {
            Vec3 me = g.player.body.pos;
            Vec3 fwd(std::sin(g.player.yaw), 0, std::cos(g.player.yaw));
            int placed = 0;
            for (Bot& b : g.bots) {
                if (b.team == 0) park(g, b, me + Vec3(fwd.z, 0, -fwd.x) * (2.0f + b.index * 1.5f) - fwd * 1.0f);
                else if (b.team == 1 && placed < 4) park(g, b, me + fwd * (22.0f + placed * 2.0f) + Vec3(fwd.z, 0, -fwd.x) * (placed * 2.5f - 4.0f)), ++placed;
            }
            g.player.pitch = -0.02f;
        }
        // Inventory screen.
        if (f == 396) press(r, KEY_TAB);
        if (f == 400) fe.inventorySel = 1;
        if (f == 404) pendingShot = "07a_inventory.bmp";
        if (f == 406) press(r, KEY_TAB);
        if (f > 384 && f < 520 && (f < 396 || f > 406)) {
            hold(r, KEY_MOUSE_RIGHT);
            if (f % 9 == 0) press(r, KEY_MOUSE_LEFT);
            g.player.shield = 100;  // keep the camera alive for the photos
            g.player.health = 100;
        }
        if (f == 470) {
            // A teammate gets knocked in front of you; hold E to revive them.
            Bot& mate = g.bots[0];
            Vec3 fwd(std::sin(g.player.yaw), 0, std::cos(g.player.yaw));
            park(g, mate, g.player.body.pos + fwd * 2.4f + Vec3(fwd.z, 0, -fwd.x) * 1.2f);
            mate.yaw = g.player.yaw;   // facing away, so the knocked pose doesn't tip into the camera
            g.damageActor(mate.index, 500, false, mate.body.pos, NO_TARGET, "");
        }
        if (f > 470 && f < 500) hold(r, 'E');
        if (f == 490) pendingShot = "07c_knocked_revive.bmp";
        if (f == 500) g.revive(0);
        if (f == 440) press(r, 'R');  // reload animation
        if (f == 452) pendingShot = "07b_reload_anim.bmp";
        if (f == 460) pendingShot = "07_teamfight_icons.bmp";
        // Pause menu.
        if (f == 522) press(r, KEY_ESCAPE);
        if (f == 530) pendingShot = "08_pause.bmp";
        if (f == 532) press(r, KEY_ESCAPE);
        // Win the match.
        if (f == 535)
            for (Bot& b : g.bots)
                if (b.team != 0 && b.alive) g.damageActor(b.index, 999, false, b.body.pos, PLAYER_ACTOR_ID, "SNIPER");
        if (f == 640) pendingShot = "09_victory.bmp";
        if (f >= stopAt) done = true;
        return r;
    }

    void afterRender(FrontEnd& fe, RendererD3D11& r) {
        if (pendingShot) {
            std::string path = dir + "\\" + pendingShot;
            bool ok = r.saveScreenshot(path.c_str());
            if (log) std::fprintf(log, "frame %d: screenshot %s %s\n", frame, pendingShot, ok ? "saved" : "FAILED");
        }
        const Game& g = fe.game;
        if (log && (frame == 140 || frame == 330 || frame == 520 || frame == 645)) {
            std::fprintf(log,
                         "frame %d: screen=%d mode=%s players=%d teams=%d mats=%d stamina=%.0f kills=%d teamAlive=%d "
                         "victory=%d placement=%d poi=%d triangles=%u anim=%s\n",
                         frame, (int)fe.screen, teamModeName(g.config.teamSize), g.playersAlive(), g.teamsAlive(),
                         g.player.matCount[0], g.player.stamina, g.player.kills, g.playerTeamAlive(), g.victory(), g.placement,
                         g.currentPoi, r.lastTriangles(), animName(playerAnim(g)));
            for (const Bot& b : g.bots)
                if (b.team == 0) std::fprintf(log, "   teammate %s alive=%d hp=%.0f kills=%d\n", b.name.c_str(), b.alive, b.health, b.kills);
        }
    }
};

}  // namespace

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR cmdLine, int) {
    SetProcessDPIAware();

    std::string args = cmdLine ? cmdLine : "";
    Demo demo;
    bool demoMode = false;
    {
        size_t at = args.find("--shots");
        if (at != std::string::npos) {
            demoMode = true;
            demo.dir = args.substr(at + 7);
            size_t next = demo.dir.find(" --");
            if (next != std::string::npos) demo.dir = demo.dir.substr(0, next);
            while (!demo.dir.empty() && (demo.dir[0] == ' ' || demo.dir[0] == '"')) demo.dir.erase(0, 1);
            while (!demo.dir.empty() && (demo.dir.back() == ' ' || demo.dir.back() == '"')) demo.dir.pop_back();
            if (demo.dir.empty()) demo.dir = ".";
            CreateDirectoryA(demo.dir.c_str(), nullptr);
            demo.log = std::fopen((demo.dir + "\\blockfall_log.txt").c_str(), "w");
        }
        size_t until = args.find("--until");
        if (until != std::string::npos) demo.stopAt = std::atoi(args.c_str() + until + 7);
    }

    App app;
    g_app = &app;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"BlockfallWindow";
    RegisterClassExW(&wc);

    if (demoMode) { app.width = 1280; app.height = 720; }
    RECT r = {0, 0, app.width, app.height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Blockfall", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInst, nullptr);
    if (!app.hwnd) return 1;
    ShowWindow(app.hwnd, SW_SHOW);
    GetClientRect(app.hwnd, &r);
    app.width = r.right - r.left;
    app.height = r.bottom - r.top;

    std::string err;
    if (!app.renderer.init(app.hwnd, app.width, app.height, err)) {
        if (demo.log) { std::fprintf(demo.log, "renderer init failed: %s\n", err.c_str()); std::fclose(demo.log); }
        MessageBoxA(app.hwnd, err.c_str(), "Blockfall - Direct3D 11 error", MB_ICONERROR);
        return 1;
    }
    if (demo.log) std::fprintf(demo.log, "renderer ok: %s, %dx%d\n", app.renderer.adapterName(), app.width, app.height);
    {
        std::string title = std::string("Blockfall  -  Direct3D 11  -  ") + app.renderer.adapterName();
        SetWindowTextA(app.hwnd, title.c_str());
    }
    app.input.init(app.hwnd);

    // The rigged character: assets/ next to the exe, or one or two folders up (bin\ in the zip,
    // build folders from CMake / Visual Studio).
    CharacterModel charModel;
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string dir = exe;
        size_t slash = dir.find_last_of("\\/");
        dir = slash == std::string::npos ? "." : dir.substr(0, slash);
        const char* tries[] = {"\\assets", "\\..\\assets", "\\..\\..\\assets", "\\..\\..\\..\\assets"};
        std::string found;
        for (const char* t : tries) {
            std::string path = dir + t + "\\character.glb";
            FILE* f = std::fopen(path.c_str(), "rb");
            if (f) { std::fclose(f); found = dir + t; break; }
        }
        if (found.empty()) {
            FILE* f = std::fopen("assets\\character.glb", "rb");
            if (f) { std::fclose(f); found = "assets"; }
        }
        std::string merr;
        if (found.empty()) {
            if (demo.log) std::fprintf(demo.log, "character model: assets folder not found, using the built-in figures\n");
        } else if (!charModel.load(found + "\\character.glb", merr) || !app.renderer.setCharacterModel(charModel, found + "\\skins", merr)) {
            if (demo.log) std::fprintf(demo.log, "character model: %s (using the built-in figures)\n", merr.c_str());
            charModel.loaded = false;
        } else {
            setCharacterModel(&charModel);
            std::string terr;
            SetWindowTextA(app.hwnd, "Blockfall  -  loading textures...");
            if (!app.renderer.loadMaterialTextures(found + "\\textures", terr)) {
                if (demo.log) std::fprintf(demo.log, "material textures: %s (using shader bumps)\n", terr.c_str());
            } else if (demo.log) {
                std::fprintf(demo.log, "material textures: loaded from %s\\textures\n", found.c_str());
            }
            if (demo.log) std::fprintf(demo.log, "character model: %s, %d meshes, %d bones, %d clips, %d skins\n", found.c_str(),
                                       (int)charModel.meshes.size(), (int)charModel.bones.size(), (int)charModel.clips.size(),
                                       app.renderer.skinTextureCount());
        }
    }

    {
        std::string title = std::string("Blockfall  -  Direct3D 11  -  ") + app.renderer.adapterName();
        SetWindowTextA(app.hwnd, title.c_str());
    }

    Audio audio;
    {
        std::string aerr;
        bool ok = audio.init(aerr);
        if (demo.log) std::fprintf(demo.log, "audio: %s\n", ok ? "XAudio2 ready" : aerr.c_str());
    }

    FrontEnd fe;
    fe.init(demoMode ? demo.dir + "\\demo_settings.ini" : settingsPath());
    SceneList scene;
    HudBuilder hud;
    std::vector<uint32_t> mapImage;
    int mapGeneration = -1;
    double last = nowSeconds();
    double fpsTimer = 0, fpsShown = 60;
    int fpsFrames = 0;
    double demoStart = last;

    while (app.running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) app.running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!app.running) break;
        if (app.resized) {
            app.renderer.resize(app.width, app.height);
            app.resized = false;
        }

        double now = nowSeconds();
        float dt = (float)(now - last);
        last = now;
        fpsTimer += dt;
        ++fpsFrames;
        if (fpsTimer >= 0.5) {
            fpsShown = fpsFrames / fpsTimer;
            fpsTimer = 0;
            fpsFrames = 0;
        }

        RawInput in = app.input.takeFrame();
        if (demoMode) {
            in = demo.step(fe, in);
            dt = 1.0f / 60.0f;
        }

        bool visible = app.renderer.width() > 0 && app.renderer.height() > 0 && !IsIconic(app.hwnd);
        fe.frame(in, dt, (float)app.renderer.width(), (float)std::max(1, app.renderer.height()), (float)fpsShown, scene, hud);
        if (!demoMode) app.input.setLocked(fe.wantsMouseLocked());
        if (fe.quitRequested) app.running = false;

        // Play the sounds the game made this frame, heard from the camera.
        if (demo.log && !fe.game.sounds.empty()) demo.soundEvents += (int)fe.game.sounds.size();
        audio.setVolume(fe.settings.volume);
        audio.play(fe.game.sounds, fe.game.camera.eye, fe.game.camera.right);
        fe.game.sounds.clear();

        if (visible) {
            app.renderer.setGraphics(fe.settings.gfx);
            app.renderer.setTerrain(fe.game.world);
            if (fe.game.world.generation != mapGeneration) {
                buildMapImage(fe.game.world, MAP_IMAGE_SIZE, mapImage);
                mapGeneration = fe.game.world.generation;
                app.renderer.setMapImage(mapImage, MAP_IMAGE_SIZE, mapGeneration);
            }
            // No depth of field while playing: aiming down sights must stay sharp.
            app.renderer.render(scene, hud.quads, 0.0f);
            if (demoMode) demo.afterRender(fe, app.renderer);
            app.renderer.present(fe.settings.vsync && !demoMode);
        } else {
            Sleep(10);
        }

        if (demoMode) {
            ++demo.frame;
            if (demo.done) {
                if (demo.log) {
                    double secs = nowSeconds() - demoStart;
                    std::fprintf(demo.log, "sound events: %d\n", demo.soundEvents);
                    std::fprintf(demo.log, "demo finished: %d frames in %.2fs\n", demo.frame, secs);
                    std::fclose(demo.log);
                    demo.log = nullptr;
                }
                app.running = false;
            }
        }
    }

    app.input.setLocked(false);
    audio.shutdown();
    app.renderer.shutdown();
    DestroyWindow(app.hwnd);
    return 0;
}
