#include "world/level/levelgen/dungeon_gen.h"
#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/loot_table.h"
#include "world/level/tile/entity/tile_entity.h"
#include "world/level/world.h"
#include "world/level/level.h"
#include "world/entity/entity_types.h"
#include "world/level/chunk/chunk.h"
#include <pspkernel.h>

static unsigned int dungeonHash(long seed, int cx, int cz) {
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

static bool liquidAt(World* w, int x, int y, int z) {
    unsigned char id = worldBlock(w, x, y, z);
    return isWaterId(id) || isLavaId(id);
}

static bool volumeSafe(World* w, int x0, int x1, int y0, int y1, int z0, int z1) {
    if (y0 <= 2 || y1 + 2 >= WORLD_H) return false;

    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = y0 - 1; y <= y1 + 1; ++y) {
                if (!worldReady(w, x, z)) return false;
                if (liquidAt(w, x, y, z)) return false;
            }

    // Dungeons are underground structures. Require a reasonably solid
    // ceiling above the room so they do not turn into random surface huts.
    int roofSolid = 0;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            if (isSolidPhys(worldBlock(w, x, y1 + 1, z))) ++roofSolid;

    return roofSolid >= 45;
}

static int chooseMob(Random& rng) {
    int r = rng.nextInt(100);
    if (r < 55) return EntityTypes::IdZombie;
    if (r < 80) return EntityTypes::IdSkeleton;
    if (r < 92) return EntityTypes::IdSpider;
    return EntityTypes::IdCreeper;
}

static void placeSpawner(World* w, int x, int y, int z, int mobType) {
    put(w, x, y, z, BLOCK_MOB_SPAWNER, 0);
    MobSpawnerTileEntity* te = new MobSpawnerTileEntity();
    te->mobType = mobType;
    te->spawnDelay = 20 * 8;
    te->minSpawnDelay = 20 * 8;
    te->maxSpawnDelay = 20 * 16;
    te->spawnCount = 4;
    te->maxNearbyEntities = 6;
    te->spawnRange = 4;
    g_level.setTileEntity(x, y, z, te);
}

static void placeChest(World* w, int x, int y, int z, Random& rng) {
    put(w, x, y, z, BLOCK_CHEST, 2);
    lootFillChest(x, y, z, LOOT_TABLE_DUNGEON, rng);
}

void dungeonGenerateChunk(World* w, long worldSeed, int chunkX, int chunkZ) {
    // No Nether dungeons yet. The Nether has its own structure ecosystem;
    // keeping this Overworld-only prevents the structure from appearing in
    // the reserved Nether strip until that generator explicitly opts in.
    if (worldChunkIsReserved(w, chunkX, chunkZ)) return;

    Random rng((long)(int)dungeonHash(worldSeed, chunkX, chunkZ));

    // Roughly one dungeon per 128 chunks. A 512x512 world therefore gets
    // around 8 compact dungeons on average, while a 1024x1024 world gets
    // around 32. The deterministic hash means chunk load order cannot
    // change which chunks contain structures.
    if (rng.nextInt(128) != 0) return;

    const int xo = chunkX * 16;
    const int zo = chunkZ * 16;

    // Keep the whole structure inside one chunk. This avoids cross-chunk
    // structure state and makes streaming/eviction safe on PSP.
    const int cx = xo + 8;
    const int cz = zo + 8;
    const int y = 16 + rng.nextInt(40); // floor 16..55

    const int x0 = cx - 4, x1 = cx + 4;
    const int z0 = cz - 4, z1 = cz + 4;
    const int roomTop = y + 4;

    if (!volumeSafe(w, x0, x1, y, roomTop, z0, z1)) return;

    // The room is a 9x9x5 chamber. First carve the complete interior,
    // then stamp the floor/walls/ceiling so caves and ore cannot leave
    // holes in the finished dungeon.
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int yy = y; yy <= roomTop; ++yy)
                put(w, x, yy, z, BLOCK_AIR);

    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            put(w, x, y, z, (rng.nextInt(5) == 0) ? BLOCK_MOSSY_COBBLE : BLOCK_COBBLESTONE);
            put(w, x, roomTop, z, BLOCK_STONE_BRICKS);
        }

    for (int yy = y + 1; yy < roomTop; ++yy)
        for (int x = x0; x <= x1; ++x) {
            put(w, x, yy, z0, BLOCK_MOSSY_COBBLE);
            put(w, x, yy, z1, BLOCK_MOSSY_COBBLE);
        }

    for (int yy = y + 1; yy < roomTop; ++yy)
        for (int z = z0; z <= z1; ++z) {
            put(w, x0, yy, z, BLOCK_MOSSY_COBBLE);
            put(w, x1, yy, z, BLOCK_MOSSY_COBBLE);
        }

    // Four one-block openings make the room easier to discover from caves.
    // The openings are not connected to artificial tunnels; a nearby cave
    // or mining is still the intended way to find the room.
    put(w, cx, y + 1, z0, BLOCK_AIR);
    put(w, cx, y + 2, z0, BLOCK_AIR);
    put(w, cx, y + 1, z1, BLOCK_AIR);
    put(w, cx, y + 2, z1, BLOCK_AIR);

    // Spawner in the centre, two chests in opposite corners. Chests use the
    // existing dungeon loot table, so loot generation remains independent
    // and deterministic for the structure.
    int mobType = chooseMob(rng);
    placeSpawner(w, cx, y + 1, cz, mobType);

    placeChest(w, x0 + 1, y + 1, z0 + 1, rng);
    if (rng.nextInt(3) != 0)
        placeChest(w, x1 - 1, y + 1, z1 - 1, rng);

    // A small amount of cobweb makes the room read as a dungeon without
    // introducing another structure or entity system.
    if (rng.nextInt(2) == 0) put(w, x0 + 1, y + 2, cz, BLOCK_COBWEB);
    if (rng.nextInt(2) == 0) put(w, x1 - 1, y + 2, cz, BLOCK_COBWEB);
}
