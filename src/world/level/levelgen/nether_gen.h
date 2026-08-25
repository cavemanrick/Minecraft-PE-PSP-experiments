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

// --- Shell geometry, published for placement code ------------------------
// The vertical extent of the Nether's sealed bedrock box. Both the portal
// builder (nether_portal.cpp) and the debug teleport (debug_teleport.cpp)
// used to hardcode their own copies of these numbers, which is how they
// ended up with a y=60 entry point after the shell was rescaled from 100
// tall to 40 -- above the bedrock ceiling entirely. Publishing them means
// there is one definition and the rescale can never again silently leave
// a stale duplicate behind.
int netherShellFloorBaseY(void);   // lowest y a floor hill can occupy
int netherShellCeilBaseY(void);    // highest y a ceiling hill can occupy

// Finds a spot in the Nether suitable for standing a 4-wide x 5-tall
// obsidian portal frame on, searching outward from chunk (cxCentre,
// czCentre). Replaces the old "carve a 5x5 pocket wherever the fixed
// coordinate happened to land" approach: rather than bulldozing real
// generated terrain into a safe shape, this looks for terrain that is
// already the right shape.
//
// A site qualifies when the four frame columns share one flat, solid,
// non-lava surface; the frame's own volume plus a block of headroom is
// clear air; there is no lava in a small margin around the footprint;
// and at least one of the two faces has standable ground to step out
// onto. Callers must have the search area generated already (see
// worldEnsureArea) -- this only reads blocks, it never generates.
//
// (*outX, *outY, *outZ) is the frame's bottom-interior block: the frame
// occupies outX-1..outX+2 horizontally and outY-1..outY+3 vertically,
// and outY is also the player's feet level on arrival.
//
// Returns false if nothing suitable exists in range, in which case the
// caller decides what to do rather than being handed a fabricated spot.
bool netherFindPortalSite(World* w, int cxCentre, int czCentre, int searchChunkR,
                          int* outX, int* outY, int* outZ);

#endif
