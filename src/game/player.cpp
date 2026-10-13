// Blockfall - player: look, movement, sprint + stamina, shooting, reloading, pickaxe, Med Kit
#include "game.h"
#include <cmath>
#include <map>

namespace bf {

void Game::tickGuns(float dt) {
    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
        Item& it = player.slots[i];
        if (it.kind != ItemKind::Gun) continue;
        const GunStats& g = gunStats(it.gun);
        if (tickGun(player.gunStates[i], g, dt)) {
            // Reload finished: fill the magazine from reserve ammo.
            int& reserve = player.ammo[(int)g.ammo];
            int take = std::min(g.magSize - it.count, reserve);
            it.count += take;
            reserve -= take;
        }
    }
}

void Game::updatePlayer(const InputState& in, float dt) {
    // Look.
    float sens = mouseSensitivity * (player.ads ? 0.6f : 1.0f);
    player.yaw += in.mouseDx * sens;
    player.pitch -= in.mouseDy * sens;
    player.pitch = clampf(player.pitch, deg2rad(-85.0f), deg2rad(85.0f));
    if (player.yaw > PI) player.yaw -= 2 * PI;
    if (player.yaw < -PI) player.yaw += 2 * PI;

    // Knocked: you can only look around and crawl.
    if (player.down) {
        player.ads = false;
        player.sprinting = false;
        player.crouched = true;
        prompt = InteractPrompt();
        updateCamera();
        Body& b = player.body;
        b.height = 0.9f;
        Vec3 fwd(std::sin(player.yaw), 0, std::cos(player.yaw));
        Vec3 right(std::cos(player.yaw), 0, -std::sin(player.yaw));
        Vec3 wish = fwd * (float)((in.forward ? 1 : 0) - (in.back ? 1 : 0)) + right * (float)((in.right ? 1 : 0) - (in.left ? 1 : 0));
        if (length(wish) > 0.01f) wish = normalize(wish);
        Vec3 target = wish * CRAWL_SPEED;
        Vec3 hv(b.vel.x, 0, b.vel.z);
        Vec3 diff = target - hv;
        float dl = length(diff), maxChange = (b.grounded ? 30.0f : 6.0f) * dt;
        if (dl > maxChange) diff = diff * (maxChange / dl);
        b.vel.x += diff.x;
        b.vel.z += diff.z;
        std::vector<AABB> others;
        collectActorBoxes(others, false, -1);
        Vec3 before = b.pos;
        world.moveBody(b, dt, others);
        checkLanding(b, PLAYER_ACTOR_ID, dt);
        player.walkPhase += lengthXZ(b.pos - before) * 2.4f;
        updateCamera();
        return;
    }

    tickGuns(dt);
    player.swingCooldown = std::max(0.0f, player.swingCooldown - dt);
    player.swingAnim = std::max(0.0f, player.swingAnim - dt);

    // Mode switching.
    if (in.buildPiecePressed >= 0 && in.buildPiecePressed < (int)PieceType::Count) {
        if (player.mode == Mode::Edit) cancelEdit();
        player.mode = Mode::Build;
        player.buildPiece = (PieceType)in.buildPiecePressed;
        player.medkitTimer = -1;
        player.ads = false;
    }
    if (in.buildTogglePressed) {
        if (player.mode == Mode::Edit) cancelEdit();
        player.mode = (player.mode == Mode::Build) ? Mode::Combat : Mode::Build;
        player.medkitTimer = -1;
        player.ads = false;
    }
    if (in.pickaxePressed) {
        if (player.mode == Mode::Edit) cancelEdit();
        player.mode = Mode::Combat;
        if (!player.pickaxeOut) {
            player.gunStates[player.selected].reloadTimer = 0;
            player.medkitTimer = -1;
            player.pickaxeOut = true;
        }
    }
    if (in.slotPressed >= 0 && in.slotPressed < HOTBAR_SLOTS) {
        if (player.mode == Mode::Edit) cancelEdit();
        player.mode = Mode::Combat;
        if (player.selected != in.slotPressed || player.pickaxeOut) {
            player.gunStates[player.selected].reloadTimer = 0;  // switching cancels a reload
            player.medkitTimer = -1;
            player.selected = in.slotPressed;
            player.pickaxeOut = false;
        }
    }
    updateCamera();
    if (in.resetEditPressed) {
        resetEdit();
    } else if (in.editPressed) {
        if (player.mode == Mode::Edit) confirmEdit();
        else startEdit();
    }

    switch (player.mode) {
        case Mode::Combat: updateCombat(in, dt); break;
        case Mode::Build: player.ads = false; updateBuild(in, dt); break;
        case Mode::Edit: player.ads = false; updateEdit(in, dt); break;
    }
    if (player.mode != Mode::Edit) updateInteract(in, dt);
    else prompt = InteractPrompt();
    updateMovement(in, dt);
    updateCamera();
}

