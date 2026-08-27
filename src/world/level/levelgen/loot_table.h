#ifndef MCPSP_LOOT_TABLE_H
#define MCPSP_LOOT_TABLE_H

class Random;

// Reusable weighted-random loot system shared by every structure that
// spawns a filled chest (villages, dungeons, and later fortresses/temples).
//
// Design notes:
//  - Tables are plain static arrays of LootEntry, so adding a new table or
//    a new drop is a one-line data change, not new code.
//  - Rolls are driven entirely by a caller-supplied Random, so a chest's
//    contents are deterministic from the world seed + chunk position the
//    same way terrain and structures already are -- no use of rand().
//  - Slot placement is randomized within the container rather than filling
//    from slot 0, so chests do not all look identical at a glance.
//  - Everything is bounded: a fixed number of rolls per chest, a fixed max
//    stack per entry. No dynamic allocation beyond what ItemInstance/
//    FillingContainer already do.

enum LootTableId {
    LOOT_TABLE_VILLAGE = 0,
    LOOT_TABLE_DUNGEON,
    LOOT_TABLE_NETHER_FORTRESS,
    LOOT_TABLE_COUNT
};

// Fills the chest tile entity at (x,y,z) with rolled loot from the given
// table. Creates the chest's tile entity if one is not already present
// (mirrors ChestTile::use's lazy-creation pattern). Safe to call on a
// freshly-placed BLOCK_CHEST during world generation.
//
// rng should be seeded once per structure (not per chest) by the caller so
// that generation order does not perturb unrelated rolls -- see
// lootRollTable for the seeding convention used by village/dungeon gen.
void lootFillChest(int x, int y, int z, LootTableId table, Random& rng);

#endif
