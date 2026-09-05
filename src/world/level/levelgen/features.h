#ifndef FEATURES_H__
#define FEATURES_H__

struct World;
class Random;

void setBlock(World* w, int x, int y, int z, unsigned char id, unsigned char data = 0);
bool isSolidGen(unsigned char id);
int heightmapAt(World* w, int x, int z);

void clayFeature(World* w, Random& random, int x, int y, int z);

bool isTreeClear(unsigned char b);

bool treeSpaceClear(World* w, int x, int y, int z, int treeHeight,
                    int (*radiusAt)(int layer, int treeHeight, int arg), int arg);

// Shared 2x2 mega-trunk support, for any tree type with a real four-column
// trunk (vanilla mega jungle trees and dark oak both use this footprint,
// as opposed to a single thick 1x1 column). (x,z) is always the
// "northwestern" corner log, matching vanilla's own 2x2-sapling placement
// convention -- the other three columns are at +1 in x and/or z from there.
extern const int kTrunk2x2Dx[4];
extern const int kTrunk2x2Dz[4];

// Ensures dirt sits under all four trunk columns (the caller is expected to
// have already validated/placed dirt under the northwestern corner itself,
// typically as part of its own treeSpaceClear-adjacent setup).
void trunk2x2BaseDirt(World* w, int x, int y, int z);

// Writes one height-level of a 2x2 trunk (all four log columns at y).
void trunk2x2PlaceLevel(World* w, int x, int y, int z, unsigned char logData);

// For cardinal offset (ddx,ddz) -- a unit vector in whatever direction
// convention the caller's own table uses -- returns which of the two 2x2
// trunk column indices actually faces outward in that direction, so
// branches/decorations visibly originate from the trunk's outer edge
// rather than always the same corner log regardless of trunk width.
// Picks randomly between the two columns sharing that face.
int trunk2x2OutwardColumn(Random& random, int ddx, int ddz);

void treeBasic(World* w, Random& random, int x, int y, int z,
               int minHeight, unsigned char leafData, unsigned char logData);

void treeOak(World* w, Random& random, int x, int y, int z);
void treeBirch(World* w, Random& random, int x, int y, int z);
void treeSpruce(World* w, Random& random, int x, int y, int z);
void treePine(World* w, Random& random, int x, int y, int z);
void treeJungle(World* w, Random& random, int x, int y, int z);

// Very low-cost jungle floor decoration: a tiny static bush/fern patch.
// See feature_tree_jungle.cpp for the fixed version of the fern-accent
// logic (the originally-submitted patch had a dead branch: it always
// filled the center cell with a leaf first, so the "fern poking through"
// check -- which required the center to still be BLOCK_AIR -- could never
// fire).
void jungleUnderstoryFeature(World* w, Random& random, int x, int y, int z);

// Sheathes a log column in vines from (x,y,z) upward. Exposed so the
// ordinary oaks mixed into the jungle can be coated the same way the
// jungle trees are -- see the B_JUNGLE branch in mcpegen.cpp.
void vineCoatTrunk(World* w, Random& random, int x, int y, int z);
void treeDarkOak(World* w, Random& random, int x, int y, int z);
void flowerFeature(World* w, Random& random, int x, int y, int z, unsigned char tile, unsigned char data = 0);
void mushroomFeature(World* w, Random& random, int x, int y, int z, unsigned char tile);

// Huge mushrooms (mushroom-forest / mushroom-fields / dark-forest style
// biomes). Brown: flat 7x7 plate cap, corners clipped, stem fully hidden
// beneath it. Red: hollow box of walls standing off around the stem,
// partway down only, topped with a slightly rounded roof. Both use
// BLOCK_HUGE_MUSHROOM_CAP/STEM (see chunk.h) -- placement/biome choice is
// left entirely to the caller, same as every treeXxx function.
void mushroomHugeBrown(World* w, Random& random, int x, int y, int z);
void mushroomHugeRed(World* w, Random& random, int x, int y, int z);
void cactusFeature(World* w, Random& random, int x, int y, int z);
void reedsFeature(World* w, Random& random, int x, int y, int z);
void oreFeature(World* w, Random& random, int x, int y, int z, unsigned char tile, int count);
void springFeature(World* w, int x, int y, int z, unsigned char tile);
void lakeFeature(World* w, Random& random, int x, int y, int z, unsigned char tile);
void snowCap(World* w, int chunkX, int chunkZ, float* mTemp);

// Debug world only (WORLD_TYPE_DEBUG) -- places whatever's currently being
// tested right at the player's real spawn point. See
// debug_spawn_content.cpp for the actual content and how to change it.
void placeDebugSpawnContent(World* w, int sx, int sz, int feetY);

#endif
