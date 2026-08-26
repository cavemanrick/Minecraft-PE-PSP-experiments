#include "client/renderer/entity/strider_renderer.h"
#include "client/renderer/entity/mob_model.h"
#include "world/entity/mob.h"
#include "world/entity/animal/strider.h"
#include "gpu/texture.h"
#include <math.h>
#include <pspgu.h>

static const float DEG2RAD = 3.14159265f / 180.0f;
enum { S_BODY, S_HEAD, S_LEG0, S_LEG1, S_LEG2, S_LEG3, S_COUNT };
static MobPart parts[S_COUNT];
static bool built = false;
static Texture tex;
static bool haveTex = false;
static Texture saddleTex;
static bool haveSaddleTex = false;

static void build() {
    if (built) return;
    // Compact low-poly strider silhouette. The texture is intentionally kept
    // in the normal mob texture pipeline so no new rendering machinery is
    // needed; data/images/mob/strider.png can be supplied independently.
    mobBuildBox(parts[S_BODY].base, -7,-8,-6, 7,7,7, 0,0, 14,15,13, false, 0);
    parts[S_BODY].px = 0; parts[S_BODY].py = 10; parts[S_BODY].pz = 0;
    mobBuildBox(parts[S_HEAD].base, -5,-5,-6, 5,4,5, 0,28, 10,9,11, false, 0);
    parts[S_HEAD].px = 0; parts[S_HEAD].py = 5; parts[S_HEAD].pz = -1;
    parts[S_HEAD].head = true;

    const float lp[4][3] = {
        {-5, 15, -4}, {5, 15, -4}, {-5, 15, 4}, {5, 15, 4}
    };
    for (int i = 0; i < 4; ++i) {
        mobBuildBox(parts[S_LEG0+i].base, -1.5f,0,-1.5f, 1.5f,10,1.5f,
                    0, 40, 3,10,3, false, 0);
        parts[S_LEG0+i].px = lp[i][0];
        parts[S_LEG0+i].py = lp[i][1];
        parts[S_LEG0+i].pz = lp[i][2];
    }
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

    parts[S_BODY].xRot = 90.0f * DEG2RAD;
    parts[S_BODY].yRot = parts[S_BODY].zRot = 0;
    parts[S_HEAD].xRot = -m.pitch * DEG2RAD;
    parts[S_HEAD].yRot = -m.headYaw * DEG2RAD;
    parts[S_HEAD].zRot = 0;
    parts[S_LEG0].xRot = pend;
    parts[S_LEG1].xRot = -pend;
    parts[S_LEG2].xRot = -pend;
    parts[S_LEG3].xRot = pend;
    for (int i = S_LEG0; i < S_COUNT; ++i) {
        parts[i].yRot = parts[i].zRot = 0;
    }

    mobRenderParts(mob, parts, S_COUNT, activeTex, x, y, z, m.bodyRot, a,
                   0xFFFFFFFFu, 4.0f, 4.0f);
}
