#include "world/level/tile/nether_portal.h"
#include "world/level/world.h"
#include "world/level/level.h"
#include "world/level/levelgen/features.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/entity/entity.h"
#include "world/entity/player.h"
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

// Fixed Nether-side portal location -- same single-entry-point design as
// the existing debug teleport (see debug_teleport.cpp), and deliberately
// reused as the SAME coordinate: one canonical Nether portal for the
// whole world, matching this project's "fixed single entry point, no
// coordinate mapping" scope decision.
// No longer compile-time constants: the strip's X origin depends on the
// preset's overworld width (worldNetherOriginCX in world.h).
static int netherPortalCX(const World* w) { return worldNetherOriginCX(w) + WORLD_NETHER_CHUNKS / 2; }
// Offset from the debug teleport's own entry point so the two don't
// overlap. WORLD_NETHER_CHUNKS is 16 now, so +2 rather than the old +4 --
// +4 would land 4 chunks from an 8-chunk half-width, uncomfortably close
// to the strip's bedrock side wall.
static int netherPortalCZ(void) { return WORLD_NETHER_CHUNKS / 2 + 2; }

// Inside the guaranteed navigable gap of the 40-tall shell. The worst-case
// gap runs y=15..27 (floor hills top out at 14, ceiling hills bottom out
// at 28 -- see the budget check on NETHER_H in nether_gen.cpp), and the
// portal frame occupies y-1 through y+3, so 18 puts the frame at 17..21,
// clear at both ends even on the tightest column. The old value of 60 is
// now above the bedrock ceiling entirely.
#define kNetherPortalY 18

// The Nether-side frame's anchor block, derived in exactly one place so
// the builder and the teleporter cannot disagree about where it is.
//
// Both coordinates are chunk-CENTRED (+8), which is the fix for a real
// fall-to-your-death bug: kNetherPortalCZ * 16 with no +8 put the frame on
// the very first block of chunk Z 20, so the landing spot one block in
// front of it (bz - 1 = 319) fell in chunk Z *19* -- a chunk
// ensureNetherSidePortal never claimed. carveSafePortalLanding's writes
// there were silently dropped by setBlock's own !worldReady guard
// (features_common.cpp), so no netherrack floor was ever placed under the
// arrival point; and an unloaded chunk reads back as
// BLOCK_INVISIBLE_BEDROCK, which shapeOf maps to SHAPE_AIR (tile.cpp) and
// therefore produces no collision box at all. Arriving players were in
// free fall from y=60 with the nearest real surface at the floor hills'
// y=29 ceiling (NETHER_FLOOR_BASE_Y + NETHER_HILL_MAX_HEIGHT,
// nether_gen.cpp). Centring keeps the frame, the 5x5 carved pocket and the
// landing spot inside one chunk -- which is exactly why the debug
// teleport, which has always used cz * 16 + 8, never showed this.
static void netherSidePortalAnchor(const World* w, int* bx, int* by, int* bz) {
    *bx = netherPortalCX(w) * 16 + 8;
    *by = kNetherPortalY;
    *bz = netherPortalCZ() * 16 + 8;
}

// How many chunks either side of a teleport destination to force-generate
// before the player is placed there. r=1 (a 3x3 block of chunks) is
// deliberate belt-and-braces: r=0 alone is enough to give the player solid
// ground now that the landing spot is chunk-centred, but a one-chunk skirt
// means they cannot walk off the edge of their own arrival chunk into
// not-yet-streamed space in the first second after landing either.
//
// Flagging the cost honestly rather than hiding it: this is up to 9
// synchronous chunk generations on the teleport frame, which will be a
// visible hitch on the S905 stick. It is acceptable because a teleport is
// already a hard cut with no continuity to preserve, but if it measures
// worse than expected, dropping this to 0 is safe and costs only the
// walk-off-the-edge protection.
#define PORTAL_ARRIVAL_CHUNK_RADIUS 1

// Land safely at (px, py, pz): force the destination's chunks into memory
// first, then place the entity and clear its fall state.
//
// The chunk claim is not optional on EITHER side of the trip. worldStream
// evicts every chunk outside radius R+1 of the player (chunk_cache.cpp),
// so by the time someone in the Nether steps back into the return portal,
// the Overworld chunks around their recorded return position are long
// gone. teleportToOverworld previously called nothing at all here, so it
// dropped the player into unloaded space, which -- per the
// BLOCK_INVISIBLE_BEDROCK / SHAPE_AIR chain described above -- is not
// solid, and they fell until the streamer caught up underneath them.
//
// Zeroing fallDistance and yd matters too: Entity::moveTo (entity.cpp)
// sets position and rotation only. It does not touch velocity or
// accumulated fall damage, so without this a player who crossed while
// falling kept both across the teleport and took the damage on landing at
// the far end.
static void arriveAt(World* w, Entity* e, float px, float py, float pz, float yr, float xr) {
    worldEnsureArea(w, Mth::floor(px) >> 4, Mth::floor(pz) >> 4, PORTAL_ARRIVAL_CHUNK_RADIUS);
    e->moveTo(px, py, pz, yr, xr);
    e->fallDistance = 0.0f;
    e->yd = 0.0f;
}

