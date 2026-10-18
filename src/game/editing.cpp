// Blockfall - build mode (placing pieces) and edit mode (cutting tiles out of pieces)
#include "game.h"
#include <cmath>

namespace bf {

// ------------------------------------------------------------------ build mode
void Game::updateBuild(const InputState& in, float dt) {
    if (in.reloadPressed) player.rampRotation = (player.rampRotation + 1) & 3;  // R rotates ramps
    if (in.aimPressed) {  // right click cycles wood -> brick -> metal
        player.buildMat = (BuildMat)(((int)player.buildMat + 1) % (int)BuildMat::Count);
        showToast(std::string("BUILDING WITH ") + buildMatName(player.buildMat));
    }

    // Out of the current material? Switch to the next one you have: wood -> brick -> metal.
    if (player.curMats() < BUILD_COST) {
        for (int step = 1; step < (int)BuildMat::Count; ++step) {
            BuildMat next = (BuildMat)(((int)player.buildMat + step) % (int)BuildMat::Count);
            if (player.matCount[(int)next] >= BUILD_COST) {
                player.buildMat = next;
                showToast(std::string("OUT OF MATS - SWITCHED TO ") + buildMatName(next));
                break;
            }
        }
    }
    Slot s = computeBuildSlot(player.buildPiece, player.body.pos, player.yaw, camera.aimRay,
                              aimDistanceWorld(), player.rampRotation);
    buildTarget.slot = s;
    buildTarget.occupied = !world.build.isFree(s);
    buildTarget.unsupported = !buildTarget.occupied && !world.wouldBeSupported(s);
    buildTarget.valid = !buildTarget.occupied && !buildTarget.unsupported && player.curMats() >= BUILD_COST;

    player.buildCooldown = std::max(0.0f, player.buildCooldown - dt);
    // Hold left click to keep placing as you move ("turbo build").
    if (in.fire && player.buildCooldown <= 0) {
        if (tryPlaceBuild()) player.buildCooldown = 0.08f;
        else if (in.firePressed && player.curMats() < BUILD_COST)
            showToast(std::string("NOT ENOUGH ") + buildMatName(player.buildMat), Vec4(1, 0.5f, 0.4f, 1));
        else if (in.firePressed && buildTarget.unsupported)
            showToast("CAN'T BUILD IN THE AIR - NEEDS SUPPORT", Vec4(1, 0.5f, 0.4f, 1));
    }
}

bool Game::tryPlaceBuild() {
    if (!buildTarget.valid || player.curMats() < BUILD_COST) return false;
    if (world.build.place(buildTarget.slot, true, player.buildMat) < 0) return false;
    player.curMats() -= BUILD_COST;
    player.buildAnim = 0.2f;
    buildTarget.valid = false;
    buildTarget.occupied = true;
    return true;
}

// ------------------------------------------------------------------ edit mode
void Game::startEdit() {
    Ray r{camera.aimRay.at(camera.aimStartT), camera.aimRay.dir};
    HitInfo h;
    if (!world.raycast(r, 60.0f, h, false) || h.kind != HitKind::Piece) {
        showToast("AIM AT ONE OF YOUR BUILDS TO EDIT");
        return;
    }
    Piece* p = world.build.get(h.id);
    if (!p || !p->alive || !p->playerBuilt) return;
    if (length(h.point - player.eyePivot()) > EDIT_RANGE) {
        showToast("TOO FAR AWAY TO EDIT");
        return;
    }
    if (editGridSize(p->slot.type) == 0) {
        showToast(std::string(pieceName(p->slot.type)) + "S CAN'T BE EDITED");
        return;
    }
    player.edit.pieceId = p->id;
    player.edit.selected = p->slot.type == PieceType::Ramp ? 0 : p->removed;  // start from the current shape
    if (p->slot.type == PieceType::Ramp) showToast("STAIRS: SWIPE THE WAY THEY SHOULD GO UP");
    player.edit.dragging = false;
    player.edit.returnMode = player.mode == Mode::Edit ? Mode::Combat : player.mode;
    player.mode = Mode::Edit;
    player.medkitTimer = -1;
}

void Game::confirmEdit() {
    Piece* p = world.build.get(player.edit.pieceId);
    if (p && p->alive) {
        int n = editGridSize(p->slot.type);
        uint16_t full = (uint16_t)((1u << (n * n)) - 1u);
        // Cones flatten instead of disappearing, so every quadrant may be edited.
        if ((player.edit.selected & full) == full && p->slot.type != PieceType::Cone && p->slot.type != PieceType::Ramp) {
            showToast("CAN'T REMOVE EVERY TILE", Vec4(1, 0.6f, 0.4f, 1));
        } else if (!validEdit(p->slot.type, player.edit.selected & full)) {
            showToast("NOT A DOOR, WINDOW OR ARCH SHAPE", Vec4(1, 0.6f, 0.4f, 1));
        } else if (p->slot.type == PieceType::Ramp) {
            applyStairEdit(*p, player.edit.selected & full, player.edit.firstTile, player.edit.lastTile);
            p->hp = std::min(p->hp, p->targetHp * BUILD_UP_START);
            buildsDirty = true;
        } else if ((player.edit.selected & full) != p->removed) {
            p->removed = player.edit.selected & full;
            buildsDirty = true;
            // An edited piece is rebuilt: it builds back up to the health it had (damage isn't healed).
            p->hp = std::min(p->hp, p->targetHp * BUILD_UP_START);
        }
    }
    cancelEdit();
}

void Game::cancelEdit() {
    player.mode = player.edit.returnMode;
    player.edit.pieceId = -1;
    player.edit.dragging = false;
}

bool Game::resetEdit() {
    Piece* p = nullptr;
    if (player.mode == Mode::Edit) {
        p = world.build.get(player.edit.pieceId);
    } else {
        Ray r{camera.aimRay.at(camera.aimStartT), camera.aimRay.dir};
        HitInfo h;
        if (world.raycast(r, 60.0f, h, false) && h.kind == HitKind::Piece && length(h.point - player.eyePivot()) <= EDIT_RANGE)
            p = world.build.get(h.id);
    }
    if (!p || !p->alive || !p->playerBuilt || editGridSize(p->slot.type) == 0) return false;
    p->removed = 0;
    buildsDirty = true;
    if (player.mode == Mode::Edit) cancelEdit();
    showToast("EDIT RESET");
    return true;
}

void Game::updateEdit(const InputState& in, float /*dt*/) {
    Piece* p = world.build.get(player.edit.pieceId);
    if (!p || !p->alive) {
        cancelEdit();
        return;
    }
    Vec3 c = pieceBounds(p->slot).center();
    if (length(c - player.eyePivot()) > EDIT_RANGE + 4.0f) {
        cancelEdit();
        return;
    }

    // Right click: reset the piece back to full and leave edit mode.
    if (in.aimPressed) {
        p->removed = 0;
        showToast("EDIT RESET");
        cancelEdit();
        return;
    }

    int tile = pieceTileUnderRay(*p, camera.aimRay, 60.0f);
    EditState& ed = player.edit;
    if (in.firePressed && tile >= 0) {
        ed.dragging = true;
        ed.dragSelect = !((ed.selected >> tile) & 1u);
        ed.firstTile = ed.lastTile = tile;
        if (p->slot.type == PieceType::Ramp) ed.selected = 0;  // stairs: each swipe starts fresh
    }
    if (ed.dragging && in.fire && tile >= 0) {
        if (ed.dragSelect) ed.selected |= (uint16_t)(1u << tile);
        else ed.selected &= (uint16_t)~(1u << tile);
        if (tile != ed.lastTile) ed.lastTile = tile;
    }
    // Releasing the mouse confirms the edit.
    if (ed.dragging && (in.fireReleased || !in.fire)) {
        ed.dragging = false;
        confirmEdit();
    }
}

}  // namespace bf
