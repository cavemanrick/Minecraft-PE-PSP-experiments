#include "world/level/world.h"
#include "util/prof.h"
#include "world/level/chunk/chunk.h"
#include <pspkernel.h>

#define PLAYER_EDIT_QUEUE_CAP 128
static int g_editQueue[PLAYER_EDIT_QUEUE_CAP][2];
static int g_editQueueN = 0;

static bool g_inEditQueue[WORLD_CHUNKS_X * WORLD_CHUNKS_Z][N_SECTIONS];

// Deferred dirty marks: a section whose chunk was not yet settled
// (worldChunkSettled false -- either the slot doesn't exist yet or the
// chunk is still generating) when markSecDirty was called for it.
//
// This exists because world generation's decoration phase deliberately
// writes across chunk boundaries -- postProcessPhase in mcpegen.cpp is
// called once per chunk in a 2x2 group (offsets {0,0},{-1,0},{0,-1},
// {-1,-1}) specifically so trees, rivers, and other features can straddle
// chunk edges. Every setBlock call along the way routes through
// worldMarkDirty -> markSecDirty, which used to just silently drop the
// mark whenever the write's target chunk wasn't settled at that exact
// moment. In the normal case that's fine (the target chunk isn't done
// generating yet, so it'll get a correct first mesh once it does finish)
// -- but the SAME reasoning is wrong when the write lands in a chunk
// that's a neighbour being reached into rather than the chunk currently
// finishing its own generation: that neighbour might not become settled
// until later (or might already be an existing chunk that just hasn't
// reached ST_DONE this tick), and once it does settle, nothing was ever
// recorded to say "you were written to while unsettled -- remesh
// yourself". The result: a tree canopy, warped-fungus branch, or river
// bank that reaches into a chunk whose generation timing didn't line up
// gets its blocks placed correctly in world data, but the neighbouring
// section's mesh never learns about them and keeps rendering whatever it
// looked like before the write -- missing faces, or faces that should
// have been culled by the new neighbour and weren't. This is exactly the
// "warped trees/lava-shore blocks occasionally missing faces" bug: both
// features write across chunk boundaries during generation, and the
// timing of which chunk in the 2x2 group happens to be settled first is
// not deterministic.
//
// Fix: instead of dropping the mark, record it here and flush it the
// next time worldChunkSettled would return true for that chunk (checked
// from markSecDirty itself, and also from a settle-time flush hook so a
// chunk doesn't need a second unrelated write to discover its pending
// marks).
//
// cx/cz are stored as plain int, not packed: world.h's worldSlotIndex
// masks cx/cz with a bitmask (cx & w->slotMask) rather than assuming a
// small 0..WORLD_CHUNKS_X range, meaning this is a streaming/ring-buffer
// chunk cache where coordinates can be arbitrary (including negative)
// signed values, not indices already normalized to a small grid. An
// earlier version of this fix packed cx/cz into unsigned char fields on
// the assumption WORLD_CHUNKS_X/Z (16) bounded them directly -- that
// would have silently aliased any real coordinate outside 0..255,
// corrupting which chunk a deferred mark actually targets. Plain int
// costs a few more bytes per entry, which is a non-issue at this cap.
#define PENDING_DIRTY_CAP 256
struct PendingDirty { int cx, cz; unsigned char si; };
static PendingDirty g_pendingDirty[PENDING_DIRTY_CAP];
static int g_pendingDirtyN = 0;

static void queuePendingDirty(int cx, int cz, int si) {
    for (int i = 0; i < g_pendingDirtyN; i++)
        if (g_pendingDirty[i].cx == cx && g_pendingDirty[i].cz == cz &&
            g_pendingDirty[i].si == (unsigned char)si)
            return; // already queued, no need to duplicate
    if (g_pendingDirtyN >= PENDING_DIRTY_CAP) {
        // Cap hit: worldFlushPendingDirty below is called every settle,
        // so entries drain continuously and this should stay well clear
        // of the cap in practice. If it ever fills, the oldest entry is
        // dropped rather than growing unbounded -- a lost cross-boundary
        // face is a much smaller problem than an unbounded array on a
        // fixed-memory platform.
        for (int i = 1; i < g_pendingDirtyN; i++) g_pendingDirty[i-1] = g_pendingDirty[i];
        g_pendingDirtyN--;
    }
    g_pendingDirty[g_pendingDirtyN].cx = cx;
    g_pendingDirty[g_pendingDirtyN].cz = cz;
    g_pendingDirty[g_pendingDirtyN].si = (unsigned char)si;
    g_pendingDirtyN++;
}

