// Blockfall - match setup, teams, camera, damage, inventory, chests and pickups
#include "game.h"
#include "loot.h"
#include "skins.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace bf {

namespace {
// Random player names for bots (made-up word pairs).
std::string makeBotName(Rng& rng, std::set<std::string>& used) {
    static const char* first[] = {"SWIFT", "QUIET", "LUCKY", "BRAVE", "ROGUE", "NOVA",  "PIXEL", "COBALT",
                                  "EMBER", "FROST", "SUNNY", "MOSSY", "TURBO", "CRISP", "DUSTY", "NEON"};
    static const char* second[] = {"FOX",  "OWL",   "WOLF", "HAWK",  "BEAR",  "LYNX", "OTTER", "VIPER",
                                   "MOTH", "CRANE", "BISON", "GECKO", "RAVEN", "PIKE", "BADGER", "KOI"};
    for (int tries = 0; tries < 50; ++tries) {
        std::string n = std::string(first[rng.rangeInt(0, 15)]) + "_" + second[rng.rangeInt(0, 15)];
        if (rng.chance(0.4f)) n += std::to_string(rng.rangeInt(1, 99));
        if (!used.count(n)) {
            used.insert(n);
            return n;
        }
    }
    std::string n = "BOT_" + std::to_string(used.size() + 1);
    used.insert(n);
    return n;
}
}  // namespace

Vec3 Player::forward() const {
    float cp = std::cos(pitch);
    return {cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)};
}

Game::Game(uint64_t seed) { reset(seed); }

void Game::reset(uint64_t seed) {
    MatchConfig cfg;
    cfg.seed = seed;
    startMatch(cfg);
}

void Game::startMatch(const MatchConfig& cfgIn) {
    config = cfgIn;
    config.teamSize = std::max(1, std::min(4, config.teamSize));
    world.generate(config.seed);
    rng.seed(config.seed * 7919u + 17u);

    player = Player();
    player.name = config.playerName.empty() ? "PLAYER" : config.playerName;
    player.skin = config.skin;
    player.body.pos = world.playerSpawn;
    player.yaw = 0.0f;
    // You drop in with your pickaxe out, a Common Pistol, and a few materials.
    player.pickaxeOut = true;
    player.slots[0] = Item::makeGun(GunType::Pistol, Rarity::Common);
    player.ammo[(int)AmmoType::Light] = 48;

    bots.clear();
    std::set<std::string> used;
    used.insert(player.name);
    for (const std::string& f : config.party) used.insert(f);
    static const GunType botGuns[] = {GunType::AssaultRifle, GunType::SMG, GunType::Pistol, GunType::Shotgun};
    int teams = teamCountFor(config.teamSize);
    for (int t = 0; t < teams; ++t) {
        Vec3 base = t == 0 ? world.playerSpawn
                           : world.enemySpawns[(t - 1) % std::max<size_t>(1, world.enemySpawns.size())];
        if (t > 0 && (size_t)(t - 1) >= world.enemySpawns.size()) base += Vec3(8, 0, 8);  // more teams than spots
        int members = config.teamSize - (t == 0 ? 1 : 0);
        for (int m = 0; m < members; ++m) {
            Bot b;
            b.index = (int)bots.size();
            b.team = t;
            if (t == 0 && m < (int)config.party.size()) {
                b.name = config.party[m];
                b.isFriend = true;
            } else {
                b.name = makeBotName(rng, used);
            }
            float a = (float)(m + (t == 0 ? 1 : 0)) * 2.0f * PI / (float)config.teamSize + 0.4f;
            Vec3 p = base + Vec3(std::sin(a) * 2.5f, 0, std::cos(a) * 2.5f);
            if (config.teamSize == 1) p = base;
            p.y = world.terrainHeight(p.x, p.z);
            b.body.pos = p;
            b.yaw = rng.range(0, 2 * PI);
            b.gun = Item::makeGun(botGuns[rng.rangeInt(0, 3)], rng.chance(0.7f) ? Rarity::Common : Rarity::Uncommon);
            b.skin = rng.rangeInt(0, skinCount() - 1);
            // Play style: teammates fight or mix; enemies are a spread of all three.
            float roll = rng.range(0, 1);
            if (t == 0) b.style = roll < 0.5f ? BotStyle::Fighter : BotStyle::Mixed;
            else b.style = roll < 0.4f ? BotStyle::Fighter : (roll < 0.7f ? BotStyle::Hider : BotStyle::Mixed);
            b.mats[0] = b.style == BotStyle::Fighter ? 250 : 150;
            b.mats[1] = b.style == BotStyle::Hider ? 150 : 50;
            b.mats[2] = b.style == BotStyle::Hider ? 50 : 0;
            b.moveTarget = b.body.pos;
            b.losTimer = rng.range(0, 0.3f);
            bots.push_back(b);
        }
    }

    damageNumbers.clear();
    damageStacks.clear();
    tracers.clear();
    killFeed.clear();
    toast = Message();
    toast.age = 99;
    prompt = InteractPrompt();
    buildTarget = BuildTarget();
    hitMarker = 0;
    time = 0;
    spectating = -1;
    placement = 0;
    currentPoi = -1;
    poiBannerTime = 0;
    // Battle bus: a straight line across the map, picked at random.
    {
        float a = rng.range(0, 2 * PI);
        Vec3 d(std::sin(a), 0, std::cos(a));
        Vec3 side(d.z, 0, -d.x);
        float off = rng.range(-MAP_HALF * 0.3f, MAP_HALF * 0.3f);
        bus = Bus();
        bus.flying = true;
        bus.start = side * off - d * (MAP_HALF + 20.0f) + Vec3(0, BUS_HEIGHT, 0);
        bus.end = side * off + d * (MAP_HALF + 20.0f) + Vec3(0, BUS_HEIGHT, 0);
        bus.t = 0;
        dropPhase = DropPhase::OnBus;
        player.body.pos = bus.pos() + d * 4.0f + Vec3(0, 3.0f, 0);  // on the roof, at the front
        player.yaw = std::atan2(d.x, d.z);
        for (Bot& b : bots) {
            b.drop = DropPhase::OnBus;
            b.dropAt = b.team == 0 ? 2.0f : rng.range(0.15f, 0.85f);  // teammates wait and jump with you
            // Land somewhere along the path, a little to the side, or at your spot for teammates.
            float along = clampf(b.dropAt + rng.range(0.05f, 0.25f), 0.1f, 0.95f);
            b.landing = lerp(bus.start, bus.end, along) + side * rng.range(-MAP_HALF * 0.4f, MAP_HALF * 0.4f);
            b.landing.x = clampf(b.landing.x, -MAP_HALF + 20, MAP_HALF - 20);
            b.landing.z = clampf(b.landing.z, -MAP_HALF + 20, MAP_HALF - 20);
            b.landing.y = world.terrainHeight(b.landing.x, b.landing.z);
            b.body.pos = bus.pos();
        }
    }
    if (!config.useBus) landEveryone();
    storm = Storm();
    storm.center = storm.nextCenter = Vec3(0, 0, 0);
    storm.radius = storm.nextRadius = MAP_HALF * 1.5f;
    startStormStage(0);
    updateCamera();
}

