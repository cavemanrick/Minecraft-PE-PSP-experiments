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

#endif
