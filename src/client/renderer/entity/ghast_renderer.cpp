#include "client/renderer/entity/ghast_renderer.h"
#include "client/renderer/entity/mob_model.h"
#include "world/entity/mob.h"
#include "world/entity/monster/ghast.h"
#include "gpu/texture.h"
#include <pspgu.h>

// Body + 9 tentacles, matching vanilla Bedrock's real geometry.ghast (the
// texture is confirmed to be Bedrock's actual shipped ghast.png/
// ghast_shooting.png, not a custom layout -- see the investigation this
// replaces). Every cube in vanilla's own geometry, body AND all 9
// tentacles, uses the exact same uv:[0,0] -- this genuinely is how the
// real model textures itself, not a placeholder or a mismatch; Bedrock's
// ghast does not use a Java-style box-cross UV unwrap where each face
// gets a distinct region. Reproducing that (rather than trying to invent
// a cross-layout that fits this texture) is what "matches vanilla" means
// here.
enum {
    G_BODY,
    G_TENT0, G_TENT1, G_TENT2, G_TENT3, G_TENT4, G_TENT5, G_TENT6, G_TENT7, G_TENT8,
    G_COUNT
};
static MobPart parts[G_COUNT];
static bool built = false;
static Texture tex;
static bool haveTex = false;
static Texture texShooting;
static bool haveTexShooting = false;

// Bedrock geometry.ghast, tentacles_0..8: each cube's local box (relative
// to its own pivot) is a uniform 2x2 cross-section hanging straight down
// from the pivot by the tentacle's own height -- (-1,-h,-1) to (1,0,1).
// Pivot itself is the one thing that varies per tentacle (arranged in a
// 3x3 grid under the body) along with height. Values below are copied
// directly from the shipped geometry.ghast bones (pivot x,y,z; height).
struct TentacleSpec { float px, py, pz; float height; };
static const TentacleSpec kTentacles[9] = {
    { -3.8f, 1.0f, -5.0f,  9.0f },
    {  1.3f, 1.0f, -5.0f, 11.0f },
    {  6.3f, 1.0f, -5.0f,  8.0f },
    { -6.3f, 1.0f,  0.0f,  9.0f },
    { -1.3f, 1.0f,  0.0f, 13.0f },
    {  3.8f, 1.0f,  0.0f, 11.0f },
    { -3.8f, 1.0f,  5.0f, 12.0f },
    {  1.3f, 1.0f,  5.0f, 12.0f },
    {  6.3f, 1.0f,  5.0f, 13.0f },
};

static void build() {
    if (built) return;
    // Body: origin (-8,0,-8) size (16,16,16) in Bedrock's own model space,
    // pivot (0,1.5,0). Bedrock's "origin" is model-space, not pivot-
    // relative like this codebase's box vertices are, so the local box
    // here is origin-space unchanged (Bedrock's pivot only matters for
    // rotation, and the body never rotates independently of the whole
    // entity). The box geometry itself was already correct before this
    // rewrite; only the tentacles and modelScale (see render() below)
    // were missing/wrong.
    mobBuildBox(parts[G_BODY].base, -8,-8,-8, 8,8,8, 0,0, 16,16,16, false, 0, 128.0f, 64.0f);
    parts[G_BODY].px = 0; parts[G_BODY].py = 8; parts[G_BODY].pz = 0;

    for (int i = 0; i < 9; i++) {
        const TentacleSpec& t = kTentacles[i];
        MobPart& p = parts[G_TENT0 + i];
        // Local box: (-1,-height,-1) to (1,0,1) -- 2x2 cross-section,
        // hanging down from the pivot. uv (0,0) same as every other part
        // in this model.
        mobBuildBox(p.base, -1.0f, -t.height, -1.0f, 1.0f, 0.0f, 1.0f,
                    0, 0, 2, (int)t.height, 2, false, 0, 128.0f, 64.0f);
        p.px = t.px; p.py = t.py; p.pz = t.pz;
    }

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
    // the way the entity is actually moving/aiming. Tentacles are static
    // (no sway animation) -- vanilla's own animateTentacles is a subtle
    // idle wave that would cost per-tentacle trig every frame for detail
    // that reads as noise at PSP's resolution and distance; skipped for
    // the same reason Strider's leg swing is the only animated part of
    // that model rather than every possible joint.
    MobAnim m = mobAnimSetup(mob, rot, a);
    for (int i = 0; i < G_COUNT; i++)
        parts[i].xRot = parts[i].yRot = parts[i].zRot = 0;

    // modelScale=4.0f: a real ghast is a 4x4x4 block entity (see
    // Ghast::Ghast's setSize(4.0f, 4.0f) in ghast.cpp, which was always
    // correct). The two trailing args here used to be 4.0f, 4.0f as well
    // -- but those parameter slots are babyHeadY/babyHeadZ (baby-mob head
    // repositioning, irrelevant to a ghast, which has no baby variant),
    // not modelScale. modelScale defaulted to 1.0f, so the ghast rendered
    // at ordinary 1-block scale -- a quarter of its real hitbox size, and
    // (with the wrong body UV on top of that) most of the visible cube
    // sampling blank texture. Fixed to pass modelScale explicitly.
    mobRenderParts(mob, parts, G_COUNT, activeTex, x, y, z, m.bodyRot, a,
                   0xFFFFFFFFu, 8.0f, 4.0f, 4.0f);
}