// ------------------------------------------------------------------ match status
bool Game::playerTeamAlive() const {
    if (player.alive) return true;
    for (const Bot& b : bots)
        if (b.alive && b.team == 0) return true;
    return false;
}

int Game::teamsAlive() const {
    std::set<int> teams;
    if (player.alive) teams.insert(0);
    for (const Bot& b : bots)
        if (b.alive) teams.insert(b.team);
    return (int)teams.size();
}

int Game::playersAlive() const {
    int n = player.alive ? 1 : 0;
    for (const Bot& b : bots) n += b.alive ? 1 : 0;
    return n;
}

int Game::enemiesAlive() const {
    int n = 0;
    for (const Bot& b : bots) n += (b.alive && b.team != 0) ? 1 : 0;
    return n;
}

const Item& Game::heldItem() const {
    static const Item pick = Item::makePickaxe();
    return player.pickaxeOut ? pick : player.slots[player.selected];
}

std::string Game::actorName(int id) const {
    if (id == PLAYER_ACTOR_ID) return player.name;
    if (id >= 0 && id < (int)bots.size()) return bots[id].name;
    return "THE STORM";
}
int Game::actorTeam(int id) const {
    if (id == PLAYER_ACTOR_ID) return 0;
    if (id >= 0 && id < (int)bots.size()) return bots[id].team;
    return -1;
}
bool Game::actorAlive(int id) const {
    if (id == PLAYER_ACTOR_ID) return player.alive;
    if (id >= 0 && id < (int)bots.size()) return bots[id].alive;
    return false;
}
bool Game::actorDown(int id) const {
    if (id == PLAYER_ACTOR_ID) return player.alive && player.down;
    if (id >= 0 && id < (int)bots.size()) return bots[id].alive && bots[id].down;
    return false;
}
bool Game::teamStanding(int team, int except) const {
    if (team == 0 && except != PLAYER_ACTOR_ID && player.alive && !player.down) return true;
    for (const Bot& b : bots)
        if (b.team == team && b.index != except && b.alive && !b.down) return true;
    return false;
}
Vec3 Game::actorPos(int id) const {
    if (id == PLAYER_ACTOR_ID) return player.body.pos;
    if (id >= 0 && id < (int)bots.size()) return bots[id].body.pos;
    return Vec3();
}

// ------------------------------------------------------------------ update
void Game::update(const InputState& in, float dt) {
    dt = clampf(dt, 0.0f, 0.05f);
    time += dt;
    if (in.helpPressed) showHelp = !showHelp;
    if (in.restartPressed && matchOver()) {
        MatchConfig next = config;
        next.seed = config.seed + 1;
        startMatch(next);
        return;
    }

    world.build.update(dt);
    world.updatePickups(dt);
    world.updateProps(dt);
    for (Chest& c : world.chests)
        if (c.opened) c.openAnim = std::min(1.0f, c.openAnim + dt * 3.0f);

    updateBus(in, dt);
    if (player.alive && dropPhase == DropPhase::Landed) updatePlayer(in, dt);
    updateBots(dt);
    updateKnocked(dt);
    if (!player.alive) updateSpectate();
    updatePoi();
    settleBuilds();
    updateStorm(dt);
    // Which build is under the crosshair? (for the health bar)
    aimedPiece = -1;
    if (player.alive) {
        Ray r{camera.aimRay.at(camera.aimStartT), camera.aimRay.dir};
        HitInfo h;
        if (world.raycast(r, 40.0f, h, false) && h.kind == HitKind::Piece) aimedPiece = h.id;
    }
    poiBannerTime = std::max(0.0f, poiBannerTime - dt);
    player.shotAnim = std::max(0.0f, player.shotAnim - dt);
    player.buildAnim = std::max(0.0f, player.buildAnim - dt);
    for (Bot& b : bots) b.shotAnim = std::max(0.0f, b.shotAnim - dt);

    if (placement == 0 && matchOver()) placement = victory() ? 1 : teamsAlive() + 1;

    // Effects.
    hitMarker = std::max(0.0f, hitMarker - dt);
    player.hurtTimer = std::max(0.0f, player.hurtTimer - dt);
    for (auto& d : damageNumbers) { d.age += dt; d.pos.y += dt * 1.2f; }
    for (auto& d : damageStacks) {
        d.sinceHit += dt;
        if (d.victim >= 0 && d.victim < (int)bots.size()) d.pos = bots[d.victim].body.pos + Vec3(0, 2.4f, 0);
    }
    damageStacks.erase(std::remove_if(damageStacks.begin(), damageStacks.end(),
                                      [](const DamageStack& d) { return d.sinceHit > 1.0f; }),
                       damageStacks.end());
    for (auto& t : tracers) t.age += dt;
    for (auto& m : killFeed) m.age += dt;
    // Shockwave grenades fly in an arc, stop where they land (or hit something), sit there for
    // SHOCKWAVE_FUSE seconds, then go off.
    for (Grenade& gr : grenades) {
        if (!gr.alive) continue;
        gr.age += dt;
        if (gr.fuse >= 0) {
            gr.fuse -= dt;
            if (gr.fuse <= 0) {
                explodeShockwave(gr.pos);
                gr.alive = false;
            }
            continue;
        }
        Vec3 next = gr.pos + gr.vel * dt;
        gr.vel.y -= 18.0f * dt;
        HitInfo h;
        Vec3 d = next - gr.pos;
        float len = length(d);
        bool hit = len > 1e-4f && world.raycast(Ray{gr.pos, d / len}, len, h, false);
        float ground = world.terrainHeight(next.x, next.z);
        if (hit || next.y <= ground || gr.age > 4.0f) {
            gr.pos = hit ? h.point + h.normal * 0.15f : Vec3(next.x, std::max(next.y, ground + 0.15f), next.z);
            gr.vel = Vec3();
            gr.fuse = SHOCKWAVE_FUSE;
        } else {
            gr.pos = next;
        }
    }
    grenades.erase(std::remove_if(grenades.begin(), grenades.end(), [](const Grenade& g) { return !g.alive; }), grenades.end());
    for (auto& r : shockRings) r.age += dt;
    shockRings.erase(std::remove_if(shockRings.begin(), shockRings.end(), [](const ShockRing& r) { return r.age > 0.6f; }), shockRings.end());
    toast.age += dt;
    damageNumbers.erase(std::remove_if(damageNumbers.begin(), damageNumbers.end(),
                                       [](const DamageNumber& d) { return d.age > 1.0f; }),
                        damageNumbers.end());
    tracers.erase(std::remove_if(tracers.begin(), tracers.end(), [](const Tracer& t) { return t.age > 0.08f; }),
                  tracers.end());
    killFeed.erase(std::remove_if(killFeed.begin(), killFeed.end(), [](const Message& m) { return m.age > 6.0f; }),
                   killFeed.end());
}

