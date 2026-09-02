#ifndef BIOME_H__
#define BIOME_H__

enum BiomeId { B_TUNDRA, B_SAVANNA, B_DESERT, B_SWAMP, B_TAIGA, B_SHRUB,
               B_FOREST, B_PLAINS, B_SEASONAL, B_RAIN, B_JUNGLE, B_MUSHROOM };

struct World;

BiomeId classifyBiomeSpatial(long worldSeed, const World* w, int worldX, int worldZ);

// Same classification, but also reports where the column sits relative to
// the mushroom island. The island is the one biome region with a hard
// radius, sited in real ocean rather than carving its own moat -- see
// mushroomLandLift below for the one thing this margin now still drives.
//
// *mushroomMargin is written as the column's distance INSIDE the island's
// outer edge, in blocks:
//     margin <= 0                       -> not on the island at all
//     0 < margin <= MUSHROOM_SHORE_WIDTH -> shore band (lift still fading in)
//     margin > MUSHROOM_SHORE_WIDTH      -> fully inland (full lift, mycelium)
// It is only meaningful when the returned biome is B_MUSHROOM; for every
// other biome it is written as 0 so callers can test it without first
// testing the biome. Pass 0 if the margin isn't wanted -- the extra work
// is trivial (the loop already has the numbers), so there is no separate
// cheap path.
//
// Use this rather than calling classifyBiomeSpatial and then recomputing
// distances: one call does the whole nearest-seed loop once, which matters
// because it runs per surface column (256 per chunk).
// riverChannel/riverValley are the two river fields, described in the
// "Rivers" block further down. Both are optional out-params defaulted to 0
// so the existing five-argument call sites keep compiling unchanged; the
// point of hanging them off this function rather than giving rivers their
// own entry point is that they are computed from the SAME nearest-seed
// loop, which is the expensive part. A separate riverAt() would have meant
// running that twelve-seed loop twice per surface column.
BiomeId classifyBiomeSpatialEx(long worldSeed, const World* w, int worldX, int worldZ,
                               float* mushroomMargin,
                               float* riverChannel = 0, float* riverValley = 0);

// Inner reference radius for the island's terrain-lift fade, in blocks.
// Formerly also the width of a self-carved moat ring; the moat is gone
// now that the island is sited in real, pre-existing ocean (see
// ensureBiomeSeeds' ocean search in biome.cpp) rather than manufacturing
// its own water. Kept as MUSHROOM_MOAT_WIDTH rather than renamed to avoid
// unnecessary churn, but nothing carves a moat with this value any more --
// it is purely a height-shaping reference for mushroomLandLift below.
#define MUSHROOM_MOAT_WIDTH 6.0f

// How far inland the terrain lift fades in over, in blocks, past
// MUSHROOM_MOAT_WIDTH. Without this the island would rise out of the sea
// as a vertical wall right at the shore.
#define MUSHROOM_SHORE_FADE 14.0f

// 0 at the shore, rising to 1 once fully inland. Callers use
// this to scale the island's elevation lift so the shore slopes instead of
// forming a cliff. Returns 0 for any column not on the island.
// --- Rivers ---------------------------------------------------------------
//
// Rivers run along the seams of the biome Voronoi diagram. That is the
// whole design: the nearest-seed loop in classifyBiomeSpatialEx already
// knows the distance to the nearest seed and to the runner-up, and the
// halfway point between those two is exactly the border between two
// biomes. Placing the channel there means a river is never a stripe that
// happens to cross a biome -- it is always the thing separating two of
// them, and river junctions land on the Voronoi vertices where three
// regions meet, which is what a real confluence looks like.
//
// Only SOME seams get a river (see RIVER_PAIR_PERCENT). Which pairs is
// decided by hashing the two biome ids together with the world seed, so it
// is stable for a given world, different between worlds, and needs no
// storage. Any seam touching the mushroom island is always excluded -- that
// border already has the moat, and two water features fighting over the
// same columns would produce neither.
//
// A seam that has no river is left completely alone: no channel, no
// valley, no surface change of any kind.

// Half-width of the water channel, in blocks, before the width noise
// modulates it. The channel is symmetric about the seam, so this is a
// river of about seven blocks across at its widest. Measured over six
// seeds on the 512 preset this puts open river water on 2.4%-3.8% of
// overworld columns.
#define RIVER_HALF_WIDTH 3.5f

// How far, in blocks, the channel is allowed to wander sideways off the
// true seam. Without this the river would trace the mathematical bisector,
// which reads as unnaturally deliberate even with the border wobble that
// classifyBiomeSpatialEx already applies -- the wobble makes the border
// curve, but the river needs to cross back and forth over it.
#define RIVER_MEANDER_AMP 9.0f

// Half-width of the much wider, much gentler valley field. The density
// pass (getHeights in mcpegen.cpp) uses this to sink the land along a
// river seam before any water is placed, so the channel ends up at the
// bottom of a shallow valley instead of being a slot cut into whatever
// height the terrain happened to be. This is what makes the banks read as
// banks. Deliberately far wider than the channel, and wider than the
// density pass's own 4-block sampling grid, so the coarse grid resolves it.
#define RIVER_VALLEY_HALF_WIDTH 18.0f

// Percentage of biome-pair seams that carry a river. At 45 a typical world
// gets roughly a dozen river runs: enough that rivers feel like a feature
// of the map, few enough that crossing a biome border is not always a swim.
//
// Set to 0 to disable rivers entirely: pairHasRiver's h % 100u is always
// in [0,99], so "< 0" is never true for any seed or biome pair -- every
// seam is treated as riverless. The rest of the river machinery (channel
// carving, valley shaping in mcpegen.cpp) is left in place rather than
// removed; it simply never fires once no pair ever qualifies, so this one
// constant is the sole on/off switch.
#define RIVER_PAIR_PERCENT 0u

// 0 for a column with no river. 1 at the centre of the channel, falling to
// 0 at the bank. *riverValley is the same shape over RIVER_VALLEY_HALF_WIDTH
// and smoothstepped, so it has no crease at its outer edge.
//
// Both are written as 0 for every column on a seam that has no river, and
// for every column near the mushroom island, so a caller can act on them
// without first testing anything else.

float mushroomLandLift(float mushroomMargin);

void biomeSurface(BiomeId b, unsigned char* top, unsigned char* material);

#endif
