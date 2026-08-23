
#include "world/level/world.h"
#include "world/level/storage/chunk_storage.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "world/level/levelgen/PerlinNoise.h"
#include "world/level/levelgen/mcpegen.h"
#include "world/level/levelgen/Random.h"

#include <stdlib.h>
#include <string.h>

#define WORLDGEN_PROFILE 0
#if WORLDGEN_PROFILE
#include <stdio.h>
#include <time.h>
#endif
#include <math.h>
#include <pspkernel.h>

static int g_meshBuildCursor = 0;
static long g_worldSeed = 0;

long worldGetSeed(void) { return g_worldSeed; }

int g_skyDarken = 0;

float worldTimeOfDay(long dayTime, float a) {
    int dayStep = (int)(dayTime % TICKS_PER_DAY);
    float td = (dayStep + a) / (float)TICKS_PER_DAY - 0.25f;
    if (td < 0.0f) td += 1.0f;
    if (td > 1.0f) td -= 1.0f;
    float tdo = td;
    td = 1.0f - (cosf(td * 3.14159265f) + 1.0f) * 0.5f;
    return tdo + (td - tdo) / 3.0f;
}

static int calcSkyDarken(long dayTime) {
    float td = worldTimeOfDay(dayTime, 1.0f);
    float br = 1.0f - (cosf(td * 2.0f * 3.14159265f) * 2.0f + 0.5f);
    if (br < 0.0f) br = 0.0f;
    if (br > 0.80f) br = 0.80f;
    return (int)(br * 11.0f);
}

static const long MIDDLE_OF_NIGHT_TIME = 12000;
static bool g_nightMode = false;
void worldSetNightMode(World* w, bool night) { (void)w; g_nightMode = night; }

bool worldNightModeTick(World* w) {
    if (!g_nightMode) return false;
    long curTime = w->dayTime;
    if (curTime % TICKS_PER_DAY != MIDDLE_OF_NIGHT_TIME) {
        if (curTime % TICKS_PER_DAY < MIDDLE_OF_NIGHT_TIME && (curTime + 20) % TICKS_PER_DAY > MIDDLE_OF_NIGHT_TIME)
            curTime = MIDDLE_OF_NIGHT_TIME;
        else
            curTime += 20;
        w->dayTime = curTime % TICKS_PER_DAY;
    }
    return true;
}

void worldUpdateSkyDarken(World* w) {
    int nd = calcSkyDarken(w->dayTime);
    if (nd == g_skyDarken) return;
    g_skyDarken = nd;
    for (int ci = 0; ci < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; ci++)
        for (int si = 0; si < N_SECTIONS; si++)
            if (w->chunks[ci].sec[si].skyLit)
                w->chunks[ci].sec[si].dirty = true;
}

volatile int g_terrainProgress = 0;
volatile bool g_terrainThreadDone = false;

bool worldAllocArrays(World* w) {

    worldSetWindow(w, WORLD_SLOT_BITS);
    memset(w->chunks, 0, sizeof(w->chunks));

    chunkStorageShutdown();

    memset(w->slots, 0, sizeof(w->slots));
    worldSlotsReset(w);

    blockAlloc(w);

    w->dataCol = (unsigned char**)calloc((size_t)WORLD_W * WORLD_D, sizeof(unsigned char*));
    w->dataPages = 0;

    bool lightOk = lightAlloc(w);
    w->heightmap = (unsigned char*)malloc((size_t)WORLD_W * WORLD_D);
    if (!w->dataCol || !lightOk || !w->heightmap) {

        worldFree(w);
        return false;
    }
    memset(w->heightmap, 0, (size_t)WORLD_W * WORLD_D);

    w->time = 0;
    w->dayTime = 0;
    g_skyDarken = 0;

    g_nightMode = false;
    w->tickNextTickList.clear();
    w->tickSet.clear();
    w->lightQueue.clear();
    lightQueuesReserve(w);
    w->preservedTileEntities.clear();
    w->lightReady = false;
    chunkInitBrightRamp();
    g_meshBuildCursor = 0;
    return true;
}

