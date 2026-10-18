// Blockfall - bot AI: Wander (or follow the leader) -> Spot -> Chase -> Shoot -> Take cover.
// Bots fight anyone not on their team: you, your teammates, and each other.
#include "game.h"
#include "loot.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace bf {

namespace {

float preferredRange(GunType g) {
    switch (g) {
        case GunType::Shotgun: return 8.0f;
        case GunType::SMG: return 18.0f;
        case GunType::Pistol: return 24.0f;
        case GunType::Sniper: return 60.0f;
        default: return 32.0f;
    }
}

float angleDiff(float a, float b) {
    float d = a - b;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    return d;
}

void turnToward(float& yaw, float target, float maxStep) {
    float d = angleDiff(target, yaw);
    yaw += clampf(d, -maxStep, maxStep);
}

}  // namespace

const char* botStyleName(BotStyle s) {
    static const char* names[] = {"FIGHTER", "HIDER", "MIXED"};
    return names[(int)s];
}

// ------------------------------------------------------------------ bot building
bool Game::botBuild(Bot& b, PieceType type, int facing, int levelOffset) {
    // Best material it has, cheapest first (wood, then brick, then metal).
    int matIdx = -1;
    for (int m = 0; m < 3; ++m)
        if (b.mats[m] >= BUILD_COST) { matIdx = m; break; }
    if (matIdx < 0) return false;
    const Vec3& p = b.body.pos;
    int ci = (int)std::floor(p.x / TILE), cj = (int)std::floor(p.z / TILE), ck = (int)std::floor((p.y + 0.5f) / STORY) + levelOffset;
    Slot s;
    s.type = type;
    s.k = ck;
    static const int dx[4] = {0, 1, 0, -1}, dz[4] = {1, 0, -1, 0};
    facing &= 3;
    switch (type) {
        case PieceType::Wall:
            s.i = ci + (facing == 1 ? 1 : 0);
            s.j = cj + (facing == 0 ? 1 : 0);
            s.side = (facing == 0 || facing == 2) ? 1 : 0;
            break;
        case PieceType::Ramp:
            s.i = ci + dx[facing];
            s.j = cj + dz[facing];
            s.dir = facing;
            break;
        case PieceType::Cone:
            s.i = ci;
            s.j = cj;
            s.k = ck + 1;
            break;
        default:
            s.i = ci + dx[facing];
            s.j = cj + dz[facing];
            break;
    }
    if (!world.build.isFree(s) || !world.wouldBeSupported(s)) return false;
    // Don't drop a wall through someone standing on the line.
    AABB bounds = pieceBounds(s);
    if (type == PieceType::Wall) {
        AABB me(p + Vec3(-0.4f, 0, -0.4f), p + Vec3(0.4f, 1.9f, 0.4f));
        if (bounds.min.x < me.max.x && bounds.max.x > me.min.x && bounds.min.y < me.max.y && bounds.max.y > me.min.y &&
            bounds.min.z < me.max.z && bounds.max.z > me.min.z) return false;
    }
    if (world.build.place(s, false, (BuildMat)matIdx) < 0) return false;
    sound(Sfx::BuildPlace, bounds.center());
    b.mats[matIdx] -= BUILD_COST;
    b.builds++;
    return true;
}

void Game::botPanicWall(Bot& b, const Vec3& threat) {
    if (b.buildCooldown > 0) return;
    Vec3 to = threat - b.body.pos;
    int facing = cardinalFromYaw(std::atan2(to.x, to.z));
    if (botBuild(b, PieceType::Wall, facing)) {
        b.buildCooldown = b.style == BotStyle::Fighter ? 1.2f : (b.style == BotStyle::Hider ? 0.8f : 2.0f);
    } else {
        b.buildCooldown = 0.5f;
    }
}

// A box: four walls round the bot's cell and a cone on top.
void Game::botBox(Bot& b) {
    for (int f = 0; f < 4; ++f) botBuild(b, PieceType::Wall, f);
    botBuild(b, PieceType::Cone, 0);
    b.buildCooldown = 3.0f;
}

void Game::enterFlee(Bot& b, const Vec3& threat) {
    Vec3 away = normalize(Vec3(b.body.pos.x - threat.x, 0, b.body.pos.z - threat.z));
    if (length(away) < 0.5f) away = Vec3(std::sin(b.yaw), 0, std::cos(b.yaw));
    // Prefer a house to hide in, if there's one further from the threat than we are.
    Vec3 best = b.body.pos + away * 35.0f;
    float bestD = 1e9f;
    for (const Vec3& hc : world.houseCenters) {
        float d = lengthXZ(hc - b.body.pos);
        if (d > 70.0f || d < 4.0f) continue;
        if (lengthXZ(hc - threat) < lengthXZ(b.body.pos - threat) + 6.0f) continue;  // toward the enemy
        if (d < bestD) { bestD = d; best = hc; }
    }
    best.x = clampf(best.x, -MAP_HALF + 8, MAP_HALF - 8);
    best.z = clampf(best.z, -MAP_HALF + 8, MAP_HALF - 8);
    b.moveTarget = best;
    b.state = AIState::Flee;
    b.stateTime = 0;
    b.fleeTime = rng.range(4.0f, 8.0f);
}

