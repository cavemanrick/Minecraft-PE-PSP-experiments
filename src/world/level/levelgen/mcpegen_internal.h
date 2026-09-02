#ifndef MCPEGEN_INTERNAL_H__
#define MCPEGEN_INTERNAL_H__

#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/PerlinNoise.h"

#define BIOME_ZOOM        2.0f
#define BIOME_TEMP_SCALE  (BIOME_ZOOM / 80.0f)
#define BIOME_DOWN_SCALE  (BIOME_ZOOM / 40.0f)
#define BIOME_NOISE_SCALE (1.0f / 4.0f)

struct World;

struct McpeGen {
    Random random;
    Random rndTemp, rndDownfall, rndNoise;

    PerlinNoise lperlinNoise1, lperlinNoise2, perlinNoise1, perlinNoise2, perlinNoise3,
                scaleNoise, depthNoise, forestNoise;
    PerlinNoise temperatureMap, downfallMap, noiseMap;

    float* buffer;
    float *pnr, *ar, *br, *sr, *dr;
    float *rawTemp, *rawDownfall, *rawNoise;
    float mTemp[16 * 16], mDownfall[16 * 16];
    float sandBuffer[16 * 16], gravelBuffer[16 * 16], depthBuffer[16 * 16];
    long  worldSeed;

    McpeGen(long seed);
    ~McpeGen();

    void computeBiome(int chunkX, int chunkZ);
    // Takes World* purely so it can classify the mushroom island and lift
    // its terrain above sea level -- see the island block in getHeights.
    float* getHeights(const World* w, int x, int y, int z, int xSize, int ySize, int zSize);
    void prepareChunk(World* w, int chunkX, int chunkZ);
    void buildSurfacesChunk(World* w, int chunkX, int chunkZ);

    bool postProcessPhase(World* w, int chunkX, int chunkZ, int phase);
    int mPhaseBiome;
};

// True if a real, sufficiently deep body of open water exists at world
// column (wx,wz) -- see McpeGen_isOceanAt's definition in mcpegen.cpp for
// what "sufficiently deep" means and why this has to reproduce
// getHeights' density formula rather than call it. Exposed here (not just
// static in mcpegen.cpp) because the mushroom island's seed-placement
// search in biome.cpp needs it, and that search runs before any per-chunk
// generation -- see ensureBiomeSeeds' own comment on why it builds a
// throwaway McpeGen just for this rather than depending on mcpegen.cpp's
// file-static g_gen.
bool McpeGen_isOceanAt(McpeGen* g, float wx, float wz);

#endif
