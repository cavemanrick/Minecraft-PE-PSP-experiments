#include "world/level/levelgen/loot_table.h"
#include "world/level/levelgen/Random.h"
#include "world/level/level.h"
#include "world/level/chunk/chunk.h"
#include "world/level/tile/entity/chest_tile_entity.h"
#include "world/item/item.h"
#include "world/item/item_instance.h"

// --- Table data -------------------------------------------------------
//
// Each entry is one possible drop: an item/block id, a data value (for
// items that carry one, e.g. none of the current entries do), a min/max
// stack count, and a weight. Weight is relative within its own table only
// -- LOOT_TABLE_VILLAGE and LOOT_TABLE_DUNGEON do not share a weight
// space. rarityFloor marks entries that should never roll more than once
// per chest (rare-item slots), enforced by lootRollTable below.

struct LootEntry {
    short id;
    short data;
    unsigned char minCount;
    unsigned char maxCount;
    unsigned char weight;
    bool rare;
};

// Village chest: food, farming basics, a little early-game metal, and an
// occasional rare item. Matches the roadmap's "bread, wheat, coal, iron,
// occasional rare item" spec.
static const LootEntry kVillageLoot[] = {
    { ITEM_BREAD,        0, 1, 3, 30, false },
    { ITEM_WHEAT,        0, 2, 5, 25, false },
    { ITEM_SEEDS_WHEAT,  0, 2, 6, 20, false },
    { ITEM_COAL,         0, 1, 4, 18, false },
    { ITEM_IRON_INGOT,   0, 1, 2, 10, false },
    { ITEM_STRING,       0, 1, 3, 12, false },
    { ITEM_APPLE,        0, 1, 2, 14, false },
    { ITEM_LEATHER,      0, 1, 3, 10, false },
    { ITEM_FLINT,        0, 1, 3,  8, false },
    { ITEM_GOLD_INGOT,   0, 1, 1,  4, true  },
    { ITEM_DIAMOND,      0, 1, 1,  1, true  },
    { ITEM_SADDLE,       0, 1, 1,  2, true  },
};

// Dungeon chest: combat/utility supplies plus a rarer high-value item.
// Matches the roadmap's "arrows, iron, gold, food, rare item" spec.
static const LootEntry kDungeonLoot[] = {
    { ITEM_ARROW,         0, 2, 7, 25, false },
    { ITEM_IRON_INGOT,    0, 1, 4, 18, false },
    { ITEM_GOLD_INGOT,    0, 1, 3, 14, false },
    { ITEM_BREAD,         0, 1, 3, 16, false },
    { ITEM_PORKCHOP_RAW,  0, 1, 3, 12, false },
    { ITEM_GUNPOWDER,     0, 1, 4, 12, false },
    { ITEM_STRING,        0, 1, 4, 12, false },
    { ITEM_BONE,          0, 2, 5, 14, false },
    { ITEM_SADDLE,        0, 1, 1,  5, true  },
    { ITEM_DIAMOND,       0, 1, 2,  3, true  },
};

struct LootTable {
    const LootEntry* entries;
    int count;
    unsigned char minRolls, maxRolls;
};

static const LootTable kTables[LOOT_TABLE_COUNT] = {
    { kVillageLoot, sizeof(kVillageLoot) / sizeof(kVillageLoot[0]), 2, 4 },
    { kDungeonLoot, sizeof(kDungeonLoot) / sizeof(kDungeonLoot[0]), 3, 5 },
};

// --- Rolling ------------------------------------------------------------

static int totalWeight(const LootTable& t) {
    int sum = 0;
    for (int i = 0; i < t.count; ++i) sum += t.entries[i].weight;
    return sum;
}

static const LootEntry& pickEntry(const LootTable& t, Random& rng, int sumWeight) {
    int roll = rng.nextInt(sumWeight);
    for (int i = 0; i < t.count; ++i) {
        roll -= t.entries[i].weight;
        if (roll < 0) return t.entries[i];
    }
    return t.entries[t.count - 1]; // unreachable in practice; safe fallback
}

void lootFillChest(int x, int y, int z, LootTableId table, Random& rng) {
    if (table < 0 || table >= LOOT_TABLE_COUNT) return;

    TileEntity* te = g_level.getTileEntity(x, y, z);
    if (!te || te->type != TE_CHEST) {
        ChestTileEntity* fresh = new ChestTileEntity();
        g_level.setTileEntity(x, y, z, fresh);
        te = fresh;
    }
    if (!te || te->type != TE_CHEST) return; // placement failed; nothing to fill
    ChestTileEntity* chest = (ChestTileEntity*)te;

    const LootTable& t = kTables[table];
    int sumWeight = totalWeight(t);
    if (sumWeight <= 0) return;

    int size = chest->getContainerSize(); // 27, or 54 if already paired
    int rolls = t.minRolls + rng.nextInt(t.maxRolls - t.minRolls + 1);

    bool rareUsed[64];
    for (int i = 0; i < 64; ++i) rareUsed[i] = false;

    for (int r = 0; r < rolls; ++r) {
        const LootEntry& e = pickEntry(t, rng, sumWeight);

        // Rare entries are capped at one per chest. If this rare slot was
        // already used this chest, just skip the roll rather than retry --
        // retrying could loop indefinitely if every rare entry in a small
        // table has already hit its cap, and a slightly emptier chest on
        // an unlucky roll is a fine outcome.
        int idx = (int)(&e - t.entries);
        if (e.rare) {
            if (idx < 64 && rareUsed[idx]) continue;
            if (idx < 64) rareUsed[idx] = true;
        }

        int span = (int)e.maxCount - (int)e.minCount;
        int count = e.minCount + (span > 0 ? rng.nextInt(span + 1) : 0);
        if (count <= 0) continue;

        // Random empty slot rather than sequential fill, so chests read as
        // hand-placed rather than machine-stamped. Bounded to a handful of
        // tries: an almost-full chest simply keeps whatever it already has
        // rather than spending unbounded time hunting for the last slot.
        bool placed = false;
        for (int tries = 0; tries < 8 && !placed; ++tries) {
            int slot = rng.nextInt(size);
            if (chest->getItem(slot) != 0) continue;
            // Container storage caps at 254 per FillingContainer, but a
            // realistic single-stack loot count never approaches that, so
            // no extra clamp is needed here beyond each entry's own max.
            chest->container.setItem(slot, new ItemInstance((short)e.id, (short)count, (short)e.data));
            placed = true;
        }
        // If no empty slot was found within the try budget, the roll is
        // simply dropped -- the chest is presumably already well-stocked.
    }
}
