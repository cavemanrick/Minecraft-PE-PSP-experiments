
#ifndef MCPSP_WORLD_ENTITY_PATH_FINDER_MOB_H
#define MCPSP_WORLD_ENTITY_PATH_FINDER_MOB_H

#include "world/entity/mob.h"
#include "world/level/pathfinder/path.h"

class PathfinderMob : public Mob {
public:
    PathfinderMob(Level* level);
    // Releases any borrowed path back to the pool. Entity's destructor is
    // virtual, so this runs on `delete somMob` through any base pointer --
    // which matters, because a leaked pool slot is permanent.
    virtual ~PathfinderMob();

    virtual void  updateAi();
    virtual float getWalkingSpeedModifier();
    virtual float getWalkTargetValue(int x, int y, int z) { return 0.0f; }
    virtual Entity* findAttackTarget() { return 0; }

    virtual void checkHurtTarget(Entity* target, float dist) {}
    virtual void checkCantSeeTarget(Entity* target, float dist);

    // Called once per world tick, before entities are ticked, to refill the
    // shared budget for how many A* searches may run this tick. See
    // PATH_SEARCHES_PER_TICK in path_finder_mob.cpp.
    static void resetPathBudget();

protected:
    void findRandomStrollLocation();
    bool isPathFinding();

    // Borrows a path from the pool if this mob does not already hold one.
    // False means the pool is full and the caller should skip pathing --
    // an ordinary outcome, not a failure.
    bool acquirePath();
    void releasePath();

    // Runs a search only if this mob holds (or can borrow) a path AND the
    // per-tick search budget allows it. Releases the borrow again if the
    // search came back empty, so an unreachable target does not sit on a
    // pool slot. Both overloads mirror Level::findPath.
    void tryFindPath(Entity* target, float maxDist);
    void tryFindPath(int x, int y, int z, float maxDist);

    // Null whenever this mob is not following a computed route, which is
    // the common case. Never dereference without checking.
    Path* path;
    int   attackTargetId;
    int   fleeTime;
    float runSpeed;

    bool  holdGround;
    static const int MAX_TURN = 30;
};

#endif
