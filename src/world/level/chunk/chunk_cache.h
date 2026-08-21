
#ifndef MCPSP_WORLD_CHUNK_CACHE_H
#define MCPSP_WORLD_CHUNK_CACHE_H

struct World;

void worldGetChunk(World* w, int cx, int cz);

// Marks a chunk resident/populated WITHOUT running normal terrain
// generation -- for level sources that write real block data directly
// across the whole resident window rather than per-chunk (FlatLevelSource,
// DebugLevelSource in level_source.cpp). See the implementation comment
// in chunk_cache.cpp for why this is necessary, not just a convenience:
// worldBlock and friends gate on chunk-claim state, not on whether real
// data exists underneath.
void worldClaimChunkPrebuilt(World* w, int cx, int cz);

void worldEnsureArea(World* w, int cx, int cz, int r);

int worldStream(World* w, float px, float pz, int budgetMs);

bool worldStreamBusy();

void worldSaveResident(World* w);

void worldGenWorkerStart(World* w);
void worldGenWorkerStop();

extern unsigned int g_streamIn, g_streamOut;

// Batch pre-generation sweep for the two pre-generated world-size presets
// (512x512, 1024x1024). Generates, decorates, saves, and evicts every
// chunk across [x0,x1) x [z0,z1) (chunk coordinates, half-open ranges) in
// a single blocking pass before play begins -- unlike normal streaming,
// which only ever holds a resident window around the player and generates
// lazily on approach, this deliberately touches every chunk in the given
// range up front so the world is fully constructed on disk before the
// player is ever dropped into it.
//
// Processes chunks in reverse row-major order (highest z first, highest x
// first within each row) specifically because decoration features only
// ever spill in the +X/+Z direction from the chunk they're rooted in
// (confirmed against chunkGenerateTerrain's own feature-placement offsets,
// which are always +8..+23 from a chunk's origin, never negative) -- a
// chunk's only unmet dependency is its own (cx+1,cz+1) neighbor
// (worldNeighbourSettled's exact check), and reverse order guarantees
// that neighbor was already fully processed (and is therefore either
// already evicted, or if still resident, at least "ready" in the
// worldChunkReady sense) before the chunk itself is reached. This means
// eviction can happen immediately after each chunk finishes, with no
// lookahead lag needed -- nothing left in the sweep could ever write back
// into an already-evicted chunk.
//
// g_terrainProgress is updated as this runs, same convention as
// worldGenerateMCPE, so a progress bar can reflect it.
void worldPreGenerateSweep(World* w, int x0, int z0, int x1, int z1);

#endif
