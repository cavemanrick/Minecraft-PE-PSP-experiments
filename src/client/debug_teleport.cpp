#include "world/level/world.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/level/levelgen/features.h"
#include "client/debug_teleport.h"

// TEMPORARY dev/testing scaffolding: lets you jump into the reserved
// Nether region (see WORLD_NETHER_*/WORLD_PRESET_1024_* in world.h)
// without needing a real portal yet. Meant to be deleted once real Nether
// portals exist -- everything relevant lives in this one file and its one
// call site in main.cpp for exactly that reason.
//
// Only does anything on a genuine 1024-preset world (worldChunkIsReserved
// gates this the same way it gates chunkGenerateTerrain elsewhere) --
// silently a no-op on any other world size, so it can never misplace a
// player into real overworld terrain on a 512 or infinite world.
//
// Now that chunkGenerateNether produces real terrain in this strip
// (floor hills rising from a lava floor, ceiling hills hanging down, a
// guaranteed-navigable gap between them -- see nether_gen.cpp), a blind
// teleport-to-fixed-coordinate can no longer assume it's landing in
// untouched air: the entry point might come up inside a rare touch-point
// pillar where floor and ceiling hills are allowed to meet, depending on
// how generation happened to land there. carveSafePlatform below
// clears a small landing pocket and floors it with netherrack (not
// BLOCK_STONE, which would look visually wrong sitting inside real
// generated Nether terrain) rather than assuming a floor already exists.

static bool s_haveReturnPos = false;
static float s_returnX = 0, s_returnY = 0, s_returnZ = 0;
static float s_returnYRot = 0, s_returnXRot = 0;

// A fixed, centered point well inside the Nether strip's footprint,
// clear of the boundary wall (Level::getCubes) and the strip's own edges.
// Not compile-time constants any more: the strip's X origin depends on
// which preset this world is (worldNetherOriginCX in world.h).
static int netherEntryCX(const World* w) { return worldNetherOriginCX(w) + WORLD_NETHER_CHUNKS / 2; }
static int netherEntryCZ(void) { return WORLD_NETHER_CHUNKS / 2; }

// Comfortably above the lava floor and safely inside the guaranteed
// navigable gap (see NETHER_MIN_GAP in nether_gen.cpp) for any non-touch-
// point column, so the cleared pocket only ever needs to punch through
// the rare touch-point pillar case, never open straight into the lava
// floor from below.
// Rescaled with the shell: the Nether is 40 tall now, so 60 would be
// above the bedrock ceiling. The tightest guaranteed gap is y=15..27
// (see the NETHER_H budget check in nether_gen.cpp), so 18 sits inside
// it with room for the carved pocket above and below.
static const int kNetherEntryY = 18;

static void carveSafePlatform(World* w, int bx, int by, int bz) {
    // Real Nether terrain already exists here by the time this runs (see
    // comment above) -- this just guarantees a clear, netherrack-floored
    // pocket at the exact entry point regardless of whether the generated
    // hill-generation shape happened to leave solid rock or open space there.
    for (int dx = -2; dx <= 2; dx++)
    for (int dz = -2; dz <= 2; dz++) {
        setBlock(w, bx + dx, by - 1, bz + dz, BLOCK_NETHERRACK);
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
    // Both pre-generated presets have a Nether strip now, so this is no
    // longer 1024-only. Legacy infinite saves have no reserved region and
    // are still excluded.
    if (!worldHasReservedRegions(w)) return;

    if (s_haveReturnPos) {
        player->moveTo(s_returnX, s_returnY, s_returnZ, s_returnYRot, s_returnXRot);
        s_haveReturnPos = false;
        return;
    }

    s_returnX = player->x; s_returnY = player->y; s_returnZ = player->z;
    s_returnYRot = player->yRot; s_returnXRot = player->xRot;
    s_haveReturnPos = true;

    int bx = netherEntryCX(w) * 16 + 8, bz = netherEntryCZ() * 16 + 8;
    int by = kNetherEntryY;

    worldGetChunk(w, netherEntryCX(w), netherEntryCZ()); // claim + generate real Nether terrain here (see chunkGenerateNether) before writing to it
    carveSafePlatform(w, bx, by, bz);

    player->moveTo((float)bx + 0.5f, (float)by, (float)bz + 0.5f, player->yRot, player->xRot);
}
