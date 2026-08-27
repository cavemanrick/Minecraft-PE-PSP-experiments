#ifndef MCPSP_VILLAGE_GEN_H
#define MCPSP_VILLAGE_GEN_H

struct World;

// Generates a small, PSP-friendly village entirely inside the supplied
// chunk. Generation is deterministic from the world seed and chunk position.
void villageGenerateChunk(World* w, long worldSeed, int chunkX, int chunkZ);

// True if a village was actually placed in this chunk (i.e. it passed the
// density/biome/flatness gates in villageGenerateChunk, not just that the
// chunk was checked). Backed by a small fixed registry populated at
// generation time -- see MAX_TRACKED_VILLAGES in village_gen.cpp for the
// bound and what happens once it fills. Used by the achievement tick to
// detect "player is standing in a village" without scanning blocks.
bool villageChunkHasVillage(int chunkX, int chunkZ);

#endif
