#include "client/renderer/entity/fireball_renderer.h"
#include "world/entity/fireball.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "client/renderer/item_model.h"
#include "client/renderer/render.h"
#include "gpu/texture.h"
#include "util/mth.h"
#include <cmath>
#include <pspgu.h>
#include <pspgum.h>

extern World g_world;
extern unsigned int g_brightColor[16];

static Texture tex;
static bool haveTex = false;

// Standalone dedicated texture rather than a shared item-icon atlas cell
// (the approach ThrowableRenderer uses via itemFlatIconUV): a fireball
// isn't a player-holdable inventory item in vanilla, only a ghast's
// projectile, so there's no gui_blocks.png entry to reuse, and adding one
// would need editing that runtime atlas file, which isn't available in
// this source tree (see the item_icons.h header comment for how prior
// atlas edits like the fishing rod/fish icons were done -- pasting a new
// cell into the real gui_blocks.png offline). Loading fireball.png
// directly, the same way GhastRenderer/StriderRenderer load their own
// dedicated mob skins, sidesteps needing that file at all.
void FireballRenderer::render(Entity* entity, float x, float y, float z, float , float ) {
    if (!haveTex) {
        haveTex = textureLoad16("data/images/misc/fireball.png", &tex, GU_PSM_5551);
        if (!haveTex) return;
    }
    (void)entity; // Fireball carries no per-instance visual state to read

    int br = lightRawAt(&g_world, Mth::floor(x), Mth::floor(y), Mth::floor(z));
    unsigned int c = g_brightColor[br];

    // Full 0..1 UV: fireball.png is used whole, not as one cell of a
    // shared grid, unlike itemFlatIconUV's atlas-cell math.
    const float xo = 0.5f, yo = 0.5f, r = 1.0f;
    ChunkVertex q[6] = {
        { 0.0f, 1.0f, c, 0 - xo, 0 - yo, 0.0f },
        { 1.0f, 1.0f, c, r - xo, 0 - yo, 0.0f },
        { 1.0f, 0.0f, c, r - xo, 1 - yo, 0.0f },
        { 1.0f, 0.0f, c, r - xo, 1 - yo, 0.0f },
        { 0.0f, 0.0f, c, 0 - xo, 1 - yo, 0.0f },
        { 0.0f, 1.0f, c, 0 - xo, 0 - yo, 0.0f },
    };

    sceGuDisable(GU_CULL_FACE);
    sceGumMatrixMode(GU_MODEL);
    sceGumPushMatrix();
    sceGumLoadIdentity();
    ScePspFVector3 tr = { x - g_relBaseX, y - g_relBaseY, z - g_relBaseZ };
    sceGumTranslate(&tr);
    const float s = 1.0f; // fireball's own setSize(1.0f,1.0f) matches full scale
    ScePspFVector3 sc = { s, s, s };
    sceGumScale(&sc);

    float dpx = g_camX - x, dpy = g_camY - y, dpz = g_camZ - z;
    float dhoriz = sqrtf(dpx * dpx + dpz * dpz);
    sceGumRotateY(atan2f(dpx, dpz));
    sceGumRotateX(-atan2f(dpy, dhoriz));
    ItemModelRenderer::drawMesh(q, 6, 0xFFFFFFFFu, &tex, true);
    sceGumPopMatrix();
    sceGuEnable(GU_CULL_FACE);
}
