
#ifndef MCPSP_WORLD_LEVEL_PATHFINDER_PATH_H
#define MCPSP_WORLD_LEVEL_PATHFINDER_PATH_H

#include "world/level/pathfinder/node.h"
#include "world/level/pathfinder/vec3.h"

class Entity;

class Path {
public:
    Path();
    ~Path();

    void  copyNodes(Node** nodes, int length);
    void  destroy();
    void  next();

    int   getSize() const;
    bool  isEmpty() const;
    bool  isDone() const;

    Node* last() const;
    Node* get(int i) const;
    int   getIndex() const;
    void  setIndex(int index);

    Vec3  currentPos(Entity* e) const;
    Node* currentPos();
    Vec3  getPos(Entity* e, int index) const;

private:

    static const int MAX_PATH = 64;
    Node nodes[MAX_PATH];
    int length;
    int index;
    static int p;
};

// --- Path pooling ---------------------------------------------------------
//
// A Path is 64 Nodes at 32 bytes each, so a shade over 2KB, and it used to
// be embedded by value in every PathfinderMob. That made the path about
// 80% of a mob's entire footprint and forced Entity::ENTITY_SLOT up to
// 2560 bytes, which capped the entity pool at 96 for 240KB of static
// storage. It also meant a mob standing still, which is most mobs most of
// the time, was holding 2KB of route it was not using.
//
// So paths live here instead and are borrowed for as long as a mob is
// actually walking one. A mob that is idle, strolling by dead reckoning,
// or chasing something in a straight line holds nothing.
//
// PATH_POOL_SIZE is the number of mobs that can be following a computed
// route SIMULTANEOUSLY, which is not the number of mobs that can exist --
// those are very different quantities. Twelve is generous next to the
// two-searches-per-tick budget in path_finder_mob.cpp, which is what
// really governs how fast paths get created.
//
// acquire() returning 0 is an ordinary outcome, not an error: the caller
// skips pathing for that tick and falls back to Mob::updateAi's wander.
#define PATH_POOL_SIZE 12

namespace PathPool {
    Path* acquire();
    // Releases and NULLs the caller's pointer in one step, so a mob cannot
    // keep a dangling reference to a path another mob has since taken.
    void  release(Path*& p);
    int   inUse();
}

#endif
