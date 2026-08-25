#ifndef MCPSP_CLIENT_DEBUG_TELEPORT_H
#define MCPSP_CLIENT_DEBUG_TELEPORT_H

struct World;
class LocalPlayer;

// See debug_teleport.cpp -- TEMPORARY dev/testing scaffolding, delete once
// portals are no longer the thing under test. Starts the same Nether
// crossing that standing in a lit portal starts (fade to black, arrival
// stepped clear of the frame facing away from it), choosing its direction
// from whichever side the player is currently on. No-op on a world with no
// reserved region.
void debugToggleNetherTeleport(World* w, LocalPlayer* player);

#endif
