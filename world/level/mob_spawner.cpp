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
#include "world/entity/monster/ghast.h"
#include "world/entity/monster/pig_zombie.h"
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

        // Overworld animals (sheep/pig/chicken/cow) have no business in the
        // Nether. Without this check getTopSolidBlock reads the sealed
        // bedrock roof in Nether chunks (same issue netherProbeStandableY's
        // comment documents for the monster spawner) and happily reports it
        // as standable, letting cows spawn on top of the world.
        if (worldChunkIsReserved(level->w, cx, cz) &&
            worldChunkIsNether(level->w, cx, cz)) continue;

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

        // This function is Overworld-only. Zombified piglins used to
        // spawn from here too, gated to Nether Wastes via a second table
        // (NETHER_WASTES_TABLE) -- moved out to their own spawnPigZombies
        // below, with a real dedicated population cap (8) and a wander
        // leash, the same way striders already have their own spawn
        // function separate from this one rather than sharing the
        // Overworld monster pool and its proportional-weight cap math.
        // MONSTER_TABLE below is therefore reached unconditionally now;
        // if this function is ever called for a Nether chunk it should
        // simply find no valid standable spot via probeStandableY's
        // Overworld-shaped search and skip the attempt, not spawn
        // Overworld mobs into the Nether.
        int xStart = cx * 16 + s_rng.nextInt(16);
        int zStart = cz * 16 + s_rng.nextInt(16);
        int yStart = probeStandableY(level, xStart, zStart);
        if (yStart < 0) continue;

        const SpawnEntry* table = MONSTER_TABLE;
        int tableCount = MONSTER_COUNT;
        int tableWeight = MONSTER_TOTAL_WEIGHT;

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

// Zombified piglins: their own dedicated population budget, the same
// pattern striders already use below, rather than sharing the Overworld
// monster pool via a weighted table (NETHER_WASTES_TABLE, removed --
// see spawnMonsters' comment). That table's per-type cap was derived
// proportionally from MobCategory::monster.maxPerLevel and the table
// weight, which produced a much higher and less predictable effective
// cap than a plain fixed number; PIGZOMBIE_MAX_PER_LEVEL below is exactly
// what it says instead.
static const int PIGZOMBIE_MAX_PER_LEVEL = 8;
static const int PIGZOMBIE_SPAWN_ATTEMPTS = 4;
static const int PIGZOMBIE_MIN_SPAWN_DISTANCE = 24;