// ------------------------------------------------------------------ bot looting + healing
bool Game::botWantsGun(const Bot& b, const Item& gun) const {
    if (gun.kind != ItemKind::Gun) return false;
    auto score = [](const Item& g) {
        int base = (int)g.rarity * 10;
        if (g.gun == GunType::Pistol) base -= 15;
        if (g.gun == GunType::AssaultRifle) base += 6;
        if (g.gun == GunType::Shotgun) base += 3;
        return base;
    };
    return score(gun) > score(b.gun);
}

void Game::botTakeItem(Bot& b, const Item& it) {
    switch (it.kind) {
        case ItemKind::Gun: {
            Item old = b.gun;
            b.gun = it;
            b.gun.count = gunStats(it.gun).magSize;
            b.gunState.reset();
            old.count = gunStats(old.gun).magSize;
            world.spawnPickup(old, b.body.pos + Vec3(0, 1.0f, 0), Vec3(rng.range(-1, 1), 2.5f, rng.range(-1, 1)));
            break;
        }
        case ItemKind::MedKit: b.medkits = std::min(3, b.medkits + it.count); break;
        case ItemKind::Mini: b.minis = std::min(6, b.minis + it.count); break;
        case ItemKind::BigPot: b.bigpots = std::min(2, b.bigpots + it.count); break;
        case ItemKind::Mats: b.mats[std::max(0, std::min(2, it.mat))] += it.count; break;
        default: return;
    }
    b.looted++;
}

void Game::botLoot(Bot& b) {
    // Things lying within reach.
    for (size_t i = 0; i < world.pickups.size(); ++i) {
        Pickup& p = world.pickups[i];
        if (!p.alive || lengthXZ(p.pos - b.body.pos) > 2.5f || std::fabs(p.pos.y - b.body.pos.y) > 2.5f) continue;
        const Item it = p.item;
        bool want = (it.kind == ItemKind::Gun && botWantsGun(b, it)) || ((it.kind == ItemKind::MedKit && b.medkits < 3) || (it.kind == ItemKind::Mini && b.minis < 6) ||
                    (it.kind == ItemKind::BigPot && b.bigpots < 2) || it.kind == ItemKind::Mats);
        if (!want) continue;
        p.alive = false;  // mark first: taking a gun drops the old one (may reallocate the vector)
        botTakeItem(b, it);
    }
    // A chest next to it: open it, keep what's useful, leave the rest on the ground.
    for (Chest& c : world.chests) {
        if (c.opened || lengthXZ(c.pos - b.body.pos) > 2.6f || std::fabs(c.pos.y - b.body.pos.y) > 2.5f) continue;
        c.opened = true;
        sound(Sfx::ChestOpen, c.pos);
        std::vector<Item> items = rollChest(rng);
        for (const Item& it : items) {
            bool want = (it.kind == ItemKind::Gun && botWantsGun(b, it)) || it.kind == ItemKind::MedKit || it.kind == ItemKind::Mini || it.kind == ItemKind::BigPot;
            if (want) botTakeItem(b, it);
            else world.spawnPickup(it, c.pos + Vec3(0, 1.0f, 0), Vec3(rng.range(-1.5f, 1.5f), 3.5f, rng.range(-1.5f, 1.5f)));
        }
        break;
    }
}

bool Game::botHeal(Bot& b, float dt) {
    if (b.healTimer < 0) {
        // Start one when it's safe and worth it.
        if (b.canSee || b.hurtTimer > 0.8f) return false;
        if (b.health < 70.0f && b.medkits > 0) b.healing = ItemKind::MedKit;
        else if (b.shield < 100.0f && b.bigpots > 0) b.healing = ItemKind::BigPot;
        else if (b.shield < MINI_SHIELD_CAP && b.minis > 0) b.healing = ItemKind::Mini;
        else return false;
        b.healTimer = 0;
    }
    b.healTimer += dt;
    if (b.healTimer >= useTime(b.healing)) {
        if (b.healing == ItemKind::MedKit) { b.health = std::min(100.0f, b.health + MEDKIT_HEAL); b.medkits--; }
        else if (b.healing == ItemKind::BigPot) { b.shield = std::min(100.0f, b.shield + BIGPOT_SHIELD); b.bigpots--; }
        else { b.shield = std::max(b.shield, std::min(MINI_SHIELD_CAP, b.shield + MINI_SHIELD)); b.minis--; }
        b.healTimer = -1;
        b.healing = ItemKind::None;
        return false;
    }
    return true;
}

void Game::updateBots(float dt) {
    for (Bot& b : bots)
        if (b.alive && b.drop == DropPhase::Landed) updateBot(b, dt);
}

