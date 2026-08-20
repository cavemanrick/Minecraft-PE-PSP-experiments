
#include "world/level/tile/tile_behavior.h"

bool reedCanSurvive(World* w, int x, int y, int z) {
    unsigned char below = worldBlock(w, x, y - 1, z);
    if (below == BLOCK_REEDS) return true;
    if (below != BLOCK_GRASS && below != BLOCK_DIRT && below != BLOCK_SAND) return false;
    return isWaterId(worldBlock(w, x - 1, y - 1, z)) || isWaterId(worldBlock(w, x + 1, y - 1, z)) ||
           isWaterId(worldBlock(w, x, y - 1, z - 1)) || isWaterId(worldBlock(w, x, y - 1, z + 1));
}

bool cactusCanSurvive(World* w, int x, int y, int z) {
    if (isSolidPhys(worldBlock(w, x - 1, y, z)) || isSolidPhys(worldBlock(w, x + 1, y, z)) ||
        isSolidPhys(worldBlock(w, x, y, z - 1)) || isSolidPhys(worldBlock(w, x, y, z + 1))) return false;
    unsigned char below = worldBlock(w, x, y - 1, z);
    return below == BLOCK_CACTUS || below == BLOCK_SAND;
}

bool bambooCanSurvive(World* w, int x, int y, int z) {
    unsigned char below = worldBlock(w, x, y - 1, z);
    return below == BLOCK_BAMBOO || below == BLOCK_GRASS || below == BLOCK_DIRT;
}

// Vine faces reuse the ladder direction convention exactly as ladder itself
// defines it (see LadderTile::getAABB / getPlacedOnFaceDataValue and the
// matching isLadder branch in tile_shapes.cpp): data encodes where the
// support wall is, and the quad is drawn flush against that same side --
// data=2: wall/quad at z+1  data=3: wall/quad at z-1
// data=4: wall/quad at x+1  data=5: wall/quad at x-1
// Getting this backwards is exactly what caused vines to render on the
// face opposite their actual support (floating in open air on one side,
// sky visible through the "attached" side) plus a hitbox on the wrong side.
static bool vineWallSolid(World* w, int x, int y, int z, int data) {
    unsigned char wallBlock;
    switch (data) {
        case 2: wallBlock = worldBlock(w, x, y, z + 1); break;
        case 3: wallBlock = worldBlock(w, x, y, z - 1); break;
        case 4: wallBlock = worldBlock(w, x + 1, y, z); break;
        case 5: wallBlock = worldBlock(w, x - 1, y, z); break;
        default: return false;
    }
    return isSolidPhys(wallBlock) || isLeaf(wallBlock);
}

bool vineCanSurvive(World* w, int x, int y, int z) {
    unsigned char above = worldBlock(w, x, y + 1, z);
    if (above == BLOCK_VINE || isSolidPhys(above) || isLeaf(above)) return true;
    int data = worldData(w, x, y, z);
    return vineWallSolid(w, x, y, z, data);
}

static inline bool isJungleLog(World* w, int x, int y, int z) {
    return worldBlock(w, x, y, z) == BLOCK_LOG && (worldData(w, x, y, z) & LOG_TYPE_MASK) == LOG_JUNGLE;
}

bool cocoaCanSurvive(World* w, int x, int y, int z, int data) {
    int dir = data & COCOA_DIR_MASK;
    if (dir == 0) return isJungleLog(w, x, y, z + 1);
    if (dir == 1) return isJungleLog(w, x - 1, y, z);
    if (dir == 2) return isJungleLog(w, x, y, z - 1);
    return isJungleLog(w, x + 1, y, z);
}

void bambooGrow(World* w, int x, int y, int z, int ageThreshold, int maxHeight) {
    if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) return;
    int height = 1;
    while (worldBlock(w, x, y - height, z) == BLOCK_BAMBOO) height++;
    if (height >= maxHeight) return;
    int age = worldData(w, x, y, z);
    if (age >= ageThreshold) {
        worldSetTileUpdate(w, x, y + 1, z, BLOCK_BAMBOO, 0);
        worldSetDataNoUpdate(w, x, y, z, 0);
    } else {
        worldSetDataNoUpdate(w, x, y, z, (unsigned char)(age + 1));
    }
}

void reedCactusGrow(World* w, int x, int y, int z, unsigned char id, int ageThreshold) {
    if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) return;
    int height = 1;
    while (worldBlock(w, x, y - height, z) == id) height++;
    if (height >= 3) return;
    int age = worldData(w, x, y, z);
    if (age >= ageThreshold) {
        worldSetTileUpdate(w, x, y + 1, z, id, 0);
        worldSetDataNoUpdate(w, x, y, z, 0);
    } else {
        worldSetDataNoUpdate(w, x, y, z, (unsigned char)(age + 1));
    }
}
