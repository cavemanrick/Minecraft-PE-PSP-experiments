#include "world/level/levelgen/village_gen.h"
#include "world/level/levelgen/features.h"
#include "world/level/levelgen/biome.h"
#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/loot_table.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"

// A deliberately small village system. Each village lives entirely inside
// one 16x16 chunk, which avoids cross-chunk structure bookkeeping on PSP.
// The village is made from three houses, a well, paths, and a small farm.
// It is intentionally lightweight: no new entity AI is required.

// Lowest village floor that is still dry land. Sea level is 64 in this
// generator and the highest water block is 63.
#define VILLAGE_MIN_BASE_Y 64

// Small fixed registry of chunks that actually placed a village, checked
// by villageChunkHasVillage() so the achievement tick can detect "player
// is standing in a village" without scanning blocks. Sized generously
// against the density comment below (roughly 2-3 villages per 512 world,
// ~10 per 1024) -- 64 entries covers even a large/lucky-seed 1024 world
// with room to spare. If it ever does fill, new villages simply stop
// being tracked for this achievement (they still generate normally); this
// is a discovery-credit list, not gameplay state, so silently dropping
// the least important thing (one more entry in an already-large list of
// discovered villages) is an acceptable and safe degradation.
#define MAX_TRACKED_VILLAGES 64
static int s_villageChunkX[MAX_TRACKED_VILLAGES];
static int s_villageChunkZ[MAX_TRACKED_VILLAGES];
static int s_villageCount = 0;

static void registerVillageChunk(int chunkX, int chunkZ) {
    if (s_villageCount >= MAX_TRACKED_VILLAGES) return;
    s_villageChunkX[s_villageCount] = chunkX;
    s_villageChunkZ[s_villageCount] = chunkZ;
    s_villageCount++;
}

bool villageChunkHasVillage(int chunkX, int chunkZ) {
    for (int i = 0; i < s_villageCount; ++i)
        if (s_villageChunkX[i] == chunkX && s_villageChunkZ[i] == chunkZ) return true;
    return false;
}

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

