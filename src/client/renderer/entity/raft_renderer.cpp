
#include "client/renderer/entity/raft_renderer.h"
#include "world/entity/vehicle/raft.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "gpu/texture.h"
#include "util/mth.h"
#include <pspgu.h>
#include <pspgum.h>

extern World   g_world;
extern bool    g_haveTerrain;
extern Texture g_terrain;

static ChunkVertex s_mesh[36];
static int s_meshCount = 0;

void RaftRenderer::render(Entity* entity, float x, float y, float z, float rot, float) {
    if (!g_haveTerrain) return;

    // Built once -- BLOCK_BAMBOO_PLANKS' texture never changes and neither
    // does this shape, so there's nothing per-instance to rebuild the way
    // FallingTileRenderer rebuilds when the falling tile id/data changes.
    // Grid position (0,150,0) is arbitrary and unused for anything except
    // feeding emitPartialBox a real World* to satisfy its signature --
    // boundaryMask=0 and hiddenFaces=0 mean it never actually looks up a
    // neighbor there, same trick FallingTileRenderer uses for its own
    // out-of-the-way probe coordinate. The real position comes from the
    // sceGumTranslate below, using the entity's own interpolated x/y/z.
    //
    // Bounds are a thin platform, not a full block: 0.125-0.375 in Y
    // (a slim deck sitting low rather than a full-height crate), and
    // inset slightly on X/Z (0.05-0.95) so the footprint doesn't share an
    // edge exactly with the water/ground block boundary, which reads
    // better at this resolution than a mesh flush with the full 1x1 cell.
    if (s_meshCount == 0) {
        s_meshCount = emitPartialBox(&g_world, 0, 150, 0, BLOCK_BAMBOO_PLANKS, 0,
                                      0.05f, 0.125f, 0.05f, 0.95f, 0.375f, 0.95f,
                                      0, 0, s_mesh, 0);
    }
    if (s_meshCount <= 0) return;

    int br = lightRawAt(&g_world, Mth::floor(x), Mth::floor(y), Mth::floor(z));
    unsigned int brCol = g_brightColor[br];

    ChunkVertex* mesh = (ChunkVertex*)sceGuGetMemory(s_meshCount * sizeof(ChunkVertex));
    for (int i = 0; i < s_meshCount; i++) {
        mesh[i] = s_mesh[i];
        mesh[i].color = mulColor(s_mesh[i].color, brCol);
    }

    sceGumMatrixMode(GU_MODEL);
    sceGumPushMatrix();
    sceGumLoadIdentity();

    // Undo the (0,150,0) probe position (the mesh's own local vertices are
    // baked relative to that grid cell's corner, same as
    // FallingTileRenderer's -0.5f/-150.5f/-0.5f correction below), then
    // place at the raft's real interpolated position and face it along
    // its interpolated yaw.
    ScePspFVector3 tr = { x - 0.5f - g_relBaseX, y - 150.5f - g_relBaseY, z - 0.5f - g_relBaseZ };
    sceGumTranslate(&tr);
    // Matches the sign/offset convention mob_model.cpp and
    // tripod_camera_renderer.cpp use for yaw-driven rotation
    // ((yaw + 180) * DEG2RAD), not a negated angle -- a plain box has no
    // inherent "front" face so the +180 offset doesn't change how it
    // looks, but the sign does, and this keeps the raft consistent with
    // every other yaw-following renderer in the codebase rather than
    // introducing an unverified new convention.
    sceGumRotateY((rot + 180.0f) * (3.14159265f / 180.0f));

    textureBind(&g_terrain);
    sceGumDrawArray(GU_TRIANGLES,
                    GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    s_meshCount, 0, mesh);

    sceGumPopMatrix();
}
