#include "world/level/tile/entity/tile_entity.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/entity/mob.h"
#include "world/entity/mob_factory.h"
#include "world/entity/entity_types.h"
#include "world/difficulty.h"
#include "world/entity/mob_category.h"
#include "world/level/levelgen/Random.h"
#include "nbt/compound_tag.h"
#include <cmath>

static unsigned int spawnerHash(int x, int y, int z, long time) {
    unsigned int h = (unsigned int)x * 0x45d9f3bu;
    h ^= (unsigned int)y * 0x119de1f3u;
    h ^= (unsigned int)z * 0x3449a8ddu;
    h ^= (unsigned int)time * 0x27d4eb2du;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return h;
}

static bool validSpawnerMob(int id) {
    return id == EntityTypes::IdZombie ||
           id == EntityTypes::IdSkeleton ||
           id == EntityTypes::IdSpider ||
           id == EntityTypes::IdCreeper ||
           id == EntityTypes::IdPigZombie;
}

MobSpawnerTileEntity::MobSpawnerTileEntity()
: TileEntity(TE_MOB_SPAWNER),
  mobType(EntityTypes::IdZombie),
  spawnDelay(20 * 10),
  minSpawnDelay(20 * 10),
  maxSpawnDelay(20 * 20),
  spawnCount(4),
  maxNearbyEntities(6),
  spawnRange(4),
  initialized(false) {}

void MobSpawnerTileEntity::tick() {
    if (!level || level->isClientSide || !level->player) return;
    if (level->getTile(x, y, z) != BLOCK_MOB_SPAWNER) {
        removed = true;
        return;
    }

    // Vanilla-like activation range. This is deliberately checked before
    // any entity query or spawn attempts so dormant dungeon rooms are cheap.
    float dx = level->player->x - (x + 0.5f);
    float dy = level->player->y - (y + 0.5f);
    float dz = level->player->z - (z + 0.5f);
    if (dx * dx + dy * dy + dz * dz > 16.0f * 16.0f) return;

    if (!validSpawnerMob(mobType)) mobType = EntityTypes::IdZombie;

    if (spawnDelay > 0) {
        --spawnDelay;
        return;
    }

    // Hostile dungeon spawners stay quiet on Peaceful.
    if (level->getDifficulty() == Difficulty::PEACEFUL) {
        spawnDelay = 20 * 10;
        return;
    }

    // A dark room is required. getRawBrightness includes emitted light, so
    // torches or another light source can suppress the spawner naturally.
    if (level->getRawBrightness(x, y + 1, z) > 8) {
        spawnDelay = 20;
        return;
    }

    AABB nearbyBox((float)(x - 8), (float)(y - 4), (float)(z - 8),
                   (float)(x + 9), (float)(y + 5), (float)(z + 9));
    EntityList nearby;
    int nearbyCount = level->getEntitiesOfType(mobType, nearbyBox, nearby);

    if (nearbyCount >= maxNearbyEntities) {
        spawnDelay = 20;
        return;
    }

    int room = spawnCount;
    if (room < 1) room = 1;
    if (room > maxNearbyEntities - nearbyCount)
        room = maxNearbyEntities - nearbyCount;

    Random rng((long)spawnerHash(x, y, z, level->w->time));
    int spawned = 0;

    for (int i = 0; i < room; ++i) {
        int sx = x + rng.nextInt(spawnRange * 2 + 1) - spawnRange;
        int sz = z + rng.nextInt(spawnRange * 2 + 1) - spawnRange;

        // Find the first standable block at or below the sampled point. The
        // dungeon room is only five blocks tall, but searching a few blocks
        // makes uneven carved floors robust without scanning a large volume.
        int sy = -1;
        int start = y + rng.nextInt(3) - 1;
        for (int d = 0; d <= 3 && sy < 0; ++d) {
            int yy = start - d;
            if (yy > 0 && yy + 2 < WORLD_H &&
                level->isSolidBlockingTile(sx, yy - 1, sz) &&
                !level->isSolidBlockingTile(sx, yy, sz) &&
                !level->isSolidBlockingTile(sx, yy + 1, sz) &&
                !isLiquidId((unsigned char)level->getTile(sx, yy, sz)))
                sy = yy;
        }
        if (sy < 0) continue;

        Mob* mob = MobFactory::createMob(mobType, level);
        if (!mob) break;
        mob->moveTo(sx + 0.5f, (float)sy, sz + 0.5f,
                    rng.nextFloat() * 360.0f, 0.0f);

        // Keep the actual mob validation in one place. This catches
        // bounding-box differences between zombies, spiders, etc.
        if (!mob->canSpawn()) {
            delete mob;
            continue;
        }

        level->addEntity(mob);
        ++spawned;
    }

    // Successful or failed activation both get a bounded delay. Failed
    // attempts must not turn a bad room into a 20-tick busy loop forever.
    if (spawned > 0) {
        int span = maxSpawnDelay - minSpawnDelay;
        if (span < 0) span = 0;
        spawnDelay = minSpawnDelay + (span ? rng.nextInt(span + 1) : 0);
    } else {
        spawnDelay = 40;
    }
}

bool MobSpawnerTileEntity::save(CompoundTag* tag) {
    TileEntity::save(tag);
    tag->putInt("MobType", mobType);
    tag->putInt("SpawnDelay", spawnDelay);
    tag->putInt("MinSpawnDelay", minSpawnDelay);
    tag->putInt("MaxSpawnDelay", maxSpawnDelay);
    tag->putInt("SpawnCount", spawnCount);
    tag->putInt("MaxNearbyEntities", maxNearbyEntities);
    tag->putInt("SpawnRange", spawnRange);
    return true;
}

void MobSpawnerTileEntity::load(CompoundTag* tag) {
    TileEntity::load(tag);
    mobType = tag->getInt("MobType");
    if (!validSpawnerMob(mobType)) mobType = EntityTypes::IdZombie;
    spawnDelay = tag->contains("SpawnDelay") ? tag->getInt("SpawnDelay") : 20 * 10;
    minSpawnDelay = tag->contains("MinSpawnDelay") ? tag->getInt("MinSpawnDelay") : 20 * 10;
    maxSpawnDelay = tag->contains("MaxSpawnDelay") ? tag->getInt("MaxSpawnDelay") : 20 * 20;
    spawnCount = tag->contains("SpawnCount") ? tag->getInt("SpawnCount") : 4;
    maxNearbyEntities = tag->contains("MaxNearbyEntities") ? tag->getInt("MaxNearbyEntities") : 6;
    spawnRange = tag->contains("SpawnRange") ? tag->getInt("SpawnRange") : 4;
}