static void spawnPigZombies(Level* level) {
    LocalPlayer* p = level->player;
    if (!p) return;
    if (level->getDifficulty() == Difficulty::PEACEFUL) return;

    int count = level->countInstanceOfType(EntityTypes::IdPigZombie);
    if (count >= PIGZOMBIE_MAX_PER_LEVEL) return;

    int pcx = (int)floorf(p->x / 16.0f);
    int pcz = (int)floorf(p->z / 16.0f);
    const int R = 128 / 16;

    for (int attempt = 0; attempt < PIGZOMBIE_SPAWN_ATTEMPTS; ++attempt) {
        if (count >= PIGZOMBIE_MAX_PER_LEVEL) return;

        int cx = pcx + s_rng.nextInt(2 * R + 1) - R;
        int cz = pcz + s_rng.nextInt(2 * R + 1) - R;

        // worldChunkIsNether deliberately does not check world size or the
        // reserved bound itself (see its comment in world.h), so
        // worldChunkIsReserved is checked alongside it rather than assumed
        // -- on an infinite world those chunk coordinates are ordinary
        // walkable overworld and must not be treated as the Nether.
        if (!worldChunkIsReserved(level->w, cx, cz) ||
            !worldChunkIsNether(level->w, cx, cz)) continue;
        if (!level->hasChunksAt(cx * 16, 0, cz * 16, cx * 16 + 15, 0, cz * 16 + 15)) continue;

        int xStart = cx * 16 + s_rng.nextInt(16);
        int zStart = cz * 16 + s_rng.nextInt(16);

        // Confined to Nether Wastes: the biome check happens before the
        // (comparatively expensive) standable-floor probe, same ordering
        // spawnMonsters used for its old Wastes-only gate.
        if (classifyNetherBiome(worldGenSeed(), level->w, xStart, zStart) != NB_WASTES) continue;

        int yStart = netherProbeStandableY(level, xStart, zStart);
        if (yStart < 0) continue;

        float dx = xStart + 0.5f - p->x;
        float dy = yStart - p->y;
        float dz = zStart + 0.5f - p->z;
        if (dx * dx + dy * dy + dz * dz <
            (float)(PIGZOMBIE_MIN_SPAWN_DISTANCE * PIGZOMBIE_MIN_SPAWN_DISTANCE)) continue;

        if (level->isSolidBlockingTile(xStart, yStart, zStart)) continue;
        if (level->getTile(xStart, yStart, zStart) != BLOCK_AIR) continue;

        int cluster = 2 + s_rng.nextInt(3); // 2-4, same shape the old table entry used
        if (cluster > PIGZOMBIE_MAX_PER_LEVEL - count)
            cluster = PIGZOMBIE_MAX_PER_LEVEL - count;

        for (int i = 0; i < cluster; ++i) {
            int x = xStart + s_rng.nextInt(6) - s_rng.nextInt(6);
            int z = zStart + s_rng.nextInt(6) - s_rng.nextInt(6);
            if (!spawnOk(level, x, yStart, z)) continue;
            // Re-check biome per cluster member: same reasoning as
            // spawnStriders' own per-member check -- a jitter can walk
            // off Wastes onto neighbouring Soul Sand Valley/Warped Forest
            // ground.
            if (classifyNetherBiome(worldGenSeed(), level->w, x, z) != NB_WASTES) continue;

            PigZombie* pz = (PigZombie*)MobFactory::createMob(EntityTypes::IdPigZombie, level);
            if (!pz) return;
            pz->moveTo(x + 0.5f, (float)yStart, z + 0.5f,
                      s_rng.nextFloat() * 360.0f, 0.0f);
            if (!pz->canSpawn()) {
                delete pz;
                continue;
            }
            // Confines wandering to this patch afterward (see
            // PigZombie::getWalkTargetValue) -- this is what actually
            // keeps a spawned-in group near the Wastes ground it spawned
            // on instead of pathfinding arbitrarily far away over time.
            pz->setHome(x + 0.5f, (float)yStart, z + 0.5f);
            level->addEntity(pz);
            ++count;
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

// Warped Forest floor search, replacing the old lava-surface search
// (findStriderLavaY). Striders now spawn standing on warped nylium, not
// floating on a lava sea -- Strider::travel() already has a full
// non-lava/on-land branch (gravity + ground collision, see strider.cpp),
// so this only changes where they come from, not how they move once
// spawned. Returns the first open-air Y with warped nylium immediately
// below, or -1 if the column has none in the searched range.
static int findStriderNyliumY(Level* L, int x, int z) {
    int lo = netherShellFloorBaseY(), hi = netherShellCeilBaseY();
    for (int y = hi; y >= lo; --y) {
        if ((unsigned char)L->getTile(x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        if (L->isSolidBlockingTile(x, y, z)) continue;
        if (L->isSolidBlockingTile(x, y + 1, z)) continue;
        return y;
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

        // Biome-gated: only Warped Forest chunks are candidates at all.
        // Checked before the (cheaper-looking, but now pointless if the
        // biome is wrong) floor search, same order spawnMonsters already
        // uses for its own Wastes-only gate above.
        if (classifyNetherBiome(worldGenSeed(), level->w, x, z) != NB_WARPED_FOREST) continue;

        int y = findStriderNyliumY(level, x, z);
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
            // Re-check biome per cluster member too: a 5-block jitter can
            // walk off the Warped Forest edge onto neighbouring Wastes/
            // Soul Sand Valley ground, which happens to still have nylium
            // nowhere near it (so findStriderNyliumY would already return
            // -1 there in practice), but the explicit check keeps the
            // intent obvious rather than relying on that coincidence.
            if (classifyNetherBiome(worldGenSeed(), level->w, sx, sz) != NB_WARPED_FOREST) continue;
            int sy = findStriderNyliumY(level, sx, sz);
            if (sy < 0) continue;

            Strider* strider = (Strider*)MobFactory::createMob(EntityTypes::IdStrider, level);
            if (!strider) return;
            strider->moveTo(sx + 0.5f, (float)sy, sz + 0.5f,
                            s_rng.nextFloat() * 360.0f, 0.0f);
            if (!strider->canSpawn()) {
                delete strider;
                continue;
            }
            // Anchors this strider to a small home patch (see
            // Strider::aiStep/wanderCenter in strider.cpp): wandering is
            // clamped to a short leash from here instead of drifting
            // indefinitely, which is what actually makes them "easier to
            // catch" -- a strider that stays near where you found it
            // rather than one that can wander off through the whole
            // biome.
            strider->setWanderCenter(sx + 0.5f, (float)sy, sz + 0.5f);
            level->addEntity(strider);
            ++count;
        }
    }
}

static const int GHAST_MAX_PER_LEVEL = 3;
static const int GHAST_SPAWN_ATTEMPTS = 6;
static const int GHAST_MIN_SPAWN_DISTANCE = 24;

// Ghasts spawn in Nether Wastes and Soul Sand Valley (not Warped Forest --
// see the wiki's spawn table), floating in open air rather than standing
// on anything, so this is its own dedicated function rather than routed
// through spawnMonsters: that function's whole design (netherProbeStandableY,
// spawnOk's ground checks) assumes a standable floor under the spawn
// point, which a ghast doesn't need or want. Same reasoning spawnStriders
// already established for its own lava-surface spawn shape.
static void spawnGhasts(Level* level) {
    LocalPlayer* p = level->player;
    if (!p) return;
    if (level->getDifficulty() == Difficulty::PEACEFUL) return;

    int count = level->countInstanceOfType(EntityTypes::IdGhast);
    if (count >= GHAST_MAX_PER_LEVEL) return;

    int pcx = (int)floorf(p->x / 16.0f);
    int pcz = (int)floorf(p->z / 16.0f);
    const int R = 128 / 16;

    for (int attempt = 0; attempt < GHAST_SPAWN_ATTEMPTS; ++attempt) {
        if (count >= GHAST_MAX_PER_LEVEL) return;

        int cx = pcx + s_rng.nextInt(2 * R + 1) - R;
        int cz = pcz + s_rng.nextInt(2 * R + 1) - R;
        if (!worldChunkIsReserved(level->w, cx, cz) ||
            !worldChunkIsNether(level->w, cx, cz)) continue;
        if (!level->hasChunksAt(cx * 16, 0, cz * 16, cx * 16 + 15, 0, cz * 16 + 15)) continue;

        int x = cx * 16 + s_rng.nextInt(16);
        int z = cz * 16 + s_rng.nextInt(16);

        NetherBiomeId biome = classifyNetherBiome(worldGenSeed(), level->w, x, z);
        if (biome != NB_WASTES && biome != NB_SOUL_SAND_VALLEY) continue;

        // Random height within the navigable shell rather than a specific
        // surface -- a ghast can be anywhere in open air, not just near
        // the floor. netherShellFloorBaseY/CeilBaseY bound the shell the
        // same way findLavaSurfaceY's own top bound does in Strider.
        int loY = netherShellFloorBaseY() + 2;
        int hiY = netherShellCeilBaseY() - 2;
        if (hiY <= loY) continue;
        int y = loY + s_rng.nextInt(hiY - loY);

        float dx = x + 0.5f - p->x;
        float dy = y - p->y;
        float dz = z + 0.5f - p->z;
        if (dx * dx + dy * dy + dz * dz <
            (float)(GHAST_MIN_SPAWN_DISTANCE * GHAST_MIN_SPAWN_DISTANCE)) continue;

        Ghast* ghast = (Ghast*)MobFactory::createMob(EntityTypes::IdGhast, level);
        if (!ghast) return;
        ghast->moveTo(x + 0.5f, (float)y, z + 0.5f, s_rng.nextFloat() * 360.0f, 0.0f);
        if (!ghast->canSpawn()) {
            delete ghast;
            continue;
        }
        level->addEntity(ghast);
        ++count;
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

        // Same reserved-Nether exclusion as spawnCreatures: this loop's
        // getTopSolidBlock would otherwise read the sealed bedrock roof and
        // seed the initial world with cows standing on top of the Nether.
        if (worldChunkIsReserved(level->w, cx, cz) &&
            worldChunkIsNether(level->w, cx, cz)) continue;

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
        if ((level->w->time % 40) == 0) spawnPigZombies(level);
        if ((level->w->time % 60) == 0) spawnGhasts(level);
    }
}

}