static void carveSafePortalLanding(World* w, int bx, int by, int bz) {
    // Same reasoning as debug_teleport.cpp's carveSafePlatform: the
    // landing spot needs a guaranteed-safe pocket regardless of whatever
    // real terrain happens to already be there, and a netherrack floor
    // rather than stone so it looks right sitting inside real Nether
    // terrain. Runs before the frame is built (see ensureNetherSidePortal
    // below), so there's nothing here yet worth preserving -- the frame's
    // obsidian posts and portal blocks get written on top of this
    // clearing immediately afterward, overwriting whatever this leaves in
    // their footprint.
    for (int dx = -2; dx <= 2; dx++)
    for (int dz = -2; dz <= 2; dz++) {
        setBlock(w, bx + dx, by - 1, bz + dz, BLOCK_NETHERRACK);
        for (int dy = 0; dy < 4; dy++)
            setBlock(w, bx + dx, by + dy, bz + dz, BLOCK_AIR);
    }
}

// Builds a minimal 4-wide x 5-tall obsidian frame (2x3 interior) at the
// fixed Nether-side location if one doesn't already exist there.
// Idempotent -- safe to call every time a player arrives, since it first
// checks whether a portal block already sits there.
static void ensureNetherSidePortal(World* w) {
    int bx, by, bz;
    netherSidePortalAnchor(w, &bx, &by, &bz);

    // Claim the arrival chunk and its skirt up front, so every write below
    // lands in a ready chunk instead of being dropped by setBlock's
    // worldReady guard.
    worldEnsureArea(w, netherPortalCX(w), netherPortalCZ(), PORTAL_ARRIVAL_CHUNK_RADIUS);

    if (worldBlock(w, bx, by, bz) == BLOCK_PORTAL) return; // already built

    carveSafePortalLanding(w, bx, by, bz);

    // Frame: interior 2 wide (x) x 3 tall (y) at fixed z=bz, matching the
    // alongX=true orientation tryFindFrame/fillFrameInterior use.
    //
    // worldSetBlockAndData, not blockPut. blockPut writes the block store
    // and nothing else (block_store.cpp): no data-nibble write, no
    // worldMarkDirty, and -- the one that actually showed -- no
    // lightOnBlockChanged. BLOCK_PORTAL emits light level 11 (tile.cpp),
    // but worldUpdateLights only drains a BFS queue that
    // lightOnBlockChanged is what fills, so the auto-built Nether frame
    // never seeded its own glow and sat dark while player-lit Overworld
    // portals (which go through fillFrameInterior, and so through
    // worldSetBlockAndData) lit up correctly. The missing dirty flag was
    // masked only by carveSafePortalLanding happening to dirty the same
    // sections a moment earlier via setBlock.
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
}

static void teleportToNether(World* w, Entity* e) {
    // e is guaranteed isPlayer() by the caller (netherPortalEntityInside),
    // and Player is the only isPlayer()==true class in this codebase (see
    // Entity::isPlayer's default-false / Player's override-true split in
    // entity.h/player.h) -- same cast convention already used elsewhere
    // for g_level.player (see animal/cow.cpp) despite -fno-exceptions/
    // -fno-rtti ruling out a checked dynamic_cast.
    Player* p = (Player*)e;
    p->setNetherReturnPosition(e->x, e->y, e->z, e->yRot, e->xRot);

    ensureNetherSidePortal(w);

    int bx, by, bz;
    netherSidePortalAnchor(w, &bx, &by, &bz);
    // Land just in front of the portal plane (bz - 1), not inside the
    // portal blocks themselves. The re-entry latch would hold either way,
    // but landing outside the portal face means the latch releases on the
    // very next tick, so the player can turn around and go straight back
    // if they want to.
    arriveAt(w, e, (float)bx + 0.5f, (float)by, (float)(bz - 1) + 0.5f, e->yRot, e->xRot);
}

static void teleportToOverworld(World* w, Entity* e) {
    Player* p = (Player*)e; // see teleportToNether's comment on this cast
    if (!p->hasNetherReturnPosition()) {
        // No recorded Overworld portal (e.g. someone reached the Nether
        // portal some other way without ever crossing from the Overworld
        // side, on this save or a previous one) -- nothing sensible to
        // return to, so this is a no-op rather than guessing a position.
        // Matches this project's existing convention of failing closed
        // rather than teleporting somewhere arbitrary (see
        // worldChunkIsReserved's guards).
        return;
    }
    // The recorded position is, by construction, a spot inside the
    // Overworld portal (it is where the player was standing when the
    // outbound teleport fired), so the player arrives inside portal
    // blocks. That is fine and matches vanilla: the latch below holds
    // until they step clear of the portal, at which point it releases and
    // the portal works again.
    arriveAt(w, e, p->netherReturnX, p->netherReturnY, p->netherReturnZ,
             p->netherReturnYRot, p->netherReturnXRot);
}

void netherPortalEntityInside(World* w, int x, int y, int z, Entity* e) {
    if (!netherPortalsSupported(w)) return;  // see netherPortalsSupported
    if (!e || !e->isPlayer()) return;        // players only for this pass -- see nether_portal.h

    Player* p = (Player*)e;

    // Re-entry latch, replacing the old tick-counter cooldown. See the
    // comment on Player::inPortalThisTick in player.h for why a counter
    // incremented here could never measure time: this function runs once
    // per overlapping portal block per tick, not once per tick.
    p->inPortalThisTick = true;
    if (p->portalLatched) return;
    p->portalLatched = true;

    // Which side is this portal on? worldChunkIsNether deliberately does
    // not check world size or the reserved-region bound itself (see its
    // comment in world.h), so worldChunkIsReserved is checked alongside it
    // rather than assumed -- netherPortalsSupported above has already
    // established that this world HAS a reserved region, and this
    // establishes that this particular portal is inside it.
    int cx = x >> 4, cz = z >> 4;
    bool onNetherSide = worldChunkIsReserved(w, cx, cz) && worldChunkIsNether(w, cx, cz);

    if (onNetherSide) teleportToOverworld(w, e);
    else              teleportToNether(w, e);
}
