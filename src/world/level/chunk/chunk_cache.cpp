#include "world/level/chunk/chunk_cache.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/level/levelgen/mcpegen.h"
#include "world/level/levelgen/nether_gen.h"
#include "world/level/storage/chunk_storage.h"
#include "world/level/levelgen/village_gen.h"
#include "world/level/level.h"

#include <string.h>
#include <math.h>
#include <pspkernel.h>
#include "util/prof.h"

unsigned int g_streamIn = 0, g_streamOut = 0;

struct GenScope {
    World* w; bool saved;
    GenScope(World* world) : w(world), saved(world->lightReady) { w->lightReady = false; }
    ~GenScope() { w->lightReady = saved; }
};

enum { ST_DONE = 0, ST_DECOR0 = 1, ST_LIGHT = 5, ST_MISC = 6 };

static void evict(World* w, int slotIdx) {
    LevelChunk* c = &w->slots[slotIdx];
    if (!c->resident) return;
    profBegin(PROF_SEVICT);
    if (c->unsaved) chunkStorageSave(w, c->x, c->z);
    villageChunkUnloaded(&g_level, c->x, c->z);
    c->resident = false;
    blockSlotRecycle(w, slotIdx);
    lightSlotRelease(w, slotIdx);

    for (int i = 0; i < 256; i++) {
        unsigned char** pg = &w->dataCol[(slotIdx << 8) | i];
        if (*pg) { free(*pg); *pg = 0; w->dataPages--; }
    }
    memset(&w->heightmap[slotIdx << 8], 0, 256);

    chunkFreeMesh(&w->chunks[slotIdx]);
    g_streamOut++;
    profEnd(PROF_SEVICT);
}

static bool postProcessPhase(World* w, int cx, int cz, int phase) {
    LevelChunk* c = worldSlot(w, cx, cz);
    if (!c->isAt(cx, cz)) return true;

    if (phase > 0) return chunkPostProcessPhase(w, cx, cz, phase);
    if (c->terrainPopulated) return true;

    if (!worldNeighbourSettled(w, cx + 1, cz) || !worldNeighbourSettled(w, cx, cz + 1) ||
        !worldNeighbourSettled(w, cx + 1, cz + 1)) return true;
    c->terrainPopulated = true;

    for (int dz = 0; dz <= 1; dz++)
        for (int dx = 0; dx <= 1; dx++)
            if (worldChunkReady(w, cx + dx, cz + dz))
                worldSlot(w, cx + dx, cz + dz)->unsaved = true;
    return chunkPostProcessPhase(w, cx, cz, 0);
}

static void claim(World* w, int cx, int cz) {
    int slot = worldSlotIndex(w, cx, cz);
    evict(w, slot);

    lightSlotRelease(w, slot);
    LevelChunk* c = &w->slots[slot];
    c->x = cx; c->z = cz;
    c->unsaved = false;
    c->terrainPopulated = false;
    c->stage = ST_DONE;

    c->resident = true;
    chunkInitLazy(&w->chunks[slot], cx * 16, cz * 16);
}

void worldClaimChunkPrebuilt(World* w, int cx, int cz) {
    // Marks a chunk resident/populated without running chunkGenerateTerrain
    // -- for level sources (Flat, Debug) that write real block data
    // directly across the whole resident window in one pass (blockColumnPut
    // in level_source.cpp) rather than going through the normal per-chunk
    // worldGetChunk pipeline. Without this, that data is real and correctly
    // written, but every reader that gates on chunk-claim state --
    // worldBlock itself (returns BLOCK_INVISIBLE_BEDROCK for an unclaimed
    // chunk regardless of what's actually stored there), isValidSpawn,
    // collision, meshing -- treats it as if it doesn't exist, which is
    // exactly what "flat world, player falls into nothing" looks like: a
    // correctly-built floor nothing is allowed to see.
    claim(w, cx, cz);
    worldSlot(w, cx, cz)->terrainPopulated = true;
}

static bool s_pend = false;
static int  s_pendX = 0, s_pendZ = 0;

static int  s_decorPhase = 0;

static void finishBegin(World* w, int cx, int cz) {
    LevelChunk* c = worldSlot(w, cx, cz);

    c->generating = false;
    c->stage = ST_DECOR0;
    s_decorPhase = 0;
    s_pend = true; s_pendX = cx; s_pendZ = cz;
}

