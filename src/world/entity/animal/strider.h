#ifndef MCPSP_WORLD_ENTITY_ANIMAL_STRIDER_H
#define MCPSP_WORLD_ENTITY_ANIMAL_STRIDER_H

#include "world/entity/mob.h"

class Player;
class CompoundTag;

// A deliberately lightweight Nether mob. It does not inherit PathfinderMob:
// striders wander on lava and use direct movement, so adding them does not
// consume the shared A* budget that protects the PSP CPU from pathfinding.
class Strider : public Mob {
public:
    Strider(Level* level);

    virtual void tick();
    virtual void aiStep();
    virtual void travel(float xs, float yf);
    virtual bool canSpawn();
    virtual bool playerInteract();
    virtual void remove();
    virtual int getEntityTypeId() const;
    virtual int getCreatureBaseType() const { return 0; }
    virtual int getMaxHealth() { return 20; }
    virtual bool removeWhenFarAway() { return true; }
    virtual const char* getAmbientSound() { return "mob.strider.idle"; }
    virtual const char* getHurtSound() { return "mob.strider.hurt"; }
    virtual const char* getDeathSound() { return "mob.strider.death"; }
    virtual int getAmbientSoundInterval() { return 12 * TicksPerSecond; }

    void setRiderInput(float strafe, float forward);
    Player* getRider() const { return rider; }
    bool isSaddled() const { return saddled; }

private:
    Player* rider;
    bool saddled;
    float riderStrafe;
    float riderForward;
    int lavaSnapTimer;

    int findLavaSurfaceY(int x, int z) const;
    void syncRider();
    virtual void readAdditionalSaveData(CompoundTag* tag);
    virtual void addAdditonalSaveData(CompoundTag* tag);
};

#endif
