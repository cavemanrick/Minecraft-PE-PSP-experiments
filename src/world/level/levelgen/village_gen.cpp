#include "world/level/levelgen/village_gen.h"
#include "world/level/levelgen/features.h"
#include "world/level/levelgen/biome.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"

// A deliberately small village system. Each village lives entirely inside
// one 16x16 chunk, which avoids cross-chunk structure bookkeeping on PSP.
// The village is made from three houses, a well, paths, and a small farm.
// It is intentionally lightweight: no new entity AI is required.

static unsigned int villageHash(long seed, int cx, int cz) {
    unsigned int h = (unsigned int)seed;
    h ^= (unsigned int)cx * 0x9E3779B9u;
    h ^= (unsigned int)cz * 0x85EBCA6Bu;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

static void put(World* w, int x, int y, int z, unsigned char id, unsigned char data = 0) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return;
    worldSetBlockAndData(w, x, y, z, id, data);
}

static int villageHeight(World* w, int x, int z) {
    for (int y = WORLD_H - 1; y >= 1; --y) {
        unsigned char b = worldBlock(w, x, y, z);
        if (b != BLOCK_AIR && b != BLOCK_WATER && b != BLOCK_CALM_WATER &&
            b != BLOCK_LAVA && b != BLOCK_CALM_LAVA && b != BLOCK_TALLGRASS &&
            b != BLOCK_FLOWER && b != BLOCK_ROSE && b != BLOCK_MUSHROOM_BROWN &&
            b != BLOCK_MUSHROOM_RED)
            return y + 1;
    }
    return 0;
}

static bool villageFlatEnough(World* w, int cx, int cz, int& baseY) {
    int minY = WORLD_H, maxY = 0, sum = 0, n = 0;
    for (int z = 2; z <= 13; ++z) {
        for (int x = 2; x <= 13; ++x) {
            int y = villageHeight(w, cx * 16 + x, cz * 16 + z);
            if (y <= 0 || y >= 120) return false;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            sum += y; ++n;
        }
    }
    if (maxY - minY > 3) return false;
    baseY = sum / n;
    if (baseY < 3 || baseY > 110) return false;
    return true;
}

static void flatten(World* w, int cx, int cz, int baseY, bool desert) {
    unsigned char fill = desert ? BLOCK_SAND : BLOCK_DIRT;
    unsigned char top  = desert ? BLOCK_SAND : BLOCK_GRASS;
    for (int z = 1; z <= 14; ++z) {
        for (int x = 1; x <= 14; ++x) {
            int gx = cx * 16 + x, gz = cz * 16 + z;
            int h = villageHeight(w, gx, gz);
            if (h <= 0) continue;
            if (h > baseY) {
                for (int y = baseY; y < h; ++y) put(w, gx, y, gz, BLOCK_AIR);
            } else if (h < baseY) {
                for (int y = h; y < baseY; ++y) put(w, gx, y, gz, fill);
            }
            put(w, gx, baseY - 1, gz, fill);
            put(w, gx, baseY,     gz, top);
        }
    }
}

static void clearBox(World* w, int x0, int z0, int x1, int z1, int y0, int y1) {
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                put(w, x, y, z, BLOCK_AIR);
}

static void buildHouse(World* w, int x0, int z0, int width, int depth,
                       int y, bool desert, bool doorSouth) {
    const unsigned char wall = desert ? BLOCK_SANDSTONE : BLOCK_COBBLESTONE;
    const unsigned char roof = desert ? BLOCK_SANDSTONE : BLOCK_PLANKS;
    const unsigned char floor = BLOCK_PLANKS;

    clearBox(w, x0, z0, x0 + width - 1, z0 + depth - 1, y + 1, y + 5);

    for (int z = z0; z < z0 + depth; ++z)
        for (int x = x0; x < x0 + width; ++x)
            put(w, x, y, z, floor);

    for (int z = z0; z < z0 + depth; ++z) {
        for (int x = x0; x < x0 + width; ++x) {
            bool edge = (x == x0 || x == x0 + width - 1 || z == z0 || z == z0 + depth - 1);
            if (!edge) continue;
            for (int yy = y + 1; yy <= y + 4; ++yy)
                put(w, x, yy, z, wall);
        }
    }

    // Windows. They are intentionally simple 1x1 panes so the generator
    // remains cheap and the house remains visually readable at PSP scale.
    int wy = y + 2;
    put(w, x0, wy, z0 + depth / 2, BLOCK_GLASS_PANE);
    put(w, x0 + width - 1, wy, z0 + depth / 2, BLOCK_GLASS_PANE);
    put(w, x0 + width / 2, wy, z0, BLOCK_GLASS_PANE);
    put(w, x0 + width / 2, wy, z0 + depth - 1, BLOCK_GLASS_PANE);

    // Door on the side facing the village center.
    int dx = x0 + width / 2;
    int dz = doorSouth ? z0 + depth - 1 : z0;
    unsigned char doorData = doorSouth ? 0 : 1;
    put(w, dx, y + 1, dz, BLOCK_DOOR_WOOD, doorData);
    put(w, dx, y + 2, dz, BLOCK_DOOR_WOOD, (unsigned char)(doorData | 8));

    // Small roof cap. Flat roofs are deliberate: stairs have orientation
    // quirks and a slab roof is cheaper while still reading as a village home.
    for (int z = z0 - 0; z < z0 + depth; ++z)
        for (int x = x0; x < x0 + width; ++x)
            put(w, x, y + 5, z, roof);

    // A torch inside each house makes the structure useful at night.
    put(w, dx, y + 3, z0 + depth / 2, BLOCK_TORCH, 0);
}

