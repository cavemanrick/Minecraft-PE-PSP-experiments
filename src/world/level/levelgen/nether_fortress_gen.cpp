#include "world/level/levelgen/nether_fortress_gen.h"
#include "world/level/levelgen/nether_gen.h"
#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/loot_table.h"
#include "world/level/world.h"
#include "world/level/level.h"
#include "world/level/chunk/chunk.h"
#include "world/level/tile/entity/tile_entity.h"
#include "world/entity/entity_types.h"

static unsigned int fortressHash(long seed, int cx, int cz) {
    unsigned int h = (unsigned int)seed;
    h ^= (unsigned int)cx * 0x6D2B79F5u;
    h ^= (unsigned int)cz * 0x1B873593u;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

static void put(World* w, int x, int y, int z, unsigned char id, unsigned char data = 0) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return;
    worldSetBlockAndData(w, x, y, z, id, data);
}

static void placeSpawner(World* w, int x, int y, int z, int mobType, int delayMin, int delayMax) {
    put(w, x, y, z, BLOCK_MOB_SPAWNER);
    MobSpawnerTileEntity* te = new MobSpawnerTileEntity();
    te->mobType = mobType;
    te->spawnDelay = delayMin;
    te->minSpawnDelay = delayMin;
    te->maxSpawnDelay = delayMax;
    te->spawnCount = 3;
    te->maxNearbyEntities = 6;
    te->spawnRange = 4;
    g_level.setTileEntity(x, y, z, te);
}

static void placeChest(World* w, int x, int y, int z, Random& rng) {
    put(w, x, y, z, BLOCK_CHEST, 4);
    lootFillChest(x, y, z, LOOT_TABLE_NETHER_FORTRESS, rng);
}

static int findFloor(World* w, int x, int z) {
    // The Nether generator has a dry floor above lava and an open cavern
    // beneath the ceiling. Find the highest solid block below our bridge
    // deck. This also works when a fortress happens to cross a small hill.
    for (int y = 28; y >= 1; --y) {
        unsigned char id = worldBlock(w, x, y, z);
        if (isSolidPhys(id) && !isLiquidId(id)) return y;
    }
    return -1;
}

static void clearBox(World* w, int x0, int x1, int y0, int y1, int z0, int z1) {
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y) {
                if (worldBlock(w, x, y, z) != BLOCK_BEDROCK)
                    put(w, x, y, z, BLOCK_AIR);
            }
}

static void buildRail(World* w, int x0, int x1, int z0, int z1, int y) {
    // Nether-brick fences are not a separate block in this build, so use
    // normal Nether brick as a waist-high parapet. It keeps the fortress
    // readable without inventing another block id.
    for (int x = x0; x <= x1; ++x) {
        put(w, x, y + 1, z0, BLOCK_NETHER_BRICK);
        put(w, x, y + 1, z1, BLOCK_NETHER_BRICK);
    }
    for (int z = z0; z <= z1; ++z) {
        put(w, x0, y + 1, z, BLOCK_NETHER_BRICK);
        put(w, x1, y + 1, z, BLOCK_NETHER_BRICK);
    }
}

static void buildTower(World* w, int x0, int z0, int y, int h) {
    const int x1 = x0 + 3;
    const int z1 = z0 + 3;

    for (int y2 = y; y2 <= y + h; ++y2) {
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z) {
                bool wall = x == x0 || x == x1 || z == z0 || z == z1;
                put(w, x, y2, z, wall ? BLOCK_NETHER_BRICK : BLOCK_AIR);
            }
    }

    // Open the lower doorway toward the centre of the fortress.
    put(w, x0 + 1, y + 1, z1, BLOCK_AIR);
    put(w, x0 + 2, y + 1, z1, BLOCK_AIR);

    // Battlement corners.
    for (int x = x0; x <= x1; ++x) {
        if ((x - x0) & 1) continue;
        put(w, x, y + h + 1, z0, BLOCK_NETHER_BRICK);
        put(w, x, y + h + 1, z1, BLOCK_NETHER_BRICK);
    }
    for (int z = z0; z <= z1; ++z) {
        if ((z - z0) & 1) continue;
        put(w, x0, y + h + 1, z, BLOCK_NETHER_BRICK);
        put(w, x1, y + h + 1, z, BLOCK_NETHER_BRICK);
    }
}

