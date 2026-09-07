#ifndef MCPSP_NETHER_FORTRESS_GEN_H
#define MCPSP_NETHER_FORTRESS_GEN_H

class World;

// Generates one compact Nether fortress entirely inside the current chunk.
// Keeping the footprint chunk-local is intentional for PSP streaming: a
// chunk can be generated/evicted without retaining structure state for its
// neighbours.
void netherFortressGenerateChunk(World* w, long worldSeed, int chunkX, int chunkZ);

// Drains pending fortress-guard spawn requests queued by
// netherFortressGenerateChunk into real entities. Must be called from the
// main gameplay thread only (see netherFortressGenerateChunk's comment on
// why entity creation can't happen from the chunk-generation worker) --
// same pattern and same call site as villageTick(w) in Level::tickEntities.
void netherFortressTick(World* w);

#endif
