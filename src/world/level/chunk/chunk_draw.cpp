#include "world/level/chunk/chunk.h"
#include "util/prof.h"
#include "world/level/chunk/mesh_sink.h"
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include <malloc.h>
#include <pspkernel.h>
#include <pspgum.h>

void chunkPackInto(DrawVertex* d, const ChunkVertex* s, int n,
                   int ox, int oy, int oz, int* qlo, int* qhi) {
    profAdd(PROFC_PACKVERTS, n);
    profBegin(PROF_MCONV);
    int lo = *qlo, hi = *qhi;
    for (int i = 0; i < n; i++) {
        d[i].u = uvQ(s[i].u); d[i].v = uvQ(s[i].v); d[i].color = s[i].color;
        d[i].x = posQ(s[i].x - ox); d[i].y = posQ(s[i].y - oy); d[i].z = posQ(s[i].z - oz); d[i].w = 0;
        if (d[i].y < lo) lo = d[i].y;
        if (d[i].y > hi) hi = d[i].y;
    }
    *qlo = lo; *qhi = hi;
    profEnd(PROF_MCONV);
}

float chunkPackDecodeY(int q, int oy) { return (float)q / (float)POS_ENC + oy; }

DrawVertex* chunkPackFinish(const DrawVertex* staging, int n) {
    profBegin(PROF_MALLOC);
    DrawVertex* d = (DrawVertex*)memalign(16, (size_t)n * sizeof(DrawVertex));
    profEnd(PROF_MALLOC);
    if (!d) return 0;
    memcpy(d, staging, (size_t)n * sizeof(DrawVertex));
    sceKernelDcacheWritebackInvalidateRange(d, (size_t)n * sizeof(DrawVertex));
    return d;
}

DrawVertex* chunkPack(const ChunkVertex* s, int n, int ox, int oy, int oz,
                      float* ylo, float* yhi) {
    profBegin(PROF_MALLOC);
    DrawVertex* d = (DrawVertex*)memalign(16, (size_t)n * sizeof(DrawVertex));
    profEnd(PROF_MALLOC);
    if (!d) return 0;
    int qlo = 32767, qhi = -32768;
    chunkPackInto(d, s, n, ox, oy, oz, &qlo, &qhi);
    if (ylo) *ylo = chunkPackDecodeY(qlo, oy);
    if (yhi) *yhi = chunkPackDecodeY(qhi, oy);
    sceKernelDcacheWritebackInvalidateRange(d, (size_t)n * sizeof(DrawVertex));
    return d;
}

float g_relBaseX = 0.0f, g_relBaseY = 0.0f, g_relBaseZ = 0.0f;

#define SEAM_OVERSCALE_OPAQUE (32768.0f / 32753.0f)
#define SEAM_OVERSCALE_TRANS  (32768.0f / 32763.0f)

static inline void chunkSetModel(const ChunkSection* s, float scaleMul) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 t = { (float)s->ox - g_relBaseX, (float)s->oy - g_relBaseY, (float)s->oz - g_relBaseZ };
    sceGumTranslate(&t);
    float sm = POS_MODEL_SCALE * scaleMul;
    ScePspFVector3 sc = { sm, sm, sm };
    sceGumScale(&sc);
}

// PPSSPP's software transform path decodes each sceGumDrawArray call into a
// fixed-size buffer (VERTEX_BUFFER_MAX = 65536 verts, see PPSSPP's
// GPU/Common/DrawEngineCommon.h) -- exceeding that in one call is what
// surfaces as a "vertex cache buffer overflow". On real hardware, the GE's
// own vertex cache is documented safe only for single-digit-thousands of
// verts per draw call, well below PPSSPP's limit -- so the real-hardware
// number is the one that matters here, not PPSSPP's.
//
// Nothing upstream caps a section's vertexCount below SCRATCH_VERTS
// (24576/65536 verts), and a section packed with lots of fence/stair/slab
// partial-geometry can legitimately reach into the low thousands, so a
// single draw call per section isn't safe to assume bounded. Split into
// batches well under the hardware-safe ceiling instead.
//
// Configurable rather than hardcoded so it can be tuned per device/firmware
// without touching this file, or lowered further if a specific unit still
// overflows at the default. This is a hardware vertex-cache-capacity limit,
// not something that should vary by biome or block palette -- what drives
// batch count is a section's vertex density (how many partial-geometry
// blocks like fences/stairs/slabs it packs), which batching already handles
// generically regardless of what generated that density.
extern int g_guDrawBatchVerts;
static int guDrawBatchVerts() { return g_guDrawBatchVerts; }

static inline void chunkDrawBatched(const unsigned int fmt, const DrawVertex* mesh, int count) {
    // Must stay a multiple of 6 (2 triangles per quad, see
    // writeQuadDouble/emit* -- all geometry here is built as whole
    // triangles, never split mid-triangle) so a batch boundary never lands
    // inside a triangle. g_guDrawBatchVerts is expected to already be a
    // multiple of 6; round down defensively in case it's ever set to
    // something that isn't.
    int batch = guDrawBatchVerts();
    if (batch < 6) batch = 6;
    batch -= batch % 6;

    int off = 0;
    while (off < count) {
        int n = count - off;
        if (n > batch) n = batch;
        sceGumDrawArray(GU_TRIANGLES, fmt, n, 0, mesh + off);
        off += n;
    }
}

void chunkDrawSection(const ChunkSection* s) {
    if (s->vertexCount <= 0 || !s->mesh) return;
    chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
    const unsigned int fmt = GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D;
    chunkDrawBatched(fmt, s->mesh, s->vertexCount);
}

void chunkDrawWaterSection(const ChunkSection* s) {
    if (s->waterCount > 0 && s->water) {
        chunkSetModel(s, SEAM_OVERSCALE_TRANS);
        chunkDrawBatched(GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D,
                         s->water, s->waterCount);
    }
}

void chunkDrawLeavesSection(const ChunkSection* s) {
    if (s->leavesCount > 0 && s->leaves) {
        chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
        chunkDrawBatched(GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D,
                         s->leaves, s->leavesCount);
    }
}

void chunkDrawNoMipSection(const ChunkSection* s, int part) {
    if (s->noMipCount <= 0 || !s->noMip) return;

    int first = 0, count = s->noMipCount;
    if (part == NOMIP_NO_LAVA) count = s->noMipLavaStart;
    else if (part == NOMIP_LAVA) { first = s->noMipLavaStart; count = s->noMipCount - first; }
    if (count <= 0) return;
    chunkSetModel(s, SEAM_OVERSCALE_OPAQUE);
    chunkDrawBatched(GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D,
                     s->noMip + first, count);
}

void chunkFreeMesh(ChunkMesh* c) {
    for (int si = 0; si < N_SECTIONS; si++) {
        ChunkSection* s = &c->sec[si];
        if (s->mesh)   { free(s->mesh);   s->mesh = 0; }
        if (s->water)  { free(s->water);  s->water = 0; }
        if (s->leaves) { free(s->leaves); s->leaves = 0; }
        if (s->noMip)  { free(s->noMip);  s->noMip = 0; }
        s->vertexCount = s->waterCount = s->leavesCount = s->noMipCount = 0;
        s->noMipLavaStart = 0;
    }
}
