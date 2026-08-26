#include "world/entity/path_finder_mob.h"
#include "world/level/level.h"
#include "world/level/pathfinder/vec3.h"
#include "util/mth.h"
#include <cmath>

static const float PF_PI = 3.14159265f;

PathfinderMob::PathfinderMob(Level* level)
:   Mob(level), path(0), attackTargetId(0), fleeTime(0), runSpeed(0.7f), holdGround(false) {}

PathfinderMob::~PathfinderMob() { releasePath(); }

// How many A* searches may run across ALL mobs in one world tick.
//
// This is the real CPU governor, and it matters more than the size of the
// path pool. A single search memsets an 8KB hash table before it starts
// (PathFinder::findPathTo) and may expand up to MAX_NODES = 2048 nodes, so
// it is not something to run once per mob per tick once there are dozens
// of mobs. Capping searches rather than shrinking MAX_NODES bounds the
// worst case without making any individual mob worse at navigating.
//
// Two per tick is 40 a second. A mob that misses its turn re-rolls next
// tick; the visible effect is that a pack takes a moment longer to commit
// to a route, not that it stops moving -- the fallback is Mob::updateAi's
// wander, which is what an idle mob does anyway.
static const int PATH_SEARCHES_PER_TICK = 2;
static int s_pathBudget = PATH_SEARCHES_PER_TICK;

void PathfinderMob::resetPathBudget() { s_pathBudget = PATH_SEARCHES_PER_TICK; }

bool PathfinderMob::acquirePath() {
    if (path) return true;
    path = PathPool::acquire();
    return path != 0;
}

void PathfinderMob::releasePath() { PathPool::release(path); }

void PathfinderMob::tryFindPath(Entity* target, float maxDist) {
    if (s_pathBudget <= 0) return;
    if (!acquirePath()) return;
    --s_pathBudget;
    level->findPath(path, this, target, maxDist, false, false);
    // An unreachable target leaves the path empty (findPathNodes calls
    // destroy() on failure). Hand the slot straight back rather than
    // holding it for a route that does not exist.
    if (path->isEmpty()) releasePath();
}

void PathfinderMob::tryFindPath(int x, int y, int z, float maxDist) {
    if (s_pathBudget <= 0) return;
    if (!acquirePath()) return;
    --s_pathBudget;
    level->findPath(path, this, x, y, z, maxDist, false, false);
    if (path->isEmpty()) releasePath();
}

bool PathfinderMob::isPathFinding() { return path && !path->isEmpty(); }

float PathfinderMob::getWalkingSpeedModifier() {
    float speed = Mob::getWalkingSpeedModifier();
    if (fleeTime > 0) speed *= 2.0f;
    return speed;
}

void PathfinderMob::updateAi() {
    if (fleeTime > 0) fleeTime--;
    holdGround = false;
    float maxDist = 16.0f;

    Entity* attackTarget = 0;
    if (attackTargetId == 0) {
        attackTarget = findAttackTarget();
        if (attackTarget) {
            tryFindPath(attackTarget, maxDist);
            attackTargetId = attackTarget->entityId;
        }
    } else {

        attackTarget = level->getEntity(attackTargetId);
        if (!attackTarget || !attackTarget->isAlive()) {
            attackTargetId = 0;
            attackTarget = 0;
        } else {
            float d = attackTarget->distanceTo(this);
            if (canSee(attackTarget)) checkHurtTarget(attackTarget, d);
            else                      checkCantSeeTarget(attackTarget, d);
        }
    }

    if (holdGround) {
        xxa = 0; yya = 0; jumping = false;
        if (attackTarget) {
            float dx = attackTarget->x - x, dz = attackTarget->z - z;
            yRot = atan2f(dx, dz) * 180.0f / PF_PI;
        }
        applySwimUrge();
        return;
    }

    if (attackTarget) {
        float dx = attackTarget->x - x, dz = attackTarget->z - z;
        if (dx * dx + dz * dz < 25.0f && canSee(attackTarget)) {
            float want = atan2f(dx, dz) * 180.0f / PF_PI;
            float diff = want - yRot;
            while (diff < -180.0f) diff += 360.0f;
            while (diff >= 180.0f) diff -= 360.0f;
            if (diff >  MAX_TURN) diff =  MAX_TURN;
            if (diff < -MAX_TURN) diff = -MAX_TURN;
            yRot += diff;
            xxa = 0; yya = runSpeed; xRot = 0; jumping = false;
            if (horizontalCollision) jumping = true;
            releasePath();
            applySwimUrge();
            return;
        }
    }

    bool doStroll = false;
    if (attackTarget && (!isPathFinding() || sharedRandom.nextInt(20) == 0)) {
        tryFindPath(attackTarget, maxDist);
    } else {
        if (!isPathFinding() && sharedRandom.nextInt(180) == 0) {
            doStroll = true;
        } else {
            if (sharedRandom.nextInt(120) == 0) doStroll = true;
            else if (fleeTime > 0 && (fleeTime & 7) == 1) doStroll = true;
        }
    }
    if (doStroll && noActionTime < TicksPerSecond * 5) findRandomStrollLocation();

    int yFloor = Mth::floor(bb.y0 + 0.5f);
    xRot = 0;
    if (!isPathFinding() || sharedRandom.nextInt(100) == 0) {
        Mob::updateAi();
        return;
    }

    Vec3 target = path->currentPos(this);
    float r = bbWidth * 2.0f;
    bool looping = true;
    while (looping && target.distanceToSqr(x, target.y, z) < r * r) {
        path->next();
        // Reaching the end of a route returns the slot immediately. This
        // is the common way a path ends, so it is the main thing keeping
        // the pool from silting up.
        if (path->isDone()) { looping = false; releasePath(); }
        else target = path->currentPos(this);
    }

    jumping = false;
    if (looping) {
        float dx = target.x - x, dz = target.z - z, dy = target.y - yFloor;

        float yRotD = atan2f(dx, dz) * 180.0f / PF_PI;
        float rotDiff = yRotD - yRot;
        yya = runSpeed;
        while (rotDiff < -180.0f) rotDiff += 360.0f;
        while (rotDiff >= 180.0f) rotDiff -= 360.0f;
        if (rotDiff >  MAX_TURN) rotDiff =  MAX_TURN;
        if (rotDiff < -MAX_TURN) rotDiff = -MAX_TURN;
        yRot += rotDiff;
        if (dy > 0) jumping = true;
    }

    if (horizontalCollision && (!isPathFinding() || attackTarget != 0)) jumping = true;
    applySwimUrge();
}

void PathfinderMob::checkCantSeeTarget(Entity* target, float d) {
    if (d > 32.0f) attackTargetId = 0;
}

void PathfinderMob::findRandomStrollLocation() {
    bool hasBest = false;
    int xBest = 0, yBest = 0, zBest = 0;
    float best = -99999.0f;
    for (int i = 0; i < 10; i++) {
        int xt = Mth::floor(x + sharedRandom.nextInt(13) - 6);
        int yt = Mth::floor(y + sharedRandom.nextInt(7) - 3);
        int zt = Mth::floor(z + sharedRandom.nextInt(13) - 6);
        float value = getWalkTargetValue(xt, yt, zt);
        if (value > best) { best = value; xBest = xt; yBest = yt; zBest = zt; hasBest = true; }
    }
    if (hasBest) tryFindPath(xBest, yBest, zBest, 10.0f);
}
