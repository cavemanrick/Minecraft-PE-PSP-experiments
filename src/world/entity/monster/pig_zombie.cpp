#include "world/entity/monster/pig_zombie.h"
#include "world/entity/entity_types.h"
#include "world/entity/arrow.h"
#include "world/level/level.h"
#include "world/item/item.h"
#include "nbt/compound_tag.h"
#include <vector>

PigZombie::PigZombie(Level* level)
:   Zombie(level, ER_PIGZOMBIE_RENDERER),
    angerTime(0),
    playAngrySoundIn(0),
    stunedTime(TicksPerSecond * 3),
    hasHome(false), homeX(0), homeY(0), homeZ(0)
{
    runSpeed = 0.7f;
    attackDamage = 5;
    fireImmune = true;
    health = getMaxHealth();
}

int PigZombie::getEntityTypeId() const { return EntityTypes::IdPigZombie; }

// See the note in pig_zombie.h: light level is deliberately not consulted.
bool PigZombie::canSpawn() { return Mob::canSpawn(); }

void PigZombie::setHome(float hx, float hy, float hz) {
    hasHome = true;
    homeX = hx; homeY = hy; homeZ = hz;
}

// Deliberately does NOT call Monster::getWalkTargetValue (which biases
// toward darker spots -- an ordinary Overworld-monster preference).
// PigZombie already opts out of the light-level rule entirely for
// spawning (see canSpawn() above and the comment in pig_zombie.h); the
// same reasoning applies to wandering, since the Nether Wastes is meant
// to be lit by lava/glowstone/torches rather than the pockets of darkness
// a light-avoiding stroll would seek out. With no home set, every
// candidate scores equally (0), which is the same as
// PathfinderMob::getWalkTargetValue's own unbiased default -- an
// old-save pig zombie with no stored home just wanders freely, same as
// before this existed.
float PigZombie::getWalkTargetValue(int x, int y, int z) {
    if (!hasHome) return 0.0f;
    float dx = (float)x - homeX, dy = (float)y - homeY, dz = (float)z - homeZ;
    float distSq = dx * dx + dy * dy + dz * dz;
    // Strongly discourage stroll candidates far from home without an
    // outright hard cutoff -- findRandomStrollLocation just picks
    // whichever of its 10 random nearby candidates scores highest, so a
    // smooth penalty that grows with distance is enough to keep the
    // chosen stroll target biased homeward without needing to reject
    // candidates outright (which could leave hasBest false on every
    // sampled point once actually far from home and stop the mob from
    // strolling back at all).
    return -distSq * 0.01f;
}

void PigZombie::tick() {
    if (stunedTime > 0) stunedTime--;
    if (playAngrySoundIn > 0) {
        if (--playAngrySoundIn == 0)

            level->playSound(this, "mob.zombiepig.zpigangry",
                             getSoundVolume() * 2.0f, getVoicePitch() * 1.8f);
    }
    Zombie::tick();
}

Entity* PigZombie::findAttackTarget() {
    if (stunedTime != 0) return 0;
    Entity* t = Monster::findAttackTarget();
    if (angerTime == 0) {

        if (t && t->distanceTo(x, y, z) < 5.0f) return t;
        return 0;
    }
    return t;
}

void PigZombie::alert(Entity* target) {
    if (!target) return;
    attackTargetId = target->entityId;

    angerTime = 400 + sharedRandom.nextInt(400);
    playAngrySoundIn = sharedRandom.nextInt(40);
}

bool PigZombie::hurt(Entity* source, int damage) {

    Entity* attacker = 0;
    if (source) {
        if (source->isPlayer()) {
            attacker = source;
        } else if (source->isEntityType(EntityTypes::IdArrow)) {
            Arrow* ar = (Arrow*)source;
            if (ar->ownerId != 0) {
                Entity* o = level->getEntity(ar->ownerId);
                if (o && o->isPlayer()) attacker = o;
            }
        }
    }

    bool applied = Zombie::hurt(source, damage);

    if (applied && attacker) {

        AABB box = bb.grow(12.0f, 12.0f, 12.0f);
        static std::vector<Entity*> nearby;
        level->getEntities(this, box, nearby);
        for (size_t i = 0; i < nearby.size(); i++) {
            if (nearby[i]->isEntityType(EntityTypes::IdPigZombie))
                ((PigZombie*)nearby[i])->alert(attacker);
        }
        alert(attacker);
    }
    return applied;
}

void PigZombie::dropDeathLoot() {
    int count = sharedRandom.nextInt(2);
    for (int i = 0; i < count; i++) spawnAtLocation(ITEM_GOLD_INGOT, 1);
}

void PigZombie::addAdditonalSaveData(CompoundTag* tag) {
    Mob::addAdditonalSaveData(tag);
    tag->putShort("Anger", (short)angerTime);
    tag->putBoolean("HasHome", hasHome);
    if (hasHome) {
        tag->putFloat("HomeX", homeX);
        tag->putFloat("HomeY", homeY);
        tag->putFloat("HomeZ", homeZ);
    }
}

void PigZombie::readAdditionalSaveData(CompoundTag* tag) {
    Mob::readAdditionalSaveData(tag);
    angerTime = tag->getShort("Anger");
    hasHome = tag->getBoolean("HasHome");
    if (hasHome) {
        homeX = tag->getFloat("HomeX");
        homeY = tag->getFloat("HomeY");
        homeZ = tag->getFloat("HomeZ");
    }
}
