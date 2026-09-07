#include "world/entity/animal/pig.h"
#include "world/entity/entity_types.h"
#include "world/entity/player.h"
#include "world/entity/local_player.h"
#include "world/level/level.h"
#include "world/item/item.h"
#include "world/inventory/inventory.h"
#include "nbt/compound_tag.h"

// Riding currently gives literally zero speed benefit over walking on
// foot: Pig has no travel() override, so it runs through Mob::travel's
// ground-friction math same as an unridden pig always has, and that math
// multiplies walkingSpeed by the same friction factor a walking player
// gets. Reported as noticeably too slow -- bumped to roughly 1.7x the
// normal 0.1f walking pace while ridden, restored to normal when
// dismounted. Estimate, not hardware-measured -- may need another pass,
// same caveat as Strider's ridden constant.
static const float kPigWalkSpeed       = 0.1f;
static const float kPigRiddenWalkSpeed = 0.17f;

Pig::Pig(Level* level)
: Animal(level), rider(0), saddled(false), riderStrafe(0), riderForward(0) {
    setSize(0.9f, 0.9f);
    heightOffset = 0.0f;
    walkingSpeed = kPigWalkSpeed;
    entityRendererId = ER_PIG_RENDERER;
    health = getMaxHealth();
}

int Pig::getEntityTypeId() const { return EntityTypes::IdPig; }

int Pig::getDeathLoot()          { return onFire > 0 ? ITEM_PORKCHOP_COOKED : ITEM_PORKCHOP_RAW; }

void Pig::setRiderInput(float strafe, float forward) {
    riderStrafe = strafe;
    riderForward = forward;
}

void Pig::syncRider() {
    if (!rider || rider->getVehicle() != this) {
        rider = 0;
        riderStrafe = riderForward = 0.0f;
        return;
    }

    // Same eye-vs-feet correction Strider::syncRider documents: the
    // player's y is an eye/reference position while the pig's own y is
    // feet-relative (Pig::heightOffset == 0), so the seat height has to
    // add the rider's own heightOffset back in. 0.7f is the pig's own
    // body height (setSize(0.9f, 0.9f) above) minus a little sink so the
    // rider doesn't appear to float above the pig's back -- tune on
    // hardware like Strider's 1.15f and the raft's 0.35f were.
    rider->setPos(x, y + 0.7f + rider->heightOffset, z);
    rider->xd = xd;
    rider->yd = yd;
    rider->zd = zd;
}

void Pig::remove() {
    if (rider && rider->getVehicle() == this) rider->dismountVehicle();
    rider = 0;
    Animal::remove();
}

bool Pig::playerInteract() {
    Player* p = level ? level->player : 0;
    if (!p || !p->isAlive()) return false;

    if (p->getVehicle() == this) {
        p->dismountVehicle();
        rider = 0;
        riderStrafe = riderForward = 0.0f;
        walkingSpeed = kPigWalkSpeed;
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
    walkingSpeed = kPigRiddenWalkSpeed;
    syncRider();
    return true;
}

void Pig::aiStep() {
    if (rider && rider->getVehicle() != this) {
        // Caught an external dismount here too (e.g. the L-trigger
        // shortcut in gamemode.cpp, which clears Player::vehicle directly
        // without going through Pig::playerInteract() at all) -- that
        // path has no way to notify the mount, so this per-tick check is
        // the only place a dismount via that route is ever observed.
        rider = 0;
        walkingSpeed = kPigWalkSpeed;
    }

    if (rider) {
        // Bypass Animal's own wander/flee/breed AI entirely while ridden --
        // same reasoning as Strider, just arrived at from the opposite
        // direction: Strider skips PathfinderMob at all times because it
        // never wants that cost, while Pig only skips it here, for exactly
        // the ticks a rider is steering it.
        xxa = riderStrafe;
        yya = riderForward;
        yRot = rider->yRot;
        xRot = 0.0f;
        travel(xxa, yya);
        mobRiderAutoJump();
        syncRider();
        return;
    }

    Animal::aiStep();
}

void Pig::addAdditonalSaveData(CompoundTag* tag) {
    Animal::addAdditonalSaveData(tag);
    tag->putBoolean("Saddled", saddled);
}

void Pig::readAdditionalSaveData(CompoundTag* tag) {
    Animal::readAdditionalSaveData(tag);
    saddled = tag->getBoolean("Saddled");
}
