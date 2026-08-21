#include "world/level/levelgen/level_source.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/level/levelgen/mcpegen.h"
#include "world/level/levelgen/gen_features.h"
#include "world/level/storage/level_storage.h"

#include <pspkernel.h>
#include <cstring>

namespace {

class RandomLevelSource : public LevelSource {
public:
    void buildTerrain(World* w, long seed) {

        worldGenerateMCPE(w, seed, LevelStorage::getActiveGenMask());
        worldSettleLiquids(w);
    }
    const char* label() const { return "Old"; }
};

class FlatLevelSource : public LevelSource {
public:
    void buildTerrain(World* w, long ) {

        unsigned char col[WORLD_H];
        std::memset(col, BLOCK_AIR, sizeof(col));
        col[0] = BLOCK_BEDROCK;
        col[1] = BLOCK_DIRT;
        col[2] = BLOCK_DIRT;
        col[3] = BLOCK_GRASS;

        for (int z = 0; z < WORLD_D; z++) {
            for (int x = 0; x < WORLD_W; x++) blockColumnPut(w, x, z, col);

            g_terrainProgress = (z * 50) / WORLD_D;

            if ((z & 15) == 0) sceKernelDelayThread(100);
        }

        // The block data above is real and correct, but nothing is
        // readable/solid/spawnable without this -- see the comment on
        // worldClaimChunkPrebuilt (chunk_cache.cpp) for exactly why:
        // worldBlock itself returns BLOCK_INVISIBLE_BEDROCK for any chunk
        // that was never claimed through the normal per-chunk pipeline,
        // regardless of what's actually stored there. This was the root
        // cause of flat worlds leaving the player falling through nothing
        // at spawn -- the floor existed, every reader just treated it as
        // if it didn't.
        for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
        for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
            worldClaimChunkPrebuilt(w, cx, cz);

        g_terrainProgress = 50;
    }

    bool spawnsMobs() const { return false; }

    bool supportsGenFeatures() const { return false; }

    int forcedGameType() const { return 1; }
    const char* label() const { return "Flat"; }
};

// Debug world: identical flat-ground terrain to FlatLevelSource (same
// bedrock/dirt/grass column, everything else air), but reachable as its
// own world type so plain flat worlds are never affected by anything
// placed here. Actual test content isn't placed by buildTerrain -- it's
// placed by placeDebugSpawnContent() in debug_spawn_content.cpp, called
// once real spawn coordinates are known (see render.cpp, right after
// worldFindSpawn), so content can be positioned relative to the player's
// actual spawn point instead of guessed world-space coordinates. Edit
// placeDebugSpawnContent() directly with whatever block/feature/tree call
// you're actively testing -- that's the intended workflow, not a
// permanent fixed test scene.
class DebugLevelSource : public LevelSource {
public:
    void buildTerrain(World* w, long ) {

        unsigned char col[WORLD_H];
        std::memset(col, BLOCK_AIR, sizeof(col));
        col[0] = BLOCK_BEDROCK;
        col[1] = BLOCK_DIRT;
        col[2] = BLOCK_DIRT;
        col[3] = BLOCK_GRASS;

        for (int z = 0; z < WORLD_D; z++) {
            for (int x = 0; x < WORLD_W; x++) blockColumnPut(w, x, z, col);

            g_terrainProgress = (z * 50) / WORLD_D;

            if ((z & 15) == 0) sceKernelDelayThread(100);
        }

        // Same claim-after-write requirement as FlatLevelSource above --
        // see that comment and worldClaimChunkPrebuilt (chunk_cache.cpp).
        for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
        for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
            worldClaimChunkPrebuilt(w, cx, cz);

        g_terrainProgress = 50;
    }

    bool spawnsMobs() const { return false; }

    bool supportsGenFeatures() const { return false; }

    int forcedGameType() const { return 1; }
    const char* label() const { return "Debug"; }
};

RandomLevelSource s_random;
FlatLevelSource   s_flat;
DebugLevelSource  s_debug;

}

LevelSource& levelSourceFor(int worldType) {

    if (worldType == WORLD_TYPE_DEBUG) return (LevelSource&)s_debug;
    return (worldType == WORLD_TYPE_FLAT) ? (LevelSource&)s_flat : (LevelSource&)s_random;
}

LevelSource& activeLevelSource() {
    return levelSourceFor(LevelStorage::getActiveWorldType());
}