bool worldStreamBusy() { return s_pend; }

static bool finishStep(World* w) {
    if (!s_pend) return false;
    const int cx = s_pendX, cz = s_pendZ;
    LevelChunk* c = worldSlot(w, cx, cz);

    if (!c->isAt(cx, cz)) { c->generating = false; c->stage = ST_DONE; s_pend = false; return false; }

    GenScope gen(w);
    switch (c->stage) {

    case 1: case 2: case 3: case 4: {
        static const int kDx[4] = { 0, -1,  0, -1 };
        static const int kDz[4] = { 0,  0, -1, -1 };
        int i = c->stage - ST_DECOR0;
        profBegin(PROF_SDECOR);

        const unsigned int DECOR_BUDGET_US = 2000;
        unsigned int t0 = sceKernelGetSystemTimeLow();
        bool decorDone;
        do {
            decorDone = postProcessPhase(w, cx + kDx[i], cz + kDz[i], s_decorPhase);
            s_decorPhase++;
        } while (!decorDone &&
                 (unsigned int)(sceKernelGetSystemTimeLow() - t0) < DECOR_BUDGET_US);
        profEnd(PROF_SDECOR);
        if (!decorDone) return true;
        s_decorPhase = 0;
        break;
    }

    case ST_LIGHT:
        profBegin(PROF_SLIGHT);
        worldInitChunkLight(w, cx, cz);
        profEnd(PROF_SLIGHT);
        break;
    case ST_MISC:
    default:
        profBegin(PROF_SMISC);

        worldPlaceMushrooms(w);
        worldPlaceFlowers(w);
        worldScheduleChunkLiquids(w, cx, cz);
        profEnd(PROF_SMISC);
        c->stage = ST_DONE;
        s_pend = false;
        g_streamIn++;
        return true;
    }
    c->stage++;
    return true;
}

void worldGetChunk(World* w, int cx, int cz) {

    if (!worldChunkInBounds(w, cx, cz)) return;
    if (worldChunkReady(w, cx, cz)) return;
    claim(w, cx, cz);
    {
        GenScope gen(w);
        bool gotLight = false, populated = true;
        profBegin(PROF_SDISK);
        bool fromDisk = chunkStorageLoad(w, cx, cz, &gotLight, &populated);
        profEnd(PROF_SDISK);
        if (fromDisk) {

            worldSlot(w, cx, cz)->terrainPopulated = populated;
        } else if (worldChunkIsReserved(w, cx, cz)) {
            // Reserved Nether/End space (1024 preset only). The Nether
            // strip (WORLD_NETHER_ORIGIN_CX/CZ..+WORLD_NETHER_CHUNKS) now
            // gets real terrain via chunkGenerateNether; the End strip
            // past it still has no generator, so it's left as claimed-but-
            // empty air exactly as before. Without the worldChunkIsReserved
            // guard here, ANY caller reaching a reserved coordinate -- not
            // just the pre-gen sweep, which already avoids this range
            // structurally -- would fall through to chunkGenerateTerrain
            // below and silently pollute reserved space with overworld
            // terrain, so the guard stays even though the Nether half of
            // it now does real work instead of a no-op.
            profBegin(PROF_SGEN);
            if (worldChunkIsNether(w, cx, cz)) chunkGenerateNether(w, worldGenSeed(), cx, cz);
            profEnd(PROF_SGEN);
            worldSlot(w, cx, cz)->terrainPopulated = true;
        } else {
            profBegin(PROF_SGEN);
            chunkGenerateTerrain(w, cx, cz);
            profEnd(PROF_SGEN);

        }
    }

    finishBegin(w, cx, cz);
    while (finishStep(w)) {}
}

void worldEnsureArea(World* w, int cx, int cz, int r) {
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++)
            worldGetChunk(w, cx + dx, cz + dz);
}

static volatile bool g_jobPending = false, g_jobDone = false, g_workerQuit = false;
static volatile int  g_jobX = 0, g_jobZ = 0;
static int s_workerThid = -1;

static World* volatile s_genWorld = 0;

