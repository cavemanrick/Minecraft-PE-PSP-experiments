#ifndef MCPSP_NETHER_FORTRESS_GEN_H
#define MCPSP_NETHER_FORTRESS_GEN_H

class World;

// Generates one compact Nether fortress entirely inside the current chunk.
// Keeping the footprint chunk-local is intentional for PSP streaming: a
// chunk can be generated/evicted without retaining structure state for its
// neighbours.
void netherFortressGenerateChunk(World* w, long worldSeed, int chunkX, int chunkZ);

#endif
