#ifndef NETHER_BIOME_H__
#define NETHER_BIOME_H__

// The Nether strip's three biomes. Kept as a separate enum from the
// overworld's BiomeId (biome.h) rather than folded into it -- the two
// biome maps are placed over completely disjoint, non-overlapping
// coordinate spans (overworld proper vs. the reserved Nether X-column,
// see WORLD_NETHER_* in world.h) and share no classification logic, so
// there's no value in a single combined enum and real risk of a caller
// accidentally mixing the two spaces if the id spaces were merged.
enum NetherBiomeId { NB_WASTES, NB_SOUL_SAND_VALLEY, NB_WARPED_FOREST };

struct World;

// Nearest-seed classification, same Voronoi approach as the overworld's
// classifyBiomeSpatial (biome.cpp) -- one fixed seed point per biome,
// placed once per world seed, jittered so borders aren't dead straight.
// worldX/worldZ are the same flat world-block coordinate space every
// other worldBlock/setBlock call already uses (i.e. already offset into
// the reserved Nether X-column, not a Nether-local 0-based space).
NetherBiomeId classifyNetherBiome(long worldSeed, int worldX, int worldZ);

#endif
