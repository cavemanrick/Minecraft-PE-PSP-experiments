#include "world/level/world.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "world/level/tile/nether_portal.h"
#include "client/debug_teleport.h"

// TEMPORARY dev/testing scaffolding: lets you cross into the reserved
// Nether region (see WORLD_NETHER_*/worldChunkIsReserved in world.h)
// without having to build and light a portal first. Meant to be deleted
// once portals are no longer the thing under test -- everything relevant
// lives in this one file and its one call site in main.cpp for exactly
// that reason.
//
// It no longer does any teleporting of its own. It used to keep a private
// return position, a private entry coordinate one chunk over from the real
// portal's, and its own carveSafePlatform that stamped a netherrack slab
// into whatever terrain it landed on -- which meant the dev shortcut and
// the real portal exercised almost none of the same code, and testing the
// shortcut told you nothing about whether the portal worked.
//
// Now it just starts the same crossing a portal starts: same fade to
// black, same site search, same arrival stepped clear of the frame and
// turned to face away from it, same persisted return position. The only
// difference between the two is what triggers them.
//
// Only does anything on a world that actually has a reserved region --
// silently a no-op otherwise, so it can never misplace a player into real
// overworld terrain on a legacy infinite world.
void debugToggleNetherTeleport(World* w, LocalPlayer* player) {
    if (!player || !w) return;
    if (!worldHasReservedRegions(w)) return;

    // Direction is decided inside the crossing, from where the player is
    // standing when the charge completes -- so this single entry point
    // toggles, exactly as the old two-branch version did, without needing
    // to know which side it is on.
    netherPortalBeginForcedCrossing(player);
}