static void markSecDirtyBySi(World* w, int cx, int cz, int si) {
    ChunkSection* csec = &worldMesh(w, cx, cz)->sec[si];
    if (!csec->dirty) profAdd(PROFC_MARKED, 1);
    csec->dirty = true;

    if (!w->lightReady) return;

    worldSlot(w, cx, cz)->unsaved = true;

    if (w->simTick) return;

    int ci = worldSlotIndex(w, cx, cz);
    if (g_inEditQueue[ci][si]) return;
    if (g_editQueueN >= PLAYER_EDIT_QUEUE_CAP) return;
    g_editQueue[g_editQueueN][0] = ci; g_editQueue[g_editQueueN][1] = si; g_editQueueN++;
    g_inEditQueue[ci][si] = true;
}

static inline void markSecDirty(World* w, int cx, int cz, int y) {

    if (y < 0 || y >= WORLD_H) return;
    int si = y / SECTION_SY;

    if (!worldChunkSettled(w, cx, cz)) {
        queuePendingDirty(cx, cz, si);
        return;
    }

    markSecDirtyBySi(w, cx, cz, si);
}

// Called once a chunk transitions to settled (see finishBegin in
// chunk_cache.cpp) so any dirty marks deferred while it was unsettled --
// see the long comment above PendingDirty -- actually get applied instead
// of staying lost forever. Cheap in the overwhelmingly common case (no
// pending entries at all, or none matching this chunk): the list only
// grows during the brief window where cross-boundary generation writes
// outrun chunk settling, and PENDING_DIRTY_CAP keeps a worst case bounded
// regardless.
void worldFlushPendingDirty(World* w, int cx, int cz) {
    if (g_pendingDirtyN == 0) return;
    if (!worldChunkSettled(w, cx, cz)) return; // caller's claim was wrong; nothing to flush yet

    int i = 0;
    while (i < g_pendingDirtyN) {
        if (g_pendingDirty[i].cx != cx || g_pendingDirty[i].cz != cz) { i++; continue; }
        int si = g_pendingDirty[i].si;

        // Remove this entry first (swap-with-last). markSecDirtyBySi
        // never re-queues (it's the settled-chunk path directly, not
        // markSecDirty), so there's no risk of this loop processing an
        // entry it just re-added, but removing up front keeps that
        // invariant obviously true from reading this function alone.
        g_pendingDirty[i] = g_pendingDirty[g_pendingDirtyN - 1];
        g_pendingDirtyN--;

        markSecDirtyBySi(w, cx, cz, si);
    }
}

bool g_smoothLighting = true;

void worldMarkAllDirty(World* w) {
    for (int ci = 0; ci < WORLD_CHUNKS_X * WORLD_CHUNKS_Z; ci++)
        for (int si = 0; si < N_SECTIONS; si++)
            w->chunks[ci].sec[si].dirty = true;
}

void worldMarkDirty(World* w, int x, int y, int z) {

    int cx[2], cz[2], sy[2], ncx = 1, ncz = 1, nsy = 1;
    cx[0] = (x - 1) >> 4;  if (((x + 1) >> 4) != cx[0]) cx[ncx++] = (x + 1) >> 4;
    cz[0] = (z - 1) >> 4;  if (((z + 1) >> 4) != cz[0]) cz[ncz++] = (z + 1) >> 4;
    int ylo = (y > 0) ? y - 1 : 0;
    int yhi = (y < WORLD_H - 1) ? y + 1 : WORLD_H - 1;
    sy[0] = ylo;           if ((yhi >> 4) != (ylo >> 4)) sy[nsy++] = yhi;
    for (int a = 0; a < ncx; a++)
        for (int b = 0; b < ncz; b++)
            for (int c = 0; c < nsy; c++)
                markSecDirty(w, cx[a], cz[b], sy[c]);
}

