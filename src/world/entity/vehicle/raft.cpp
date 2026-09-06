#include "world/entity/vehicle/raft.h"
#include "world/entity/player.h"
#include "world/entity/local_player.h"
#include "world/entity/entity_types.h"
#include "world/level/level.h"
#include "world/item/item.h"
#include "nbt/compound_tag.h"
#include <cmath>

Raft::Raft(Level* level)
: Mob(level), rider(0), riderStrafe(0), riderForward(0) {
    // Wider and flatter than any mob currently in the codebase (a raft is
    // a platform, not a body), and short enough to duck under a 2-high
    // doorway while unridden -- riding height is handled separately in
    // syncRider(), same seat-offset correction Strider needed.
    setSize(1.4f, 0.6f);
    heightOffset = 0.3f;
    health = getMaxHealth();
    // No renderer exists yet for the raft. ER_RAFT_RENDERER is a real,
    // dedicated slot in EntityRendererId (see that header), but nothing is
    // assigned to it yet in EntityRenderDispatcher's constructor -- a raft
    // left here will ride, collide, and break correctly, it just won't
    // draw. This is a deliberate placeholder, not a bug to silently work
    // around: a real raft mesh/renderer needs a RaftRenderer class and an
    // assign(ER_RAFT_RENDERER, ...) call once art exists.
    entityRendererId = ER_RAFT_RENDERER;
}

int Raft::getEntityTypeId() const { return EntityTypes::IdBambooRaft; }

int Raft::getDeathLoot() { return ITEM_BAMBOO_RAFT; }

bool Raft::findWaterSurfaceY(float* outY) const {
    // Sample the block column at the raft's own feet rather than scanning
    // a whole range like Strider's lava search does -- a raft is placed
    // directly on water by its item (see BambooRaftItem::useOn) and never
    // needs to locate a distant surface, only to hold the one under it.
    int bx = (int)floorf(x);
    int bz = (int)floorf(z);
    int by = (int)floorf(y);

    for (int dy = 1; dy >= -2; --dy) {
        unsigned char id = (unsigned char)level->getTile(bx, by + dy, bz);
        if (isWaterId(id)) {
            unsigned char above = (unsigned char)level->getTile(bx, by + dy + 1, bz);
            if (!isWaterId(above) && !level->isSolidBlockingTile(bx, by + dy + 1, bz)) {
                *outY = (float)(by + dy) + 1.0f;
                return true;
            }
        }
    }
    return false;
}

void Raft::setRiderInput(float strafe, float forward) {
    riderStrafe = strafe;
    riderForward = forward;
}

void Raft::syncRider() {
    if (!rider || rider->getVehicle() != this) {
        rider = 0;
        riderStrafe = riderForward = 0.0f;
        return;
    }
    // Same eye-vs-feet correction Strider::syncRider documents: the
    // player's y is an eye/reference position (heightOffset == PLAYER_EYE)
    // while the raft's own y is feet-relative (heightOffset == 0.3f
    // above), so the rider's seat position must add the rider's own
    // heightOffset back in, not the raft's.
    rider->setPos(x, y + 0.35f + rider->heightOffset, z);
    rider->xd = xd;
    rider->yd = yd;
    rider->zd = zd;
}

void Raft::remove() {
    if (rider && rider->getVehicle() == this) rider->dismountVehicle();
    rider = 0;
    Mob::remove();
}

bool Raft::playerInteract() {
    Player* p = level ? level->player : 0;
    if (!p || !p->isAlive()) return false;

    if (p->getVehicle() == this) {
        p->dismountVehicle();
        rider = 0;
        riderStrafe = riderForward = 0.0f;
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

void Raft::aiStep() {
    if (rider && rider->getVehicle() != this) rider = 0;

    // Unridden rafts despawn at range like other lightweight entities.
    // Never removed while ridden -- the rider being nearby is definitionally
    // guaranteed, same guard Strider::aiStep uses.
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
    } else {
        xxa = 0.0f;
        yya = 0.0f;
    }

    travel(xxa, yya);

    float surfaceY;
    if (findWaterSurfaceY(&surfaceY)) {
        // Snap toward the surface rather than jumping straight to it --
        // the same "gentle correction, not a teleport" approach Strider
        // uses for lava, so crossing a one-block change in water depth
        // doesn't pop the raft visibly.
        float diff = surfaceY - y;
        if (fabsf(diff) < 1.5f) {
            y += diff * 0.3f;
            yd = 0.0f;
        }
    } else {
        // Beached or never over water: fall like any solid-colliding
        // entity rather than hang in the air.
        yd -= 0.08f;
        if (onGround && yd < 0.0f) yd = 0.0f;
    }

    syncRider();
}

void Raft::travel(float xs, float yf) {
    // Canoe-style: forward/back paddling plus turning, no independent
    // sideways strafe -- xs steers yaw instead of sliding the raft
    // sideways, which reads as "paddling" rather than the strider's
    // holonomic movement.
    const float turnSpeed = 3.0f;
    yRot -= xs * turnSpeed;

    float speed = rider ? 0.10f : 0.0f;
    float rad = yRot * (float)M_PI / 180.0f;
    xd += -sinf(rad) * yf * speed * 0.1f;
    zd += cosf(rad) * yf * speed * 0.1f;

    move(xd, yd, zd);
    xd *= 0.90f;
    zd *= 0.90f;
}

void Raft::tick() {
    Mob::tick();
}

void Raft::addAdditonalSaveData(CompoundTag* tag) {
    Mob::addAdditonalSaveData(tag);
    // Nothing raft-specific to persist yet -- no saddle-equivalent, no
    // color/variant data. Present for symmetry with Strider and so a
    // future field (e.g. a raft skin variant) has an obvious place to go.
}

void Raft::readAdditionalSaveData(CompoundTag* tag) {
    Mob::readAdditionalSaveData(tag);
}
