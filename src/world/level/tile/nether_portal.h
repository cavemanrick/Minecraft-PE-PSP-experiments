#ifndef NETHER_PORTAL_H__
#define NETHER_PORTAL_H__

struct World;
class Player;
class Entity;

// Attempts to light a Nether portal anchored at the obsidian frame touching
// (x,y,z) on the given face -- called from FlintAndSteelItem::useOn (see
// tile_item.cpp) the same way it already handles BLOCK_TNT as a special
// case. Searches for a valid vanilla-shaped rectangular obsidian frame
// (min interior 2 wide x 3 tall, either X-aligned or Z-aligned) touching
// the clicked position, and if found, fills its interior with BLOCK_PORTAL
// and returns true. Returns false (does nothing) if no valid frame is
// found, exactly like flint and steel already does when fireMayPlace
// fails on a normal block.
bool netherPortalTryIgnite(World* w, int x, int y, int z);

// Called from BLOCK_PORTAL's entityInside (see tile.cpp) every tick an
// entity's bounding box overlaps a portal block. Only acts on players
// (isPlayer()) -- see the design note in nether_portal.cpp for why mobs
// are excluded for this first pass. Handles both directions (Overworld other
// side of the reserved-region boundary in world.h). Debounced internally
// so standing in a portal doesn't re-teleport every tick.
void netherPortalEntityInside(World* w, int x, int y, int z, Entity* e);

// Consumes one tick of portal state for the local player: advances or
// drains the crossing charge, fires the teleport when the charge is full,
// and counts down the arrival fade. Must be called exactly once per tick,
// immediately after travel() -- travel() -> move() is what runs the
// block-overlap loop that calls netherPortalEntityInside, so anything
// earlier reads a stale flag and anything after an early return never runs
// at all. See the block comment on Player::inPortalThisTick.
void netherPortalPlayerTick(World* w, Player* p);

// Starts a crossing without a portal, for the debug teleport (see
// debug_teleport.cpp). Direction is decided from where the player
// currently is, exactly as it is for a real portal, and the crossing then
// runs through the identical fade/arrival path -- the dev shortcut and the
// real thing differ only in what triggers them. Harmless to call twice;
// a crossing already in progress is left alone.
void netherPortalBeginForcedCrossing(Player* p);

// --- Nether-side portal anchor -------------------------------------------
// Where this world's single Nether-side portal stands. Chosen once, by
// searching the generated terrain for a flat, lava-free spot with room for
// a frame (netherFindPortalSite in nether_gen.h), and then remembered --
// it cannot simply be re-derived on demand, because once the frame exists
// the site no longer looks like an empty site to the search, and a second
// search would reject its own portal and wander off to build another one
// somewhere else.
//
// Persisted in level.dat alongside the other world-scoped positions (see
// level_storage.cpp). netherPortalResetAnchor must be called whenever a
// different world is about to be loaded or created, so a stale anchor from
// the previous save cannot leak across.
bool netherPortalAnchorKnown(void);
void netherPortalGetAnchor(int* x, int* y, int* z);
void netherPortalSetAnchor(int x, int y, int z);
void netherPortalResetAnchor(void);

#endif
