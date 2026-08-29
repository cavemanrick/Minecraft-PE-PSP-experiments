#include "world/level/levelgen/cheat_spawn_content.h"
#include "world/level/world.h"
#include "world/level/level.h"
#include "world/level/chunk/chunk.h"
#include "world/level/tile/nether_portal.h"
#include "world/entity/player.h"
#include "world/entity/local_player.h"
#include "world/inventory/inventory.h"
#include "world/item/item.h"
#include "world/item/item_instance.h"

volatile bool g_cheatWorldPending = false;

// --- Finding a flat spot for the frame -----------------------------------
// Same idea as isValidSpawn (world.cpp), reimplemented locally because that
// scan is file-static there and this only has a World*, not the Level
// machinery isValidSpawn's caller has access to. Scans down from the top
// of the world for the first solid, non-leaf, physically solid block,
// matching what a player would actually stand on.
static bool cheatColumnTop(World* w, int x, int z, int* outY) {
    for (int y = WORLD_H - 1; y > 0; y--) {
        unsigned char id = worldBlock(w, x, y, z);
        if (id == BLOCK_AIR) continue;
        if (!isSolidPhys(id) || isLeaf(id)) continue;
        *outY = y;
        return true;
    }
    return false;
}

// A flat, clear rectangular footprint big enough for a 4-wide (z) x
// 5-tall (y) obsidian frame with a full block of clearance on every open
// side: z from z0-1 to z0+4 (6 columns: 1 clearance + 4 frame + 1
// clearance), x from x0-1 to x0+1 (3 columns: the frame's fixed column
// plus one clearance column on each side). All columns must share the
// same surface height and have five clear blocks above it.
static bool flatPatchOk(World* w, int x0, int z0, int* outY) {
    int y;
    if (!cheatColumnTop(w, x0, z0, &y)) return false;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 4; dz++) {
            int cy;
            if (!cheatColumnTop(w, x0 + dx, z0 + dz, &cy)) return false;
            if (cy != y) return false;
            unsigned char below = worldBlock(w, x0 + dx, y, z0 + dz);
            if (isLavaId(below) || isWaterId(below)) return false;
            for (int h = 1; h <= 5; h++)
                if (worldBlock(w, x0 + dx, y + h, z0 + dz) != BLOCK_AIR) return false;
        }
    }
    *outY = y;
    return true;
}

// Builds a minimum-size (2 interior wide x 3 interior tall) Z-aligned
// obsidian frame with its floor resting on the surface found at (x0,z0),
// then ignites it through the same path a player's flint and steel would
// use -- so it gets the exact same interior fill, lighting update and
// anchor registration as a real player-built portal, rather than this
// function trying to duplicate that logic.
//
// Frame footprint (fixed x = x0, z running z0..z0+3, y running
// y0..y0+4): obsidian posts at z0 and z0+3, floor at y0, ceiling at
// y0+4, interior air at the 2x3 block bounded by those.
static void buildAndIgnitePortal(World* w, int x0, int y0, int z0) {
    for (int z = z0; z <= z0 + 3; z++) {
        for (int y = y0; y <= y0 + 4; y++) {
            bool isPost  = (z == z0 || z == z0 + 3);
            bool isFloor = (y == y0 || y == y0 + 4);
            if (isPost || isFloor) blockPut(w, x0, y, z, BLOCK_OBSIDIAN);
            else                   blockPut(w, x0, y, z, BLOCK_AIR);
        }
    }
    // Any interior cell works as the ignite point; netherPortalTryIgnite
    // scans outward from it to rediscover and validate the frame just
    // built, exactly as if a player had clicked flint and steel there.
    netherPortalTryIgnite(w, x0, y0 + 1, z0 + 1);
}

// --- Item grants ----------------------------------------------------------
static void grantItem(short id, short count, short data) {
    Player* p = g_level.player;
    if (!p || !p->inventory) return;
    ItemInstance* it = new ItemInstance(id, count, data);
    if (!p->inventory->add(it)) delete it;
}

void placeCheatSpawnContent(World* w, int sx, int sz, int feetY) {
    (void)feetY; // not needed: the portal search finds its own ground
                 // height per candidate rather than assuming the spawn
                 // column's height applies a few blocks over
    g_cheatWorldPending = false; // one-shot: consume regardless of outcome below

    // Search a small ring of candidate offsets around spawn for a flat,
    // clear patch to build the frame on -- spawn itself is only
    // guaranteed valid for the player's own column (isValidSpawn in
    // world.cpp), not for the wider area a few blocks off to the side, so
    // this can't just assume the very first offset tried will work.
    static const int kOffsets[][2] = {
        {4, 0}, {-4, 0}, {0, 4}, {0, -4},
        {5, 3}, {-5, 3}, {5, -3}, {-5, -3},
        {6, 0}, {-6, 0}, {0, 6}, {0, -6},
    };
    for (unsigned int i = 0; i < sizeof(kOffsets) / sizeof(kOffsets[0]); i++) {
        int cx = sx + kOffsets[i][0], cz = sz + kOffsets[i][1];
        int y;
        if (!flatPatchOk(w, cx, cz, &y)) continue;
        // flatPatchOk validated x in [cx-1,cx+1] and z in [cz-1,cz+4], so
        // the frame (fixed x = cx, z spanning cz..cz+3) sits fully inside
        // the checked region with one clear column of margin on every side.
        buildAndIgnitePortal(w, cx, y + 1, cz);
        break; // first candidate that works is used; no flat spot in the
               // whole ring just means no portal, which is fine -- the
               // items below are still granted regardless of this outcome.
    }

    grantItem(ITEM_PICKAXE_DIAMOND, 1, 0);
    grantItem(ITEM_SADDLE, 1, 0);
}
