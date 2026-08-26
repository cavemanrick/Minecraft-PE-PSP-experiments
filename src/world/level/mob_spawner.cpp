#include "world/level/mob_spawner.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/entity/local_player.h"
#include "world/entity/mob.h"
#include "world/entity/mob_factory.h"
#include "world/entity/mob_category.h"
#include "world/inventory/inventory.h"
#include "world/entity/entity_types.h"
#include "world/entity/animal/animal.h"
#include "world/entity/animal/strider.h"
#include "world/entity/monster/monster.h"
#include "world/difficulty.h"
#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/nether_gen.h"    // netherShellFloorBaseY/CeilBaseY
#include "world/level/levelgen/nether_biome.h"  // classifyNetherBiome
#include "world/level/levelgen/mcpegen.h"       // worldGenSeed
#include <pspkernel.h>
#include <cmath>

namespace MobSpawner {

struct SpawnEntry { int mobId, weight, minCount, maxCount; };
static const SpawnEntry CREATURE_TABLE[] = {
    { EntityTypes::IdSheep,   12, 2, 3 },
    { EntityTypes::IdPig,     10, 1, 3 },
    { EntityTypes::IdChicken, 10, 2, 4 },
    { EntityTypes::IdCow,      8, 2, 3 },
};
static const int CREATURE_COUNT = (int)(sizeof(CREATURE_TABLE) / sizeof(CREATURE_TABLE[0]));
static const int CREATURE_TOTAL_WEIGHT = 40;

static const SpawnEntry MONSTER_TABLE[] = {
    { EntityTypes::IdZombie,   12, 2, 4 },
    { EntityTypes::IdSpider,    8, 2, 3 },
    { EntityTypes::IdSkeleton,  6, 1, 3 },
    { EntityTypes::IdCreeper,   4, 1, 1 },

};
static const int MONSTER_COUNT = (int)(sizeof(MONSTER_TABLE) / sizeof(MONSTER_TABLE[0]));

static const int MONSTER_TOTAL_WEIGHT = 30;

// Nether Wastes gets its own monster table. Zombified piglins are the
// biome's signature mob and, until ghasts and magma cubes exist here, its
// only one -- so this is a single-entry table rather than a weighted mix.
// The weight still has to match NETHER_WASTES_TOTAL_WEIGHT because
// spawnMonsters derives a per-type population cap from
// (weight / totalWeight), and a mismatch would silently cap the only mob
// in the table at a fraction of the level limit.
//
// The other two Nether biomes deliberately still fall through to
// MONSTER_TABLE above. Giving Soul Sand Valley skeletons and Warped Forest
// its own spawns is a separate piece of work; leaving them alone here
// keeps this change to the one biome that was asked for rather than
// quietly redesigning Nether spawning as a whole.
static const SpawnEntry NETHER_WASTES_TABLE[] = {
    { EntityTypes::IdPigZombie, 30, 2, 4 },
};
static const int NETHER_WASTES_COUNT = (int)(sizeof(NETHER_WASTES_TABLE) / sizeof(NETHER_WASTES_TABLE[0]));
static const int NETHER_WASTES_TOTAL_WEIGHT = 30;

static const int MIN_SPAWN_DISTANCE = 24;

static const int MAX_SPAWN_CLUSTER = 4;

static const int SPAWN_ATTEMPTS = 8;

static const SpawnEntry& pickWeighted(const SpawnEntry* table, int n, int totalWeight,
                                      Random& rng) {
    int r = rng.nextInt(totalWeight);
    for (int i = 0; i < n; i++) {
        r -= table[i].weight;
        if (r < 0) return table[i];
    }
    return table[n - 1];
}

static Random s_rng((long)sceKernelGetSystemTimeLow());

typedef char assert_order_fits[((WORLD_W / 16) * (WORLD_D / 16) <= 256) ? 1 : -1];

static bool spawnOk(Level* L, int x, int y, int z) {
    if (y <= 0 || y + 1 >= WORLD_H) return false;
    if (!L->isSolidBlockingTile(x, y - 1, z)) return false;
    if (L->isSolidBlockingTile(x, y, z))      return false;
    if (L->isSolidBlockingTile(x, y + 1, z))  return false;
    unsigned char here = (unsigned char)L->getTile(x, y, z);
    if (isWaterId(here) || isLavaId(here)) return false;
    return true;
}

static void spawnCreatures(Level* level) {
    LocalPlayer* p = level->player;
    if (!p) return;

    float pfy = p->y - p->heightOffset;

    int mobCount = level->countInstanceOfBaseType(MobCategory::creature.baseType);
    for (int attempt = 0; attempt < SPAWN_ATTEMPTS; attempt++) {
        if (mobCount >= MobCategory::creature.maxPerLevel) return;

        int cx = s_rng.nextInt(WORLD_W / 16);
        int cz = s_rng.nextInt(WORLD_D / 16);
        int bx = cx * 16 + s_rng.nextInt(16);
        int bz = cz * 16 + s_rng.nextInt(16);
        int by = level->getTopSolidBlock(bx, bz);
        if (!spawnOk(level, bx, by, bz)) continue;

        float dx = bx + 0.5f - p->x, dy = by - pfy, dz = bz + 0.5f - p->z;
        if (dx * dx + dy * dy + dz * dz < (float)(MIN_SPAWN_DISTANCE * MIN_SPAWN_DISTANCE)) continue;

        const SpawnEntry& e = pickWeighted(CREATURE_TABLE, CREATURE_COUNT,
                                           CREATURE_TOTAL_WEIGHT, s_rng);
        int cluster = e.minCount + s_rng.nextInt(1 + e.maxCount - e.minCount);
        for (int i = 0; i < cluster; i++) {
            int sx = bx + s_rng.nextInt(6) - s_rng.nextInt(6);
            int sz = bz + s_rng.nextInt(6) - s_rng.nextInt(6);
            int sy = level->getTopSolidBlock(sx, sz);
            if (!spawnOk(level, sx, sy, sz)) continue;
            Mob* m = MobFactory::createMob(e.mobId, level);
            if (!m) continue;
            m->moveTo(sx + 0.5f, (float)sy, sz + 0.5f, s_rng.nextFloat() * 360.0f, 0.0f);

            if (!m->canSpawn()) { delete m; continue; }

            if (s_rng.nextInt(2) == 0) ((Animal*)m)->setAge(-24000);
            level->addEntity(m);
            mobCount++;
        }
    }
}

static const int SURFACE_PROBE_ODDS = 2;
static const int PROBE_SNAP = 8;

static int probeStandableY(Level* L, int x, int z) {

    if (SURFACE_PROBE_ODDS > 0 && s_rng.nextInt(SURFACE_PROBE_ODDS) == 0) {
        int y = L->getTopSolidBlock(x, z);
        return spawnOk(L, x, y, z) ? y : -1;
    }
    int y0 = s_rng.nextInt(WORLD_H);
    for (int d = 0; d <= PROBE_SNAP; d++) {
        if (spawnOk(L, x, y0 - d, z)) return y0 - d;
        if (d && spawnOk(L, x, y0 + d, z)) return y0 + d;
    }
    return -1;
}

// Vertical probe for the Nether, replacing the Overworld one.
//
// probeStandableY's surface branch calls getTopSolidBlock, which reads the
// world heightmap -- and in the Nether that is the top of the sealed
// bedrock roof, not any floor a mob could stand on. Worse, the space above
// the roof is plain air, so spawnOk cheerfully accepts "standing on
// bedrock at y=39" and the spawner was free to populate the outside of the
// Nether's ceiling with mobs the player can never see. Its other branch
// picks a random y across the full 128-block world height, of which only
// the bottom 40 exist here at all, so roughly two thirds of attempts were
// thrown away before they started.
//
// This samples only the shell interior, which is both correct and about
// three times as likely to land somewhere usable per attempt.
static int netherProbeStandableY(Level* L, int x, int z) {
    int lo = netherShellFloorBaseY();
    int hi = netherShellCeilBaseY();
    int y0 = lo + s_rng.nextInt(hi - lo + 1);
    for (int d = 0; d <= PROBE_SNAP; d++) {
        if (y0 - d >= lo && spawnOk(L, x, y0 - d, z)) return y0 - d;
        if (d && y0 + d <= hi && spawnOk(L, x, y0 + d, z)) return y0 + d;
    }
    return -1;
}

static void spawnMonsters(Level* level) {
    LocalPlayer* p = level->player;
    if (!p) return;
    if (level->getDifficulty() == Difficulty::PEACEFUL) return;

    int pcx = (int)floorf(p->x / 16.0f);
    int pcz = (int)floorf(p->z / 16.0f);
    const int R = 128 / 16;

    int mobCount = level->countInstanceOfBaseType(MobCategory::monster.baseType);
    if (mobCount > MobCategory::monster.maxPerLevel) return;

    for (int attempt = 0; attempt < SPAWN_ATTEMPTS; attempt++) {
        if (mobCount > MobCategory::monster.maxPerLevel) return;

        int cx = pcx + s_rng.nextInt(2 * R + 1) - R;
        int cz = pcz + s_rng.nextInt(2 * R + 1) - R;

        if (!level->hasChunksAt(cx * 16, 0, cz * 16, cx * 16 + 15, 0, cz * 16 + 15)) continue;

        // worldChunkIsNether deliberately does not check world size or the
        // reserved bound itself (see its comment in world.h), so
        // worldChunkIsReserved is checked alongside it rather than assumed
        // -- on an infinite world those chunk coordinates are ordinary
        // walkable overworld and must not be treated as the Nether.
        bool nether = worldChunkIsReserved(level->w, cx, cz) &&
                      worldChunkIsNether(level->w, cx, cz);

        int xStart = cx * 16 + s_rng.nextInt(16);
        int zStart = cz * 16 + s_rng.nextInt(16);
        int yStart = nether ? netherProbeStandableY(level, xStart, zStart)
                            : probeStandableY(level, xStart, zStart);
        if (yStart < 0) continue;

        // Biome is classified once per attempt, at the attempt's origin,
        // rather than per pack member: a pack wanders up to a few blocks
        // from the origin while placing, and re-rolling the table midway
        // through would let a single pack come out half piglin and half
        // zombie on a biome border.
        const SpawnEntry* table = MONSTER_TABLE;
        int tableCount = MONSTER_COUNT;
        int tableWeight = MONSTER_TOTAL_WEIGHT;
        if (nether && classifyNetherBiome(worldGenSeed(), level->w, xStart, zStart) == NB_WASTES) {
            table = NETHER_WASTES_TABLE;
            tableCount = NETHER_WASTES_COUNT;
            tableWeight = NETHER_WASTES_TOTAL_WEIGHT;
        }

        if (level->isSolidBlockingTile(xStart, yStart, zStart)) continue;
        if (level->getTile(xStart, yStart, zStart) != BLOCK_AIR) continue;

        int clusterSize = 0;
        for (int pack = 0; pack < 3 && clusterSize < MAX_SPAWN_CLUSTER; pack++) {
            int x = xStart, y = yStart, z = zStart;
            const SpawnEntry* type = 0;
            int packMax = 0, packCount = 0;

            for (int tries = 0; tries < 4; tries++) {
                if (type && packCount > packMax) break;

                x += s_rng.nextInt(6) - s_rng.nextInt(6);
                z += s_rng.nextInt(6) - s_rng.nextInt(6);
                if (!spawnOk(level, x, y, z)) continue;

                float xx = x + 0.5f, yy = (float)y, zz = z + 0.5f;
                if (level->getNearestPlayer(xx, yy, zz, (float)MIN_SPAWN_DISTANCE)) continue;

                if (!type) {
                    type = &pickWeighted(table, tableCount, tableWeight, s_rng);

                    int typeMax = (int)(1.5f * type->weight * MobCategory::monster.maxPerLevel)
                                  / tableWeight;
                    if (level->countInstanceOfType(type->mobId) >= typeMax) break;
                    packMax = type->minCount + s_rng.nextInt(1 + type->maxCount - type->minCount);
                }

                Mob* m = MobFactory::createMob(type->mobId, level);
                if (!m) continue;
                m->moveTo(xx, yy, zz, s_rng.nextFloat() * 360.0f, 0.0f);

                if (!m->canSpawn()) { delete m; continue; }
                level->addEntity(m);
                packCount++;
                mobCount++;
                if (++clusterSize >= MAX_SPAWN_CLUSTER) break;
            }
        }
    }
}


// Striders are passive Nether mobs, but they deliberately have their own
// small population budget rather than consuming the Overworld creature cap.
// They are also spawned on a much slower cadence than hostile mobs: there is
// no reason to spend spawn-probe CPU 10 times per second for a four-mob cap.
static const int STRIDER_MAX_PER_LEVEL = 4;
static const int STRIDER_SPAWN_ATTEMPTS = 4;
static const int STRIDER_MIN_SPAWN_DISTANCE = 24;

static int findStriderLavaY(Level* L, int x, int z) {
    // The generated Nether's lava sea is below the land base. Scan only the
    // useful part of the shell rather than the full 128-block world height.
    for (int y = netherShellFloorBaseY() - 1; y >= 1; --y) {
        unsigned char id = (unsigned char)L->getTile(x, y, z);
        if (!isLavaId(id)) continue;
        unsigned char above = (unsigned char)L->getTile(x, y + 1, z);
        if (!L->isSolidBlockingTile(x, y + 1, z) && !isLavaId(above)) return y;
    }
    return -1;
}

static void spawnStriders(Level* level) {
    LocalPlayer* p = level->player;
    if (!p) return;
    // Striders are passive and therefore spawn even on Peaceful.
    int count = level->countInstanceOfType(EntityTypes::IdStrider);
    if (count >= STRIDER_MAX_PER_LEVEL) return;

    int pcx = (int)floorf(p->x / 16.0f);
    int pcz = (int)floorf(p->z / 16.0f);
    const int R = 128 / 16;

    for (int attempt = 0; attempt < STRIDER_SPAWN_ATTEMPTS; ++attempt) {
        if (count >= STRIDER_MAX_PER_LEVEL) return;

        int cx = pcx + s_rng.nextInt(2 * R + 1) - R;
        int cz = pcz + s_rng.nextInt(2 * R + 1) - R;
        if (!worldChunkIsReserved(level->w, cx, cz) ||
            !worldChunkIsNether(level->w, cx, cz)) continue;
        if (!level->hasChunksAt(cx * 16, 0, cz * 16, cx * 16 + 15, 0, cz * 16 + 15)) continue;

        int x = cx * 16 + s_rng.nextInt(16);
        int z = cz * 16 + s_rng.nextInt(16);
        if (classifyNetherBiome(worldGenSeed(), level->w, x, z) != NB_WARPED_FOREST) continue;

        int y = findStriderLavaY(level, x, z);
        if (y < 0) continue;

        float dx = x + 0.5f - p->x;
        float dy = y - p->y;
        float dz = z + 0.5f - p->z;
        if (dx * dx + dy * dy + dz * dz <
            (float)(STRIDER_MIN_SPAWN_DISTANCE * STRIDER_MIN_SPAWN_DISTANCE)) continue;

        // Small clusters, but never enough to exceed the dedicated cap.
        int cluster = 1 + s_rng.nextInt(2);
        if (cluster > STRIDER_MAX_PER_LEVEL - count)
            cluster = STRIDER_MAX_PER_LEVEL - count;

        for (int i = 0; i < cluster; ++i) {
            int sx = x + s_rng.nextInt(5) - s_rng.nextInt(5);
            int sz = z + s_rng.nextInt(5) - s_rng.nextInt(5);
            int sy = findStriderLavaY(level, sx, sz);
            if (sy < 0) continue;

            Strider* strider = (Strider*)MobFactory::createMob(EntityTypes::IdStrider, level);
            if (!strider) return;
            strider->moveTo(sx + 0.5f, (float)sy, sz + 0.5f,
                            s_rng.nextFloat() * 360.0f, 0.0f);
            if (!strider->canSpawn()) {
                delete strider;
                continue;
            }
            level->addEntity(strider);
            ++count;
        }
    }
}

static const int GEN_CREATURE_CAP = 40;

void populateInitial(Level* level) {

    if (g_level.player->inventory->isCreative()) return;

    if (!activeLevelSource().spawnsMobs()) return;
    const float CREATURE_PROBABILITY = 0.08f;
    const int NCHUNKS = (WORLD_W / 16) * (WORLD_D / 16);

    unsigned char order[256];
    for (int i = 0; i < NCHUNKS; i++) order[i] = (unsigned char)i;
    for (int i = NCHUNKS - 1; i > 0; i--) {
        int j = s_rng.nextInt(i + 1);
        unsigned char t = order[i]; order[i] = order[j]; order[j] = t;
    }

    for (int oi = 0; oi < NCHUNKS; oi++) {
        int cx = order[oi] % (WORLD_W / 16), cz = order[oi] / (WORLD_W / 16);
        int xo = cx * 16, zo = cz * 16;
        while (s_rng.nextFloat() < CREATURE_PROBABILITY) {
            if (level->countInstanceOfBaseType(MobCategory::creature.baseType)
                    >= GEN_CREATURE_CAP) return;
            const SpawnEntry& e = pickWeighted(CREATURE_TABLE, CREATURE_COUNT,
                                               CREATURE_TOTAL_WEIGHT, s_rng);
            int count = e.minCount + s_rng.nextInt(1 + e.maxCount - e.minCount);
            int x = xo + s_rng.nextInt(16), z = zo + s_rng.nextInt(16);
            int startX = x, startZ = z;
            for (int c = 0; c < count; c++) {
                for (int a = 0; a < 4; a++) {
                    int y = level->getTopSolidBlock(x, z);
                    if (spawnOk(level, x, y, z)) {
                        Mob* m = MobFactory::createMob(e.mobId, level);
                        if (!m) return;
                        m->moveTo(x + 0.5f, (float)y, z + 0.5f, s_rng.nextFloat() * 360.0f, 0.0f);

                        if (m->canSpawn()) {
                            if (s_rng.nextInt(2) == 0) ((Animal*)m)->setAge(-24000);
                            level->addEntity(m);
                            break;
                        }
                        delete m;
                    }
                    x += s_rng.nextInt(5) - s_rng.nextInt(5);
                    z += s_rng.nextInt(5) - s_rng.nextInt(5);

                    while (x < xo || x >= xo + 16 || z < zo || z >= zo + 16) {
                        x = startX + s_rng.nextInt(5) - s_rng.nextInt(5);
                        z = startZ + s_rng.nextInt(5) - s_rng.nextInt(5);
                    }
                }
            }
        }
    }
}

void tick(Level* level, bool spawnEnemies, bool spawnFriendlies) {

    if (g_level.player->inventory->isCreative()) return;

    if (!activeLevelSource().spawnsMobs()) return;

    if (spawnFriendlies) spawnCreatures(level);
    if (spawnEnemies)    {
        spawnMonsters(level);
        if ((level->w->time % 40) == 0) spawnStriders(level);
    }
}

}
