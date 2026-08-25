#include "world/item/fishing_rod_item.h"
#include "world/entity/fishing_bobber.h"
#include "world/entity/local_player.h"
#include "world/entity/item_entity.h"
#include "world/level/level.h"
#include "world/level/levelgen/Random.h"
#include "world/inventory/inventory.h"
#include "world/item/item_instance.h"
#include "client/player/player_state.h"
#include "util/mth.h"
#include <cmath>
#include <pspkernel.h>

// Id, never a pointer: Level::tickEntities deletes removed entities in
// place, so a cached FishingBobber* would dangle the moment the bobber
// timed out, and 0 is a valid "no cast" id because Level::getEntity
// rejects it outright.
static int s_bobberId = 0;

// This file has no Entity subclass of its own -- it's plain functions --
// and Entity::sharedRandom is declared `protected:` in entity.h, so it is
// only reachable from inside an Entity (or subclass) method, not from a
// free function here. FishingBobber does derive from Entity and could
// reach it internally, but the loot roll and sound-pitch jitter below
// happen entirely in fishingReel()/fishingCast(), not in the bobber's own
// code, so borrowing it there isn't an option either without adding
// friend declarations or plumbing a Random reference through the whole
// call chain for two functions' worth of dice rolls.
//
// Same pattern Tile::popResource already uses for exactly this situation
// (tile_drops.cpp): a private, function-local Random seeded once from the
// system clock. This is gameplay randomness (what you catch, how a splash
// sounds), not world generation, so it doesn't need to be seeded from or
// reproducible against the world seed the way terrain generation is.
static Random& fishingRandom() {
    static Random rng((long)sceKernelGetSystemTimeLow());
    return rng;
}

// Beyond this the line snaps. Also the reason fishingTick has to run every
// tick rather than only while the rod is held -- a player who swaps to a
// pickaxe and sprints off would otherwise tow an invisible bobber around.
#define FISHING_MAX_DISTANCE 32.0f

static FishingBobber* activeBobber() {
    if (!s_bobberId) return 0;
    Entity* e = g_level.getEntity(s_bobberId);
    if (!e) { s_bobberId = 0; return 0; }
    return (FishingBobber*)e;
}

static void cutLine() {
    FishingBobber* b = activeBobber();
    if (b) b->remove();
    s_bobberId = 0;
}

bool fishingLineIsOut() { return activeBobber() != 0; }

// --- Loot ---------------------------------------------------------------
// Weights are relative within each pool; the pool itself is picked by the
// percentages below. Roughly vanilla's unenchanted split (85/10/5), with
// the contents cut down to items this codebase actually has.
//
// Everything about what comes out of the water is in this one block on
// purpose -- retuning it should never mean touching the cast/reel logic.
#define LOOT_TREASURE_PCT 5
#define LOOT_JUNK_PCT     10   // the remainder, 85%, is fish

struct LootEntry { short id; short data; int weight; };

static const LootEntry kFish[] = {
    { ITEM_FISH_RAW, 0, 100 },
};

static const LootEntry kJunk[] = {
    { ITEM_STICK,  0, 25 },
    { ITEM_STRING, 0, 25 },
    { ITEM_BOWL,   0, 20 },
    { ITEM_BONE,   0, 20 },
    { ITEM_LEATHER,0, 10 },
};

// No enchanting in this codebase, so vanilla's enchanted book and enchanted
// rod have nothing to offer. A plain diamond stands in as the jackpot
// instead -- deliberately generous, and the first number to lower if
// fishing turns out to trivialise mining.
static const LootEntry kTreasure[] = {
    { ITEM_BOW,          0, 35 },
    { ITEM_FISHING_ROD,  0, 35 },
    { ITEM_BOOK,         0, 20 },
    { ITEM_DIAMOND,      0, 10 },
};

static ItemInstance pickFrom(const LootEntry* table, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) total += table[i].weight;
    int roll = fishingRandom().nextInt(total);
    for (int i = 0; i < n; i++) {
        roll -= table[i].weight;
        if (roll < 0) return ItemInstance(table[i].id, 1, table[i].data);
    }
    return ItemInstance(table[n - 1].id, 1, table[n - 1].data);
}

static ItemInstance rollCatch() {
    int roll = fishingRandom().nextInt(100);
    if (roll < LOOT_TREASURE_PCT)
        return pickFrom(kTreasure, (int)(sizeof(kTreasure) / sizeof(kTreasure[0])));
    if (roll < LOOT_TREASURE_PCT + LOOT_JUNK_PCT)
        return pickFrom(kJunk, (int)(sizeof(kJunk) / sizeof(kJunk[0])));
    return pickFrom(kFish, (int)(sizeof(kFish) / sizeof(kFish[0])));
}

// --- Cast / reel --------------------------------------------------------

void fishingCast() {
    if (fishingLineIsOut()) return;
    LocalPlayer* p = g_level.player;
    if (!p || !p->isAlive()) return;

    FishingBobber* b = new FishingBobber(&g_level, p->x, p->y + p->getHeadHeight(), p->z,
                                         p->yRot, p->xRot);
    g_level.addEntity(b);
    s_bobberId = b->entityId;
    g_level.playSound(p, "random.bow", 0.5f,
                      0.4f / (fishingRandom().nextFloat() * 0.4f + 0.8f));
}

bool fishingReel() {
    FishingBobber* b = activeBobber();
    if (!b) return false;

    bool caught = b->canCatch();
    float bx = b->x, by = b->y, bz = b->z;
    cutLine();

    LocalPlayer* p = g_level.player;
    if (!p) return false;

    if (!caught) {
        g_level.playSound(p, "random.pop", 0.3f, 0.8f);
        return false;
    }

    // The catch flies to the player rather than being placed straight into
    // the inventory, so a full inventory drops it at your feet instead of
    // silently eating it, and so the reel-in reads visually.
    ItemInstance loot = rollCatch();
    ItemEntity* drop = new ItemEntity(&g_level, bx, by, bz, loot);
    float dx = p->x - bx, dy = (p->y - PLAYER_EYE) - by, dz = p->z - bz;
    float dist = Mth::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 0.1f) dist = 0.1f;
    const float pull = 0.12f;
    drop->xd = dx * pull;
    drop->yd = dy * pull + dist * 0.08f; // arc upward so it clears the water
    drop->zd = dz * pull;
    g_level.addEntity(drop);

    g_level.playSound(p, "random.pop", 0.6f,
                      fishingRandom().nextFloat() * 0.4f + 0.8f);

    if (!p->inventory->isCreative()) p->inventory->hurtSelected(1);
    return true;
}

void fishingTick() {
    FishingBobber* b = activeBobber();
    if (!b) return;

    LocalPlayer* p = g_level.player;
    if (!p || !p->isAlive()) { cutLine(); return; }

    ItemInstance* sel = p->inventory->getSelected();
    if (!sel || sel->id != ITEM_FISHING_ROD) { cutLine(); return; }

    float dx = p->x - b->x, dy = p->y - b->y, dz = p->z - b->z;
    if (dx * dx + dy * dy + dz * dz > FISHING_MAX_DISTANCE * FISHING_MAX_DISTANCE)
        cutLine();
}

const char* fishingUseLabel() {
    LocalPlayer* p = g_level.player;
    if (!p) return 0;
    ItemInstance* sel = p->inventory->getSelected();
    if (!sel || sel->id != ITEM_FISHING_ROD) return 0;
    return fishingLineIsOut() ? "Reel in" : "Cast";
}