void Game::updateStamina(bool sprinting, float dt) {
    if (sprinting) {
        player.stamina = std::max(0.0f, player.stamina - STAMINA_DRAIN * dt);
        player.staminaDelay = STAMINA_REGEN_DELAY;
        if (player.stamina <= 0) player.exhausted = true;
    } else if (player.staminaDelay > 0) {
        player.staminaDelay -= dt;
    } else {
        player.stamina = std::min(STAMINA_MAX, player.stamina + STAMINA_REGEN * dt);
    }
    if (player.exhausted && player.stamina >= STAMINA_RECOVER) player.exhausted = false;
}

void Game::updateMovement(const InputState& in, float dt) {
    Body& b = player.body;
    player.crouched = in.crouch;
    b.height = player.crouched ? 1.3f : 1.8f;

    Vec3 fwd(std::sin(player.yaw), 0, std::cos(player.yaw));
    Vec3 right(std::cos(player.yaw), 0, -std::sin(player.yaw));
    Vec3 wish = fwd * (float)((in.forward ? 1 : 0) - (in.back ? 1 : 0)) +
                right * (float)((in.right ? 1 : 0) - (in.left ? 1 : 0));
    bool moving = length(wish) > 0.01f;
    if (moving) wish = normalize(wish);

    // Sprint: hold the sprint key. Not while aiming, shooting, crouching, healing, or out of stamina.
    const Item& held = heldItem();
    bool shooting = player.mode == Mode::Combat && held.kind == ItemKind::Gun &&
                    (player.gunStates[player.selected].sinceShot < 0.35f || in.fire);
    bool wantsSprint = in.sprint && moving && !player.ads && !shooting && !player.crouched && player.medkitTimer < 0;
    player.sprinting = wantsSprint && !player.exhausted && player.stamina > 0;
    updateStamina(player.sprinting, dt);

    float speed = player.crouched ? CROUCH_SPEED : (player.sprinting ? SPRINT_SPEED : WALK_SPEED);
    if (player.ads) speed *= 0.8f;

    // Healing needs you to stand still.
    if (player.medkitTimer >= 0 && (moving || in.jumpPressed)) {
        player.medkitTimer = -1;
        showToast("HEALING CANCELLED - STAND STILL", Vec4(1, 0.6f, 0.4f, 1));
    }

    Vec3 target = wish * speed;
    float accel = b.grounded ? 45.0f : 9.0f;
    Vec3 hv(b.vel.x, 0, b.vel.z);
    Vec3 diff = target - hv;
    float dl = length(diff);
    float maxChange = accel * dt;
    if (dl > maxChange) diff = diff * (maxChange / dl);
    b.vel.x += diff.x;
    b.vel.z += diff.z;

    if (in.jumpPressed && b.grounded) {
        b.vel.y = JUMP_SPEED;
        b.grounded = false;
    }

    std::vector<AABB> others;
    collectActorBoxes(others, false, -1);
    Vec3 before = b.pos;
    world.moveBody(b, dt, others);
    checkLanding(b, PLAYER_ACTOR_ID, dt);
    float moved = lengthXZ(b.pos - before);
    player.walkPhase += moved * 2.4f;
    player.idleTime = moved > 0.001f || !b.grounded ? 0.0f : player.idleTime + dt;
}