void worldSetData(World* w, int x, int y, int z, unsigned char data) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return;
    worldDataPut(w, worldIndex(w, x, y, z), data);
    worldMarkDirty(w, x, y, z);
}

void worldSetDataNoUpdate(World* w, int x, int y, int z, unsigned char data) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return;
    worldDataPut(w, worldIndex(w, x, y, z), data);
}

bool worldSetBlockAndData(World* w, int x, int y, int z, unsigned char id, unsigned char data) {
    if (y < 0 || y >= WORLD_H || !worldReady(w, x, z)) return false;
    unsigned char was = worldBlock(w, x, y, z);

    if (!blockPut(w, x, y, z, id)) return false;
    worldDataPut(w, worldIndex(w, x, y, z), data);
    worldMarkDirty(w, x, y, z);
    if (w->lightReady) {
        lightOnBlockChanged(w, x, y, z);

        if (lightEmit(was) > 0 && lightEmit(id) == 0) worldRemoveBlockLight(w, x, y, z);
    }
    return true;
}

static void editQueuePromote(int ci, int si) {
    if (!g_inEditQueue[ci][si]) return;
    for (int i = 0; i < g_editQueueN; i++) {
        if (g_editQueue[i][0] != ci || g_editQueue[i][1] != si) continue;
        for (int j = i; j > 0; j--) {
            g_editQueue[j][0] = g_editQueue[j-1][0];
            g_editQueue[j][1] = g_editQueue[j-1][1];
        }
        g_editQueue[0][0] = ci; g_editQueue[0][1] = si;
        return;
    }
}

void worldRebuildAroundNow(World* w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_H) return;

    for (int dz = 1; dz >= -1; dz--)
    for (int dx = 1; dx >= -1; dx--)
    for (int dy = 1; dy >= -1; dy--) {
        int nx = x + dx, ny = y + dy, nz = z + dz;
        if (ny < 0 || ny >= WORLD_H) continue;
        int cx = nx >> 4, cz = nz >> 4;
        if (!worldChunkSettled(w, cx, cz)) continue;
        editQueuePromote(worldSlotIndex(w, cx, cz), ny / SECTION_SY);
    }
}

int worldEditQueueDepth() { return g_editQueueN; }
int worldEditQueueFront(int field) { return g_editQueueN ? g_editQueue[0][field] : -1; }

void worldDrainPlayerEdits(World* w, int maxSections) {

    static const unsigned int TIME_BUDGET_US = 1000;
    unsigned int tStart = sceKernelGetSystemTimeLow();
    int n = g_editQueueN < maxSections ? g_editQueueN : maxSections;
    for (int i = 0; i < n; i++) {
        int ci = g_editQueue[0][0], si = g_editQueue[0][1];
        for (int j = 1; j < g_editQueueN; j++) { g_editQueue[j-1][0] = g_editQueue[j][0]; g_editQueue[j-1][1] = g_editQueue[j][1]; }
        g_editQueueN--;
        g_inEditQueue[ci][si] = false;
        ChunkMesh* c = &w->chunks[ci];
        if (c->sec[si].dirty) chunkBuildSection(c, w, si);
        if (sceKernelGetSystemTimeLow() - tStart >= TIME_BUDGET_US) break;
    }
}

void worldScheduleTick(World* w, int x, int y, int z, unsigned char id, int tickDelay) {

    if (y < 0 || y >= WORLD_H || !worldChunkSettled(w, x >> 4, z >> 4)) return;

    unsigned int key = (unsigned int)worldIndex(w, x, y, z);
    if (!w->tickSet.insert(key).second) return;
    TickNextTickData td = {x, y, z, id, w->time + tickDelay};
    w->tickNextTickList.push_back(td);
}
