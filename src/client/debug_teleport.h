#ifndef MCPSP_CLIENT_DEBUG_TELEPORT_H
#define MCPSP_CLIENT_DEBUG_TELEPORT_H

struct World;
class LocalPlayer;

// See debug_teleport.cpp -- TEMPORARY dev/testing scaffolding, delete once
// real Nether portals exist. Toggles the player between wherever they were
// standing and a safe platform inside the reserved Nether region. No-op on
// any world that isn't the 1024x1024 preset.
void debugToggleNetherTeleport(World* w, LocalPlayer* player);

#endif
