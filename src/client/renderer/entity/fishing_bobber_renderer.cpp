
#include "client/renderer/entity/fishing_bobber_renderer.h"
#include "world/entity/fishing_bobber.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "client/renderer/item_hand.h"
#include "client/renderer/item_model.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "client/player/player_state.h"
#include "client/renderer/render.h"
#include "world/item/item.h"
#include "gpu/texture.h"
#include "util/mth.h"
#include <math.h>
#include <pspgu.h>
#include <pspgum.h>

extern World g_world;
extern unsigned int g_brightColor[16];

void FishingBobberRenderer::render(Entity* entity, float x, float y, float z, float , float ) {
    (void)entity;

    // Drawn as a snowball, which is the only small pale round sprite
    // gui_blocks.png has. Still placeholder art -- the rod itself now has
    // real art (see II_FISHING_ROD in item_icons.h), but the bobber and
    // the two fish items don't yet.
    float u0, v0, u1, v1;
    const Texture* tex = itemFlatIconUV(ITEM_SNOWBALL, 0, &u0, &v0, &u1, &v1);
    if (!tex) return;

    int br = lightRawAt(&g_world, Mth::floor(x), Mth::floor(y), Mth::floor(z));
    unsigned int c = g_brightColor[br];

    const float xo = 0.5f, yo = 0.25f, r = 1.0f;
    ChunkVertex q[6] = {
        { u0, v1, c, 0 - xo, 0 - yo, 0.0f },
        { u1, v1, c, r - xo, 0 - yo, 0.0f },
        { u1, v0, c, r - xo, 1 - yo, 0.0f },
        { u1, v0, c, r - xo, 1 - yo, 0.0f },
        { u0, v0, c, 0 - xo, 1 - yo, 0.0f },
        { u0, v1, c, 0 - xo, 0 - yo, 0.0f },
    };

    sceGuDisable(GU_CULL_FACE);
    sceGumMatrixMode(GU_MODEL);
    sceGumPushMatrix();
    sceGumLoadIdentity();
    ScePspFVector3 tr = { x - g_relBaseX, y - g_relBaseY, z - g_relBaseZ };
    sceGumTranslate(&tr);
    const float s = 0.35f; // smaller than a thrown item; it is a float, not a projectile
    ScePspFVector3 sc = { s, s, s };
    sceGumScale(&sc);

    float dpx = g_camX - x, dpy = g_camY - y, dpz = g_camZ - z;
    float dhoriz = sqrtf(dpx * dpx + dpz * dpz);
    sceGumRotateY(atan2f(dpx, dpz));
    sceGumRotateX(-atan2f(dpy, dhoriz));
    ItemModelRenderer::drawMesh(q, 6, 0xFFFFFFFFu, tex, true);
    sceGumPopMatrix();
    sceGuEnable(GU_CULL_FACE);
}