void netherFortressGenerateChunk(World* w, long worldSeed, int chunkX, int chunkZ) {
    if (!worldChunkIsNether(w, chunkX, chunkZ)) return;

    Random rng((long)(int)fortressHash(worldSeed, chunkX, chunkZ));

    // About 1 fortress per 96 Nether chunks. The standard Nether strip is
    // 16x16 chunks, giving roughly 2-3 fortresses per generated Nether.
    if (rng.nextInt(96) != 0) return;

    const int xo = chunkX * 16;
    const int zo = chunkZ * 16;

    // Stay one block inside the reserved border so the fortress cannot
    // overwrite the Nether's bedrock wall.
    const int x0 = xo + 1;
    const int x1 = xo + 14;
    const int z0 = zo + 1;
    const int z1 = zo + 14;

    // Deck at y=29 leaves room for a two-block lava cavern underneath and
    // keeps the 7-block structure comfortably below the Nether ceiling.
    const int deckY = 29;
    const int topY = 37;

    // If the chunk is too cramped by a ceiling/floor touch point, do not
    // generate a fortress that would be visibly embedded in rock.
    int openColumns = 0;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            if (worldBlock(w, x, deckY, z) == BLOCK_AIR &&
                worldBlock(w, x, topY, z) == BLOCK_AIR)
                ++openColumns;
        }
    if (openColumns < 130) return;

    // The fortress interior is never given its own ceiling -- the halls
    // and tower tops are open by design, same as the dungeon room relies
    // on volumeSafe()'s roof check rather than placing a lid of its own.
    // Without this check, a chunk whose natural Nether ceiling happens to
    // dip low here (see ceilHillDepth's per-column hill variation) would
    // get carved straight through into open sky-adjacent space: skylight
    // has no Nether-specific cutoff in this engine (lightRawAtNoProp is
    // purely heightmap-driven), so an open roof reads as lit rather than
    // dark, and the fortress's mob spawners below the brightness-8
    // threshold in MobSpawnerTileEntity::tick() would never activate.
    // The Nether ceiling shell bottoms out at y=39. The old code tested
    // topY+1 (y=38), which is deliberately part of the air volume, so this
    // test always counted zero and rejected every fortress. Check the real
    // underside of the ceiling instead. A moderate majority threshold keeps
    // this robust if ceiling decoration changes later.
    const int ceilingBottomY = netherShellCeilBaseY() - netherShellCeilHillMax();
    int roofSolid = 0;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            if (isSolidPhys(worldBlock(w, x, ceilingBottomY, z))) ++roofSolid;
    if (roofSolid < 128) return;

    clearBox(w, x0, x1, deckY + 1, topY, z0, z1);

    // A full Nether-brick deck makes the structure navigable even when it
    // crosses lava. Four support piers visually anchor it to the terrain.
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            put(w, x, deckY, z, BLOCK_NETHER_BRICK);

    const int piers[4][2] = {
        { x0 + 1, z0 + 1 }, { x1 - 1, z0 + 1 },
        { x0 + 1, z1 - 1 }, { x1 - 1, z1 - 1 }
    };
    for (int i = 0; i < 4; ++i) {
        int px = piers[i][0], pz = piers[i][1];
        int fy = findFloor(w, px, pz);
        if (fy < 0) continue;
        for (int y = fy + 1; y < deckY; ++y)
            put(w, px, y, pz, BLOCK_NETHER_BRICK);
    }

    // Central north/south hall.
    for (int z = z0 + 2; z <= z1 - 2; ++z) {
        for (int x = xo + 6; x <= xo + 9; ++x) {
            put(w, x, deckY + 1, z, BLOCK_AIR);
            put(w, x, deckY + 2, z, BLOCK_AIR);
            put(w, x, deckY + 3, z, BLOCK_AIR);
        }
    }

    // Cross-hall.
    for (int x = x0 + 2; x <= x1 - 2; ++x)
        for (int z = zo + 6; z <= zo + 9; ++z) {
            put(w, x, deckY + 1, z, BLOCK_AIR);
            put(w, x, deckY + 2, z, BLOCK_AIR);
            put(w, x, deckY + 3, z, BLOCK_AIR);
        }

    // Low parapets around the two bridges.
    buildRail(w, x0 + 1, x1 - 1, z0 + 1, z1 - 1, deckY);

    // Four compact towers. They deliberately use the existing Nether brick
    // blocks and stairs rather than adding new assets.
    buildTower(w, x0 + 1, z0 + 1, deckY + 1, 6);
    buildTower(w, x1 - 4, z0 + 1, deckY + 1, 6);
    buildTower(w, x0 + 1, z1 - 4, deckY + 1, 6);
    buildTower(w, x1 - 4, z1 - 4, deckY + 1, 6);

    // Reopen the hall entrances after the tower shells were built.
    for (int z = z0 + 2; z <= z0 + 5; ++z) {
        put(w, xo + 7, deckY + 1, z, BLOCK_AIR);
        put(w, xo + 7, deckY + 2, z, BLOCK_AIR);
        put(w, xo + 8, deckY + 1, z, BLOCK_AIR);
        put(w, xo + 8, deckY + 2, z, BLOCK_AIR);
    }
    for (int z = z1 - 5; z <= z1 - 2; ++z) {
        put(w, xo + 7, deckY + 1, z, BLOCK_AIR);
        put(w, xo + 7, deckY + 2, z, BLOCK_AIR);
        put(w, xo + 8, deckY + 1, z, BLOCK_AIR);
        put(w, xo + 8, deckY + 2, z, BLOCK_AIR);
    }

    // Fortress loot. Two chests are guaranteed, with a third chance in the
    // upper-right tower. The loot table keeps generation data-driven.
    placeChest(w, xo + 3, deckY + 1, zo + 3, rng);
    placeChest(w, xo + 12, deckY + 1, zo + 12, rng);
    if (rng.nextInt(3) == 0)
        placeChest(w, xo + 12, deckY + 1, zo + 3, rng);

    // Until a Blaze entity exists in the engine, the fortress uses the
    // existing Nether skeleton/pig-zombie mob set. These are real persistent
    // mob spawners, not generation-time one-shot entities.
    placeSpawner(w, xo + 7, deckY + 1, zo + 8,
                 EntityTypes::IdSkeleton, 20 * 7, 20 * 14);
    if (rng.nextInt(2) == 0)
        placeSpawner(w, xo + 8, deckY + 1, zo + 8,
                     EntityTypes::IdPigZombie, 20 * 8, 20 * 16);

    // A small Nether-brick stair marker at the centre makes the fortress
    // entrance readable from a distance without relying on new textures.
    put(w, xo + 7, deckY + 1, zo + 7, BLOCK_STAIRS_NETHER_BRICK, 0);
    put(w, xo + 8, deckY + 1, zo + 7, BLOCK_STAIRS_NETHER_BRICK, 0);
}
