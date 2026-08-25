#include "world/level/tile/nether_portal.h"
#include "world/level/world.h"
#include "world/level/level.h"
#include "world/level/levelgen/features.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/entity/entity.h"
#include "world/entity/player.h"
#include "world/level/levelgen/nether_gen.h" // netherFindPortalSite -- the
                      // shell's own geometry decides what a good portal
                      // site is, so the search lives with the generator
#include <math.h>     // sinf/cosf for the exit-face heading test
#include "util/mth.h" // Mth::floor -- (int) truncation is wrong for negative
                      // coordinates, and the Overworld return position can
                      // legitimately be negative on an Infinite-origin world

// --- World-size gate ----------------------------------------------------
// Nether portals only mean anything on a world created at the 1024 preset:
// that is the only size that has a reserved Nether strip at all (see
// WORLD_PRESET_1024_TOTAL_X_CHUNKS / WORLD_NETHER_ORIGIN_CX in world.h).
//
// This gate is not cosmetic. Without it:
//
//   * netherPortalEntityInside called worldChunkIsNether directly, which
//     deliberately ignores its World* argument and only compares chunk
//     coordinates (see its own comment in world.h: "Only meaningful to call
//     where worldChunkIsReserved is already known true"). On an Infinite
//     world, chunk X [64,96) x chunk Z [0,32) -- block X 1024..1535,
//     Z 0..511, i.e. ordinary, walkable, reachable overworld -- therefore
//     answered "yes, this is the Nether side", so any portal built there
//     silently did nothing instead of working.
//
//   * a portal built anywhere else on an Infinite or 512 world took the
//     other branch and ran teleportToNether, which carves a netherrack
//     pocket and stamps an obsidian frame into real overworld terrain at
//     the fixed Nether coordinate, then drops the player there.
//
// debug_teleport.cpp already gates itself the same way (see its sizeX
// check); this brings the real portal path in line with it.
static bool netherPortalsSupported(const World* w) {
    // Both pre-generated presets now carry a Nether strip, so this is no
    // longer a 1024-only feature -- worldHasReservedRegions covers 512 as
    // well. Legacy infinite saves (sizeX == 0) still have nowhere to go
    // and are correctly excluded.
    return w && worldHasReservedRegions(w);
}

// --- Frame detection --------------------------------------------------
// Vanilla-shaped rectangular obsidian frame: interior at least 2 wide and
// 3 tall (up to 21 wide / 21 tall in real vanilla, but this project has
// no redstone/other reason to support giant portals, so the search caps
// at a smaller MAX below purely as a runaway-scan guard, not a design
// choice to limit player-buildable size in any way that matters in
// practice). Either X-aligned (frame's obsidian columns at fixed x,
// varying z) or Z-aligned (fixed z, varying x) -- never both at once,
// matching vanilla.

#define PORTAL_MIN_INTERIOR_W 2
#define PORTAL_MIN_INTERIOR_H 3
#define PORTAL_MAX_INTERIOR_W 21
#define PORTAL_MAX_INTERIOR_H 21

struct PortalFrame {
    bool alongX; // true: frame extends along x, obsidian posts at fixed z; false: fixed x
    int fixedCoord;      // the frame's fixed x (if !alongX) or z (if alongX)
    int loA, hiA;        // interior span along the varying horizontal axis (x if alongX, z otherwise)
    int loY, hiY;        // interior span along y
};

// blockAt(a, y): reads the world block at the varying-axis coordinate `a`
// and height `y`, translated back into real (x,y,z) for whichever
// orientation is being tested. Keeps the search logic below orientation-
// agnostic instead of duplicating it once per axis.
struct FrameAxis {
    World* w;
    bool alongX;
    int fixedCoord;
    unsigned char at(int a, int y) const {
        return alongX ? worldBlock(w, a, y, fixedCoord) : worldBlock(w, fixedCoord, y, a);
    }
    bool obsidianAt(int a, int y) const { return at(a, y) == BLOCK_OBSIDIAN; }
    bool interiorOkAt(int a, int y) const { unsigned char id = at(a, y); return id == BLOCK_AIR || id == BLOCK_PORTAL; }
};

