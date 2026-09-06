
#ifndef MCPSP_WORLD_ENTITY_ANIMAL_PIG_H
#define MCPSP_WORLD_ENTITY_ANIMAL_PIG_H

#include "world/entity/animal/animal.h"

class Player;
class CompoundTag;

class Pig : public Animal {
public:
    Pig(Level* level);

    virtual int  getEntityTypeId() const;
    virtual int  getMaxHealth() { return 10; }
    virtual int  getDeathLoot();

    virtual const char* getAmbientSound() { return "mob.pig"; }
    virtual const char* getHurtSound()    { return "mob.pig"; }
    virtual const char* getDeathSound()   { return "mob.pigdeath"; }

    // Rider/vehicle plumbing, same shape as Strider's -- see
    // strider.h/.cpp for the fuller explanation of each piece. Pig differs
    // from Strider in two ways worth noting up front:
    //   - Pig keeps its normal Animal (PathfinderMob) wander/flee/breed AI
    //     when unridden, instead of a hand-rolled cheap wander -- there was
    //     no reason to bypass PathfinderMob here the way Strider does,
    //     since a pig was already paying that cost before this existed.
    //   - travel() is NOT overridden. Mob::travel() already does ordinary
    //     ground walking with gravity, and a mounted pig doesn't need
    //     Strider's lava-surface locking or the raft's water buoyancy, so
    //     the inherited implementation is used as-is.
    virtual void aiStep();
    virtual bool playerInteract();
    virtual void remove();

    void setRiderInput(float strafe, float forward);
    Player* getRider() const { return rider; }
    bool isSaddled() const { return saddled; }

private:
    Player* rider;
    bool saddled;
    float riderStrafe;
    float riderForward;

    void syncRider();
    virtual void readAdditionalSaveData(CompoundTag* tag);
    virtual void addAdditonalSaveData(CompoundTag* tag);
};

#endif