void Game::pickNewWander(Bot& b) {
    // Teammates stay near you (or near another living teammate).
    if (b.team == 0) {
        Vec3 leader;
        bool has = false;
        if (player.alive) { leader = player.body.pos; has = true; }
        else
            for (const Bot& o : bots)
                if (o.alive && o.team == 0 && o.index < b.index) { leader = o.body.pos; has = true; break; }
        if (has) {
            float a = rng.range(0, 2 * PI), d = rng.range(3.0f, 6.0f);
            b.moveTarget = leader + Vec3(std::sin(a) * d, 0, std::cos(a) * d);
            return;
        }
    }
    // Hiders like to wander between houses.
    if (b.style == BotStyle::Hider && rng.chance(0.6f) && !world.houseCenters.empty()) {
        for (int i = 0; i < 6; ++i) {
            const Vec3& hc = world.houseCenters[rng.rangeInt(0, (int)world.houseCenters.size() - 1)];
            if (lengthXZ(hc - b.body.pos) < 90.0f) {
                b.moveTarget = hc;
                return;
            }
        }
    }
    for (int i = 0; i < 10; ++i) {
        float a = rng.range(0, 2 * PI), d = rng.range(8, 30);
        Vec3 t = b.body.pos + Vec3(std::sin(a) * d, 0, std::cos(a) * d);
        if (std::fabs(t.x) < MAP_HALF - 8 && std::fabs(t.z) < MAP_HALF - 8) {
            b.moveTarget = t;
            return;
        }
    }
    b.moveTarget = Vec3(0, 0, 0);
}

void Game::steerBot(Bot& b, const Vec3& target, float speed, float dt) {
    Body& body = b.body;
    // Following a path around something? Head for its next corner instead of the target.
    Vec3 aim = target;
    if (!b.path.empty()) {
        b.pathAge += dt;
        if (speed <= 0 || lengthXZ(target - b.pathGoal) > 3.0f || b.pathAge > 8.0f) {
            b.path.clear();   // stopped, the target moved, or it's taking too long: plan again if stuck
        } else {
            while (b.pathNext < b.path.size() && lengthXZ(b.path[b.pathNext] - body.pos) < 0.6f) ++b.pathNext;
            if (b.pathNext >= b.path.size()) {
                b.path.clear();
                b.pathFails = 0;
            } else {
                aim = b.path[b.pathNext];
            }
        }
    }
    Vec3 to(aim.x - body.pos.x, 0, aim.z - body.pos.z);
    float dist = length(to);
    Vec3 desired(0, 0, 0);
    if (dist > 0.4f && speed > 0) {
        Vec3 dir = to / dist;
        if (b.detourTime > 0 && b.path.empty()) {
            b.detourTime -= dt;
            float c = std::cos(b.detourAngle), s = std::sin(b.detourAngle);
            dir = Vec3(dir.x * c + dir.z * s, 0, -dir.x * s + dir.z * c);
        }
        // Only slow down near the real target, not at each corner of the path.
        float slowDist = b.path.empty() ? dist : lengthXZ(target - body.pos);
        desired = dir * std::min(speed, slowDist * 3.0f);
    }
    Vec3 hv(body.vel.x, 0, body.vel.z);
    Vec3 diff = desired - hv;
    float dl = length(diff), maxChange = (body.grounded ? 30.0f : 6.0f) * dt;
    if (dl > maxChange) diff = diff * (maxChange / dl);
    body.vel.x += diff.x;
    body.vel.z += diff.z;

    std::vector<AABB> others;
    collectActorBoxes(others, true, b.index);
    Vec3 before = body.pos;
    world.moveBody(body, dt, others);
    checkLanding(body, b.index, dt);
    float moved = lengthXZ(body.pos - before);
    b.walkPhase += moved * 2.4f;
    footstep(b.walkPhase, b.lastStep, body, speed > 5.0f ? 0.9f : 0.6f);

    // Stuck on something? Plan a path around it. If that fails (no route, not on the ground, too
    // many bots planning this frame, or stuck again on the path), swerve to one side and hop.
    if (length(desired) > 1.0f && moved < length(desired) * dt * 0.3f) {
        b.stuckTime += dt;
        if (b.stuckTime > 0.5f) {
            b.stuckTime = 0;
            if (!b.path.empty()) ++b.pathFails;
            bool onGround = body.grounded && body.pos.y < world.terrainHeight(body.pos.x, body.pos.z) + 1.0f;
            bool planned = false;
            if (onGround && pathPlansLeft > 0 && b.pathFails < 2) {
                --pathPlansLeft;
                planned = findPath(world, body.pos, target, b.path);
                if (planned) {
                    b.pathNext = 0;
                    b.pathGoal = target;
                    b.pathAge = 0;
                    b.detourTime = 0;
                    ++b.paths;
                }
            }
            if (!planned) {
                b.path.clear();
                b.pathFails = 0;
                b.detourAngle = (rng.chance(0.5f) ? 1.0f : -1.0f) * deg2rad(rng.range(55, 85));
                b.detourTime = 1.0f;
                if (body.grounded) body.vel.y = JUMP_SPEED;
            }
        }
    } else {
        b.stuckTime = 0;
    }
}

