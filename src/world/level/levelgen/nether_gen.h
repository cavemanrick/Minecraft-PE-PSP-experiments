#ifndef NETHER_GEN_H__
#define NETHER_GEN_H__

struct World;

// Generates one 16x16 column of the reserved Nether strip (see
// WORLD_NETHER_*/worldChunkIsReserved in world.h) at world-block chunk
// coordinates (cx,cz). Caller (chunk_cache.cpp) is responsible for
// checking worldChunkIsReserved before calling this -- this function
// itself does not re-check, matching chunkGenerateTerrain's own contract
// of "caller decides whether this chunk should exist".
void chunkGenerateNether(World* w, long worldSeed, int cx, int cz);

#endif
