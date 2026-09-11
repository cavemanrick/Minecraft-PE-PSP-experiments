
#include "client/renderer/entity/raft_renderer.h"
#include "client/renderer/entity/mob_model.h"
#include "world/entity/mob.h"
#include "gpu/texture.h"
#include <math.h>
#include <pspgu.h>

enum { P_DECK, P_RAIL0, P_RAIL1, P_COUNT };
static MobPart parts[P_COUNT];
static bool    g_built = false;
static Texture g_tex;
static bool    g_have = false;

// Texture is data/images/mob/raft.png (Bamboo_Raft__texture__JE1_BE1.png as
// supplied), a real vanilla-style boat-model skin, not a flat icon or a
// terrain-atlas block texture -- 128x64, with content actually confined
// to the top-left ~80x44. Pixel bounds below were measured directly
// against that file (row-by-row alpha scan), not guessed or taken from
// Mojang's internal model source, which was unavailable to check against.
//
// Only two of vanilla's five real box parts (bottom/front/back/left/right)
// are modeled here, per the "deck + side rails, not a full hull" decision:
//   - P_DECK: the wide top strip (rows 0-23) is a standard box-unwrap for
//     a flat slab. tx=0,ty=0,w=24,d=8,h=4 was chosen as the largest
//     w/d/h combination whose unwrapped footprint (2w+2d wide, d+h tall)
//     still fits inside that 64x24 region without reading past it into
//     the side-rail art below.
//   - P_RAIL0/1: vanilla's actual front/back/left/right posts each have
//     a distinct T-shaped crossbar-plus-post silhouette (see the texture
//     itself, rows 24-41); this uses just the plain vertical post
//     sub-region (x=18-21, y=24-41) as a single box for both, mirrored
//     front/back by pz sign, rather than modeling all four distinct
//     T-shapes.
static void build() {
    if (g_built) return;

    // Deck: centered on its own pivot, not hanging from a joint the way a
    // leg does, so it's built symmetrically (y0=-2,y1=2) rather than
    // committing to a signed "away from pivot" direction at all -- there
    // is no ambiguity to get backwards here.
    mobBuildBox(parts[P_DECK].base, -7,-2,-9, 7,2,9, 0,0, 24,8,4, false, 0, 128.0f, 64.0f);
    parts[P_DECK].px = 0; parts[P_DECK].py = 0; parts[P_DECK].pz = 0;

    // Rails: this codebase's mobRenderParts applies a global (-msXZ,-msY,
    // msXZ) scale before any per-part pivot translate (see
    // ghast_renderer.cpp's tentacle comment, written after a real bug
    // where this was gotten backwards), which makes local +Y mean "away
    // from this part's own pivot, toward the ground" -- confirmed against
    // Strider's own leg (pivot above, box y:0..16 extending DOWN to the
    // foot) and Pig's legs (same shape, y:0..6). A leg's pivot sits high
    // and its geometry reaches down to the ground, so it correctly uses
    // POSITIVE local Y.
    //
    // A rail post is the opposite case: its pivot sits at deck height
    // (low) and the geometry needs to reach UP, away from the ground --
    // so it correctly needs NEGATIVE local Y (y0=-height, y1=0), the same
    // sign the ghast bug used incorrectly for a tentacle that was
    // supposed to hang down from a high pivot. Same sign, opposite
    // situation: there it was wrong because the part needed to go down
    // from a high pivot; here it's right because the part needs to go up
    // from a low one. The rule is about which way the geometry actually
    // needs to extend, not a fixed sign to copy.
    mobBuildBox(parts[P_RAIL0].base, -1,-9,-1, 1,0,1, 18,24, 2,9,2, false, 0, 128.0f, 64.0f);
    parts[P_RAIL0].px = 0; parts[P_RAIL0].py = 2; parts[P_RAIL0].pz = -8;

    mobBuildBox(parts[P_RAIL1].base, -1,-9,-1, 1,0,1, 18,24, 2,9,2, false, 0, 128.0f, 64.0f);
    parts[P_RAIL1].px = 0; parts[P_RAIL1].py = 2; parts[P_RAIL1].pz = 8;

    g_built = true;
}

void RaftRenderer::render(Entity* e, float x, float y, float z, float rot, float a) {
    if (!g_have) { g_have = textureLoad16("data/images/mob/raft.png", &g_tex, GU_PSM_5551); if (!g_have) return; }
    build();
    Mob* mob = (Mob*)e;
    // mobAnimSetup also computes headYaw/pitch/walk-cycle speed & pos --
    // all unused here (the raft has no head to swivel and no walk cycle),
    // but its bodyRot handles the +-180 wraparound case correctly, which
    // reimplementing just that one line by hand would have skipped.
    MobAnim m = mobAnimSetup(mob, rot, a);
    mobRenderParts(mob, parts, P_COUNT, &g_tex, x, y, z, m.bodyRot, a, 0xFFFFFFFFu);
}