static int genWorker(SceSize, void*) {
    while (!g_workerQuit) {
        if (!g_jobPending) { sceKernelDelayThread(2000); continue; }
        // Same reserved-region guard as worldGetChunk -- this background
        // worker is the async path worldStream uses for live gameplay
        // streaming, a genuinely different call site that would otherwise
        // bypass the guard there entirely. Mirrors worldGetChunk's own
        // Nether/End split: real generation for the Nether strip, still a
        // no-op for the End strip past it.
        if (worldChunkIsReserved(s_genWorld, g_jobX, g_jobZ)) {
            if (worldChunkIsNether(s_genWorld, g_jobX, g_jobZ))
                chunkGenerateNether(s_genWorld, worldGenSeed(), g_jobX, g_jobZ);
        } else {
            chunkGenerateTerrain(s_genWorld, g_jobX, g_jobZ);
        }
        g_jobPending = false;
        g_jobDone = true;
    }
    return 0;
}

void worldGenWorkerStart(World* w) {
    if (s_workerThid >= 0) return;

    if (worldFitsInWindow(w)) return;
    g_workerQuit = false; g_jobPending = false; g_jobDone = false; s_pend = false;

    s_workerThid = sceKernelCreateThread("chunk_gen", genWorker, 0x24, 0x10000, 0, 0);
    if (s_workerThid >= 0) sceKernelStartThread(s_workerThid, 0, 0);
}

void worldGenWorkerStop() {
    if (s_workerThid < 0) return;
    g_workerQuit = true;

    for (int i = 0; i < 2000 && g_jobPending; i++) sceKernelDelayThread(1000);
    sceKernelTerminateDeleteThread(s_workerThid);
    s_workerThid = -1;

    g_jobPending = false; g_jobDone = false; s_pend = false;
}

static int loadRadius(const World* w) {
    extern float g_viewDistEff;
    float d = (g_viewDistEff > 0.0f) ? g_viewDistEff : WORLD_VIEW_DIST;
    int r = (int)(d / 16.0f) + 2;
    int cap = w->slotN / 2;
    return r > cap ? cap : r;
}

