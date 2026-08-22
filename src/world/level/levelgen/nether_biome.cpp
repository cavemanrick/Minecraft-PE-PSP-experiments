#include "world/level/levelgen/nether_biome.h"
#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/PerlinNoise.h"
#include "world/level/world.h"

// Same nearest-seed Voronoi approach as the overworld biome map
// (biome.cpp), but scoped to the Nether strip's own fixed 512x512-block
// footprint (WORLD_NETHER_CHUNKS x WORLD_NETHER_CHUNKS, always exactly
// that size -- the reserved region only exists on the 1024 preset, and
// worldChunkIsReserved already gates that) rather than the overworld's
// whole-world/infinite-world span logic, which doesn't apply here: the
// Nether strip is never infinite, so there's no zero-span case to guard
// against the way ensureBiomeSeeds does for w->sizeX==0.
static const int N_NETHER_BIOMES = 3;
static const NetherBiomeId kNetherBiomeOrder[N_NETHER_BIOMES] = {
    NB_WASTES, NB_SOUL_SAND_VALLEY, NB_WARPED_FOREST
};

static bool  s_seedsReady = false;
static long  s_seedsForWorldSeed = 0;
static float s_seedX[N_NETHER_BIOMES];
static float s_seedZ[N_NETHER_BIOMES];
static PerlinNoise* s_borderNoise = 0;

static void ensureNetherBiomeSeeds(long worldSeed) {
    if (s_seedsReady && s_seedsForWorldSeed == worldSeed) return;

    // Different XOR constant than the overworld's ensureBiomeSeeds (which
    // uses ^0x610E5EEDL) so the two seed sets are independent even on the
    // same world seed -- otherwise the Nether's 3 regions would always
    // land in a fixed relationship to the overworld's 11, which would look
    // like an obviously-derived pattern rather than an independent layout.
    Random seedRandom(worldSeed ^ 0x4E45544CL);

    float spanX = (float)(WORLD_NETHER_CHUNKS * CHUNK_SX);
    float spanZ = (float)(WORLD_NETHER_CHUNKS * CHUNK_SZ);
    // Origin offset: seeds must live in the same flat world-block space
    // classifyNetherBiome is queried in (already-offset worldX/worldZ),
    // not a Nether-local 0-based space, so every distance comparison
    // downstream stays in one consistent coordinate system.
    float originX = (float)(WORLD_NETHER_ORIGIN_CX * CHUNK_SX);
    float originZ = (float)(WORLD_NETHER_ORIGIN_CZ * CHUNK_SZ);

    // 3 seeds on a simple 3x1 strip of cells across X, full height in Z --
    // unlike the overworld's 4x3 grid for 11 biomes, 3 regions read more
    // naturally as side-by-side bands than a cramped 2x2 grid would.
    float cellW = spanX / N_NETHER_BIOMES;
    for (int i = 0; i < N_NETHER_BIOMES; i++) {
        float centerX = originX + i * cellW + cellW * 0.5f;
        float centerZ = originZ + spanZ * 0.5f;
        float jitterX = (seedRandom.nextFloat() - 0.5f) * cellW * 0.5f;
        float jitterZ = (seedRandom.nextFloat() - 0.5f) * spanZ * 0.5f;
        s_seedX[i] = centerX + jitterX;
        s_seedZ[i] = centerZ + jitterZ;
    }

    delete s_borderNoise;
    s_borderNoise = new PerlinNoise(&seedRandom, 2);

    s_seedsForWorldSeed = worldSeed;
    s_seedsReady = true;
}

NetherBiomeId classifyNetherBiome(long worldSeed, int worldX, int worldZ) {
    ensureNetherBiomeSeeds(worldSeed);

    float bx = (float)worldX, bz = (float)worldZ;
    int best = 0;
    float bestD2 = 1e18f;
    for (int i = 0; i < N_NETHER_BIOMES; i++) {
        float dx = bx - s_seedX[i], dz = bz - s_seedZ[i];
        float d2 = dx * dx + dz * dz;

        float wobble = s_borderNoise->getValue(worldX * 0.01f, worldZ * 0.01f + i * 37.0f);
        d2 += wobble * 900.0f;

        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return kNetherBiomeOrder[best];
}