static void buildWell(World* w, int x0, int z0, int y) {
    for (int z = z0; z <= z0 + 2; ++z)
        for (int x = x0; x <= x0 + 2; ++x) {
            if (x == x0 + 1 && z == z0 + 1) put(w, x, y, z, BLOCK_CALM_WATER);
            else put(w, x, y, z, BLOCK_COBBLESTONE);
        }
    for (int z = z0; z <= z0 + 2; ++z) {
        put(w, x0,     y + 1, z, BLOCK_FENCE);
        put(w, x0 + 2, y + 1, z, BLOCK_FENCE);
    }
    for (int x = x0; x <= x0 + 2; ++x) {
        put(w, x, y + 1, z0,     BLOCK_FENCE);
        put(w, x, y + 1, z0 + 2, BLOCK_FENCE);
    }
    put(w, x0 + 1, y + 2, z0 + 1, BLOCK_PLANKS);
}

static void buildFarm(World* w, int x0, int z0, int y, bool desert) {
    unsigned char soil = BLOCK_FARMLAND;
    for (int z = z0; z < z0 + 5; ++z) {
        for (int x = x0; x < x0 + 4; ++x) {
            if (x == x0 + 2) put(w, x, y, z, BLOCK_CALM_WATER);
            else {
                put(w, x, y - 1, z, desert ? BLOCK_SAND : BLOCK_DIRT);
                put(w, x, y, z, soil, 0);
                put(w, x, y + 1, z, BLOCK_WHEAT, (unsigned char)((x + z) & 7));
            }
        }
    }
}

void villageGenerateChunk(World* w, long worldSeed, int chunkX, int chunkZ) {
    unsigned int h = villageHash(worldSeed, chunkX, chunkZ);
    // Roughly one village every 96 chunks, but only in suitable biomes.
    if ((h % 96u) != 0u) return;

    int centerX = chunkX * 16 + 8;
    int centerZ = chunkZ * 16 + 8;
    // Villages are intentionally restricted to the two most natural early-
    // game settlement biomes. Jungle/forest villages can be added later.
    BiomeId biome = classifyBiomeSpatial(worldSeed, w, centerX, centerZ);
    if (biome != B_PLAINS && biome != B_DESERT) return;
    bool desert = biome == B_DESERT;

    int baseY = 0;
    if (!villageFlatEnough(w, chunkX, chunkZ, baseY)) return;
    flatten(w, chunkX, chunkZ, baseY, desert);

    int ox = chunkX * 16, oz = chunkZ * 16;

    // Three houses form a compact U around a central well/path junction.
    buildHouse(w, ox + 1,  oz + 1, 5, 6, baseY, desert, true);
    buildHouse(w, ox + 10, oz + 1, 5, 6, baseY, desert, true);
    buildHouse(w, ox + 5,  oz + 10, 5, 5, baseY, desert, false);
    buildWell(w, ox + 7, oz + 7, baseY);

    // Two-block-wide paths. Cobble in plains and sandstone in deserts gives
    // the same geometry two distinct visual identities.
    unsigned char path = desert ? BLOCK_SANDSTONE : BLOCK_GRAVEL;
    for (int x = 2; x <= 13; ++x) {
        put(w, ox + x, baseY, oz + 7, path);
        put(w, ox + x, baseY, oz + 8, path);
    }
    for (int z = 6; z <= 13; ++z) {
        put(w, ox + 7, baseY, oz + z, path);
        put(w, ox + 8, baseY, oz + z, path);
    }

    // Small shared crop plot beside the southern house. The water strip is
    // central so every crop row is adjacent to irrigation.
    buildFarm(w, ox + 1, oz + 9, baseY, desert);
}