// Given a candidate interior cell (a0, y0), try to grow and fully validate
// a frame rectangle around it. Pure test -- writes to `out` only on
// success, so a failed candidate leaves the caller free to try another.
static bool growFrameFrom(const FrameAxis& ax, int a0, int y0, PortalFrame* out) {
    // Find the interior's y-span: walk down/up from y0 while the column at
    // a0 is interior-clear.
    int loY = y0, hiY = y0;
    while (ax.interiorOkAt(a0, loY - 1)) { loY--; if (y0 - loY > PORTAL_MAX_INTERIOR_H) return false; }
    while (ax.interiorOkAt(a0, hiY + 1)) { hiY++; if (hiY - y0 > PORTAL_MAX_INTERIOR_H) return false; }
    // Floor and ceiling must be obsidian.
    if (!ax.obsidianAt(a0, loY - 1) || !ax.obsidianAt(a0, hiY + 1)) return false;
    if (hiY - loY + 1 < PORTAL_MIN_INTERIOR_H) return false;

    // Find the interior's horizontal span at this same height, walking
    // left/right while clear, then confirm obsidian posts cap both ends.
    int loA = a0, hiA = a0;
    while (ax.interiorOkAt(loA - 1, y0)) { loA--; if (a0 - loA > PORTAL_MAX_INTERIOR_W) return false; }
    while (ax.interiorOkAt(hiA + 1, y0)) { hiA++; if (hiA - a0 > PORTAL_MAX_INTERIOR_W) return false; }
    if (!ax.obsidianAt(loA - 1, y0) || !ax.obsidianAt(hiA + 1, y0)) return false;
    if (hiA - loA + 1 < PORTAL_MIN_INTERIOR_W) return false;

    // Verify the full rectangle: every interior cell must be air/portal,
    // every border cell (the frame ring one step outside the interior)
    // must be obsidian. This is the real validation -- the walks above
    // only established candidate bounds from a single row/column each.
    for (int a = loA; a <= hiA; a++)
        for (int y = loY; y <= hiY; y++)
            if (!ax.interiorOkAt(a, y)) return false;

    for (int a = loA; a <= hiA; a++) {
        if (!ax.obsidianAt(a, loY - 1)) return false;
        if (!ax.obsidianAt(a, hiY + 1)) return false;
    }
    for (int y = loY; y <= hiY; y++) {
        if (!ax.obsidianAt(loA - 1, y)) return false;
        if (!ax.obsidianAt(hiA + 1, y)) return false;
    }
    // Corners can be anything in vanilla (frame corners aren't required
    // obsidian), so they're deliberately not checked here.

    out->loA = loA; out->hiA = hiA;
    out->loY = loY; out->hiY = hiY;
    return true;
}