bool worldInitTerrain(World* w, long seed, int worldType, int sizeX, int sizeZ) {
    g_terrainProgress = 0;
    g_terrainThreadDone = false;
    w->sizeX = sizeX;
    w->sizeZ = sizeZ;
    if (!worldAllocArrays(w)) return false;

    g_worldSeed = seed;
#if WORLDGEN_PROFILE
    clock_t t0 = clock();
#endif

    levelSourceFor(worldType).buildTerrain(w, seed);

    // A finite world (sizeX != 0) too large to fit in the resident window
    // is one of the two pre-generated presets (512x512, 1024x1024, the
    // latter's logical bound wider still to fit reserved Nether/End space
    // -- see WORLD_PRESET_* in this header) -- RandomLevelSource's own
    // buildTerrain only handles the small-world case where the whole
    // world already fits resident (WORLD_CHUNKS_X-sized path in
    // worldGenerateMCPE) or the normal lazy-streaming case; neither of
    // those touches more than a small window's worth of chunks, which is
    // wrong here since a pre-generated world needs every chunk in its
    // full logical bound actually built before play begins, not just a
    // window around the origin. worldPreGenerateSweep does the real work:
    // generate, decorate, save, and evict every chunk across the full
    // bound, one at a time, so nothing stays resident that doesn't need
    // to. Infinite worlds (sizeX == 0) and worlds that DO fit the window
    // are untouched by this and keep their existing behavior exactly.
    //
    // Restricted to WORLD_TYPE_OLD specifically: Flat and Debug both
    // write their terrain directly as raw block columns across the whole
    // WORLD_W x WORLD_D grid in buildTerrain, never going through
    // worldGetChunk/chunk slots at all -- if the sweep ran for those
    // types it would find no chunk slot ever marked "ready" and call
    // chunkGenerateTerrain on every chunk, silently overwriting the flat
    // ground that was just correctly written with real procedural
    // terrain instead. Large flat/debug worlds aren't a real use case
    // today (nothing offers that combination), but this guard is here so
    // it can't silently corrupt one if that ever changes.
    //
    // The sweep only ever covers the OVERWORLD portion of sizeX/sizeZ,
    // never the full logical bound directly. For the 512 preset those are
    // the same thing (no reserved regions exist). For the 1024 preset
    // they're not: sizeX/sizeZ include the reserved Nether/End strips
    // beyond the overworld's own 1024x1024 extent (see WORLD_NETHER_*/
    // WORLD_END_* constants), and those strips must NOT run through
    // normal overworld terrain generation -- they're reserved for a
    // future, separate Nether/End generator that doesn't exist yet (same
    // "keep the coordinate layout correct now, build the actual generator
    // later" pattern used elsewhere in this project). Left untouched here,
    // those chunks simply stay whatever a freshly allocated World defaults
    // to (air) rather than either real overworld content or a
    // half-built stand-in.
    if (worldType == WORLD_TYPE_OLD && sizeX != 0 && !worldFitsInWindow(w)) {
        int overworldX = (sizeX == WORLD_PRESET_1024_TOTAL_X_CHUNKS) ? WORLD_PRESET_1024_CHUNKS : sizeX;
        int overworldZ = (sizeZ == WORLD_PRESET_1024_TOTAL_Z_CHUNKS && sizeX == WORLD_PRESET_1024_TOTAL_X_CHUNKS)
                        ? WORLD_PRESET_1024_CHUNKS : sizeZ;
        worldPreGenerateSweep(w, 0, 0, overworldX, overworldZ);
    }

    g_terrainProgress = 60;
    worldInitLight(w);
    g_terrainProgress = 90;

    w->lightReady = true;

    worldPlaceMushrooms(w);
    worldPlaceFlowers(w);

    g_terrainProgress = 100;
#if WORLDGEN_PROFILE
    printf("[WORLDGEN] noise gen: %d ms\n", (int)((clock() - t0) * 1000 / CLOCKS_PER_SEC));
#endif

    g_meshBuildCursor = 0;
    return true;
}

int worldBuildMeshesStep(World* w, int maxChunks) {

    extern float g_viewDist;
    const float maxD2 = g_viewDist * g_viewDist;
    const float px = g_level.player ? g_level.player->x : 0.0f;
    const float pz = g_level.player ? g_level.player->z : 0.0f;
    const int total = w->slotN * w->slotN;

    int budget = maxChunks;
    while (g_meshBuildCursor < total && budget-- > 0) {
        LevelChunk* lc = &w->slots[g_meshBuildCursor];
        ChunkMesh* c = &w->chunks[g_meshBuildCursor];
        g_meshBuildCursor++;
        if (!lc->resident) continue;
        int ox = lc->x * CHUNK_SX, oz = lc->z * CHUNK_SZ;
        float dx = (ox + CHUNK_SX * 0.5f) - px, dz = (oz + CHUNK_SZ * 0.5f) - pz;

        if (dx * dx + dz * dz <= maxD2) chunkBuildMesh(c, w, ox, oz);
        else chunkInitLazy(c, ox, oz);
    }
    return g_meshBuildCursor;
}