float Game::currentSpread() const {
    const Item& it = heldItem();
    if (it.kind != ItemKind::Gun) return 0.0f;
    SpreadContext c;
    c.ads = player.ads;
    c.crouched = player.crouched;
    c.moving = lengthXZ(player.body.vel) > 0.5f;
    c.airborne = !player.body.grounded;
    return shotSpread(gunStats(it.gun), player.gunStates[player.selected], c);
}

void Game::reloadSelected() {
    if (player.pickaxeOut) return;
    Item& it = player.slots[player.selected];
    if (it.kind != ItemKind::Gun) return;
    const GunStats& g = gunStats(it.gun);
    GunState& gs = player.gunStates[player.selected];
    if (gs.reloadTimer > 0 || it.count >= g.magSize) return;
    if (player.ammo[(int)g.ammo] <= 0) {
        showToast(std::string("NO ") + ammoName(g.ammo) + " AMMO", Vec4(1, 0.6f, 0.4f, 1));
        return;
    }
    gs.reloadTimer = g.reloadTime;
}

void Game::updateCombat(const InputState& in, float dt) {
    if (player.pickaxeOut) {
        player.ads = false;
        player.medkitTimer = -1;
        // Hold to keep swinging.
        if (in.fire && player.swingCooldown <= 0) swingPickaxe();
        return;
    }
    Item& it = player.slots[player.selected];
    if (it.kind == ItemKind::Gun) {
        const GunStats& g = gunStats(it.gun);
        GunState& gs = player.gunStates[player.selected];
        player.ads = in.aim;
        if (in.reloadPressed) reloadSelected();
        bool wantFire = g.automatic ? in.fire : in.firePressed;
        if (wantFire && gs.reloadTimer <= 0 && gs.cooldown <= 0) {
            if (it.count > 0) {
                firePlayerGun();
                it.count--;
                if (it.count == 0 && player.ammo[(int)g.ammo] > 0) reloadSelected();
            } else if (in.firePressed) {
                reloadSelected();
            }
        }
    } else {
        player.ads = false;
    }

    if (it.kind == ItemKind::Shockwave) {
        player.medkitTimer = -1;
        if (in.firePressed && player.swingCooldown <= 0) {
            throwShockwave();
            player.swingCooldown = 0.5f;
            if (--it.count <= 0) it = Item();
        }
    } else if (isConsumable(it.kind)) {
        if (in.firePressed && player.medkitTimer < 0) {
            if (it.kind == ItemKind::MedKit && player.health >= PLAYER_MAX_HEALTH) showToast("ALREADY AT FULL HEALTH");
            else if (it.kind == ItemKind::BigPot && player.shield >= PLAYER_MAX_SHIELD) showToast("SHIELD IS ALREADY FULL");
            else if (it.kind == ItemKind::Mini && player.shield >= MINI_SHIELD_CAP) showToast("MINIS ONLY FILL SHIELD TO 50");
            else player.medkitTimer = 0;
        }
        if (player.medkitTimer >= 0) {
            player.medkitTimer += dt;
            if (player.medkitTimer >= useTime(it.kind)) {
                if (it.kind == ItemKind::MedKit) {
                    player.health = std::min(PLAYER_MAX_HEALTH, player.health + MEDKIT_HEAL);
                    showToast("+50 HEALTH", Vec4(0.4f, 1, 0.4f, 1));
                } else if (it.kind == ItemKind::BigPot) {
                    player.shield = std::min(PLAYER_MAX_SHIELD, player.shield + BIGPOT_SHIELD);
                    showToast("+50 SHIELD", Vec4(0.4f, 0.7f, 1, 1));
                } else {
                    player.shield = std::min(MINI_SHIELD_CAP, std::max(player.shield, std::min(MINI_SHIELD_CAP, player.shield + MINI_SHIELD)));
                    showToast("+25 SHIELD", Vec4(0.4f, 0.7f, 1, 1));
                }
                player.medkitTimer = -1;
                if (--it.count <= 0) it = Item();
            }
        }
    } else {
        player.medkitTimer = -1;
    }
}

