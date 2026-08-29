#ifndef MCPSP_CHEAT_SPAWN_CONTENT_H
#define MCPSP_CHEAT_SPAWN_CONTENT_H

class World;

// One-shot signal from the create-world screen to the spawn-resolution
// code in render.cpp: "the seed box was 'cheat', so once real spawn
// coordinates are known, build starter content there." Deliberately not
// part of WorldList/LevelStorage's saved fields the way worldType is --
// this is meant to fire exactly once, at the moment of creation, not
// replay itself (re-teleport a portal in, re-grant items) every time an
// existing save is later reloaded. A plain global settable right before
// world creation and consumed once at first spawn is the same shape
// g_loadedFromDisk already uses in render.cpp for a similar "only true
// for this one creation" signal.
extern volatile bool g_cheatWorldPending;

// Called once, right after real spawn coordinates are known (same call
// site as placeDebugSpawnContent), only when g_cheatWorldPending was set.
// Builds and ignites a small obsidian portal frame near spawn and grants
// a diamond pickaxe and a saddle to the player's inventory. Clears
// g_cheatWorldPending itself, so callers don't need to.
void placeCheatSpawnContent(World* w, int sx, int sz, int feetY);

#endif