static unsigned char columnTop(World* w, int x, int z, int* outY) {
    for (int y = WORLD_H - 1; y >= 0; y--) {
        unsigned char id = worldBlock(w, x, y, z);
        if (id != BLOCK_AIR) { *outY = y; return id; }
    }
    *outY = 0; return BLOCK_AIR;
}

static bool isValidSpawn(World* w, int x, int z) {

    if (!worldChunkSettled(w, x >> 4, z >> 4)) return false;
    int ty; unsigned char top = columnTop(w, x, z, &ty);
    return isSolidPhys(top) && !isLeaf(top);
}

#define SPAWN_SEARCH_CHUNKS 2

static void clampToArea(int cx0, int cz0, int* x, int* z, int step) {
    const int lo = 4, hi = (SPAWN_SEARCH_CHUNKS * 2 + 1) * 16 - 4;
    int ox = (cx0 - SPAWN_SEARCH_CHUNKS) * 16, oz = (cz0 - SPAWN_SEARCH_CHUNKS) * 16;
    if (*x < ox + lo) *x += step;
    if (*x >= ox + hi) *x -= step;
    if (*z < oz + lo) *z += step;
    if (*z >= oz + hi) *z -= step;
}

static void spawnSearchArea(World* w, int x, int z) {
    worldEnsureArea(w, x >> 4, z >> 4, SPAWN_SEARCH_CHUNKS);
}

void worldValidateSpawn(World* w, int* x, int* y, int* z) {
    if (*y <= 0) *y = 64;

    spawnSearchArea(w, *x, *z);
    const int cx0 = *x >> 4, cz0 = *z >> 4;
    Random random(g_worldSeed);
    int xs = *x, zs = *z;
    int guard = 0;
    while (!isValidSpawn(w, xs, zs) && guard++ < 10000) {
        xs += random.nextInt(8) - random.nextInt(8);
        zs += random.nextInt(8) - random.nextInt(8);
        clampToArea(cx0, cz0, &xs, &zs, 8);
    }
    if (xs != *x || zs != *z) {
        int ty; columnTop(w, xs, zs, &ty);
        *y = ty;
    }
    *x = xs; *z = zs;
}

void worldFindSpawn(World* w, int* outX, int* outZ, int* outFeetY) {
    Random random(g_worldSeed);

    int wox, woz; worldWindowOrigin(w, &wox, &woz);
    int xSpawn = wox + w->slotN * 8, zSpawn = woz + w->slotN * 8;

    spawnSearchArea(w, xSpawn, zSpawn);
    const int cx0 = xSpawn >> 4, cz0 = zSpawn >> 4;

    int guard = 0;
    while (!isValidSpawn(w, xSpawn, zSpawn) && guard++ < 10000) {
        xSpawn += random.nextInt(32) - random.nextInt(32);
        zSpawn += random.nextInt(32) - random.nextInt(32);
        clampToArea(cx0, cz0, &xSpawn, &zSpawn, 32);
    }

    guard = 0;
    while (!isValidSpawn(w, xSpawn, zSpawn) && guard++ < 10000) {
        xSpawn += random.nextInt(8) - random.nextInt(8);
        zSpawn += random.nextInt(8) - random.nextInt(8);
        clampToArea(cx0, cz0, &xSpawn, &zSpawn, 8);
    }

    int ty; columnTop(w, xSpawn, zSpawn, &ty);

    if (!isValidSpawn(w, xSpawn, zSpawn)) ty = 63;
    *outX = xSpawn; *outZ = zSpawn; *outFeetY = ty + 1;
}

void worldFree(World* w) {

    chunkStorageShutdown();

    worldGenWorkerStop();
    worldGenFree();
    for (int i = 0; i < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; i++)
        chunkFreeMesh(&w->chunks[i]);
    blockFree(w);
    if (w->dataCol) {

        for (int i = 0; i < WORLD_W * WORLD_D; i++)
            if (w->dataCol[i]) free(w->dataCol[i]);
        free(w->dataCol); w->dataCol = 0; w->dataPages = 0;
    }
    lightFree(w);
    if (w->heightmap) { free(w->heightmap); w->heightmap = 0; }
    w->tickNextTickList.clear();
    w->tickSet.clear();
    w->lightQueue.clear();
}