void Game::swingPickaxe() {
    player.swingCooldown = PICKAXE_SWING_TIME;
    player.swingAnim = 0.3f;

    // Hit whatever the crosshair is on, if it's within reach of the player.
    Vec3 pivot = player.eyePivot();
    Ray aim{camera.aimRay.at(camera.aimStartT), camera.aimRay.dir};
    HitInfo h;
    bool hit = trace(aim, camera.aimStartT + 8.0f, h, true, -1, true) && length(h.point - pivot) <= PICKAXE_REACH;
    if (!hit) {
        // Close-up fallback: straight ahead from the head (e.g. when hugging a wall).
        Ray r{pivot, player.forward()};
        hit = trace(r, PICKAXE_REACH, h, true, -1, true);
    }
    if (!hit) return;

    switch (h.kind) {
        case HitKind::Static: {
            HarvestResult r = world.harvest(h.id, PICKAXE_BUILD_DAMAGE);
            if (r.mats > 0) {
                int& m = player.matCount[r.matType];
                int before = m;
                m = std::min(MAX_MATS, m + r.mats);
                static const Vec4 cols[3] = {{0.95f, 0.75f, 0.45f, 1}, {0.95f, 0.55f, 0.45f, 1}, {0.75f, 0.8f, 0.9f, 1}};
                damageNumbers.push_back({h.point, m - before, cols[r.matType], 0, true});
            }
            break;
        }
        case HitKind::Piece:
            world.build.damage(h.id, PICKAXE_BUILD_DAMAGE);
            damageNumbers.push_back({h.point, (int)PICKAXE_BUILD_DAMAGE, Vec4(0.7f, 0.85f, 1, 1), 0, false});
            break;
        case HitKind::Actor:
            if (h.id >= 0) damageActor(h.id, PICKAXE_PLAYER_DAMAGE, false, h.point + Vec3(0, 0.3f, 0), PLAYER_ACTOR_ID, "PICKAXE");
            break;
        default:
            break;
    }
}

void Game::firePlayerGun() {
    const Item& it = heldItem();
    const GunStats& g = gunStats(it.gun);
    GunState& gs = player.gunStates[player.selected];

    // Bullets go exactly where the crosshair points: they're traced from the camera through
    // the centre of the screen (starting just past the player, so you can't hit yourself).
    // The visible tracer is drawn from the gun to the hit point.
    Ray aim{camera.aimRay.at(camera.aimStartT), camera.aimRay.dir};
    Vec3 muzzle = player.eyePivot() + camera.right * 0.32f - Vec3(0, 0.22f, 0) + camera.forward * 0.35f;
    Vec3 baseDir = aim.dir;
    Vec3 origin = aim.origin;

    float spread = currentSpread();
    std::map<int, std::pair<float, bool>> botDamage;  // index -> (damage, any headshot)
    std::map<int, float> pieceDamage;
    std::map<int, Vec3> hitPoints;
    for (int p = 0; p < g.pellets; ++p) {
        Vec3 dir = spreadDirection(baseDir, spread, rng);
        Ray r{origin, dir};
        HitInfo h;
        Vec3 end = r.at(std::min(g.range, 300.0f));
        if (trace(r, g.range, h, true, -1, true)) {
            end = h.point;
            if (h.kind == HitKind::Actor && h.id >= 0) {
                auto& e = botDamage[h.id];
                e.first += bulletDamage(it, h.t, h.head);
                e.second = e.second || h.head;
                hitPoints[h.id] = h.point;
            } else if (h.kind == HitKind::Piece) {
                pieceDamage[h.id] += bulletDamage(it, h.t, false);
                hitPoints[100000 + h.id] = h.point;
            }
        }
        tracers.push_back({muzzle, end, Vec4(1.0f, 0.92f, 0.6f, 0.9f), 0});
    }
    for (auto& kv : botDamage)
        damageActor(kv.first, kv.second.first, kv.second.second, hitPoints[kv.first] + Vec3(0, 0.3f, 0), PLAYER_ACTOR_ID,
                    g.name);
    for (auto& kv : pieceDamage) {
        world.build.damage(kv.first, kv.second);
        damageNumbers.push_back({hitPoints[100000 + kv.first], (int)std::lround(kv.second), Vec4(0.7f, 0.85f, 1, 1), 0, false});
    }
    onShotFired(gs, g);
    player.shotAnim = 0.12f;
}

}  // namespace bf
