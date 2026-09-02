#ifndef MCPSP_WORLD_ENTITY_FIREBALL_H
#define MCPSP_WORLD_ENTITY_FIREBALL_H

#include "world/entity/entity.h"

// A Ghast's projectile. Deliberately its own Entity subclass rather than a
// Throwable: Throwable's tick() bakes in gravity and water-drag behaviour
// that a fireball's real, wiki-confirmed flight ("straight trajectory
// unaffected by gravity") explicitly does not have, and Throwable has no
// ownerId field to support the deflection mechanic below.
class Fireball : public Entity {
    typedef Entity super;
public:
    Fireball(Level* level);
    Fireball(Level* level, Entity* owner, float px, float py, float pz,
              float dx, float dy, float dz, float speed);

    virtual void tick();
    virtual int  getEntityTypeId() const;
    virtual bool isPickable() { return !removed; }

    // Deflection: melee/arrow damage against a fireball does not remove it
    // the way it would a normal entity. Instead it reverses the fireball's
    // velocity and reassigns ownership to the attacker, so the redirected
    // shot can now hurt the ghast that fired it -- see hurt()'s
    // implementation in fireball.cpp for the full reasoning.
    virtual bool hurt(Entity* source, int damage);

    virtual void addAdditonalSaveData(CompoundTag* tag);
    virtual void readAdditionalSaveData(CompoundTag* tag);

    // Entity id of whoever "owns" this shot right now: the ghast that
    // fired it, or the player if it's been deflected. 0 means no owner
    // (never hits anything by owner-exclusion). Distinct from Arrow's
    // ownerId in one respect: Arrow only ever prevents self-hits for a
    // few ticks after launch, but a fireball's owner also determines
    // whether an explosion that catches a ghast should count as a
    // deflection kill for the "Return to Sender" achievement -- see
    // deflectedByPlayer below.
    int  ownerId;

    // True once this fireball has been hit back by the player at least
    // once. Kept separate from "ownerId == player's id" because ownerId
    // gets reassigned to whichever entity most recently deflected it (in
    // principle another mob could bat it, though only the player currently
    // triggers hurt() this way), whereas the achievement specifically
    // wants "a fireball the player redirected", matching vanilla's own
    // wording for Return to Sender.
    bool deflectedByPlayer;

    int  life;

private:
    void configure();
};

#endif
