#include "client/renderer/entity/ghast_renderer.h"
#include "client/renderer/entity/mob_model.h"
#include "world/entity/mob.h"
#include "world/entity/monster/ghast.h"
#include "gpu/texture.h"
#include <pspgu.h>

enum { G_BODY, G_COUNT };
static MobPart parts[G_COUNT];
static bool built = false;
static Texture tex;
static bool haveTex = false;
static Texture texShooting;
static bool haveTexShooting = false;

static void build() {
    if (built) return;
    // Single 16x16x16 body cube at UV origin (0,0) on a 128x64 sheet.
    // Measured against the actual uploaded ghast.png/ghast_shooting.png:
    // both are 128x64 (2x vanilla's classic 64x32 mob-skin scale, same
    // situation Strider's own header comment already worked through for
    // its 64x128 sheet), and mobBuildBox's net-unwrap formula is a pure
    // function of tx,ty,w,h,d plus texW/texH -- passing the real
    // dimensions here is enough for it to lay out all six faces
    // correctly with no per-mob-specific coordinate math needed, the
    // same way Strider's rebuild proved out.
    //
    // Real ghasts also have nine thin hanging tentacle boxes below the
    // body; skipped here for the same reason Strider skips its
    // decorative bristle boxes -- extra draw calls for silhouette detail
    // that will not read at PSP resolution.
    mobBuildBox(parts[G_BODY].base, -8,-8,-8, 8,8,8, 0,0, 16,16,16, false, 0, 128.0f, 64.0f);
    parts[G_BODY].px = 0; parts[G_BODY].py = 8; parts[G_BODY].pz = 0;
    built = true;
}

GhastRenderer::GhastRenderer() {
    // Ghasts float and are frequently well clear of the ground -- a
    // small, soft shadow rather than Strider's fuller one (a strider is
    // always standing on a lava surface directly beneath it).
    shadowRadius = 0.5f;
    shadowStrength = 0.5f;
}

void GhastRenderer::render(Entity* e, float x, float y, float z, float rot, float a) {
    if (!haveTex) {
        haveTex = textureLoad16("data/images/mob/ghast.png", &tex, GU_PSM_5551);
        if (!haveTex) return;
    }
    Ghast* ghast = (Ghast*)e;
    Texture* activeTex = &tex;
    if (ghast->isCharging()) {
        if (!haveTexShooting)
            haveTexShooting = textureLoad16("data/images/mob/ghast_shooting.png", &texShooting, GU_PSM_5551);
        if (haveTexShooting) activeTex = &texShooting;
    }
    build();
    Mob* mob = (Mob*)e;

    // No walk-cycle animation at all -- a ghast has no legs and its body
    // doesn't articulate, so unlike Strider (which drives leg xRot off
    // mobAnimSetup's walk speed/pos) there's nothing here to animate
    // per-part. bodyRot still comes from mobAnimSetup so the model faces
    // the way the entity is actually moving/aiming.
    MobAnim m = mobAnimSetup(mob, rot, a);
    parts[G_BODY].xRot = parts[G_BODY].yRot = parts[G_BODY].zRot = 0;

    mobRenderParts(mob, parts, G_COUNT, activeTex, x, y, z, m.bodyRot, a,
                   0xFFFFFFFFu, 4.0f, 4.0f);
}
