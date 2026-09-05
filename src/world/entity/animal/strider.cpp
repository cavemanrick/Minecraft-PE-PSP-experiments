#include "world/entity/animal/strider.h"
#include "world/entity/player.h"
#include "world/entity/local_player.h"
#include "world/entity/entity_types.h"
#include "world/level/level.h"
#include "world/level/chunk/chunk.h"
#include "world/level/levelgen/nether_gen.h"
#include "world/item/item.h"
#include "world/inventory/inventory.h"
#include "nbt/compound_tag.h"
#include <cmath>

Strider::Strider(Level* level)
: Mob(level), rider(0), saddled(false), riderStrafe(0), riderForward(0), lavaSnapTimer(0),
  hasWanderCenter(false), wanderCenterX(0), wanderCenterY(0), wanderCenterZ(0) {
    setSize(0.9f, 1.7f);
    heightOffset = 0.0f;
    footSize = 0.5f;
    walkingSpeed = 0.10f;
    fireImmune = true;
    health = getMaxHealth();
    entityRendererId = ER_STRIDER_RENDERER;
}

int Strider::getEntityTypeId() const { return EntityTypes::IdStrider; }

int Strider::findLavaSurfaceY(int bx, int bz) const {
    const int top = netherShellFloorBaseY() - 1;
    if (top < 1) return -1;

    // The generator's main lava sea is at/under the lava level. Scanning
    // downward finds the highest lava block in this column, which is the
    // surface a strider should occupy. This is intentionally only ~20
    // block reads and is used for a four-mob cap, so it is far cheaper than
    // invoking a path search.
    for (int y = top; y >= 1; --y) {
        unsigned char id = (unsigned char)level->getTile(bx, y, bz);
        if (isLavaId(id)) {
            unsigned char above = (unsigned char)level->getTile(bx, y + 1, bz);
            if (!level->isSolidBlockingTile(bx, y + 1, bz) && !isLavaId(above))
                return y;
        }
    }
    return -1;
}

bool Strider::canSpawn() {
    // Unlike ordinary mobs, liquid is the intended spawn medium.
    return level->isUnobstructed(bb) && level->getCubes(this, bb).empty();
}

void Strider::setRiderInput(float strafe, float forward) {
    riderStrafe = strafe;
    riderForward = forward;
}

void Strider::setWanderCenter(float wx, float wy, float wz) {
    hasWanderCenter = true;
    wanderCenterX = wx; wanderCenterY = wy; wanderCenterZ = wz;
}

void Strider::syncRider() {
    if (!rider || rider->getVehicle() != this) {
        rider = 0;
        riderStrafe = riderForward = 0.0f;
        return;
    }

    // Player y is its eye/reference position (LocalPlayer::heightOffset ==
    // PLAYER_EYE, set in local_player.cpp), NOT feet-relative like the
    // strider's own y (Strider::heightOffset == 0). Writing the strider's
    // seat height directly into rider->y, with no correction, was putting
    // the player's EYES at seat height -- meaning the player's actual feet
    // (y - heightOffset) landed roughly PLAYER_EYE below the seat, well
    // under the strider's body and into the lava surface it's standing on.
    // Adding rider->heightOffset back converts the seat height into
    // whatever y-convention the specific rider entity uses, the same way
    // Player::dismountVehicle (player.cpp) already has to for the
    // dismount case.
    rider->setPos(x, y + 1.15f + rider->heightOffset, z);
    rider->xd = xd;
    rider->yd = yd;
    rider->zd = zd;
}

void Strider::remove() {
    if (rider && rider->getVehicle() == this) rider->dismountVehicle();
    rider = 0;
    Mob::remove();
}

bool Strider::playerInteract() {
    Player* p = level ? level->player : 0;
    if (!p || !p->isAlive()) return false;

    if (p->getVehicle() == this) {
        p->dismountVehicle();
        rider = 0;
        riderStrafe = riderForward = 0.0f;
        return true;
    }

    ItemInstance* sel = p->inventory ? p->inventory->getSelected() : 0;
    if (!saddled) {
        if (!sel || sel->id != ITEM_SADDLE) return false;
        saddled = true;
        if (!p->inventory->isCreative()) p->inventory->consumeSelected();
        return true;
    }

    if (p->isRiding()) return false;

    rider = p;
    p->startRiding(this);
    p->yRot = yRot;
    p->xRot = 0.0f;
    syncRider();
    return true;
}

