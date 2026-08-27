#ifndef MCPEGEN_H__
#define MCPEGEN_H__

struct World;

void worldGenerateMCPE(World* w, long seed, int genMask);

void worldGenInit(long seed, int genMask);
void worldGenFree();

// The world seed passed to the most recent worldGenInit -- exposed so
// other generation code that isn't part of McpeGen itself (the Nether
// generator, in particular) can derive its own independent randomness
// from the same world seed without chunk_cache.cpp having to thread a
// seed parameter through every reserved-region call site by hand. Same
// value chunkGenerateTerrain already passes to caveFeature internally
// (see g_genSeed in mcpegen.cpp); this just makes that value visible
// outside the file too.
long worldGenSeed();

// Same rationale as worldGenSeed() above, for the feature toggle mask:
// the Nether generator needs to see GEN_FEATURE_NETHER_FORTRESS without
// chunk_cache.cpp threading a mask parameter through every reserved-
// region call site by hand. Mirrors g_genMask in mcpegen.cpp.
int worldGenMask();

void chunkGenerateTerrain(World* w, int cx, int cz);

bool chunkPostProcessPhase(World* w, int cx, int cz, int phase);

void worldPlaceMushrooms(World* w);

void worldPlaceFlowers(World* w);

#endif
