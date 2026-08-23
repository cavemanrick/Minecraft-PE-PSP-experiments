#ifndef BIOME_H__
#define BIOME_H__

enum BiomeId { B_TUNDRA, B_SAVANNA, B_DESERT, B_SWAMP, B_TAIGA, B_SHRUB,
               B_FOREST, B_PLAINS, B_SEASONAL, B_RAIN, B_JUNGLE, B_MUSHROOM };

struct World;

BiomeId classifyBiomeSpatial(long worldSeed, const World* w, int worldX, int worldZ);

// Same classification, but also reports where the column sits relative to
// the mushroom island. The island is the one biome region with a hard
// radius and a guaranteed ring of water around it, so callers that need to
// carve the moat or lift the land need more than just "which biome".
//
// *mushroomMargin is written as the column's distance INSIDE the island's
// outer edge, in blocks:
//     margin <= 0                      -> not on the island at all
//     0 < margin <= MUSHROOM_MOAT_WIDTH -> in the moat ring (water)
//     margin > MUSHROOM_MOAT_WIDTH      -> island land (mycelium)
// It is only meaningful when the returned biome is B_MUSHROOM; for every
// other biome it is written as 0 so callers can test it without first
// testing the biome. Pass 0 if the margin isn't wanted -- the extra work
// is trivial (the loop already has the numbers), so there is no separate
// cheap path.
//
// Use this rather than calling classifyBiomeSpatial and then recomputing
// distances: one call does the whole nearest-seed loop once, which matters
// because it runs per surface column (256 per chunk).
BiomeId classifyBiomeSpatialEx(long worldSeed, const World* w, int worldX, int worldZ,
                               float* mushroomMargin);

// Width of the water ring around the mushroom island, in blocks. The moat
// is carved out of the island's OWN claimed territory rather than out of
// the neighbouring biome, so a mushroom island never eats into whatever it
// borders -- see the note in classifyBiomeSpatialEx.
#define MUSHROOM_MOAT_WIDTH 6.0f

// How far inland the terrain lift fades in over, in blocks. Without this
// the island would rise out of the sea as a vertical wall exactly at the
// moat's inner edge.
#define MUSHROOM_SHORE_FADE 14.0f

// 0 at the moat's inner edge, rising to 1 once fully inland. Callers use
// this to scale the island's elevation lift so the shore slopes instead of
// forming a cliff. Returns 0 for any column not on the island.
float mushroomLandLift(float mushroomMargin);

void biomeSurface(BiomeId b, unsigned char* top, unsigned char* material);

#endif