void Strider::aiStep() {
    if (isImmobile()) {
        riderStrafe = riderForward = 0.0f;
        return;
    }

    if (rider && rider->getVehicle() != this) rider = 0;

    // Far-away striders despawn like other lightweight mobs. Mounted
    // striders never get removed by this rule because the rider is nearby.
    if (level->player && !rider) {
        float dx = x - level->player->x, dy = y - level->player->y, dz = z - level->player->z;
        if (dx * dx + dy * dy + dz * dz > 96.0f * 96.0f) {
            remove();
            return;
        }
    }

    if (rider) {
        xxa = riderStrafe;
        yya = riderForward;
        yRot = rider->yRot;
        xRot = 0.0f;
    } else if (hasWanderCenter) {
        // Small-area wander: same cheap turn-and-walk idiom as the
        // unrestricted case below, but steered back toward wanderCenter
        // whenever the strider has drifted past WANDER_LEASH blocks from
        // it, instead of picking a heading at random. No target search or
        // pathfinding added -- this is still just a random-walk with a
        // bias term, so it stays as cheap as the plain wander case. This
        // is what actually makes a spawned-in strider easy to catch: it
        // stays near where it was found rather than drifting indefinitely
        // across the whole biome.
        const float WANDER_LEASH = 12.0f;
        float dxw = x - wanderCenterX, dzw = z - wanderCenterZ;
        float distw = sqrtf(dxw * dxw + dzw * dzw);

        xxa = 0.0f;
        yya = 0.0f;
        if (distw > WANDER_LEASH) {
            // Past the leash: turn directly toward center rather than
            // rolling the random-turn dice, so it reliably comes back
            // instead of maybe wandering further out first.
            float targetYaw = atan2f(-dxw, dzw) * (180.0f / 3.14159265f);
            float diff = targetYaw - yRot;
            while (diff < -180.0f) diff += 360.0f;
            while (diff >= 180.0f) diff -= 360.0f;
            yRotA = diff * 0.15f;
            yya = 0.35f;
        } else {
            if (sharedRandom.nextInt(30) == 0)
                yRotA = (sharedRandom.nextFloat() - 0.5f) * 18.0f;
            if (sharedRandom.nextInt(20) == 0) yya = 0.35f;
        }
        yRot += yRotA;
        yRotA *= 0.85f;
        xRot = 0.0f;
    } else {
        // Cheap wandering: no target search, no A*, no path allocation.
        xxa = 0.0f;
        yya = 0.0f;
        if (sharedRandom.nextInt(30) == 0)
            yRotA = (sharedRandom.nextFloat() - 0.5f) * 18.0f;
        yRot += yRotA;
        yRotA *= 0.85f;
        if (sharedRandom.nextInt(20) == 0) yya = 0.35f;
        xRot = 0.0f;
    }

    xxa *= 0.98f;
    yya *= 0.98f;
    travel(xxa, yya);

    // Keep an unridden strider on the lava surface. While mounted this is
    // also useful when crossing a shallow change in lava height.
    if (++lavaSnapTimer >= 4 || rider) {
        lavaSnapTimer = 0;
        int sy = findLavaSurfaceY((int)floorf(x), (int)floorf(z));
        if (sy >= 0 && fabsf(y - (float)sy) < 2.0f) {
            setPos(x, (float)sy, z);
            yd = 0.0f;
        }
    }

    syncRider();
}

void Strider::travel(float xs, float yf) {
    // Direct surface movement. Entity::move still supplies block collision,
    // but there is no gravity/pathfinding and no expensive liquid swim AI.
    const bool lava = isInLava();
    mobMoveRelative(xs, yf, rider ? 0.115f : 0.055f);
    if (lava) {
        move(xd, 0.0f, zd);
        yd = 0.0f;
    } else {
        // Striders can leave lava and walk on land. Keep this path cheap:
        // ordinary collision plus gravity, with no PathfinderMob machinery.
        yd -= 0.08f;
        if (onGround && yd < 0.0f) yd = 0.0f;
        move(xd, yd, zd);
    }
    xd *= lava ? 0.82f : 0.65f;
    zd *= lava ? 0.82f : 0.65f;
}

void Strider::tick() {
    Mob::tick();
}

void Strider::addAdditonalSaveData(CompoundTag* tag) {
    Mob::addAdditonalSaveData(tag);
    tag->putBoolean("Saddled", saddled);
    tag->putBoolean("HasWanderCenter", hasWanderCenter);
    if (hasWanderCenter) {
        tag->putFloat("WanderCenterX", wanderCenterX);
        tag->putFloat("WanderCenterY", wanderCenterY);
        tag->putFloat("WanderCenterZ", wanderCenterZ);
    }
}

void Strider::readAdditionalSaveData(CompoundTag* tag) {
    Mob::readAdditionalSaveData(tag);
    saddled = tag->getBoolean("Saddled");
    // getBoolean/getFloat both default safely (false/0.0f) when the tag is
    // absent, so a strider saved before this field existed loads with
    // hasWanderCenter=false and simply falls back to the old unrestricted
    // wander -- not yanked back to a phantom (0,0,0) center.
    hasWanderCenter = tag->getBoolean("HasWanderCenter");
    if (hasWanderCenter) {
        wanderCenterX = tag->getFloat("WanderCenterX");
        wanderCenterY = tag->getFloat("WanderCenterY");
        wanderCenterZ = tag->getFloat("WanderCenterZ");
    }
}
