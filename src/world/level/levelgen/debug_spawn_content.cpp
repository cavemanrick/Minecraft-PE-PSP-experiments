#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"

// Debug world spawn content. Called once, right after real spawn
// coordinates are known (render.cpp, immediately after worldFindSpawn),
// only when the active world type is WORLD_TYPE_DEBUG -- never touches
// normal or flat worlds. (sx, sz, feetY) is the player's actual spawn
// position, already resolved against the real chunk data, so content
// placed relative to it reliably ends up where the player will actually
// be looking, instead of a guessed world-space coordinate that might not
// match where spawn really lands.
//
// Workflow: edit the body of this function directly with whatever single
// block, tree, or feature call you're actively testing, rebuild, and it
// appears right in front of you in an otherwise empty flat world. Swap it
// out for the next thing you're testing rather than accumulating a
// permanent scene -- that's the intended use. A couple of commented-out
// examples are left below to show the pattern for the block-texture case
// and the tree/feature case.
void placeDebugSpawnContent(World* w, int sx, int sz, int feetY) {
    Random random(12345);

    // Spawn faces +Z by convention at world creation, so "in front of the
    // player" is a few blocks out along +Z from the spawn column.
    int tx = sx, tz = sz + 4, ty = feetY;

    // --- Example: testing a single block/texture -----------------------
    // Uncomment and set the block id/data you're actively checking. Useful
    // for texture work, since you can stand right in front of it, walk
    // around it, and see it under real lighting immediately at spawn.
    //
    // setBlock(w, tx, ty, tz, BLOCK_VINE, 2);

    // --- Example: testing a tree/feature generator ----------------------
    // Uncomment whichever generator you're currently working on. treeJungle
    // and treeOak both take (World*, Random&, x, y, z) where (x,y,z) is
    // the base of the trunk.
    //
    // treeJungle(w, random, tx, ty, tz);
    // treeOak(w, random, tx, ty, tz);

    (void)random; (void)tx; (void)ty; (void)tz; // silence unused warnings until the above is uncommented
}
