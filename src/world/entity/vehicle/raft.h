
#ifndef MCPSP_WORLD_ENTITY_VEHICLE_RAFT_H
#define MCPSP_WORLD_ENTITY_VEHICLE_RAFT_H

#include "world/entity/mob.h"

class Player;
class CompoundTag;

// A placed, rideable vehicle -- not a creature. Deliberately a direct Mob
// subclass, the same choice Strider (strider.h) made and for the same
// reason: no PathfinderMob machinery is wanted (no AI, no A* budget spent),
// just the rider/vehicle plumbing Mob already shares with Player via
// startRiding/getVehicle/dismountVehicle.
//
// Health is real (getMaxHealth/getDeathLoot below) so hitting an empty
// raft enough times breaks it and returns the item, same as a vanilla
// boat -- there is no bespoke "destroy the vehicle" path to write.
class Raft : public Mob {
public:
    Raft(Level* level);

    virtual void tick();
    virtual void aiStep();
    virtual void travel(float xs, float yf);
    virtual bool playerInteract();
    virtual void remove();
    virtual int getEntityTypeId() const;
    virtual int getCreatureBaseType() const { return 0; }
    virtual int getMaxHealth() { return 6; }
    virtual int getDeathLoot();
    virtual bool removeWhenFarAway() { return true; }
    virtual bool isPickable() { return !removed; }

    virtual const char* getHurtSound()  { return "random.break"; }
    virtual const char* getDeathSound() { return "random.break"; }

    void setRiderInput(float strafe, float forward);
    Player* getRider() const { return rider; }

private:
    Player* rider;
    float riderStrafe;
    float riderForward;

    // Buoyancy is a simple "am I over water" surface snap, not a real
    // fluid sim -- this codebase has no flow-vector/fluid-height system
    // (confirmed absent project-wide), so the raft cannot be pushed by
    // current the way vanilla's can. It floats and can be paddled, but
    // won't drift downstream on its own.
    bool findWaterSurfaceY(float* outY) const;
    void syncRider();

    virtual void readAdditionalSaveData(CompoundTag* tag);
    virtual void addAdditonalSaveData(CompoundTag* tag);
};

#endif
