#include "world/level/tile/nether_portal.h"
#include "world/level/world.h"
#include "world/level/level.h"
#include "world/level/levelgen/features.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/entity/entity.h"
#include "world/entity/player.h"

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

// Tries to find a portal frame's interior rectangle assuming the frame's
// long axis runs along `alongX` (true = frame columns vary in z, portal
// plane faces along x; false = the mirror image). (x,y,z) is a block the
// player clicked with flint and steel -- may be obsidian (a frame post)
// or air/portal already touching one. Returns true and fills the interior
// bounds if a complete valid frame is found.
struct PortalFrame {
    bool alongX; // true: frame extends along x, obsidian posts at fixed z; false: fixed x
    int fixedCoord;      // the frame's fixed x (if !alongX) or z (if alongX)
    int loA, hiA;        // interior span along the varying horizontal axis (x if alongX, z otherwise)
    int loY, hiY;         // interior span along y
};

static bool isObsidian(World* w, int x, int y, int z) {
    return worldBlock(w, x, y, z) == BLOCK_OBSIDIAN;
}
static bool isAirOrPortal(World* w, int x, int y, int z) {
    unsigned char id = worldBlock(w, x, y, z);
    return id == BLOCK_AIR || id == BLOCK_PORTAL;
}

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

// Attempts to grow a valid frame around (aClick, yClick) on the given
// fixed coordinate/orientation. (aClick, yClick) is always where the
// player actually clicked, which per FlintAndSteelItem::useOn's own
// gating is always an obsidian post (ignition is only attempted when the
// clicked block is BLOCK_OBSIDIAN) -- so unlike a from-scratch scan, this
// starts by finding an adjacent interior cell next to the clicked post
// and grows the search from there, rather than requiring the click itself
// to be interior air (which it structurally never is, given the caller's
// gating).
static bool tryFindFrame(World* w, bool alongX, int fixedCoord, int aClick, int yClick, PortalFrame* out) {
    FrameAxis ax{ w, alongX, fixedCoord };

    if (!ax.obsidianAt(aClick, yClick)) return false; // see comment above -- caller only ever passes a post

    // The clicked post could be part of either the left/right posts or
    // the top/bottom posts of the frame (or a corner, touching both). Try
    // each of the 4 neighboring cells in turn; whichever one is real
    // frame interior (air/portal) becomes the actual search origin. This
    // also naturally handles a click on a shared corner post, since one
    // of its two interior-facing neighbors will be the real interior and
    // the other will be more obsidian (rejected by interiorOkAt).
    static const int kOffsets[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    int a0 = -1, y0 = -1;
    for (int i = 0; i < 4; i++) {
        int a = aClick + kOffsets[i][0], y = yClick + kOffsets[i][1];
        if (ax.interiorOkAt(a, y)) { a0 = a; y0 = y; break; }
    }
    if (a0 < 0) return false; // no adjacent interior cell -- not actually touching a frame's interior at all

    int aClick2 = a0, yClick2 = y0;

    // Find the interior's y-span: walk down/up from yClick2 while the
    // column at aClick2 is interior-clear.
    int loY = yClick2, hiY = yClick2;
    while (ax.interiorOkAt(aClick2, loY - 1)) { loY--; if (yClick2 - loY > PORTAL_MAX_INTERIOR_H) return false; }
    while (ax.interiorOkAt(aClick2, hiY + 1)) { hiY++; if (hiY - yClick2 > PORTAL_MAX_INTERIOR_H) return false; }
    // Floor and ceiling must be obsidian.
    if (!ax.obsidianAt(aClick2, loY - 1) || !ax.obsidianAt(aClick2, hiY + 1)) return false;
    int h = hiY - loY + 1;
    if (h < PORTAL_MIN_INTERIOR_H) return false;

    // Find the interior's horizontal span at this same height (yClick2),
    // walking left/right while clear, then confirm obsidian posts cap
    // both ends.
    int loA = aClick2, hiA = aClick2;
    while (ax.interiorOkAt(loA - 1, yClick2)) { loA--; if (aClick2 - loA > PORTAL_MAX_INTERIOR_W) return false; }
    while (ax.interiorOkAt(hiA + 1, yClick2)) { hiA++; if (hiA - aClick2 > PORTAL_MAX_INTERIOR_W) return false; }
    if (!ax.obsidianAt(loA - 1, yClick2) || !ax.obsidianAt(hiA + 1, yClick2)) return false;
    int width = hiA - loA + 1;
    if (width < PORTAL_MIN_INTERIOR_W) return false;

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

    out->alongX = alongX;
    out->fixedCoord = fixedCoord;
    out->loA = loA; out->hiA = hiA;
    out->loY = loY; out->hiY = hiY;
    return true;
}

static void fillFrameInterior(World* w, const PortalFrame& f) {
    for (int a = f.loA; a <= f.hiA; a++)
        for (int y = f.loY; y <= f.hiY; y++) {
            int x = f.alongX ? a : f.fixedCoord;
            int z = f.alongX ? f.fixedCoord : a;
            worldSetBlockAndData(w, x, y, z, BLOCK_PORTAL, 0);
        }
    // Light/mesh update once for the whole frame rather than per-block --
    // matches how firePlace's caller (tile_item.cpp) already does a
    // single update after placing, not one per affected block.
    int midA = (f.loA + f.hiA) / 2, midY = (f.loY + f.hiY) / 2;
    int mx = f.alongX ? midA : f.fixedCoord;
    int mz = f.alongX ? f.fixedCoord : midA;
    worldNotifyNeighborsChanged(w, mx, midY, mz);
    worldUpdateLights(w);
}

bool netherPortalTryIgnite(World* w, int x, int y, int z) {
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
// and Level::getCubes' synthetic boundary wall (world.cpp) only blocks
// *walking* across the seam, not a direct position set like moveTo does.

// Fixed Nether-side portal location -- same single-entry-point design as
// the existing debug teleport (see debug_teleport.cpp), and deliberately
// reused as the SAME coordinate: one canonical Nether portal for the
// whole world, matching this project's "fixed single entry point, no
// coordinate mapping" scope decision. If a portal doesn't exist there
// yet the first time it's needed, one is auto-built (see
// ensureNetherSidePortal below), same spirit as vanilla auto-building the
// answering portal on first crossing.
static const int kNetherPortalCX = WORLD_NETHER_ORIGIN_CX + WORLD_NETHER_CHUNKS / 2;
static const int kNetherPortalCZ = WORLD_NETHER_CHUNKS / 2 + 4; // offset from the debug teleport's own entry point so the two don't overlap
static const int kNetherPortalY  = 60; // matches debug_teleport.cpp's kNetherEntryY reasoning: comfortably above the lava sea

// Last Overworld portal position a player entered through, so the return
// trip from the Nether side knows where to send them back. Stored on
// Player itself now (netherReturnX/Y/Z/YRot/XRot, see player.h) and
// saved/loaded through the same NBT Player compound as bed/respawn
// position (see buildPlayerTag/level_storage.cpp) -- this used to be a
// pair of static variables here, which meant it was lost on every save/
// quit/reload; per-player storage also means this will do the right
// thing if the codebase ever grows real multiplayer, whereas a static
// here never could.

// Simple per-entity debounce: entityInside fires every tick the player's
// bounding box overlaps a portal block (see tile.cpp's call site in
// entity.cpp), so without this a single portal crossing would trigger
// dozens of teleports a second, bouncing the player back and forth
// between the two portals as their box re-enters each one immediately
// after arriving. Cooldown is in ticks-since-last-teleport, checked
// against a simple frame counter rather than wall-clock time since nothing
// else in this codebase's tile code reaches for real time here either.
static unsigned int s_lastTeleportTick = 0;
static unsigned int s_tickCounter = 0;
#define PORTAL_COOLDOWN_TICKS 40 // 2 seconds at 20 ticks/sec, matches vanilla's own portal delay ballpark

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
// fixed Nether-side location if one doesn't already exist there, then
// returns the position just in front of its portal blocks to land the
// player on. Idempotent -- safe to call every time a player arrives,
// since it first checks whether a portal block already sits there.
static void ensureNetherSidePortal(World* w) {
    int bx = kNetherPortalCX * 16 + 8, bz = kNetherPortalCZ * 16, by = kNetherPortalY;

    worldGetChunk(w, kNetherPortalCX, kNetherPortalCZ); // claim + generate real terrain here first (see chunkGenerateNether)

    if (worldBlock(w, bx, by, bz) == BLOCK_PORTAL) return; // already built

    carveSafePortalLanding(w, bx, by, bz);

    // Frame: interior 2 wide (x) x 3 tall (y) at fixed z=bz, matching the
    // alongX=true orientation tryFindFrame/fillFrameInterior use.
    for (int dx = -1; dx <= 2; dx++) {
        for (int dy = -1; dy <= 3; dy++) {
            bool isPost = (dx == -1 || dx == 2 || dy == -1 || dy == 3);
            if (isPost) blockPut(w, bx + dx, by + dy, bz, BLOCK_OBSIDIAN);
        }
    }
    for (int dx = 0; dx <= 1; dx++)
        for (int dy = 0; dy <= 2; dy++)
            blockPut(w, bx + dx, by + dy, bz, BLOCK_PORTAL);

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

    int bx = kNetherPortalCX * 16 + 8, bz = kNetherPortalCZ * 16;
    // Land just in front of the portal plane (bz - 1), not inside the
    // portal blocks themselves, so the player doesn't immediately trigger
    // another teleport back the instant they arrive (the cooldown below
    // also guards this, but landing outside the portal face is the more
    // correct fix and matches how vanilla positions the player relative
    // to the answering portal).
    e->moveTo((float)bx + 0.5f, (float)kNetherPortalY, (float)(bz - 1) + 0.5f, e->yRot, e->xRot);
}

static void teleportToOverworld(Entity* e) {
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
    e->moveTo(p->netherReturnX, p->netherReturnY, p->netherReturnZ,
             p->netherReturnYRot, p->netherReturnXRot);
}

void netherPortalEntityInside(World* w, int x, int y, int z, Entity* e) {
    if (!e || !e->isPlayer()) return; // players only for this pass -- see nether_portal.h

    s_tickCounter++;
    if (s_tickCounter - s_lastTeleportTick < PORTAL_COOLDOWN_TICKS) return;
    s_lastTeleportTick = s_tickCounter;

    // Which side is this portal on? The reserved Nether strip's chunk-x
    // range is the same test worldChunkIsNether already uses (world.h) --
    // reusing that here rather than re-deriving the boundary keeps this
    // in sync with any future change to where the strip lives.
    int cx = x >> 4, cz = z >> 4;
    bool onNetherSide = worldChunkIsNether(w, cx, cz);

    if (onNetherSide) teleportToOverworld(e);
    else              teleportToNether(w, e);
}
