
#ifndef MCPSP_WORLD_ENTITY_MONSTER_PIG_ZOMBIE_H
#define MCPSP_WORLD_ENTITY_MONSTER_PIG_ZOMBIE_H

#include "world/entity/monster/zombie.h"

class PigZombie : public Zombie {
public:
    PigZombie(Level* level);

    virtual void tick();
    virtual bool hurt(Entity* source, int damage);
    virtual int  getEntityTypeId() const;
    virtual int  getAttackTime() { return 40; }

    // Zombified piglins spawn regardless of light level, unlike every
    // other monster in this codebase. Monster::canSpawn gates on
    // isDarkEnoughToSpawn, which is the right rule for an Overworld night
    // but the wrong one for the Nether: the whole biome is lit by lava,
    // glowstone and the per-chunk torches the generator places, so a
    // light-gated pigman would only ever appear in the few genuinely dark
    // pockets between them -- which is the opposite of how the Nether
    // Wastes is meant to feel. This drops straight to Mob::canSpawn, which
    // still enforces the checks that actually matter (unobstructed, no
    // colliding geometry, not inside a liquid).
    virtual bool canSpawn();

    virtual const char* getAmbientSound() { return "mob.zombiepig.zpig"; }
    virtual const char* getHurtSound()    { return "mob.zombiepig.zpighurt"; }
    virtual const char* getDeathSound()   { return "mob.zombiepig.zpigdeath"; }

    void alert(Entity* target);

    // Anchors wandering AI to a small home patch near where this pig
    // zombie spawned, the same idea as Strider::setWanderCenter (see
    // strider.h) but expressed through getWalkTargetValue instead of a
    // direct movement override, since PigZombie is a PathfinderMob and
    // strolls via PathfinderMob::findRandomStrollLocation() picking the
    // best-scoring nearby candidate rather than Strider's own direct
    // steer-and-move. A pig zombie with no home set (e.g. one loaded from
    // a save written before this existed) just falls back to
    // Monster::getWalkTargetValue's ordinary darkness preference.
    void setHome(float hx, float hy, float hz);

protected:
    virtual Entity* findAttackTarget();
    virtual void dropDeathLoot();
    virtual int  getDeathLoot() { return 0; }
    virtual float getWalkTargetValue(int x, int y, int z);

    virtual void addAdditonalSaveData(CompoundTag* tag);
    virtual void readAdditionalSaveData(CompoundTag* tag);

private:
    int angerTime, playAngrySoundIn, stunedTime;
    bool hasHome;
    float homeX, homeY, homeZ;
};

#endif