// Looks around for the nearest enemy it can actually see.
void Game::perceive(Bot& b) {
    Vec3 eye = b.body.pos + Vec3(0, 1.6f, 0);
    struct Cand { int id; float d; };
    Cand cands[64];
    int n = 0;
    // Bots notice each other from closer than they notice you, and not at all while they are
    // still looting in the first minute or so - both keep the lobby full for longer.
    const bool earlyGame = time < 75.0f;
    auto consider = [&](int id, const Vec3& pos) {
        float d = length(pos - b.body.pos);
        float range = id == PLAYER_ACTOR_ID ? 75.0f : (earlyGame ? 22.0f : 48.0f);
        if (d < range && n < 64) cands[n++] = {id, d};
    };
    if (player.alive && b.team != 0 && dropPhase == DropPhase::Landed) consider(PLAYER_ACTOR_ID, player.body.pos);
    for (const Bot& o : bots)
        if (o.alive && o.team != b.team && o.drop == DropPhase::Landed) consider(o.index, o.body.pos);
    std::sort(cands, cands + n, [](const Cand& x, const Cand& y) { return x.d < y.d; });

    auto visible = [&](int id, float d) {
        Vec3 p = actorPos(id) + Vec3(0, 1.1f, 0);
        Vec3 to = p - eye;
        float yawTo = std::atan2(to.x, to.z);
        bool inView = std::fabs(angleDiff(yawTo, b.yaw)) < deg2rad(80) || d < 14.0f ||
                      (b.hurtTimer > 0 && id == b.targetId);
        return inView && lineOfSight(eye, p);
    };

    // Keep shooting at the current target while it's visible (no flip-flopping).
    if (b.targetId != NO_TARGET && actorAlive(b.targetId) && actorTeam(b.targetId) != b.team) {
        float d = length(actorPos(b.targetId) - b.body.pos);
        if (d < 80.0f && visible(b.targetId, d)) {
            b.canSee = true;
            return;
        }
    }
    for (int i = 0; i < n && i < 3; ++i)
        if (visible(cands[i].id, cands[i].d)) {
            b.targetId = cands[i].id;
            b.canSee = true;
            return;
        }
    b.canSee = false;
    if (b.targetId != NO_TARGET && !actorAlive(b.targetId)) b.targetId = NO_TARGET;
}

bool Game::findCover(Bot& b) {
    Vec3 tp = b.targetId != NO_TARGET ? actorPos(b.targetId) : b.lastKnown;
    struct Cand { Vec3 pos; float d; AABB obstacle; };
    std::vector<Cand> cands;
    auto consider = [&](const AABB& box) {
        Vec3 size = box.size();
        if (size.y < 1.5f) return;
        Vec3 c = box.center();
        if (lengthXZ(c - b.body.pos) > 32.0f) return;
        Vec3 away = normalize(Vec3(c.x - tp.x, 0, c.z - tp.z));
        if (length(away) < 0.5f) return;
        float ext = std::fabs(away.x) * size.x * 0.5f + std::fabs(away.z) * size.z * 0.5f;
        Vec3 spot = c + away * (ext + 1.2f);
        spot.y = world.terrainHeight(spot.x, spot.z);
        cands.push_back({spot, lengthXZ(spot - b.body.pos), box});
    };
    // Whole houses first (best cover), then single walls, rocks and tree trunks.
    for (const Vec3& hc : world.houseCenters)
        consider(AABB({hc.x - 5.0f, hc.y, hc.z - 5.0f}, {hc.x + 5.0f, hc.y + 3.6f, hc.z + 5.0f}));
    for (const StaticBox& s : world.statics)
        if (s.alive && s.collide && (s.kind == PropKind::HouseWall || s.kind == PropKind::Rock || s.kind == PropKind::Trunk))
            consider(s.box);
    std::vector<AABB> boxes;
    for (const Piece& p : world.build.pieces())
        if (p.alive && p.slot.type == PieceType::Wall) {
            boxes.clear();
            pieceBoxes(p, boxes);
            if (!boxes.empty()) consider(pieceBounds(p.slot));
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.d < y.d; });

    auto clearPath = [&](const Vec3& a, const Vec3& c) {
        for (float h : {0.5f, 1.2f}) {
            Vec3 from = a + Vec3(0, h, 0), to = c + Vec3(0, h, 0);
            Vec3 d = to - from;
            float len = length(d);
            HitInfo hit;
            if (len > 0.1f && world.raycast(Ray{from, d / len}, len, hit, true)) return false;
        }
        return true;
    };

    Vec3 threat = tp + Vec3(0, 1.2f, 0);
    int checked = 0;
    float bestScore = 1e9f;
    for (const Cand& c : cands) {
        if (++checked > 24) break;
        if (c.d > bestScore) break;
        Body probe;
        probe.pos = c.pos;
        probe.pos.y = world.groundHeight(c.pos.x, c.pos.z, 0.4f, c.pos.y + 1.0f, 0.6f);
        std::vector<AABB> solids;
        AABB pb = probe.box();
        pb.min.y += 0.3f;
        world.collectSolids(pb, solids);
        if (!solids.empty()) continue;  // spot is inside something
        // Must be hidden from the threat even if we drift half a metre to either side.
        Vec3 toT = normalize(Vec3(threat.x - probe.pos.x, 0, threat.z - probe.pos.z));
        Vec3 side(toT.z, 0, -toT.x);
        bool exposed = false;
        for (float o : {0.0f, 0.6f, -0.6f})
            for (float h : {1.0f, 1.65f})
                if (lineOfSight(probe.pos + side * o + Vec3(0, h, 0), threat)) exposed = true;
        if (exposed) continue;
        // Can we get there? Straight line, or around one corner of the obstacle.
        float score = 1e9f;
        bool viaCorner = false;
        Vec3 corner;
        if (clearPath(b.body.pos, probe.pos)) {
            score = c.d;
        } else {
            const AABB& o = c.obstacle;
            const float m = 1.3f;
            Vec3 corners[4] = {{o.min.x - m, 0, o.min.z - m}, {o.max.x + m, 0, o.min.z - m},
                               {o.min.x - m, 0, o.max.z + m}, {o.max.x + m, 0, o.max.z + m}};
            for (Vec3 k : corners) {
                k.y = world.terrainHeight(k.x, k.z);
                if (!clearPath(b.body.pos, k) || !clearPath(k, probe.pos)) continue;
                float sc = lengthXZ(k - b.body.pos) + lengthXZ(probe.pos - k);
                if (sc < score) { score = sc; corner = k; viaCorner = true; }
            }
        }
        if (score < bestScore) {
            bestScore = score;
            b.coverPos = probe.pos;
            b.coverWaypoint = corner;
            b.useWaypoint = viaCorner;
            b.hasCover = true;
        }
    }
    return bestScore < 1e8f;
}

