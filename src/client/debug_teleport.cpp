#include "world/level/world.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/level/levelgen/features.h"
#include "client/debug_teleport.h"

// TEMPORARY dev/testing scaffolding: lets you jump into the reserved
// Nether region (see WORLD_NETHER_*/WORLD_PRESET_1024_* in world.h) before
// any real Nether generator or portal exists, so the region-reservation
// and generation-exclusion work can actually be play-tested. Meant to be
// deleted once real Nether portals exist -- everything relevant lives in
// this one file and its one call site in main.cpp for exactly that reason.
//
// Only does anything on a genuine 1024-preset world (worldChunkIsReserved
// gates this the same way it gates chunkGenerateTerrain elsewhere) --
// silently a no-op on any other world size, so it can never misplace a
// player into real overworld terrain on a 512 or infinite world.

static bool s_haveReturnPos = false;
static float s_returnX = 0, s_returnY = 0, s_returnZ = 0;
static float s_returnYRot = 0, s_returnXRot = 0;

// A fixed, centered point well inside the Nether strip's footprint,
// clear of the boundary wall (Level::getCubes) and the strip's own edges.
static const int kNetherEntryCX = WORLD_NETHER_ORIGIN_CX + WORLD_NETHER_CHUNKS / 2;
static const int kNetherEntryCZ = WORLD_NETHER_CHUNKS / 2;

static void carveSafePlatform(World* w, int bx, int by, int bz) {
    // Reserved space is untouched air (see worldChunkIsReserved) -- there's
    // no floor to land on at all without this. A small flat stone platform
    // plus clear headroom above, big enough to comfortably not miss on
    // arrival.
    for (int dx = -2; dx <= 2; dx++)
    for (int dz = -2; dz <= 2; dz++) {
        setBlock(w, bx + dx, by - 1, bz + dz, BLOCK_STONE);
        for (int dy = 0; dy < 4; dy++)
            setBlock(w, bx + dx, by + dy, bz + dz, BLOCK_AIR);
    }
}

// Called on the debug teleport key combo (see main.cpp). Toggles between
// the Nether entry platform and wherever the player was standing before,
// so a single combo both enters and returns without needing two separate
// bindings.
void debugToggleNetherTeleport(World* w, LocalPlayer* player) {
    if (!player || !w) return;
    if (w->sizeX != WORLD_PRESET_1024_TOTAL_X_CHUNKS) return; // not a 1024-preset world -- no reserved region exists

    if (s_haveReturnPos) {
        player->moveTo(s_returnX, s_returnY, s_returnZ, s_returnYRot, s_returnXRot);
        s_haveReturnPos = false;
        return;
    }

    s_returnX = player->x; s_returnY = player->y; s_returnZ = player->z;
    s_returnYRot = player->yRot; s_returnXRot = player->xRot;
    s_haveReturnPos = true;

    int bx = kNetherEntryCX * 16 + 8, bz = kNetherEntryCZ * 16 + 8;
    int by = 64; // arbitrary mid-height; reserved space has no terrain/heightmap to query

    worldGetChunk(w, kNetherEntryCX, kNetherEntryCZ); // claim the chunk before writing to it (setBlock no-ops on an unclaimed chunk)
    carveSafePlatform(w, bx, by, bz);

    player->moveTo((float)bx + 0.5f, (float)by, (float)bz + 0.5f, player->yRot, player->xRot);
}