int worldStream(World* w, float px, float pz, int budgetMs) {

    if (worldFitsInWindow(w)) return 0;
    const int pcx = (int)floorf(px) >> 4, pcz = (int)floorf(pz) >> 4;
    const int R = loadRadius(w);
    const int E = R + 1;
    const unsigned int tStart = sceKernelGetSystemTimeLow();
    int brought = 0;

    const unsigned int EVICT_BUDGET_US = 2000;
    for (int i = 0; i < w->slotN * w->slotN; i++) {
        LevelChunk* c = &w->slots[i];
        if (!c->resident) continue;
        if (c->x >= pcx - E && c->x <= pcx + E && c->z >= pcz - E && c->z <= pcz + E) continue;
        if (worldSlotBusy(c)) continue;
        evict(w, i);
        if ((unsigned int)(sceKernelGetSystemTimeLow() - tStart) > EVICT_BUDGET_US) break;
    }

    if (g_jobDone && !s_pend) {
        g_jobDone = false;
        LevelChunk* c = worldSlot(w, g_jobX, g_jobZ);

        if (c->x == g_jobX && c->z == g_jobZ && c->resident) {
            // Mirrors the worker thread's own reserved-region skip (see
            // genWorker) -- set here on the main thread rather than from
            // the worker itself, to avoid a cross-thread write to c.
            if (worldChunkIsReserved(w, g_jobX, g_jobZ)) c->terrainPopulated = true;
            finishBegin(w, g_jobX, g_jobZ);
        } else {
            c->generating = false;
        }
    }

    if (finishStep(w)) {
        if (!s_pend) brought++;
        return brought;
    }
    if (g_jobPending) return brought;

    {
        if ((unsigned int)(sceKernelGetSystemTimeLow() - tStart) > (unsigned int)budgetMs * 1000u)
            return brought;
        // Whether the player themselves is currently inside the reserved
        // strip -- computed once per call, outside the dx/dz loop, and
        // used below to decide whether reserved *candidate* chunks are
        // fair game for this pass. This used to be an unconditional skip
        // (see the "visible through the wall" fix a few sessions back),
        // which fixed the original bug -- standing near the boundary in
        // the Overworld pulled in and rendered Nether terrain nobody had
        // actually walked into yet -- but was too broad: it also blocked
        // the player's own surrounding chunks from loading once they
        // WERE standing in the Nether (via portal or debug teleport),
        // since pcx/pcz then sits inside the reserved strip too. That
        // left only the single chunk worldGetChunk explicitly touched
        // ever generated -- a small island of real terrain surrounded by
        // permanently-unloaded chunks, which reads as "sky in every
        // direction, fall to death one step off the platform".
        bool playerInReservedRegion = worldChunkIsReserved(w, pcx, pcz);

        int bestX = 0, bestZ = 0, bestD = 0x7FFFFFFF;
        for (int dz = -R; dz <= R; dz++)
            for (int dx = -R; dx <= R; dx++) {
                int cx = pcx + dx, cz = pcz + dz;
                if (!worldChunkInBounds(w, cx, cz)) continue;
                // Only exclude reserved candidates when the player is NOT
                // themselves already inside the reserved strip -- see the
                // comment above playerInReservedRegion. A player standing
                // in the Nether needs their own surrounding reserved
                // chunks to stream in normally, same as any other chunk;
                // it's only ambient bleed-in from the Overworld side that
                // this guard exists to prevent.
                if (!playerInReservedRegion && worldChunkIsReserved(w, cx, cz)) continue;
                if (worldChunkReady(w, cx, cz)) continue;
                int d = dx * dx + dz * dz;
                if (d < bestD) { bestD = d; bestX = cx; bestZ = cz; }
            }
        if (bestD == 0x7FFFFFFF) return brought;

        claim(w, bestX, bestZ);

        bool gotLight = false, populated = true;
        profBegin(PROF_SDISK);
        bool onDisk = chunkStorageLoad(w, bestX, bestZ, &gotLight, &populated);
        profEnd(PROF_SDISK);
        if (onDisk) {
            worldSlot(w, bestX, bestZ)->terrainPopulated = populated;
            finishBegin(w, bestX, bestZ);
            return brought;
        }
        if (s_workerThid < 0) {
            GenScope gen(w);
            profBegin(PROF_SGEN);
            if (worldChunkIsReserved(w, bestX, bestZ)) {
                // see worldGetChunk's identical Nether/End split
                if (worldChunkIsNether(w, bestX, bestZ))
                    chunkGenerateNether(w, worldGenSeed(), bestX, bestZ);
                worldSlot(w, bestX, bestZ)->terrainPopulated = true;
            } else {
                chunkGenerateTerrain(w, bestX, bestZ);
            }
            profEnd(PROF_SGEN);
            finishBegin(w, bestX, bestZ);
            return brought;
        }

        worldSlot(w, bestX, bestZ)->generating = true;
        s_genWorld = w;
        g_jobX = bestX; g_jobZ = bestZ;
        g_jobPending = true;
    }
    return brought;
}

void worldSaveResident(World* w) {
    for (int i = 0; i < w->slotN * w->slotN; i++) {
        LevelChunk* c = &w->slots[i];

        if (c->resident && c->unsaved && !worldSlotBusy(c)) chunkStorageSave(w, c->x, c->z);
    }
}

void worldPreGenerateSweep(World* w, int x0, int z0, int x1, int z1) {
    if (x1 <= x0 || z1 <= z0) return;

    const long long totalChunks = (long long)(x1 - x0) * (long long)(z1 - z0);
    long long doneChunks = 0;

    // Reverse row-major: highest z first, and within each row, highest x
    // first. See the header comment on worldPreGenerateSweep for why this
    // ordering is what makes immediate per-chunk eviction safe.
    for (int cz = z1 - 1; cz >= z0; cz--) {
        for (int cx = x1 - 1; cx >= x0; cx--) {

            // worldGetChunk is already the full claim -> generate ->
            // decorate -> finish pipeline in one synchronous call (see its
            // own definition above) -- exactly the unit of work this sweep
            // needs per chunk, so there's no separate generate-only step
            // to call first.
            worldGetChunk(w, cx, cz);

            // Safe to evict immediately: by construction (reverse order,
            // forward-only decoration spillover), nothing left in this
            // sweep can ever write back into this chunk.
            int slot = worldSlotIndex(w, cx, cz);
            if (w->slots[slot].isAt(cx, cz)) evict(w, slot);

            doneChunks++;
            g_terrainProgress = (int)((doneChunks * 100) / totalChunks);

            sceKernelDelayThread(100);
        }
    }
}
