#ifndef MCPSP_WORLD_ENTITY_MONSTER_GHAST_H
#define MCPSP_WORLD_ENTITY_MONSTER_GHAST_H

#include "world/entity/mob.h"

class CompoundTag;

// Ghast is a direct Mob subclass, not a Monster/PathfinderMob, the same
// choice Strider already made: PathfinderMob's pool-based A* pathing
// exists to walk a mob toward a target across uneven terrain, and a
// ghast's real behaviour (per the wiki: "does not attempt to draw closer
// to the player... unlike most other aggressive mobs") never needs that
// at all. Pulling in PathfinderMob would mean overriding away behaviour
// this mob doesn't want rather than starting from a blank, correct base.
class Ghast : public Mob {
public:
    Ghast(Level* level);

    virtual void tick();
    virtual void aiStep();
    virtual void travel(float xs, float yf);
    virtual int  getEntityTypeId() const;
    virtual int  getCreatureBaseType() const;
    virtual int  getMaxHealth() { return 10; }
    virtual bool canSpawn();

    virtual bool hurt(Entity* source, int damage);
    virtual void die(Entity* source);
    virtual int  getDeathLoot();

    virtual const char* getHurtSound()  { return "mob.ghast.scream"; }
    virtual const char* getDeathSound() { return "mob.ghast.death"; }
    virtual const char* getAmbientSound() { return "mob.ghast.moan"; }

    // Whether the mouth/eyes should render open+red (mid-shot) or closed
    // (idle) -- see ghast_renderer.cpp, which picks ghast.png vs
    // ghast_shooting.png purely off this flag, matching how Strider
    // switches its texture off isSaddled().
    bool isCharging() const { return chargeTicks > 0; }

protected:
    virtual void addAdditonalSaveData(CompoundTag* tag);
    virtual void readAdditionalSaveData(CompoundTag* tag);

private:
    // Ticks remaining in the current "wind-up and fire" sequence. Set to
    // CHARGE_TICKS when a shot is committed to, counts down to 0, and the
    // actual Fireball spawns the tick it reaches 0 -- this is what lets
    // the shooting face texture appear for a beat before the shot leaves,
    // matching vanilla's "eyes turn red, mouth opens" warning tell.
    int chargeTicks;

    // Ticks until the next attack becomes eligible to start charging.
    // Reset to the full cooldown every time a shot is fired, not every
    // tick a target is in range, so this is a true "3 seconds between
    // shots" cadence rather than a retriggerable window.
    int attackCooldown;

    // Simple wandering-flight heading, refreshed periodically rather than
    // recomputed toward any particular point -- see travel()'s comment
    // for why this is deliberately not vanilla's real avoid-obstacles
    // flight.
    float moveDirX, moveDirY, moveDirZ;
    int   moveTicks;
};

#endif
