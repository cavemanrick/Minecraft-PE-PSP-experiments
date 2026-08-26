#include "client/renderer/entity/strider_renderer.h"
#include "client/renderer/entity/mob_model.h"
#include "world/entity/mob.h"
#include "world/entity/animal/strider.h"
#include "gpu/texture.h"
#include <math.h>
#include <pspgu.h>

enum { S_BODY, S_LEG_R, S_LEG_L, S_COUNT };
static MobPart parts[S_COUNT];
static bool built = false;
static Texture tex;
static bool haveTex = false;
static Texture saddleTex;
static bool haveSaddleTex = false;

static void build() {
    if (built) return;
    // Laid out to match vanilla strider.png, which is 64x128 -- NOT the
    // 64x32 that mobBuildBox defaults to, hence the explicit texW/texH on
    // every call. Measured straight off the sheet: opaque rows run 0-29,
    // 32-52 and 55-75, which is exactly a 16x16x16 body net (64 wide,
    // 32 tall) at v=0 and two 4x16x4 leg nets at v=32 and v=55.
    //
    // The previous model was a bespoke six-part rig -- body, a separate
    // head, and four legs -- with UVs at (0,0), (0,28) and (0,40) against
    // a 32-row sheet. Nothing about that fits vanilla art: the head and
    // legs ran past v=1.0 and sampled wrapped garbage, the leg net
    // overlapped the head net, and no vanilla strider sheet has anything
    // at those offsets. Rebuilding to vanilla's own layout is what lets
    // the texture you already have drop straight in.
    //
    // Vanilla's strider has no separate head part (the face is just the
    // front of the body cube) and two legs, not four, so both go.
    // Skipped: the decorative bristle boxes. They are extra draw calls for
    // silhouette detail that will not read at PSP resolution.
    mobBuildBox(parts[S_BODY].base, -8,-8,-8, 8,8,8, 0,0, 16,16,16, false, 0, 64.0f, 128.0f);
    parts[S_BODY].px = 0; parts[S_BODY].py = 8; parts[S_BODY].pz = 0;

    mobBuildBox(parts[S_LEG_R].base, -2,0,-2, 2,16,2, 0,32, 4,16,4, false, 0, 64.0f, 128.0f);
    parts[S_LEG_R].px = -4; parts[S_LEG_R].py = 8; parts[S_LEG_R].pz = 0;

    mobBuildBox(parts[S_LEG_L].base, -2,0,-2, 2,16,2, 0,55, 4,16,4, false, 0, 64.0f, 128.0f);
    parts[S_LEG_L].px = 4; parts[S_LEG_L].py = 8; parts[S_LEG_L].pz = 0;
    built = true;
}

StriderRenderer::StriderRenderer() {
    shadowRadius = 0.55f;
    shadowStrength = 1.0f;
}

void StriderRenderer::render(Entity* e, float x, float y, float z, float rot, float a) {
    if (!haveTex) {
        haveTex = textureLoad16("data/images/mob/strider.png", &tex, GU_PSM_5551);
        if (!haveTex) return;
    }
    Strider* strider = (Strider*)e;
    Texture* activeTex = &tex;
    if (strider->isSaddled()) {
        if (!haveSaddleTex)
            haveSaddleTex = textureLoad16("data/images/mob/strider_saddled.png", &saddleTex, GU_PSM_5551);
        if (haveSaddleTex) activeTex = &saddleTex;
    }
    build();
    Mob* mob = (Mob*)e;
    MobAnim m = mobAnimSetup(mob, rot, a);
    float pend = cosf(m.pos * 0.6662f) * 0.55f * m.speed;

    // No 90-degree body rotation any more. That existed to stand the old
    // bespoke body box on end; a proper cube net is already upright.
    parts[S_BODY].xRot = parts[S_BODY].yRot = parts[S_BODY].zRot = 0;
    parts[S_LEG_R].xRot = pend;
    parts[S_LEG_L].xRot = -pend;
    for (int i = S_LEG_R; i < S_COUNT; ++i) parts[i].yRot = parts[i].zRot = 0;

    mobRenderParts(mob, parts, S_COUNT, activeTex, x, y, z, m.bodyRot, a,
                   0xFFFFFFFFu, 4.0f, 4.0f);
}