// ------------------------------------------------------------------ camera
void Game::setCameraFrom(const Vec3& pivot, float yaw, float pitch, bool ads, float fovDeg) {
    float cp = std::cos(pitch);
    Vec3 f(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
    Vec3 right = normalize(cross(Vec3(0, 1, 0), f));
    if (length(right) < 0.5f) right = Vec3(std::cos(yaw), 0, -std::sin(yaw));
    Vec3 up = cross(f, right);

    // Over the right shoulder: the character sits left of the crosshair.
    float dist = ads ? 2.2f : 3.0f;
    float side = ads ? 0.95f : 0.95f;
    float lift = ads ? 0.2f : 0.3f;
    Vec3 shoulder = pivot + right * 0.15f + Vec3(0, lift, 0);
    Vec3 desired = pivot + right * side + Vec3(0, lift, 0) - f * dist;

    // Keep the camera out of walls.
    Vec3 toCam = desired - shoulder;
    float len = length(toCam);
    Vec3 eye = desired;
    if (len > 1e-3f) {
        Ray r{shoulder, toCam / len};
        HitInfo h;
        if (world.raycast(r, len + 0.25f, h, true)) eye = shoulder + r.dir * std::max(0.1f, h.t - 0.25f);
    }

    camera.eye = eye;
    camera.forward = f;
    camera.right = right;
    camera.up = up;
    camera.fovY = deg2rad(fovDeg);
    camera.aimRay = Ray{eye, f};
    // Ignore anything between the camera and the player (e.g. a wall behind you).
    camera.aimStartT = std::max(0.0f, dot(pivot - eye, f) - 0.25f);
}

void Game::updateCamera() {
    const Item& held = heldItem();
    bool sniperZoom = player.ads && held.kind == ItemKind::Gun && held.gun == GunType::Sniper;
    setCameraFrom(player.eyePivot(), player.yaw, player.pitch, player.ads,
                  sniperZoom ? 30.0f : (player.ads ? 55.0f : 75.0f));
    // The bus is not part of the world, so the wall check can't keep the camera out of it.
    // Keep the eye above the roof while you stand on it (looking up used to put it inside).
    if (player.alive && dropPhase == DropPhase::OnBus) {
        float roofTop = bus.pos().y + 2.9f + 0.4f;
        if (camera.eye.y < roofTop) {
            camera.eye.y = roofTop;
            camera.aimRay.origin = camera.eye;
        }
    }
}

void Game::updatePoi() {
    Vec3 p = player.alive ? player.body.pos : (spectating >= 0 ? bots[spectating].body.pos : player.body.pos);
    int now = world.poiAt(p.x, p.z);
    if (now != currentPoi) {
        currentPoi = now;
        if (now >= 0) {
            Poi& poi = world.pois[now];
            poiBannerNew = !poi.discovered;
            poi.discovered = true;
            poiBanner = poi.name;
            poiBannerTime = 3.5f;
        }
    }
}

// One body falling from the bus: a fast skydive, then a slow glide near the ground.
void Game::updateDrop(Body& body, DropPhase& phase, const Vec3& steer, float dt) {
    float ground = world.groundHeight(body.pos.x, body.pos.z, body.radius, body.pos.y, 0.0f);
    float above = body.pos.y - ground;
    if (phase == DropPhase::Skydive && above < 40.0f) phase = DropPhase::Glide;
    float maxFall = phase == DropPhase::Glide ? 6.0f : 28.0f;
    float airSpeed = phase == DropPhase::Glide ? 9.0f : 14.0f;
    // Steer horizontally, with momentum.
    Vec3 want = steer * airSpeed;
    Vec3 hv(body.vel.x, 0, body.vel.z);
    Vec3 diff = want - hv;
    float dl = length(diff), maxChange = 12.0f * dt;
    if (dl > maxChange) diff = diff * (maxChange / dl);
    body.vel.x += diff.x;
    body.vel.z += diff.z;
    // Gravity, capped (moveBody adds gravity * dt itself).
    body.vel.y = std::max(body.vel.y, -maxFall + 22.0f * dt);
    body.grounded = false;
    std::vector<AABB> none;
    world.moveBody(body, dt, none);
    if (body.grounded) {
        phase = DropPhase::Landed;
        body.vel = Vec3();
        body.airborne = false;   // the glider landing is gentle: no fall damage
    }
}

void Game::landEveryone() {
    bus.flying = false;
    bus.t = 1.0f;
    dropPhase = DropPhase::Landed;
    player.body.pos = world.playerSpawn;
    player.body.vel = Vec3();
    player.yaw = 0;
    player.pitch = 0;
    for (Bot& b : bots) {
        b.drop = DropPhase::Landed;
        int t = b.team;
        Vec3 base = t == 0 ? world.playerSpawn : world.enemySpawns[(t - 1) % std::max<size_t>(1, world.enemySpawns.size())];
        int m = 0;
        for (const Bot& o : bots) { if (o.index == b.index) break; if (o.team == t) ++m; }
        float a = (float)(m + (t == 0 ? 1 : 0)) * 2.0f * PI / (float)config.teamSize + 0.4f;
        Vec3 p = base + Vec3(std::sin(a) * 2.5f, 0, std::cos(a) * 2.5f);
        if (config.teamSize == 1) p = base;
        p.y = world.terrainHeight(p.x, p.z);
        b.body.pos = p;
        b.body.vel = Vec3();
        b.moveTarget = p;
    }
}

void Game::updateBus(const InputState& in, float dt) {
    busJumpCooldown = std::max(0.0f, busJumpCooldown - dt);
    if (bus.flying) {
        bus.t += dt / bus.duration;
        if (bus.t >= 1.0f) {
            bus.t = 1.0f;
            bus.flying = false;
        }
    }
    // You.
    if (player.alive && dropPhase == DropPhase::OnBus) {
        player.body.pos = bus.pos() + bus.dir() * 4.0f + Vec3(0, 3.0f, 0);
        player.body.vel = Vec3();
        // Look around freely.
        float sens = mouseSensitivity;
        player.yaw += in.mouseDx * sens;
        player.pitch = clampf(player.pitch - in.mouseDy * sens, deg2rad(-85.0f), deg2rad(85.0f));
        bool forced = !bus.flying;
        if ((in.jumpPressed && bus.t > 0.03f) || forced) {
            dropPhase = DropPhase::Skydive;
            player.body.pos = bus.pos() + Vec3(0, 1.0f, 0);
            player.body.vel = bus.dir() * 8.0f;
            showToast(forced ? "LAST STOP - EVERYONE OFF" : "JUMPED - STEER WITH WASD, GLIDER OPENS NEAR THE GROUND");
            for (Bot& b : bots)
                if (b.team == 0 && b.drop == DropPhase::OnBus) b.dropAt = bus.t;  // teammates go with you
        }
        updateCamera();
    } else if (player.alive && dropPhase != DropPhase::Landed) {
        float sens = mouseSensitivity;
        player.yaw += in.mouseDx * sens;
        player.pitch = clampf(player.pitch - in.mouseDy * sens, deg2rad(-85.0f), deg2rad(85.0f));
        Vec3 fwd(std::sin(player.yaw), 0, std::cos(player.yaw));
        Vec3 right(std::cos(player.yaw), 0, -std::sin(player.yaw));
        Vec3 steer = fwd * (float)((in.forward ? 1 : 0) - (in.back ? 1 : 0)) + right * (float)((in.right ? 1 : 0) - (in.left ? 1 : 0));
        if (length(steer) > 0.01f) steer = normalize(steer);
        DropPhase before = dropPhase;
        updateDrop(player.body, dropPhase, steer, dt);
        if (before != DropPhase::Glide && dropPhase == DropPhase::Glide) showToast("GLIDER OPEN");
        if (dropPhase == DropPhase::Landed) {
            player.pickaxeOut = true;
            showToast("LANDED - GRAB LOOT AND HARVEST MATS");
        }
        updateCamera();
    }
    // Bots.
    for (Bot& b : bots) {
        if (!b.alive) continue;
        if (b.drop == DropPhase::OnBus) {
            b.body.pos = bus.pos() + bus.dir() * 4.0f + Vec3(0, 3.0f, 0);
            if (bus.t >= b.dropAt || !bus.flying) {
                b.drop = DropPhase::Skydive;
                b.body.pos = bus.pos() + Vec3(rng.range(-2, 2), 1.0f, rng.range(-2, 2));
                b.body.vel = bus.dir() * 8.0f;
            }
        } else if (b.drop != DropPhase::Landed) {
            Vec3 to(b.landing.x - b.body.pos.x, 0, b.landing.z - b.body.pos.z);
            float d = length(to);
            Vec3 steer = d > 3.0f ? to / d : Vec3();
            updateDrop(b.body, b.drop, steer, dt);
            if (length(steer) > 0) b.yaw = std::atan2(steer.x, steer.z);
            if (b.drop == DropPhase::Landed) {
                b.state = AIState::Wander;
                b.moveTarget = b.body.pos;
            }
        }
    }
}

float Game::checkLanding(Body& b, int actorId, float dt) {
    b.fallImmune = std::max(0.0f, b.fallImmune - dt);
    if (!b.grounded) {
        if (!b.airborne) { b.airborne = true; b.fallPeak = b.pos.y; }
        b.fallPeak = std::max(b.fallPeak, b.pos.y);
        return 0;
    }
    if (!b.airborne) return 0;
    b.airborne = false;
    float drop = b.fallPeak - b.pos.y;
    if (b.fallImmune > 0 || drop <= FALL_SAFE_DROP) return 0;
    float dmg = std::min(FALL_MAX_DAMAGE, (drop - FALL_SAFE_DROP) * FALL_DAMAGE_PER_M);
    damageActor(actorId, dmg, false, b.pos + Vec3(0, 1, 0), NO_TARGET, "FALL");
    if (actorId == PLAYER_ACTOR_ID) showToast("FALL DAMAGE -" + std::to_string((int)dmg), Vec4(1, 0.5f, 0.4f, 1));
    return dmg;
}

void Game::throwShockwave() {
    Grenade g;
    Ray aim{camera.aimRay.at(camera.aimStartT), camera.aimRay.dir};
    g.pos = player.body.pos + Vec3(0, 1.5f, 0) + aim.dir * 0.8f;
    g.vel = aim.dir * 20.0f + Vec3(0, 4.0f, 0) + player.body.vel * 0.5f;
    grenades.push_back(g);
    player.shotAnim = 0.12f;
}

// Everyone near the blast (you too) gets launched away and up. No damage, like the real thing.
void Game::explodeShockwave(const Vec3& at) {
    shockRings.push_back({at, 0});
    auto push = [&](Body& body) {
        Vec3 c = body.pos + Vec3(0, 0.9f, 0);
        float d = length(c - at);
        if (d > SHOCKWAVE_RADIUS) return;
        Vec3 dir = d > 0.2f ? (c - at) / d : Vec3(0, 1, 0);
        dir.y = std::max(dir.y, 0.0f);
        float k = 1.0f - 0.35f * (d / SHOCKWAVE_RADIUS);
        body.vel = Vec3(dir.x * SHOCKWAVE_PUSH * k, SHOCKWAVE_LIFT * k + std::max(0.0f, dir.y) * 4.0f, dir.z * SHOCKWAVE_PUSH * k);
        body.pos.y += 0.05f;
        body.grounded = false;
        body.fallImmune = 5.0f;  // no fall damage after a shockwave, like the real thing
    };
    if (player.alive) push(player.body);
    for (Bot& b : bots)
        if (b.alive && b.drop == DropPhase::Landed) {
            push(b.body);
            if (length(b.body.pos + Vec3(0, 0.9f, 0) - at) <= SHOCKWAVE_RADIUS) b.stuckTime = 0;
        }
}

void Game::settleBuilds() {
    // Only worth checking when a piece or prop went away since last frame.
    int alive = world.build.aliveCount();
    int props = 0;
    for (const PropGroup& g : world.groups) props += g.alive ? 1 : 0;
    if (alive == lastAliveBuilds && props == lastAliveProps && !buildsDirty) return;
    buildsDirty = false;
    std::vector<int> broken;
    int n = world.breakUnsupported(&broken);
    if (n > 0) {
        for (int id : broken) {
            const Piece* p = world.build.get(id);
            if (p) damageNumbers.push_back({pieceBounds(p->slot).center(), 0, Vec4(1, 0.6f, 0.4f, 1), 0, false});
        }
        showToast(n == 1 ? "A FLOATING BUILD BROKE" : std::to_string(n) + " FLOATING BUILDS BROKE", Vec4(1, 0.6f, 0.4f, 1));
    }
    lastAliveBuilds = world.build.aliveCount();
    lastAliveProps = props;
}

// Stage table: wait, then shrink. Radius and timings get tighter each stage.
void Game::startStormStage(int stage) {
    static const struct { float wait, shrink, radius, dps; } stages[] = {
        // About 12 minutes of storm in all (waits 445 s + shrinks 280 s), then the last circle closes.
        {150, 80, 300, 1}, {105, 60, 170, 1}, {75, 45, 90, 2}, {55, 35, 45, 3}, {35, 30, 18, 5}, {25, 30, 0, 8},
    };
    const int n = (int)(sizeof(stages) / sizeof(stages[0]));
    storm.stage = std::min(stage, n - 1);
    const auto& st = stages[storm.stage];
    storm.shrinking = false;
    storm.timer = storm.stageLength = st.wait;
    storm.damagePerSecond = st.dps;
    // Pick the next circle inside the current one.
    float r = st.radius;
    float maxOff = std::max(0.0f, storm.radius - r);
    float a = rng.range(0, 2 * PI), d = rng.range(0, std::min(maxOff, MAP_HALF * 0.4f));
    storm.nextCenter = storm.center + Vec3(std::sin(a) * d, 0, std::cos(a) * d);
    storm.nextCenter.x = clampf(storm.nextCenter.x, -MAP_HALF + r, MAP_HALF - r);
    storm.nextCenter.z = clampf(storm.nextCenter.z, -MAP_HALF + r, MAP_HALF - r);
    storm.nextRadius = r;
}

void Game::updateStorm(float dt) {
    static const float shrinkTimes[] = {80, 60, 45, 35, 30, 30};
    Storm& st = storm;
    st.timer -= dt;
    if (!st.shrinking && st.timer <= 0) {
        st.shrinking = true;
        st.timer = st.stageLength = shrinkTimes[std::min(st.stage, 5)];
        addKillMessage("THE STORM IS SHRINKING", Vec4(0.8f, 0.5f, 1, 1));
    } else if (st.shrinking) {
        // Move smoothly toward the next circle.
        st.center = lerp(st.center, st.nextCenter, clampf(dt / std::max(st.timer, dt), 0, 1));
        st.radius = lerpf(st.radius, st.nextRadius, clampf(dt / std::max(st.timer, dt), 0, 1));
        if (st.timer <= 0) {
            st.center = st.nextCenter;
            st.radius = st.nextRadius;
            if (st.stage < 5) startStormStage(st.stage + 1);
            else { st.shrinking = false; st.timer = 1e9f; }
        }
    }
    // Damage once a second to anyone outside.
    playerInStorm = player.alive && !st.inside(player.body.pos.x, player.body.pos.z);
    st.tickTimer += dt;
    if (st.tickTimer >= 1.0f) {
        st.tickTimer -= 1.0f;
        if (playerInStorm) damageActor(PLAYER_ACTOR_ID, st.damagePerSecond, false, Vec3(), NO_TARGET, "STORM");
        for (Bot& b : bots)
            if (b.alive && !st.inside(b.body.pos.x, b.body.pos.z)) damageActor(b.index, st.damagePerSecond, false, Vec3(), NO_TARGET, "STORM");
    }
}

void Game::updateSpectate() {
    if (spectating < 0 || spectating >= (int)bots.size() || !bots[spectating].alive || bots[spectating].team != 0) {
        spectating = -1;
        for (const Bot& b : bots)
            if (b.alive && b.team == 0) { spectating = b.index; break; }
    }
    if (spectating >= 0) {
        const Bot& b = bots[spectating];
        setCameraFrom(b.body.pos + Vec3(0, 1.55f, 0), b.yaw, -0.12f, false, 75.0f);
    } else {
        updateCamera();
    }
}

float Game::aimDistanceWorld() const {
    Ray r{camera.aimRay.at(camera.aimStartT), camera.aimRay.dir};
    HitInfo h;
    if (world.raycast(r, 200.0f, h, true)) return camera.aimStartT + h.t;
    return 1e6f;
}

// ------------------------------------------------------------------ actors
AABB Game::botBodyBox(const Bot& b) const {
    const Vec3& p = b.body.pos;
    return {{p.x - 0.42f, p.y, p.z - 0.42f}, {p.x + 0.42f, p.y + 1.45f, p.z + 0.42f}};
}
AABB Game::botHeadBox(const Bot& b) const {
    const Vec3& p = b.body.pos;
    return {{p.x - 0.24f, p.y + 1.45f, p.z - 0.24f}, {p.x + 0.24f, p.y + 1.92f, p.z + 0.24f}};
}
AABB Game::playerBodyBox() const {
    const Vec3& p = player.body.pos;
    float h = player.body.height - 0.45f;
    return {{p.x - 0.42f, p.y, p.z - 0.42f}, {p.x + 0.42f, p.y + h, p.z + 0.42f}};
}
AABB Game::playerHeadBox() const {
    const Vec3& p = player.body.pos;
    float h = player.body.height - 0.45f;
    return {{p.x - 0.24f, p.y + h, p.z - 0.24f}, {p.x + 0.24f, p.y + h + 0.47f, p.z + 0.24f}};
}

void Game::collectActorBoxes(std::vector<AABB>& out, bool includePlayer, int ignoreBot) const {
    if (includePlayer && player.alive) out.push_back(player.body.box());
    for (const Bot& b : bots)
        if (b.alive && b.index != ignoreBot) out.push_back(b.body.box());
}

bool Game::trace(const Ray& r, float maxT, HitInfo& hit, bool ignorePlayer, int ignoreBot, bool includeChests) const {
    world.raycast(r, maxT, hit, includeChests);
    float t;
    Vec3 n;
    for (const Bot& b : bots) {
        if (!b.alive || b.index == ignoreBot) continue;
        // Cheap reject before the two box tests.
        Vec3 c = b.body.pos + Vec3(0, 1.0f, 0) - r.origin;
        float along = dot(c, r.dir);
        if (along < -1.5f || along > hit.t + 1.5f || length(c - r.dir * along) > 1.6f) continue;
        if (rayAABB(r, botHeadBox(b), hit.t, t, n) && t < hit.t) {
            hit.kind = HitKind::Actor; hit.t = t; hit.normal = n; hit.id = b.index; hit.head = true;
        }
        if (rayAABB(r, botBodyBox(b), hit.t, t, n) && t < hit.t) {
            hit.kind = HitKind::Actor; hit.t = t; hit.normal = n; hit.id = b.index; hit.head = false;
        }
    }
    if (!ignorePlayer && player.alive) {
        if (rayAABB(r, playerHeadBox(), hit.t, t, n) && t < hit.t) {
            hit.kind = HitKind::Actor; hit.t = t; hit.normal = n; hit.id = PLAYER_ACTOR_ID; hit.head = true;
        }
        if (rayAABB(r, playerBodyBox(), hit.t, t, n) && t < hit.t) {
            hit.kind = HitKind::Actor; hit.t = t; hit.normal = n; hit.id = PLAYER_ACTOR_ID; hit.head = false;
        }
    }
    if (hit.kind != HitKind::None) {
        hit.point = r.at(hit.t);
        return true;
    }
    return false;
}

bool Game::lineOfSight(const Vec3& from, const Vec3& to) const {
    Vec3 d = to - from;
    float len = length(d);
    if (len < 1e-3f) return true;
    HitInfo h;
    return !world.raycast(Ray{from, d / len}, len - 0.3f, h, true);
}

// ------------------------------------------------------------------ damage
float Game::damageActor(int victim, float amount, bool head, const Vec3& at, int attacker, const char* weapon) {
    if (amount <= 0 || !actorAlive(victim)) return 0;
    if (attacker != NO_TARGET && actorTeam(attacker) == actorTeam(victim)) return 0;  // no friendly fire
    const bool byPlayer = attacker == PLAYER_ACTOR_ID;
    std::string weaponTag = (weapon && *weapon) ? std::string(" (") + weapon + ")" : "";

    // Knocked players take the hit on their down-health instead.
    if (actorDown(victim)) {
        float& dh = victim == PLAYER_ACTOR_ID ? player.downHealth : bots[victim].downHealth;
        dh -= amount;
        if (victim == PLAYER_ACTOR_ID) player.hurtTimer = 0.35f;
        else bots[victim].hurtTimer = 1.5f;
        if (byPlayer) {
            damageNumbers.push_back({at, (int)std::lround(amount), Vec4(1, 1, 1, 1), 0, false});
            hitMarker = 0.18f;
        }
        if (dh <= 0) { dh = 0; eliminate(victim, attacker, weapon); }
        return amount;
    }

    if (victim == PLAYER_ACTOR_ID) {
        float toShield = std::min(player.shield, amount);
        player.shield -= toShield;
        player.health -= (amount - toShield);
        bool stormTick = attacker == NO_TARGET;
        player.hurtTimer = stormTick ? std::max(player.hurtTimer, 0.15f) : 0.35f;
        // Getting shot cancels healing; storm ticks don't (you can heal inside the storm).
        if (player.medkitTimer >= 0 && !stormTick) player.medkitTimer = -1;
        if (player.health <= 0) {
            player.health = 0;
            if (knockModes() && teamStanding(0, PLAYER_ACTOR_ID)) knock(PLAYER_ACTOR_ID, attacker, weapon);
            else eliminate(PLAYER_ACTOR_ID, attacker, weapon);
        }
        return amount;
    }

    Bot& b = bots[victim];
    {
        float toShield = std::min(b.shield, amount);
        b.shield -= toShield;
        b.health -= (amount - toShield);
    }
    b.hurtTimer = 1.5f;
    if (b.healTimer >= 0 && attacker != NO_TARGET) { b.healTimer = -1; b.healing = ItemKind::None; }  // getting shot cancels a heal
    if (attacker != NO_TARGET) b.lastAttacker = attacker;
    // Getting shot makes a bot turn on whoever shot it.
    if (attacker != NO_TARGET && actorAlive(attacker) && (b.targetId == NO_TARGET || !b.canSee)) {
        b.targetId = attacker;
        b.lastKnown = actorPos(attacker);
        if (b.state == AIState::Wander) {
            b.state = AIState::Spot;
            b.reaction = rng.range(0.25f, 0.45f);
            b.stateTime = 0;
        }
    }
    if (byPlayer) {
        damageNumbers.push_back({at, (int)std::lround(amount), head ? Vec4(1, 0.85f, 0.2f, 1) : Vec4(1, 1, 1, 1), 0, false});
        hitMarker = 0.18f;
        hitMarkerHead = head;
        // Add to the running total on this target (it resets after 1 s without a hit).
        DamageStack* st = nullptr;
        for (DamageStack& d : damageStacks)
            if (d.victim == victim) st = &d;
        if (!st) {
            damageStacks.push_back(DamageStack());
            st = &damageStacks.back();
            st->victim = victim;
        }
        st->total += amount;
        st->sinceHit = 0;
        st->head = st->head || head;
        st->pos = b.body.pos + Vec3(0, 2.4f, 0);
    }
    if (b.health <= 0) {
        b.health = 0;
        if (knockModes() && teamStanding(b.team, b.index)) knock(victim, attacker, weapon);
        else eliminate(victim, attacker, weapon);
    }
    return amount;
}

// ------------------------------------------------------------------ knocked / eliminated
void Game::knock(int victim, int attacker, const char* weapon) {
    std::string weaponTag = (weapon && *weapon) ? std::string(" (") + weapon + ")" : "";
    if (victim == PLAYER_ACTOR_ID) {
        if (player.mode == Mode::Edit) cancelEdit();
        player.down = true;
        player.downHealth = KNOCK_HEALTH;
        player.reviveProgress = 0;
        player.lastAttacker = attacker;
        player.mode = Mode::Combat;
        player.ads = false;
        player.sprinting = false;
        player.medkitTimer = -1;
        player.gunStates[player.selected].reloadTimer = 0;
        addKillMessage(attacker == NO_TARGET ? "YOU WERE KNOCKED" : actorName(attacker) + " KNOCKED YOU" + weaponTag, Vec4(1, 0.55f, 0.3f, 1));
        showToast("KNOCKED! CRAWL TO YOUR TEAM - THEY CAN REVIVE YOU", Vec4(1, 0.6f, 0.4f, 1));
        return;
    }
    Bot& b = bots[victim];
    b.down = true;
    b.downHealth = KNOCK_HEALTH;
    b.reviveProgress = 0;
    b.lastAttacker = attacker;
    b.healTimer = -1;
    b.healing = ItemKind::None;
    b.state = AIState::Wander;
    b.stateTime = 0;
    b.targetId = NO_TARGET;
    b.canSee = false;
    Vec4 col(1, 1, 1, 0.9f);
    if (b.team == 0) col = Vec4(1, 0.55f, 0.45f, 1);
    else if (actorTeam(attacker) == 0) col = Vec4(0.55f, 0.85f, 1, 1);
    if (attacker == PLAYER_ACTOR_ID) addKillMessage("YOU KNOCKED " + b.name + weaponTag, Vec4(1, 1, 1, 1));
    else addKillMessage(actorName(attacker) + " KNOCKED " + b.name + weaponTag, col);
}

void Game::eliminate(int victim, int attacker, const char* weapon) {
    const bool byPlayer = attacker == PLAYER_ACTOR_ID;
    std::string weaponTag = (weapon && *weapon) ? std::string(" (") + weapon + ")" : "";
    int team = actorTeam(victim);
    if (victim == PLAYER_ACTOR_ID) {
        if (player.mode == Mode::Edit) cancelEdit();
        player.health = 0;
        player.alive = false;
        player.down = false;
        player.mode = Mode::Combat;
        if (attacker >= 0 && attacker < (int)bots.size()) bots[attacker].kills++;
        addKillMessage(attacker == NO_TARGET ? (std::string(weapon) == "FALL" ? "YOU FELL TO YOUR DEATH" : "YOU WERE ELIMINATED")
                                             : actorName(attacker) + " ELIMINATED YOU" + weaponTag,
                       Vec4(1, 0.35f, 0.3f, 1));
    } else {
        Bot& b = bots[victim];
        b.alive = false;
        b.down = false;
        b.health = 0;
        Item drop = b.gun;
        drop.count = gunStats(drop.gun).magSize;
        world.spawnPickup(drop, b.body.pos + Vec3(0, 1.0f, 0), Vec3(rng.range(-1, 1), 3.0f, rng.range(-1, 1)));
        // Eliminated players drop 50 of a random material.
        world.spawnPickup(Item::makeMats(rng.rangeInt(0, 2), DEATH_MATS_DROP), b.body.pos + Vec3(0, 1.0f, 0),
                          Vec3(rng.range(-1.5f, 1.5f), 3.5f, rng.range(-1.5f, 1.5f)));
        if (attacker != NO_TARGET && attacker != PLAYER_ACTOR_ID && attacker < (int)bots.size()) bots[attacker].mats[0] += 50;
        Vec4 col(1, 1, 1, 0.9f);
        if (b.team == 0) col = Vec4(1, 0.55f, 0.45f, 1);                     // we lost a teammate
        else if (actorTeam(attacker) == 0) col = Vec4(0.55f, 0.85f, 1, 1);    // our team got the kill
        if (byPlayer) {
            player.kills++;
            addKillMessage("YOU ELIMINATED " + b.name + weaponTag, Vec4(1, 1, 1, 1));
        } else {
            if (attacker >= 0 && attacker < (int)bots.size()) bots[attacker].kills++;
            addKillMessage(actorName(attacker) + " ELIMINATED " + b.name + weaponTag, col);
        }
    }
    // Nobody left standing on that team? The knocked ones are out too (the team is wiped).
    if (team >= 0 && !teamStanding(team, NO_TARGET)) {
        if (team == 0 && player.alive && player.down) eliminate(PLAYER_ACTOR_ID, attacker, "TEAM WIPED");
        for (Bot& b : bots)
            if (b.team == team && b.alive && b.down) eliminate(b.index, attacker, "TEAM WIPED");
    }
}

void Game::revive(int id) {
    if (id == PLAYER_ACTOR_ID) {
        player.down = false;
        player.health = REVIVE_HEALTH;
        player.reviveProgress = 0;
        showToast("REVIVED! GET BACK IN THE FIGHT", Vec4(0.5f, 1, 0.6f, 1));
        addKillMessage("YOU WERE REVIVED", Vec4(0.5f, 1, 0.6f, 1));
        return;
    }
    Bot& b = bots[id];
    b.down = false;
    b.body.height = 1.8f;
    b.health = REVIVE_HEALTH;
    b.reviveProgress = 0;
    b.hurtTimer = 0;
    addKillMessage(b.name + " WAS REVIVED", b.team == 0 ? Vec4(0.5f, 1, 0.6f, 1) : Vec4(1, 1, 1, 0.9f));
}

void Game::addRevive(int id, float dt) {
    if (!actorDown(id)) return;
    float& prog = id == PLAYER_ACTOR_ID ? player.reviveProgress : bots[id].reviveProgress;
    bool& flag = id == PLAYER_ACTOR_ID ? player.revivedThisFrame : bots[id].revivedThisFrame;
    flag = true;
    prog += dt / REVIVE_TIME;
    if (prog >= 1.0f) revive(id);
}

// Bleeding out, and revive progress that fades when nobody is helping.
void Game::updateKnocked(float dt) {
    auto tick = [&](float& downHealth, float& prog, bool& flag, int id, int lastAttacker) {
        if (!flag) prog = std::max(0.0f, prog - dt / (REVIVE_TIME * 0.5f));
        flag = false;
        downHealth -= KNOCK_BLEED * dt;
        if (downHealth <= 0) {
            downHealth = 0;
            eliminate(id, lastAttacker, "BLED OUT");
        }
    };
    if (player.alive && player.down) tick(player.downHealth, player.reviveProgress, player.revivedThisFrame, PLAYER_ACTOR_ID, player.lastAttacker);
    for (Bot& b : bots)
        if (b.alive && b.down) tick(b.downHealth, b.reviveProgress, b.revivedThisFrame, b.index, b.lastAttacker);
}

void Game::showToast(const std::string& s, const Vec4& c) {
    toast.text = s;
    toast.color = c;
    toast.age = 0;
}

void Game::addKillMessage(const std::string& s, const Vec4& c) {
    killFeed.push_back({s, c, 0});
    if (killFeed.size() > 5) killFeed.erase(killFeed.begin());
}

// ------------------------------------------------------------------ inventory
bool Game::giveItem(const Item& it, bool dropIfFull) {
    if (it.kind == ItemKind::Mats) {
        int& m = player.matCount[it.mat];
        m = std::min(MAX_MATS, m + it.count);
        return true;
    }
    if (it.kind == ItemKind::Ammo) {
        int& a = player.ammo[(int)it.ammoType];
        a = std::min(999, a + it.count);
        return true;
    }
    if (isConsumable(it.kind)) {
        int left = it.count, cap = maxStack(it.kind);
        for (int i = 0; i < HOTBAR_SLOTS && left > 0; ++i) {
            Item& s = player.slots[i];
            if (s.kind == it.kind && s.count < cap) {
                int add = std::min(left, cap - s.count);
                s.count += add;
                left -= add;
            }
        }
        if (left == 0) return true;
        for (int i = 0; i < HOTBAR_SLOTS; ++i)
            if (player.slots[i].empty()) {
                player.slots[i] = it;
                player.slots[i].count = std::min(left, cap);
                return true;
            }
    }
    if (it.kind == ItemKind::Gun) {
        for (int i = 0; i < HOTBAR_SLOTS; ++i)
            if (player.slots[i].empty()) {
                player.slots[i] = it;
                player.gunStates[i].reset();
                return true;
            }
    }
    if (!dropIfFull) return false;
    // Hotbar full: swap with the selected slot and drop what was there.
    Item old = player.slots[player.selected];
    player.slots[player.selected] = it;
    player.gunStates[player.selected].reset();
    player.medkitTimer = -1;
    player.pickaxeOut = false;
    if (!old.empty()) {
        Vec3 f(std::sin(player.yaw), 0, std::cos(player.yaw));
        world.spawnPickup(old, player.body.pos + Vec3(0, 1.0f, 0) + f * 0.8f, f * 2.0f + Vec3(0, 2.5f, 0));
    }
    return true;
}

void Game::swapSlots(int a, int b) {
    if (a < 0 || b < 0 || a >= HOTBAR_SLOTS || b >= HOTBAR_SLOTS || a == b) return;
    std::swap(player.slots[a], player.slots[b]);
    std::swap(player.gunStates[a], player.gunStates[b]);
    player.medkitTimer = -1;
    if (player.selected == a) player.selected = b;
    else if (player.selected == b) player.selected = a;
}

bool Game::dropSlot(int i) {
    if (i < 0 || i >= HOTBAR_SLOTS || player.slots[i].empty()) return false;
    Vec3 f(std::sin(player.yaw), 0, std::cos(player.yaw));
    world.spawnPickup(player.slots[i], player.body.pos + Vec3(0, 1.0f, 0) + f * 0.8f, f * 2.0f + Vec3(0, 2.5f, 0));
    player.slots[i] = Item();
    player.gunStates[i].reset();
    if (i == player.selected) player.medkitTimer = -1;
    return true;
}

bool Game::dropAmmo(AmmoType t, int amount) {
    int& have = player.ammo[(int)t];
    int n = std::min(have, amount);
    if (n <= 0) return false;
    have -= n;
    Vec3 f(std::sin(player.yaw), 0, std::cos(player.yaw));
    world.spawnPickup(Item::makeAmmo(t, n), player.body.pos + Vec3(0, 1.0f, 0) + f * 0.8f, f * 2.0f + Vec3(0, 2.5f, 0));
    return true;
}

void Game::pickup(int index) {
    if (index < 0 || index >= (int)world.pickups.size()) return;
    Pickup& p = world.pickups[index];
    if (!p.alive) return;
    Item it = p.item;
    p.alive = false;  // mark first: giveItem may spawn a new pickup and reallocate the vector
    if (giveItem(it, true)) {
        std::string label = it.label();
        if (it.kind == ItemKind::Ammo || it.kind == ItemKind::Mats) label = std::to_string(it.count) + " " + label;
        showToast("PICKED UP " + label, it.kind == ItemKind::Gun ? rarityColor(it.rarity) : Vec4(1, 1, 1, 1));
    } else {
        world.pickups[index].alive = true;
    }
}

void Game::openChest(int index) {
    if (index < 0 || index >= (int)world.chests.size()) return;
    Chest& c = world.chests[index];
    if (c.opened) return;
    c.opened = true;
    Vec3 base = c.pos + Vec3(0, 1.0f, 0);
    Vec3 toward = normalize(Vec3(player.body.pos.x - c.pos.x, 0, player.body.pos.z - c.pos.z));
    if (length(toward) < 0.5f) toward = Vec3(std::sin(c.yaw), 0, std::cos(c.yaw));
    Vec3 side = Vec3(toward.z, 0, -toward.x);
    std::vector<Item> items = rollChest(rng);
    for (size_t i = 0; i < items.size(); ++i) {
        float s = ((float)i - (items.size() - 1) * 0.5f) * 1.6f;
        world.spawnPickup(items[i], base, toward * 1.8f + side * s + Vec3(0, 4.0f, 0));
    }
}

void Game::updateInteract(const InputState& in, float dt) {
    InteractPrompt next;
    Vec3 center = player.body.pos + Vec3(0, 0.9f, 0);
    Vec3 fwd(std::sin(player.yaw), 0, std::cos(player.yaw));

    // A chest under the crosshair wins.
    int aimedChest = -1;
    {
        Ray r{camera.aimRay.at(camera.aimStartT), camera.aimRay.dir};
        HitInfo h;
        if (world.raycast(r, 6.0f, h, true) && h.kind == HitKind::Chest &&
            length(world.chests[h.id].pos - player.body.pos) < INTERACT_RANGE + 0.8f)
            aimedChest = h.id;
    }

    float bestD = 1e9f;
    int bestChest = aimedChest, bestPickup = -1;
    if (aimedChest < 0) {
        for (size_t i = 0; i < world.chests.size(); ++i) {
            const Chest& c = world.chests[i];
            if (c.opened) continue;
            Vec3 d = c.pos + Vec3(0, 0.45f, 0) - center;
            float dist = length(d);
            if (dist > INTERACT_RANGE) continue;
            if (dist > 1.4f && dot(normalize(Vec3(d.x, 0, d.z)), fwd) < 0.2f) continue;
            if (dist < bestD) { bestD = dist; bestChest = (int)i; }
        }
        for (size_t i = 0; i < world.pickups.size(); ++i) {
            const Pickup& p = world.pickups[i];
            if (!p.alive || !p.settled) continue;
            Vec3 d = p.pos - center;
            float dist = lengthXZ(d);
            if (dist > 2.2f || std::fabs(d.y) > 2.0f) continue;
            if (dist > 1.2f && dot(normalize(Vec3(d.x, 0, d.z)), fwd) < 0.0f) continue;
            if (dist < bestD) { bestD = dist; bestPickup = (int)i; bestChest = -1; }
        }
    }

    // A knocked teammate close by? Reviving comes before everything else.
    int bestDown = -1;
    if (!player.down) {
        float bd = REVIVE_RANGE;
        for (const Bot& b : bots) {
            if (!b.alive || !b.down || b.team != 0) continue;
            float d = length(b.body.pos - player.body.pos);
            if (d < bd) { bd = d; bestDown = b.index; }
        }
    }
    if (bestDown >= 0) {
        next.kind = InteractPrompt::Revive;
        next.index = bestDown;
        next.text = "REVIVE " + bots[bestDown].name;
    } else if (bestChest >= 0) {
        next.kind = InteractPrompt::OpenChest;
        next.index = bestChest;
        next.text = "OPEN CHEST";
    } else if (bestPickup >= 0) {
        const Item& it = world.pickups[bestPickup].item;
        next.kind = InteractPrompt::PickUp;
        next.index = bestPickup;
        std::string label = it.label();
        if (it.kind == ItemKind::Ammo || (isConsumable(it.kind) && it.count > 1))
            label = std::to_string(it.count) + " " + label;
        next.text = "PICK UP " + label;
    }

    if (next.kind != prompt.kind || next.index != prompt.index) player.interactTimer = 0;

    if (next.kind == InteractPrompt::OpenChest) {
        if (in.interact) {
            player.interactTimer += dt;
            if (player.interactTimer >= CHEST_OPEN_TIME) {
                openChest(next.index);
                player.interactTimer = 0;
                next = InteractPrompt();
            }
        } else {
            player.interactTimer = 0;
        }
        next.progress = player.interactTimer / CHEST_OPEN_TIME;
    } else if (next.kind == InteractPrompt::PickUp && in.interactPressed) {
        pickup(next.index);
        next = InteractPrompt();
    } else if (next.kind == InteractPrompt::Revive) {
        if (in.interact) {
            addRevive(next.index, dt);
            if (!bots[next.index].down) next = InteractPrompt();
            else next.progress = bots[next.index].reviveProgress;
        }
    }
    prompt = next;
}

}  // namespace bf