void Game::botShoot(Bot& b) {
    const GunStats& g = gunStats(b.gun.gun);
    Vec3 fwd(std::sin(b.yaw), 0, std::cos(b.yaw));
    Vec3 right(fwd.z, 0, -fwd.x);
    Vec3 muzzle = b.body.pos + Vec3(0, 1.4f, 0) + right * 0.3f + fwd * 0.4f;
    bool targetIsPlayer = b.targetId == PLAYER_ACTOR_ID;
    float aimHeight = targetIsPlayer && player.crouched ? 0.8f : 1.1f;
    Vec3 target = actorPos(b.targetId) + Vec3(0, aimHeight, 0);
    Vec3 baseDir = normalize(target - muzzle);

    // Bots are deliberately sloppy: wider cone, worse when their target moves fast.
    Vec3 tv = targetIsPlayer ? player.body.vel : bots[b.targetId].body.vel;
    // Extra miss chance grows with distance.
    float dist = length(target - muzzle);
    float spread = g.spreadHip + b.gunState.bloom + 3.2f + dist * 0.035f;
    if (lengthXZ(tv) > WALK_SPEED + 0.5f) spread += 1.5f;
    if (std::fabs(tv.y) > 0.5f) spread += 1.0f;

    std::map<int, float> dmg;
    std::map<int, bool> head;
    std::map<int, Vec3> at;
    for (int p = 0; p < g.pellets; ++p) {
        Ray r{muzzle, spreadDirection(baseDir, spread, rng)};
        HitInfo h;
        Vec3 end = r.at(std::min(g.range, 200.0f));
        if (trace(r, g.range, h, false, b.index, true)) {
            end = h.point;
            if (h.kind == HitKind::Actor) {
                dmg[h.id] += bulletDamage(b.gun, h.t, h.head);
                head[h.id] = head[h.id] || h.head;
                at[h.id] = h.point;
            } else if (h.kind == HitKind::Piece) {
                if (world.build.damage(h.id, bulletDamage(b.gun, h.t, false))) sound(Sfx::BuildBreak, h.point);
            }
        }
        tracers.push_back({muzzle, end, Vec4(1.0f, 0.45f, 0.35f, 0.9f), 0});
    }
    for (auto& kv : dmg) {
        // Bots shooting other bots hit softer still, so the lobby doesn't thin out in the first minute.
        float scale = BOT_DAMAGE_SCALE * (kv.first == PLAYER_ACTOR_ID ? 1.0f : BOT_VS_BOT_DAMAGE_SCALE);
        damageActor(kv.first, kv.second * scale, head[kv.first], at[kv.first], b.index, g.name);
    }
    sound(shotSfx(b.gun.gun), muzzle);
    b.gun.count--;
    onShotFired(b.gunState, g);
    b.shotAnim = 0.12f;
}

// Knocked: crawl toward the nearest standing teammate and wait to be picked up.
void Game::updateKnockedBot(Bot& b, float dt) {
    b.hurtTimer = std::max(0.0f, b.hurtTimer - dt);
    b.body.height = 0.9f;
    Vec3 goal = b.body.pos;
    float best = 1e9f;
    if (b.team == 0 && player.alive && !player.down) { goal = player.body.pos; best = lengthXZ(player.body.pos - b.body.pos); }
    for (const Bot& o : bots) {
        if (o.index == b.index || o.team != b.team || !o.alive || o.down) continue;
        float d = lengthXZ(o.body.pos - b.body.pos);
        if (d < best) { best = d; goal = o.body.pos; }
    }
    if (best > 3.0f && best < 60.0f) steerBot(b, goal, CRAWL_SPEED, dt);
    else steerBot(b, b.body.pos, 0, dt);
    Vec3 v(b.body.vel.x, 0, b.body.vel.z);
    if (length(v) > 0.2f) turnToward(b.yaw, std::atan2(v.x, v.z), 4.0f * dt);
}

