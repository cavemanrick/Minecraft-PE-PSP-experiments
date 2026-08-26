#include "client/renderer/entity/strider_renderer.h"
#include "client/renderer/entity/mob_model.h"
#include "world/entity/mob.h"
#include "world/entity/animal/strider.h"
#include "gpu/texture.h"
#include <math.h>
#include <pspgu.h>

static const float DEG2RAD = 3.14159265f / 180.0f;

// Minecraft's Bedrock Strider geometry uses a 64x128 texture.  The main
// body is a 16x14x16 cube at UV 0,0; the two legs are 4x16x4 cubes at UV
// 0,32.  The six bristles are thin 12x0x16 planes using the UV strips at
// 16,33 / 16,49 / 16,65.  Keeping these coordinates here lets the supplied
// vanilla-style 64x128 textures line up without a texture conversion step.
enum {
    S_BODY,
    S_RIGHT_LEG,
    S_LEFT_LEG,
    S_BRISTLE0,
    S_BRISTLE1,
    S_BRISTLE2,
    S_BRISTLE3,
    S_BRISTLE4,
    S_BRISTLE5,
    S_COUNT
};

static MobPart parts[S_COUNT];
static bool built = false;
static Texture texNormal;
static Texture texSaddled;
static bool haveNormal = false;
static bool haveSaddled = false;

static void build() {
    if (built) return;

    // Exact vanilla Bedrock Strider proportions/UVs.  The body and legs are
    // parented through their pivots just as in the original model.
    mobBuildBox(parts[S_BODY].base,
                -8, -2, -8, 8, 12, 8,
                0, 0, 16, 14, 16, false, 0.0f, 64.0f, 128.0f);
    parts[S_BODY].px = 0.0f;
    parts[S_BODY].py = 16.0f;
    parts[S_BODY].pz = 0.0f;

    mobBuildBox(parts[S_RIGHT_LEG].base,
                -2, -16, -2, 2, 0, 2,
                0, 32, 4, 16, 4, false, 0.0f, 64.0f, 128.0f);
    parts[S_RIGHT_LEG].px = -4.0f;
    parts[S_RIGHT_LEG].py = 16.0f;
    parts[S_RIGHT_LEG].pz = 0.0f;

    mobBuildBox(parts[S_LEFT_LEG].base,
                -2, -16, -2, 2, 0, 2,
                0, 32, 4, 16, 4, true, 0.0f, 64.0f, 128.0f);
    parts[S_LEFT_LEG].px = 4.0f;
    parts[S_LEFT_LEG].py = 16.0f;
    parts[S_LEFT_LEG].pz = 0.0f;

    // The bristles are zero-height X/Z planes in the original model.  The
    // existing box builder handles this cleanly: its top/bottom faces still
    // produce the required 12x16 textured plane while the degenerate side
    // faces contribute no visible geometry.
    const int bristleTexY[3] = { 65, 49, 33 };
    const float bristleY[3] = { 19.0f, 24.0f, 28.0f };

    for (int i = 0; i < 3; ++i) {
        const int right = S_BRISTLE0 + i;
        const int left  = S_BRISTLE3 + i;

        mobBuildBox(parts[right].base,
                    -12, 0.0f, -8,
                    0, 0.0f,  8,
                    16, bristleTexY[i], 12, 0, 16,
                    true, 0.0f, 64.0f, 128.0f);
        parts[right].px = -8.0f;
        parts[right].py = bristleY[i];
        parts[right].pz = 0.0f;
        parts[right].zRot = -(i == 0 ? 70.0f : (i == 1 ? 65.0f : 50.0f)) * DEG2RAD;

        mobBuildBox(parts[left].base,
                    0, 0.0f, -8,
                    12, 0.0f, 8,
                    16, bristleTexY[i], 12, 0, 16,
                    false, 0.0f, 64.0f, 128.0f);
        parts[left].px = 8.0f;
        parts[left].py = bristleY[i];
        parts[left].pz = 0.0f;
        parts[left].zRot = (i == 0 ? 70.0f : (i == 1 ? 65.0f : 50.0f)) * DEG2RAD;
    }

    built = true;
}

StriderRenderer::StriderRenderer() {
    shadowRadius = 0.65f;
    shadowStrength = 1.0f;
}

void StriderRenderer::render(Entity* e, float x, float y, float z, float rot, float a) {
    if (!haveNormal) {
        haveNormal = textureLoad16("data/images/mob/strider.png", &texNormal, GU_PSM_5551);
        if (!haveNormal) return;
    }

    Strider* strider = (Strider*)e;
    Texture* tex = &texNormal;

    // Riding is the saddle state in this PSP implementation.  No separate
    // saddle entity/model is needed; switching the texture is cheaper and
    // matches the supplied strider_saddled.png layout exactly.
    if (strider->getRider()) {
        if (!haveSaddled)
            haveSaddled = textureLoad16("data/images/mob/strider_saddled.png",
                                        &texSaddled, GU_PSM_5551);
        if (haveSaddled) tex = &texSaddled;
    }

    build();

    MobAnim m = mobAnimSetup(strider, rot, a);
    const float pend = cosf(m.pos * 0.6662f) * 0.50f * m.speed;

    // Body remains level; the two legs alternate in the same low-cost
    // animation style used by the other PSP mob renderers.
    parts[S_BODY].xRot = 0.0f;
    parts[S_BODY].yRot = 0.0f;
    parts[S_BODY].zRot = 0.0f;

    parts[S_RIGHT_LEG].xRot = 0.0f;
    parts[S_LEFT_LEG].xRot = 0.0f;
    parts[S_RIGHT_LEG].yRot = pend;
    parts[S_LEFT_LEG].yRot = -pend;

    for (int i = S_BRISTLE0; i < S_COUNT; ++i)
        parts[i].xRot = parts[i].yRot = 0.0f;

    mobRenderParts(strider, parts, S_COUNT, tex,
                   x, y, z, m.bodyRot, a,
                   0xFFFFFFFFu, 4.0f, 4.0f,
                   1.0f);
}
