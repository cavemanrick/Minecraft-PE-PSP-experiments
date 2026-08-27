#ifndef MCPSP_DUNGEON_GEN_H
#define MCPSP_DUNGEON_GEN_H

struct World;

// Generates at most one compact dungeon entirely inside the supplied chunk.
// Placement is deterministic from world seed + chunk coordinates.
void dungeonGenerateChunk(World* w, long worldSeed, int chunkX, int chunkZ);

#endif
