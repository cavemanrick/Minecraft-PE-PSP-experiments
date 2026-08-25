
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

protected:
    virtual Entity* findAttackTarget();
    virtual void dropDeathLoot();
    virtual int  getDeathLoot() { return 0; }

    virtual void addAdditonalSaveData(CompoundTag* tag);
    virtual void readAdditionalSaveData(CompoundTag* tag);

private:
    int angerTime, playAngrySoundIn, stunedTime;
};

#endif
