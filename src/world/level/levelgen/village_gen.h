#ifndef MCPSP_VILLAGE_GEN_H
#define MCPSP_VILLAGE_GEN_H

struct World;

// Generates a small, PSP-friendly village entirely inside the supplied
// chunk. Generation is deterministic from the world seed and chunk position.
void villageGenerateChunk(World* w, long worldSeed, int chunkX, int chunkZ);

#endif