// Sampled over the same 1..14 span that flatten() and the buildings
// actually occupy, not a smaller 2..13 inset. The inset meant the outer
// ring was never flatness-tested but was still bulldozed -- and house 2
// (x0+10..x0+14) and house 3 (z0+10..z0+14) both reach x/z 14, so the
// untested ring was load-bearing, not decorative. Testing what we build on
// is the whole point of the check.
static bool villageFlatEnough(World* w, int cx, int cz, int& baseY) {
    int minY = WORLD_H, maxY = 0, sum = 0, n = 0;
    for (int z = 1; z <= 14; ++z) {
        for (int x = 1; x <= 14; ++x) {
            int gx = cx * 16 + x, gz = cz * 16 + z;
            int y = villageHeight(w, gx, gz);
            if (y <= 0 || y >= 120) return false;

            // Reject anything underwater. villageHeight deliberately skips
            // water and liquid when it scans down, so a flat sea floor or
            // river bed reads back as beautifully flat village terrain --
            // which is exactly how villages ended up generating inside
            // lakes and out in the ocean, with flatten() then filling the
            // water column with dirt and the surrounding sea pouring back
            // in over the roofs.
            //
            // The test is cheap because of that same skipping: the block
            // AT the returned height is the first thing above the ground,
            // so if the column is submerged it is water by definition.
            unsigned char above = worldBlock(w, gx, y, gz);
            if (above == BLOCK_WATER || above == BLOCK_CALM_WATER ||
                above == BLOCK_LAVA  || above == BLOCK_CALM_LAVA) return false;

            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            sum += y; ++n;
        }
    }
    // Tolerance 4 rather than 3. baseY is the mean, so the worst cut or
    // fill against a spread of 4 is about two blocks -- still a village
    // sitting in the landscape rather than on a visible plinth -- and the
    // looser bound is a large part of what makes villages actually appear
    // on gently rolling plains instead of only on near-perfect flats.
    if (maxY - minY > 4) return false;
    baseY = sum / n;
    // Floor of 64 as a second line of defence behind the submerged test
    // above: sea level is 64 and the topmost water block is 63, so a
    // village floor at 64 is the lowest that is still dry. Anything under
    // that is a lake bed or a river bed that somehow passed the liquid
    // test, and is not somewhere to put houses.
    if (baseY < VILLAGE_MIN_BASE_Y || baseY > 110) return false;
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
                       int y, bool desert, bool doorSouth, Random& lootRng) {
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
    for (int z = z0; z < z0 + depth; ++z)
        for (int x = x0; x < x0 + width; ++x)
            put(w, x, y + 5, z, roof);

    // A torch inside each house makes the structure useful at night.
    //
    // Data 1 = mounted on the wall to the WEST, i.e. supportCanSurvive
    // tests the block at x-1 (see BLOCK_TORCH in tile_support.cpp). Placing
    // it at x0+1 puts that test on the x0 wall column, which is solid
    // cobble/sandstone at this height -- the y+2 window pane is a row
    // lower, so it is never what the torch leans on.
    //
    // The previous version passed data 0 and hung the torch in mid-air at
    // the centre of the room. Data 0 matches no case in the torch's
    // support switch, so it falls through to `return false` and the torch
    // deletes itself on the first block update that reaches it -- a
    // village that is lit when you generate it and dark by the time you
    // walk back to it.
    put(w, x0 + 1, y + 3, z0 + depth / 2, BLOCK_TORCH, 1);

    // Chest in the corner opposite the torch (x0+1), so the two interior
    // fixtures don't compete for the same floor tile. width/depth are at
    // least 5, so x0+width-2 is always a genuine interior column, not the
    // wall itself.
    int cx = x0 + width - 2, cz = z0 + depth - 2;
    put(w, cx, y + 1, cz, BLOCK_CHEST, 4); // data 4: no neighbour, unpaired
    lootFillChest(cx, y + 1, cz, LOOT_TABLE_VILLAGE, lootRng);
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
    // Roughly one candidate chunk in 32, but only a fraction of those
    // survive the biome and flatness gates below -- expect on the order of
    // two or three villages on the 512 preset (1024 overworld chunks) and
    // around ten on 1024 (4096 chunks).
    //
    // This used to be 96, which is where the "I have never seen a village"
    // reports came from: 1024/96 is about ten candidate chunks per 512
    // world, the plains-or-desert gate keeps roughly one in six of those,
    // and the flatness gate then rejects most of the survivors. Zero
    // villages was the common outcome, not the unlucky one. Three
    // multiplied probabilities need the first one to be generous.
    //
    // This is the density knob. Lower for more villages, higher for fewer;
    // nothing else needs to change alongside it.
    if ((h % 32u) != 0u) return;

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
    registerVillageChunk(chunkX, chunkZ);

    int ox = chunkX * 16, oz = chunkZ * 16;

    // Loot rolls are seeded once per village from the same hash used for
    // placement, not from a shared/global RNG. That keeps chest contents
    // deterministic per world+chunk like everything else in this
    // generator, and keeps them independent of unrelated generation order
    // elsewhere (chunk load order, other structures) so the same village
    // always has the same loot on replay.
    Random lootRng((long)(h ^ 0xC5EF1A3Du));

    // Three houses form a compact U around a central well/path junction.
    buildHouse(w, ox + 1,  oz + 1, 5, 6, baseY, desert, true,  lootRng);
    buildHouse(w, ox + 10, oz + 1, 5, 6, baseY, desert, true,  lootRng);
    buildHouse(w, ox + 5,  oz + 10, 5, 5, baseY, desert, false, lootRng);

    // Two-block-wide paths, laid BEFORE the well.
    //
    // Order matters here and previously did not hold: the well was built
    // first, and the two path runs below both sweep straight through its
    // 3x3 footprint at ox+7..ox+9 / oz+7..oz+9. Between them they
    // overwrote eight of the well's nine top blocks, the centre water
    // included, so what actually generated was a fenced patch of paving
    // with a plank floating over it. Building the well afterwards lets it
    // stamp itself back over the junction, which is also what you want
    // visually -- the paths run up to the well and stop.
    //
    // Plains paving is cobblestone rather than gravel. heightmapAt
    // (features_common.cpp) counts gravel and sandstone as ground but not
    // cobble, and treeBasic does no ground-material check at all before
    // forcing the block beneath a sapling to dirt -- so a neighbouring
    // forest chunk's tree pass, which reaches 8..23 blocks into this
    // chunk, could plant a tree in the middle of a gravel path and punch a
    // dirt hole through it. Cobble makes heightmapAt fall through to the
    // dirt at baseY-1, and the clearance test then fails against the
    // paving itself, so the tree is simply not placed. If you prefer the
    // gravel look this is a one-word revert; the desert path is still
    // sandstone and does still carry the original risk, which in practice
    // needs a desert chunk bordering a forested one.
    unsigned char path = desert ? BLOCK_SANDSTONE : BLOCK_COBBLESTONE;
    for (int x = 2; x <= 13; ++x) {
        put(w, ox + x, baseY, oz + 7, path);
        put(w, ox + x, baseY, oz + 8, path);
    }
    for (int z = 6; z <= 13; ++z) {
        put(w, ox + 7, baseY, oz + z, path);
        put(w, ox + 8, baseY, oz + z, path);
    }

    // Well at ox+6/oz+6, NOT ox+7/oz+7.
    //
    // At ox+7 the well spans x ox+7..ox+9 and z oz+7..oz+9, and house 3's
    // door is at (ox+7, oz+10) facing north -- so the square you step into
    // on leaving the house, (ox+7, oz+9), was the well's south-west rim,
    // with a fence post on it at baseY+1. The door was walled shut by a
    // fence. Shifting the well one block north-west leaves that square as
    // open path and still keeps the well centred on the junction, since it
    // covers x ox+7..ox+8 and z oz+7..oz+8 either way.
    //
    // If you move any house or change a door side, re-check this: the
    // three doors exit onto (ox+3, oz+7), (ox+12, oz+7) and (ox+7, oz+9),
    // and all three need to be path or grass rather than well.
    buildWell(w, ox + 6, oz + 6, baseY);

    // Small shared crop plot beside the southern house. The water strip is
    // central so every crop row is adjacent to irrigation.
    buildFarm(w, ox + 1, oz + 9, baseY, desert);
}