// Standing bots go and pick up a knocked teammate when nothing is shooting at them.
bool Game::botRevive(Bot& b, float dt) {
    if (b.reviveTarget != NO_TARGET && (!actorDown(b.reviveTarget) || actorTeam(b.reviveTarget) != b.team)) b.reviveTarget = NO_TARGET;
    if (b.reviveTarget == NO_TARGET) {
        if (b.canSee || b.hurtTimer > 1.0f) return false;   // busy fighting
        float best = 60.0f;
        if (b.team == 0 && actorDown(PLAYER_ACTOR_ID)) { best = lengthXZ(player.body.pos - b.body.pos); b.reviveTarget = PLAYER_ACTOR_ID; }
        for (const Bot& o : bots) {
            if (o.team != b.team || !o.alive || !o.down || o.index == b.index) continue;
            float d = lengthXZ(o.body.pos - b.body.pos);
            if (d < best) { best = d; b.reviveTarget = o.index; }
        }
        if (b.reviveTarget == NO_TARGET) return false;
    }
    // Someone shooting at us? Deal with that first (but remember who needs help).
    if (b.canSee && b.hurtTimer > 0.5f) return false;
    Vec3 goal = actorPos(b.reviveTarget);
    float d = length(goal - b.body.pos);
    if (d > REVIVE_RANGE * 0.8f) {
        steerBot(b, goal, 6.0f, dt);
        Vec3 v(b.body.vel.x, 0, b.body.vel.z);
        if (length(v) > 0.3f) turnToward(b.yaw, std::atan2(v.x, v.z), 6.0f * dt);
    } else {
        steerBot(b, b.body.pos, 0, dt);
        Vec3 to = goal - b.body.pos;
        turnToward(b.yaw, std::atan2(to.x, to.z), 6.0f * dt);
        addRevive(b.reviveTarget, dt);
    }
    return true;
}

