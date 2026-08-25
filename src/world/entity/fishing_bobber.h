#ifndef MCPSP_WORLD_ENTITY_FISHING_BOBBER_H
#define MCPSP_WORLD_ENTITY_FISHING_BOBBER_H

#include "world/entity/entity.h"

// The float on the end of a cast fishing line.
//
// Lifecycle is owned entirely by FishingRodItem (fishing_rod_item.cpp),
// which holds this entity's id -- never a raw pointer, because
// Level::tickEntities deletes removed entities outright, so a stored
// pointer would dangle the moment the bobber timed out or the player
// walked away.
//
// Three states, in order:
//   ST_FLYING   -- ballistic, exactly like Throwable. Ends on hitting a
//                  tile or entering water.
//   ST_FLOATING -- bobbing on the surface, counting down to a bite.
//   ST_BITING   -- the brief window during which reeling in actually
//                  catches something. The bobber is visibly yanked under
//                  and splashes, which is the whole tell the player gets.
// A missed bite falls back to ST_FLOATING with a fresh timer rather than
// ending the cast, so the player isn't punished for a slow reaction with
// a full re-cast.
class FishingBobber : public Entity {
    typedef Entity super;
public:
    enum State { ST_FLYING = 0, ST_FLOATING = 1, ST_BITING = 2 };

    explicit FishingBobber(Level* level);
    FishingBobber(Level* level, float x, float y, float z, float yaw, float pitch);

    virtual void tick();
    virtual int  getEntityTypeId() const;
    virtual bool isPickable() { return false; }

    // Never persisted. See the note on EntityTypes::IdFishingBobber.
    virtual bool save(CompoundTag*) { return false; }
    virtual void addAdditonalSaveData(CompoundTag*) {}
    virtual void readAdditionalSaveData(CompoundTag*) {}

    // True only while a reel-in would land a catch.
    bool canCatch() const { return state == ST_BITING; }
    bool inWaterNow() const { return state != ST_FLYING; }

    int state;
    int life;        // total ticks alive, for the hard timeout
    int biteTimer;   // ticks until the next bite (ST_FLOATING)
    int biteWindow;  // ticks of bite left (ST_BITING)

private:
    void init();
    void startWaitingForBite();
};

#endif