// Attempts to find a valid frame around (aClick, yClick) on the given
// fixed coordinate/orientation. (aClick, yClick) is always where the
// player actually clicked, which per FlintAndSteelItem::useOn's own gating
// is always an obsidian post -- so this starts by finding an interior cell
// adjacent to the clicked post and grows the search from there, rather
// than requiring the click itself to be interior air.
//
// Every one of the four neighbours is tried, and each candidate gets a
// full frame validation of its own. The previous version stopped at the
// first air/portal neighbour it found and searched only from that one,
// which broke on any click where an unrelated air cell happened to be
// checked first: clicking a post from *outside* the frame (the far-side
// post at a = hiA+1, whose {+1,0} neighbour is open air outside the
// structure) picked that outside air as the interior origin, walked down
// it to the ground, found dirt where it wanted obsidian, and failed --
// and because the first candidate had already been committed to, no other
// neighbour was ever tried and both orientations reported "no frame".
static bool tryFindFrame(World* w, bool alongX, int fixedCoord, int aClick, int yClick, PortalFrame* out) {
    FrameAxis ax = { w, alongX, fixedCoord };

    if (!ax.obsidianAt(aClick, yClick)) return false; // caller only ever passes a post

    static const int kOffsets[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    for (int i = 0; i < 4; i++) {
        int a = aClick + kOffsets[i][0], y = yClick + kOffsets[i][1];
        if (!ax.interiorOkAt(a, y)) continue;
        if (!growFrameFrom(ax, a, y, out)) continue;
        out->alongX = alongX;
        out->fixedCoord = fixedCoord;
        return true;
    }
    return false;
}

static void fillFrameInterior(World* w, const PortalFrame& f) {
    for (int a = f.loA; a <= f.hiA; a++)
        for (int y = f.loY; y <= f.hiY; y++) {
            int x = f.alongX ? a : f.fixedCoord;
            int z = f.alongX ? f.fixedCoord : a;
            worldSetBlockAndData(w, x, y, z, BLOCK_PORTAL, 0);
        }
    // Light/mesh update once for the whole frame rather than per-block --
    // worldSetBlockAndData has already marked each written section dirty
    // and seeded the light BFS per block; this just drains the queue and
    // runs the one neighbour notification.
    int midA = (f.loA + f.hiA) / 2, midY = (f.loY + f.hiY) / 2;
    int mx = f.alongX ? midA : f.fixedCoord;
    int mz = f.alongX ? f.fixedCoord : midA;
    worldNotifyNeighborsChanged(w, mx, midY, mz);
    worldUpdateLights(w);
}

bool netherPortalTryIgnite(World* w, int x, int y, int z) {
    if (!netherPortalsSupported(w)) return false; // see netherPortalsSupported

    // Try both orientations at the clicked column; whichever finds a
    // valid frame first wins (a real ambiguous double-frame at the exact
    // same spot isn't buildable in practice, so no tie-break is needed).
    PortalFrame frame;
    if (tryFindFrame(w, true, z, x, y, &frame)) { fillFrameInterior(w, frame); return true; }
    if (tryFindFrame(w, false, x, z, y, &frame)) { fillFrameInterior(w, frame); return true; }
    return false;
}

// --- Teleport ------------------------------------------------------------
// See the design note in debug_teleport.cpp for why "moveTo" is sufficient
// here even though this crosses into the reserved Nether strip: there is
// no real multi-World/dimension concept in this codebase (single global
// g_world), so the Nether is just another region of the same flat world,
// and Level::getCubes' synthetic boundary wall (level.cpp) only blocks
// *walking* across the seam, not a direct position set like moveTo does.

// Where the Nether-side portal is looked for. One canonical Nether portal
// for the whole world, matching this project's "fixed single entry point,
// no coordinate mapping" scope decision -- but the exact block is now
// found by searching the terrain around this chunk rather than being the
// chunk's centre block regardless of what is there (see the anchor
// machinery below).
// No longer compile-time constants: the strip's X origin depends on the
// preset's overworld width (worldNetherOriginCX in world.h).
static int netherPortalCX(const World* w) { return worldNetherOriginCX(w) + WORLD_NETHER_CHUNKS / 2; }
static int netherPortalCZ(void) { return WORLD_NETHER_CHUNKS / 2; }

// How far out from that chunk the site search may look, tried in order.
// Every chunk in a radius has to be generated before the search runs (the
// search only reads blocks; it never generates), so the radius is also the
// cost: 1 means a 3x3 block of synchronous chunk generations, 2 means 5x5.
//
// Escalating rather than going straight to the wider search keeps the
// common case cheap. Measured over ten seeds, radius 1 succeeds on eight;
// the two failures are simply the canonical chunk happening to sit in the
// middle of a large lava sea, and radius 2 finds a site on all ten. So the
// wide search is only paid by the worlds that actually need it.
//
// Worth noting the hitch is much less objectionable than it was: a
// crossing is now a deliberate fade to black, so the chunk generation
// happens behind a dark screen instead of freezing a visible frame.
static const int kPortalSiteRadii[] = { 1, 2 };
#define PORTAL_SITE_RADIUS_COUNT ((int)(sizeof(kPortalSiteRadii) / sizeof(kPortalSiteRadii[0])))

// How many chunks either side of an arrival point to force-generate before
// the player is placed there. A one-chunk skirt means they cannot walk off
// the edge of their own arrival chunk into not-yet-streamed space in the
// first second after landing.
#define PORTAL_ARRIVAL_CHUNK_RADIUS 1

// --- Anchor storage ------------------------------------------------------
// See the contract in nether_portal.h for why this has to be remembered
// rather than recomputed.

static bool s_anchorKnown = false;
static int  s_anchorX = 0, s_anchorY = 0, s_anchorZ = 0;

bool netherPortalAnchorKnown(void) { return s_anchorKnown; }

void netherPortalGetAnchor(int* x, int* y, int* z) {
    if (x) *x = s_anchorX;
    if (y) *y = s_anchorY;
    if (z) *z = s_anchorZ;
}

void netherPortalSetAnchor(int x, int y, int z) {
    s_anchorX = x; s_anchorY = y; s_anchorZ = z;
    s_anchorKnown = true;
}

void netherPortalResetAnchor(void) { s_anchorKnown = false; }

// --- Orientation and exit ------------------------------------------------
// A portal's interior is a flat plane one block thick, so the two
// directions perpendicular to that plane are the two faces a player can
// step out onto. Working out which axis the plane runs along is what makes
// "face away from the portal" a well-defined instruction.
//
// Portal blocks carry no axis in their data nibble (fillFrameInterior and
// ensureNetherSidePortal both write data 0), so the axis is recovered from
// the neighbours instead. The minimum interior width is 2 (see
// PORTAL_MIN_INTERIOR_W), so any portal block always has at least one
// portal neighbour along the plane's horizontal axis and never has one
// across it -- which makes this test exact rather than a heuristic.
//
// Returns true if the plane runs along X (so the faces are +z and -z).
static bool portalPlaneAlongX(World* w, int x, int y, int z) {
    if (worldBlock(w, x + 1, y, z) == BLOCK_PORTAL) return true;
    if (worldBlock(w, x - 1, y, z) == BLOCK_PORTAL) return true;
    if (worldBlock(w, x, y, z + 1) == BLOCK_PORTAL) return false;
    if (worldBlock(w, x, y, z - 1) == BLOCK_PORTAL) return false;
    // A one-block portal cannot be built through netherPortalTryIgnite, so
    // reaching here means the frame was broken between the crossing
    // starting and it firing. Either answer is as good as the other; X
    // matches the orientation ensureNetherSidePortal builds.
    return true;
}

// Is (bx, by, bz) somewhere a player can stand -- feet and head clear, and
// something solid underfoot? Deliberately checks for solid ground rather
// than merely "not air": stepping out of a portal into a two-block hole
// over a lava sea is not an exit.
static bool portalStandable(World* w, int bx, int by, int bz) {
    unsigned char feet = worldBlock(w, bx, by, bz);
    unsigned char head = worldBlock(w, bx, by + 1, bz);
    if (feet != BLOCK_AIR || head != BLOCK_AIR) return false;
    unsigned char floorId = worldBlock(w, bx, by - 1, bz);
    if (floorId == BLOCK_AIR || isLiquidId(floorId)) return false;
    return true;
}

// Yaw for a unit cardinal direction. This codebase's yaw convention is
// forward = (sin(yaw), cos(yaw)) -- see Mob::mobMoveRelative in mob.cpp,
// which adds `xs * cy + yf * sy` to xd and `yf * cy - xs * sy` to zd, and
// Mob::aiStep, which recovers a yaw from a movement vector with
// atan2f(mdx, mdz). Note this is NOT vanilla's convention, which negates
// the x term; deriving the numbers from the code rather than from vanilla
// is the difference between facing away from the portal and facing along
// it.
static float portalYawFor(int dx, int dz) {
    if (dz > 0) return 0.0f;     // +z
    if (dz < 0) return 180.0f;   // -z
    if (dx > 0) return 90.0f;    // +x
    return 270.0f;               // -x
}

// Picks the spot to put an arriving player: one block clear of the portal
// plane, on a face they can actually stand on, turned to look directly
// away from the portal they just came out of.
//
// preferDx/preferDz is the direction to try first when both faces are
// usable -- normally the player's own heading, so someone who walked north
// into a portal comes out still heading north rather than being spun
// around. It is only a preference; a blocked preferred face falls through
// to the other one.
//
// Returns false when neither face is standable (a frame built flush
// against a wall on both sides, or one whose surroundings were mined out
// after it was lit), leaving the caller to fall back rather than placing
// the player inside terrain.
static bool portalExitSpot(World* w, int px, int py, int pz,
                           float preferYaw,
                           float* ox, float* oy, float* oz, float* oyaw) {
    bool alongX = portalPlaneAlongX(w, px, py, pz);

    // The two faces, as unit offsets.
    int fdx = alongX ? 0 : 1;
    int fdz = alongX ? 1 : 0;

    // Which of the two the player is already heading toward. Same yaw
    // convention as portalYawFor above.
    float rad = preferYaw * 3.14159265f / 180.0f;
    float hx = sinf(rad), hz = cosf(rad);
    float dot = (float)fdx * hx + (float)fdz * hz;

    int order[2];
    order[0] = (dot >= 0.0f) ? +1 : -1;
    order[1] = -order[0];

    // The frame's floor course sits one below the bottom interior row, so
    // walk down to the portal's own base before stepping sideways: entering
    // at head height and exiting at head height would drop the player onto
    // the frame's roof.
    int baseY = py;
    while (worldBlock(w, px, baseY - 1, pz) == BLOCK_PORTAL) baseY--;

    for (int i = 0; i < 2; i++) {
        int dx = fdx * order[i], dz = fdz * order[i];
        int ex = px + dx, ez = pz + dz;
        if (!portalStandable(w, ex, baseY, ez)) continue;
        *ox = (float)ex + 0.5f;
        *oy = (float)baseY;
        *oz = (float)ez + 0.5f;
        *oyaw = portalYawFor(dx, dz);
        return true;
    }
    return false;
}

// Land at (px, py, pz): force the destination's chunks into memory first,
// then place the entity and clear its fall state.
//
// The chunk claim is not optional on EITHER side of the trip. worldStream
// evicts every chunk outside radius R+1 of the player (chunk_cache.cpp),
// so by the time someone in the Nether steps back into the return portal,
// the Overworld chunks around their recorded return position are long
// gone. teleportToOverworld previously called nothing at all here, so it
// dropped the player into unloaded space, which is not solid (an unloaded
// chunk reads back as BLOCK_INVISIBLE_BEDROCK, which shapeOf maps to
// SHAPE_AIR in tile.cpp, producing no collision box at all), and they fell
// until the streamer caught up underneath them.
//
// Zeroing fallDistance and yd matters too: Entity::moveTo (entity.cpp)
// sets position and rotation only. It does not touch velocity or
// accumulated fall damage, so without this a player who crossed while
// falling kept both across the teleport and took the damage on landing at
// the far end.
//
// py is FEET, not eye height -- moveTo adds heightOffset itself.
static void arriveAt(World* w, Entity* e, float px, float py, float pz, float yr, float xr) {
    worldEnsureArea(w, Mth::floor(px) >> 4, Mth::floor(pz) >> 4, PORTAL_ARRIVAL_CHUNK_RADIUS);
    e->moveTo(px, py, pz, yr, xr);
    e->fallDistance = 0.0f;
    e->yd = 0.0f;
}

// --- Nether-side portal --------------------------------------------------

// Resolves this world's Nether-side anchor, searching for one the first
// time it is needed and remembering it afterwards.
//
// The search replaces the old carveSafePortalLanding, which bulldozed a
// 5x5x4 pocket with a netherrack floor at a fixed chunk-centre coordinate
// whatever happened to be there -- including straight through the middle
// of a lava sea, which is precisely the failure that made a safety
// platform feel necessary in the first place. Looking for terrain that is
// already the right shape produces a portal that sits in the landscape
// instead of one standing on a slab punched into it.
static bool resolveNetherAnchor(World* w, int* bx, int* by, int* bz) {
    if (s_anchorKnown) {
        *bx = s_anchorX; *by = s_anchorY; *bz = s_anchorZ;
        // Still claim the chunks: the anchor may have been loaded from
        // level.dat with nothing around it resident yet.
        worldEnsureArea(w, (*bx) >> 4, (*bz) >> 4, PORTAL_ARRIVAL_CHUNK_RADIUS);
        return true;
    }

    int cx = netherPortalCX(w), cz = netherPortalCZ();
    for (int i = 0; i < PORTAL_SITE_RADIUS_COUNT; i++) {
        int r = kPortalSiteRadii[i];
        worldEnsureArea(w, cx, cz, r);
        if (!netherFindPortalSite(w, cx, cz, r, bx, by, bz)) continue;
        netherPortalSetAnchor(*bx, *by, *bz);
        return true;
    }
    return false;
}

// Builds a minimal 4-wide x 5-tall obsidian frame (2x3 interior) at the
// resolved Nether-side anchor if one isn't already there. Idempotent --
// safe to call every time a player arrives, since it first checks whether
// a portal block already sits at the anchor.
//
// Returns false when no site could be found at all, so the caller can
// decline to strand the player rather than building into whatever happens
// to occupy the fallback coordinate.
static bool ensureNetherSidePortal(World* w, int* obx, int* oby, int* obz) {
    int bx, by, bz;
    if (!resolveNetherAnchor(w, &bx, &by, &bz)) return false;

    *obx = bx; *oby = by; *obz = bz;

    if (worldBlock(w, bx, by, bz) == BLOCK_PORTAL) return true; // already built

    // Frame: interior 2 wide (x) x 3 tall (y) at fixed z=bz, matching the
    // alongX=true orientation tryFindFrame/fillFrameInterior use. The site
    // search guarantees the four columns share one flat, solid, non-lava
    // surface at by-1 and that by..by+4 is clear air, so the obsidian is
    // bedded in real ground and nothing needs clearing first.
    //
    // worldSetBlockAndData, not blockPut. blockPut writes the block store
    // and nothing else (block_store.cpp): no data-nibble write, no
    // worldMarkDirty, and -- the one that actually showed -- no
    // lightOnBlockChanged. BLOCK_PORTAL emits light level 11 (tile.cpp),
    // but worldUpdateLights only drains a BFS queue that
    // lightOnBlockChanged is what fills, so an auto-built Nether frame
    // written with blockPut never seeded its own glow and sat dark while
    // player-lit Overworld portals (which go through fillFrameInterior,
    // and so through worldSetBlockAndData) lit up correctly.
    for (int dx = -1; dx <= 2; dx++) {
        for (int dy = -1; dy <= 3; dy++) {
            bool isPost = (dx == -1 || dx == 2 || dy == -1 || dy == 3);
            if (isPost) worldSetBlockAndData(w, bx + dx, by + dy, bz, BLOCK_OBSIDIAN, 0);
        }
    }
    for (int dx = 0; dx <= 1; dx++)
        for (int dy = 0; dy <= 2; dy++)
            worldSetBlockAndData(w, bx + dx, by + dy, bz, BLOCK_PORTAL, 0);

    worldNotifyNeighborsChanged(w, bx, by, bz);
    worldUpdateLights(w);
    return true;
}

// --- The two crossings ---------------------------------------------------

static bool teleportToNether(World* w, Player* p) {
    // Record the Overworld side BEFORE moving. FEET, not eye height:
    // Entity::y already includes heightOffset, and Entity::moveTo adds
    // heightOffset again on arrival, so storing the raw y made the player
    // gain about 1.6 blocks of altitude on every single round trip.
    p->setNetherReturnPosition(p->x, p->y - p->heightOffset, p->z, p->yRot, p->xRot);

    int bx, by, bz;
    if (!ensureNetherSidePortal(w, &bx, &by, &bz)) return false;

    float ex, ey, ez, eyaw;
    if (!portalExitSpot(w, bx, by, bz, p->yRot, &ex, &ey, &ez, &eyaw)) {
        // The site search requires a standable face, so this only happens
        // for an anchor loaded from an older save whose surroundings have
        // since been built over. Put the player in the frame itself rather
        // than nowhere; the re-entry latch holds until they walk clear.
        ex = (float)bx + 0.5f; ey = (float)by; ez = (float)bz + 0.5f;
        eyaw = p->yRot;
    }
    arriveAt(w, p, ex, ey, ez, eyaw, 0.0f);
    return true;
}

static bool teleportToOverworld(World* w, Player* p) {
    if (!p->hasNetherReturnPosition()) {
        // No recorded Overworld portal (e.g. someone reached the Nether
        // some other way without ever crossing from the Overworld side, on
        // this save or a previous one) -- nothing sensible to return to,
        // so this fails rather than guessing a position. Matches this
        // project's existing convention of failing closed rather than
        // teleporting somewhere arbitrary (see worldChunkIsReserved's
        // guards).
        return false;
    }

    float rx = p->netherReturnX, ry = p->netherReturnY, rz = p->netherReturnZ;

    // The recorded position is a spot inside the Overworld portal, since
    // it is where the player stood when the outbound crossing fired. The
    // chunks around it have long since been evicted by the streamer, so
    // claim them before reading any blocks there -- an unloaded chunk
    // reads as BLOCK_INVISIBLE_BEDROCK and portalPlaneAlongX would get a
    // meaningless answer from it.
    int rbx = Mth::floor(rx), rbz = Mth::floor(rz);
    worldEnsureArea(w, rbx >> 4, rbz >> 4, PORTAL_ARRIVAL_CHUNK_RADIUS);

    int rby = Mth::floor(ry);
    if (worldBlock(w, rbx, rby, rbz) == BLOCK_PORTAL) {
        float ex, ey, ez, eyaw;
        if (portalExitSpot(w, rbx, rby, rbz, p->netherReturnYRot, &ex, &ey, &ez, &eyaw)) {
            arriveAt(w, p, ex, ey, ez, eyaw, 0.0f);
            return true;
        }
    }
    // Portal gone (mined out, or an old save recorded before exit spots
    // existed): drop the player where they were, facing as they were.
    arriveAt(w, p, rx, ry, rz, p->netherReturnYRot, p->netherReturnXRot);
    return true;
}

// --- Per-tick crossing state ---------------------------------------------

// Which side is a given chunk on? worldChunkIsNether deliberately does not
// check world size or the reserved-region bound itself (see its comment in
// world.h), so worldChunkIsReserved is checked alongside it rather than
// assumed.
static bool chunkIsNetherSide(World* w, int cx, int cz) {
    return worldChunkIsReserved(w, cx, cz) && worldChunkIsNether(w, cx, cz);
}

void netherPortalEntityInside(World* w, int x, int y, int z, Entity* e) {
    if (!netherPortalsSupported(w)) return;  // see netherPortalsSupported
    if (!e || !e->isPlayer()) return;        // players only for this pass -- see nether_portal.h

    // e is guaranteed isPlayer() by the check above, and Player is the only
    // isPlayer()==true class in this codebase (see Entity::isPlayer's
    // default-false / Player's override-true split in entity.h/player.h) --
    // same cast convention already used elsewhere for g_level.player (see
    // animal/cow.cpp) despite -fno-exceptions/-fno-rtti ruling out a
    // checked dynamic_cast.
    Player* p = (Player*)e;

    // No teleport here any more, just a note that contact happened. This
    // function runs once per overlapping portal block per tick, which is
    // why it cannot own the timing; netherPortalPlayerTick below is the
    // once-per-tick half. Recording the block lets that half work out the
    // portal's orientation without having to search for it again.
    p->inPortalThisTick = true;
    p->havePortalBlock = true;
    p->portalBlockX = x; p->portalBlockY = y; p->portalBlockZ = z;
}

void netherPortalBeginForcedCrossing(Player* p) {
    if (!p) return;
    if (p->portalCharge > 0 || p->portalArrive > 0) return; // already crossing
    p->portalForced = true;
    p->havePortalBlock = false;
}

void netherPortalPlayerTick(World* w, Player* p) {
    if (!p) return;

    // Arrival fade runs down regardless of anything else.
    if (p->portalArrive > 0) p->portalArrive--;

    bool charging = p->inPortalThisTick || p->portalForced;
    p->inPortalThisTick = false;

    if (!charging) {
        // A whole tick clear of any portal releases the re-entry latch.
        p->portalLatched = false;
        // Drain the charge so a partial crossing fades back in rather than
        // holding the screen half-dark until the player commits.
        if (p->portalCharge > 0) p->portalCharge--;
        return;
    }

    if (p->portalLatched) return;   // stood in the arrival portal; wait until clear

    if (++p->portalCharge < Player::PORTAL_CHARGE_TICKS) return;

    // Fully charged: cross now, with the screen at full black.
    p->portalCharge = 0;
    p->portalLatched = true;
    p->portalForced = false;

    if (!netherPortalsSupported(w)) return;

    // Which way? From the portal block if there is one, otherwise (a
    // forced/debug crossing) from where the player is standing.
    int cx, cz;
    if (p->havePortalBlock) { cx = p->portalBlockX >> 4; cz = p->portalBlockZ >> 4; }
    else                    { cx = Mth::floor(p->x) >> 4; cz = Mth::floor(p->z) >> 4; }

    bool ok = chunkIsNetherSide(w, cx, cz) ? teleportToOverworld(w, p)
                                           : teleportToNether(w, p);

    // Only fade back in if something actually happened. A failed crossing
    // (no site found, no recorded return position) leaves the player where
    // they were with a clear screen, rather than blacking out and back for
    // a move that never took place.
    if (ok) p->portalArrive = Player::PORTAL_ARRIVE_TICKS;
    p->havePortalBlock = false;
}