void Game::updateBot(Bot& b, float dt) {
    if (b.down) { updateKnockedBot(b, dt); return; }
    const GunStats& g = gunStats(b.gun.gun);
    if (tickGun(b.gunState, g, dt)) b.gun.count = g.magSize;  // bots have unlimited reserve ammo
    if (b.gun.count <= 0 && b.gunState.reloadTimer <= 0) b.gunState.reloadTimer = g.reloadTime;
    b.hurtTimer = std::max(0.0f, b.hurtTimer - dt);
    b.stateTime += dt;
    b.buildCooldown = std::max(0.0f, b.buildCooldown - dt);
    const bool fighter = b.style == BotStyle::Fighter, hider = b.style == BotStyle::Hider;

    // Perception (a few times per second, not every frame).
    b.losTimer -= dt;
    if (b.losTimer <= 0) {
        b.losTimer = 0.2f + rng.range(0, 0.1f);
        perceive(b);
        botLoot(b);
    }
    // A knocked teammate to pick up?
    if (knockModes() && (b.state == AIState::Wander || b.state == AIState::TakeCover || b.state == AIState::Chase) && botRevive(b, dt)) return;
    // Using a heal item: stand still until it's done (or until it gets shot).
    if ((b.state == AIState::Wander || b.state == AIState::TakeCover) && botHeal(b, dt)) {
        steerBot(b, b.body.pos, 0, dt);
        return;
    }
    if (b.targetId != NO_TARGET && !actorAlive(b.targetId)) {
        b.targetId = NO_TARGET;
        b.canSee = false;
    }
    if (b.canSee) {
        b.lastKnown = actorPos(b.targetId);
        b.unseenTime = 0;
    } else {
        b.unseenTime += dt;
    }

    // Outside the storm (or about to be)? Head for the safe circle before anything else.
    {
        const Storm& st = storm;
        Vec3 safeC = st.shrinking ? st.nextCenter : st.center;
        float safeR = st.shrinking ? st.nextRadius : st.radius;
        float d = lengthXZ(b.body.pos - safeC);
        if (d > safeR - 6.0f && b.state != AIState::TakeCover) {
            if (b.state != AIState::Wander || lengthXZ(b.moveTarget - safeC) > safeR - 8.0f) {
                float a = rng.range(0, 2 * PI), rr = rng.range(0, std::max(0.0f, safeR - 10.0f));
                b.moveTarget = safeC + Vec3(std::sin(a) * rr, 0, std::cos(a) * rr);
                b.state = AIState::Wander;
                b.stateTime = 0;
                b.targetId = NO_TARGET;
                b.canSee = false;
            }
            steerBot(b, b.moveTarget, 6.0f, dt);
            Vec3 v(b.body.vel.x, 0, b.body.vel.z);
            if (length(v) > 0.3f) turnToward(b.yaw, std::atan2(v.x, v.z), 6.0f * dt);
            return;
        }
    }
    bool hasTarget = b.targetId != NO_TARGET;
    Vec3 tpos = hasTarget ? actorPos(b.targetId) : b.lastKnown;
    Vec3 eye = b.body.pos + Vec3(0, 1.6f, 0);
    Vec3 toT = tpos + Vec3(0, 1.1f, 0) - eye;
    float dist = length(toT);
    float yawToT = std::atan2(toT.x, toT.z);

    float pref = preferredRange(b.gun.gun);
    auto enter = [&](AIState s) {
        b.state = s;
        b.stateTime = 0;
    };

    // Just got shot and know who did it? Throw up a wall between us (every style, fighters least).
    if (b.hurtTimer > 1.3f && b.lastAttacker != NO_TARGET && actorAlive(b.lastAttacker) && b.state != AIState::TakeCover) {
        float chance = fighter ? 0.5f : (hider ? 0.95f : 0.8f);
        if (b.buildCooldown <= 0 && rng.chance(chance)) botPanicWall(b, actorPos(b.lastAttacker));
    }

    switch (b.state) {
        case AIState::Wander: {
            // Teammates keep re-checking where the leader is so they don't fall behind.
            if (b.team == 0 && player.alive && lengthXZ(b.moveTarget - player.body.pos) > 7.0f) pickNewWander(b);
            float speed = 2.8f;
            if (b.team == 0 && player.alive && lengthXZ(player.body.pos - b.body.pos) > 10.0f) speed = 6.5f;
            steerBot(b, b.moveTarget, speed, dt);
            Vec3 v(b.body.vel.x, 0, b.body.vel.z);
            if (length(v) > 0.3f) turnToward(b.yaw, std::atan2(v.x, v.z), 6.0f * dt);
            else if (b.team == 0 && player.alive) turnToward(b.yaw, player.yaw, 2.0f * dt);
            if (lengthXZ(b.moveTarget - b.body.pos) < 1.5f || b.stateTime > 12.0f) {
                if (b.team != 0 || b.stateTime > 3.0f) {
                    pickNewWander(b);
                    b.stateTime = 0;
                }
            }
            // Loot in sight? Go get it (unopened chests, guns, heals) now and then.
            b.lootTimer -= dt;
            if (b.lootTimer <= 0 && b.team != 0) {
                b.lootTimer = 2.5f;
                float bestD = 28.0f;
                Vec3 best;
                bool found = false;
                for (const Chest& c : world.chests)
                    if (!c.opened && lengthXZ(c.pos - b.body.pos) < bestD) { bestD = lengthXZ(c.pos - b.body.pos); best = c.pos; found = true; }
                for (const Pickup& p : world.pickups) {
                    if (!p.alive) continue;
                    bool useful = (p.item.kind == ItemKind::Gun && botWantsGun(b, p.item)) || p.item.kind == ItemKind::MedKit || p.item.kind == ItemKind::Mini || p.item.kind == ItemKind::BigPot;
                    if (useful && lengthXZ(p.pos - b.body.pos) < bestD) { bestD = lengthXZ(p.pos - b.body.pos); best = p.pos; found = true; }
                }
                if (found) b.moveTarget = best;
            }
            if (b.canSee) {
                if (hider && b.team != 0 && rng.chance(0.75f)) {
                    enterFlee(b, tpos);
                } else {
                    enter(AIState::Spot);
                    b.reaction = fighter ? rng.range(0.2f, 0.45f) : rng.range(0.45f, 0.85f);  // reaction delay
                }
            }
            break;
        }
        case AIState::Flee: {
            // Run away (toward a house if there is one), building a wall behind when hit.
            steerBot(b, b.moveTarget, 6.5f, dt);
            Vec3 v(b.body.vel.x, 0, b.body.vel.z);
            if (length(v) > 0.3f) turnToward(b.yaw, std::atan2(v.x, v.z), 8.0f * dt);
            b.fleeTime -= dt;
            bool arrived = lengthXZ(b.moveTarget - b.body.pos) < 2.0f;
            // Cornered or hurt badly: box up and heal.
            if (b.health < 55.0f && hasTarget && dist < 25.0f && b.buildCooldown <= 0 && b.mats[0] + b.mats[1] + b.mats[2] >= BUILD_COST * 5) {
                botBox(b);
                b.coverPos = b.body.pos;
                b.useWaypoint = false;
                b.hasCover = true;
                b.coverTime = 0;
                enter(AIState::TakeCover);
                break;
            }
            if (arrived || b.fleeTime <= 0) {
                if (b.canSee && dist < 14.0f) {  // can't get away: fight back
                    enter(AIState::Shoot);
                    b.burstLeft = 0;
                    b.burstPause = 0.1f;
                } else {
                    b.targetId = NO_TARGET;
                    b.canSee = false;
                    enter(AIState::Wander);
                    pickNewWander(b);
                }
            }
            break;
        }
        case AIState::Spot: {
            steerBot(b, b.body.pos, 0, dt);
            turnToward(b.yaw, yawToT, 7.0f * dt);
            b.reaction -= dt;
            if (b.reaction <= 0) {
                if (b.canSee && dist <= pref * 1.2f) enter(AIState::Shoot);
                else enter(AIState::Chase);
                b.burstLeft = 0;
                b.burstPause = rng.range(0.1f, 0.3f);
            }
            if (!hasTarget) enter(AIState::Wander);
            break;
        }
        case AIState::Chase: {
            steerBot(b, tpos, dist > 30.0f ? (fighter ? 6.5f : 5.6f) : (fighter ? 5.5f : 4.6f), dt);
            Vec3 v(b.body.vel.x, 0, b.body.vel.z);
            // Fighters (and sometimes mixed bots) build ramps to reach a higher target or get unstuck.
            if (!hider && b.buildCooldown <= 0 && hasTarget && lengthXZ(tpos - b.body.pos) < 30.0f &&
                (tpos.y > b.body.pos.y + 2.5f || b.stuckTime > 0.3f) && (fighter || rng.chance(0.4f))) {
                int facing = cardinalFromYaw(std::atan2(tpos.x - b.body.pos.x, tpos.z - b.body.pos.z));
                if (botBuild(b, PieceType::Ramp, facing)) {
                    b.moveTarget = b.body.pos + Vec3(std::sin(b.yaw), 0, std::cos(b.yaw)) * TILE;
                    b.buildCooldown = 1.0f;
                } else {
                    b.buildCooldown = 0.6f;
                }
            }
            if (b.canSee) turnToward(b.yaw, yawToT, 7.0f * dt);
            else if (length(v) > 0.3f) turnToward(b.yaw, std::atan2(v.x, v.z), 7.0f * dt);
            if (b.canSee && dist <= pref) enter(AIState::Shoot);
            else if (!b.canSee && (b.unseenTime > 6.0f || lengthXZ(b.lastKnown - b.body.pos) < 1.5f || !hasTarget)) {
                b.targetId = NO_TARGET;
                enter(AIState::Wander);
                pickNewWander(b);
            }
            // Teammates don't run off too far chasing.
            if (b.team == 0 && player.alive && lengthXZ(player.body.pos - b.body.pos) > 40.0f) {
                b.targetId = NO_TARGET;
                enter(AIState::Wander);
                pickNewWander(b);
            }
            break;
        }
        case AIState::Shoot: {
            // Strafe side to side while shooting.
            b.strafeTimer -= dt;
            if (b.strafeTimer <= 0) {
                b.strafe = rng.chance(0.5f) ? 1.0f : -1.0f;
                b.strafeTimer = rng.range(0.8f, 1.6f);
            }
            Vec3 fwd(std::sin(b.yaw), 0, std::cos(b.yaw));
            Vec3 right(fwd.z, 0, -fwd.x);
            steerBot(b, b.body.pos + right * (b.strafe * 3.0f), 2.2f, dt);
            turnToward(b.yaw, yawToT, 8.0f * dt);

            bool facing = std::fabs(angleDiff(yawToT, b.yaw)) < deg2rad(12);
            if (hasTarget && b.canSee && facing && b.gunState.reloadTimer <= 0) {
                if (b.burstPause > 0) {
                    b.burstPause -= dt;
                } else if (b.burstLeft > 0) {
                    if (b.gunState.cooldown <= 0 && b.gun.count > 0) {
                        botShoot(b);
                        b.burstLeft--;
                    }
                } else {
                    b.burstLeft = g.automatic ? rng.rangeInt(3, 6) : rng.rangeInt(1, 2);
                    b.burstPause = rng.range(0.5f, 1.1f);
                }
            }
            float coverAt = fighter ? 20.0f : (hider ? 60.0f : 35.0f);
            if (b.health < coverAt && !b.triedCover) {
                b.triedCover = true;
                if (hider && b.buildCooldown <= 0 && b.mats[0] + b.mats[1] + b.mats[2] >= BUILD_COST * 5) {
                    botBox(b);
                    b.coverPos = b.body.pos;
                    b.useWaypoint = false;
                    b.hasCover = true;
                    b.coverTime = 0;
                    enter(AIState::TakeCover);
                    break;
                }
                if (findCover(b)) {
                    enter(AIState::TakeCover);
                    b.coverTime = 0;
                    break;
                }
            }
            if (!hasTarget) enter(AIState::Wander);
            else if (b.unseenTime > 1.5f || (b.canSee && dist > pref * 1.5f)) enter(AIState::Chase);
            break;
        }
        case AIState::TakeCover: {
            if (b.useWaypoint && lengthXZ(b.coverWaypoint - b.body.pos) < 1.0f) b.useWaypoint = false;
            float d = lengthXZ(b.coverPos - b.body.pos);
            if (b.useWaypoint) steerBot(b, b.coverWaypoint, 5.6f, dt);
            else steerBot(b, b.coverPos, d > 0.3f ? 5.6f : 0.0f, dt);
            Vec3 v(b.body.vel.x, 0, b.body.vel.z);
            if (d > 1.0f && length(v) > 0.3f) turnToward(b.yaw, std::atan2(v.x, v.z), 8.0f * dt);
            else turnToward(b.yaw, yawToT, 4.0f * dt);
            if (d <= 0.6f) {
                b.coverTime += dt;
                b.health = std::min(hider ? 100.0f : 60.0f, b.health + (hider ? 10.0f : 6.0f) * dt);  // patch up while hidden
            }
            float stay = hider ? 7.0f : 4.0f;
            if (b.coverTime > stay || b.stateTime > 12.0f) {
                if (hider && hasTarget) enterFlee(b, tpos);
                else enter(hasTarget ? AIState::Shoot : AIState::Wander);
                b.triedCover = b.health < (fighter ? 20.0f : 35.0f);  // allow another retreat later if healed
            }
            break;
        }
    }
}

}  // namespace bf
