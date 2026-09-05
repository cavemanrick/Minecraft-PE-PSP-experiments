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

    // Anchors wandering AI to a small home patch instead of an unbounded
    // random walk. Sets both the center and a "has a center" flag in one
    // call -- a strider spawned before this existed (loaded from an old
    // save) simply has no center and falls back to the old unrestricted
    // wander, rather than defaulting to (0,0,0) and yanking itself across
    // the map on load.
    void setWanderCenter(float wx, float wy, float wz);

private:
    Player* rider;
    bool saddled;
    float riderStrafe;
    float riderForward;
    int lavaSnapTimer;
    bool hasWanderCenter;
    // wanderCenterY is stored but not currently read by the leash logic in
    // aiStep() (Warped Forest floor is flat nylium, so only the X/Z
    // distance matters for now) -- kept for a future vertical leash if
    // striders ever wander terrain with real elevation change, rather
    // than dropping a third of the spawn point and having to re-add it
    // later.
    float wanderCenterX, wanderCenterY, wanderCenterZ;

    int findLavaSurfaceY(int x, int z) const;
    void syncRider();
    virtual void readAdditionalSaveData(CompoundTag* tag);
    virtual void addAdditonalSaveData(CompoundTag* tag);
};

#endif
