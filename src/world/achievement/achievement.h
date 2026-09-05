#ifndef MCPSP_WORLD_ACHIEVEMENT_ACHIEVEMENT_H
#define MCPSP_WORLD_ACHIEVEMENT_ACHIEVEMENT_H

// Lightweight, event-driven achievement system.
//
// Architecture (per the design plan): gameplay systems report small events
// to AchievementManager, which checks only the achievements relevant to
// that event and unlocks/saves/notifies as needed. No per-frame scan of
// every achievement, no persistent per-block bookkeeping.
//
// Storage: an unlocked bitmask plus a small number of progress counters,
// written as flat binary inside the active world's own save directory
// (saves/<name>/achievements.dat), via the same fopen/fwrite convention
// options.txt's global settings already use, just at a per-world path
// instead of the shared install directory. Achievement progress is
// per-world here (deliberately -- see achievementsInit's comment), not a
// player-profile concept shared across every world.

#define ACHV_MAX 40

enum AchievementId {
    ACHV_GETTING_STARTED = 0,  // Craft your first tool
    ACHV_STONE_AGE,            // Obtain stone
    ACHV_GETTING_UPGRADE,      // Craft a better pickaxe
    ACHV_DIAMONDS,             // Obtain a diamond
    ACHV_RETURN_TO_SENDER,     // Kill a Ghast (reserved; no Ghast yet -- see achievement.cpp)

    ACHV_ADVENTURER,           // Visit 5 biomes
    ACHV_EXPLORER,             // Visit 10 biomes
    ACHV_DEEP_EXPLORER,        // Reach a specified depth
    ACHV_NETHER_EXPLORER,      // Visit all Nether biomes
    ACHV_VILLAGE_DISCOVERER,   // Find a village
    ACHV_DUNGEON_DELVER,       // Find a dungeon (reserved; no dungeon structure yet)

    ACHV_BUILDER,              // Place 100 blocks
    ACHV_ARCHITECT,            // Place 1,000 blocks
    ACHV_MASTER_BUILDER,       // Place 10,000 blocks
    ACHV_HOME_SWEET_HOME,      // Sleep in a bed
    ACHV_TOWN_BUILDER,         // Build near a village (reserved; needs proximity-while-placing hook)

    ACHV_FIRST_BLOOD,          // Kill your first hostile mob
    ACHV_MONSTER_HUNTER,       // Kill 100 hostile mobs
    ACHV_SNIPER,               // Kill a mob from long range
    ACHV_SLAYER,               // Kill every basic hostile mob

    ACHV_MINER,                // Mine 100 blocks
    ACHV_DEEP_MINER,           // Mine at great depth
    ACHV_TREASURE_HUNTER,      // Find a rare resource
    ACHV_DIAMOND_MINER,        // Mine your first diamond

    ACHV_VILLAGE_HERO,         // Complete a village-related objective (reserved; no objective system yet)
    ACHV_FARMER,                // Harvest crops
    ACHV_TRADER,                // Trade with a villager (reserved; no villagers yet)

    ACHV_INTO_THE_FIRE,        // Enter the Nether
    ACHV_FORTRESS_FOUND,       // Discover a Nether Fortress (reserved; no fortress structure yet)
    ACHV_BLAZE_HUNTER,         // Kill a Blaze (reserved; no Blaze yet)
    ACHV_NETHER_TREASURE,      // Open a fortress chest (reserved; no fortress structure yet)
    ACHV_RETURN_FROM_HELL,     // Return to the Overworld

    ACHV_COUNT
};

enum AchievementCategory {
    ACHV_CAT_PROGRESSION = 0,
    ACHV_CAT_EXPLORATION,
    ACHV_CAT_BUILDING,
    ACHV_CAT_COMBAT,
    ACHV_CAT_MINING,
    ACHV_CAT_VILLAGE,
    ACHV_CAT_NETHER,
    ACHV_CAT_COUNT
};

struct AchievementDef {
    AchievementId    id;
    AchievementCategory category;
    const char* name;
    const char* description;
    // A locked achievement whose unlock condition cannot currently be
    // reached (no Ghast/Blaze/dungeon/fortress/villager in the game yet)
    // is still listed -- so the menu reads as a genuine roadmap -- but is
    // flagged so the menu can visually mark it "Not yet available" instead
    // of implying it is a bugged, reachable-but-broken entry.
    bool implemented;
};

const AchievementDef* achievementDef(AchievementId id);
const char* achievementCategoryName(AchievementCategory c);

// --- Events ---------------------------------------------------------
//
// Each call is cheap (array scan over at most a handful of relevant
// achievements, not all ACHV_COUNT) and safe to call every time the
// underlying gameplay action happens -- callers do not need to
// pre-filter or debounce.

void achievementsInit();          // load save file; call once at startup
void achievementsShutdown();      // flush any pending save

void achvOnBlockMined(unsigned char blockId, int y);
void achvOnBlockPlaced(unsigned char blockId);
void achvOnMobKilled(int entityTypeId, float killDistance);
// Narrow, purpose-built hook rather than folding this into
// achvOnMobKilled: "killed by a fireball the player personally deflected"
// isn't expressible from entityTypeId + killDistance, and stretching the
// generic hostile-kill signature to carry a one-achievement-only bit of
// context would compromise its own deliberately narrow scope (see
// isHostileEntityType's comment in achievement.cpp). Ghast::die() calls
// achvOnMobKilled as usual for the shared hostile-kill counters, then
// this separately, only when the fireball that killed it had actually
// been hit back by the player -- matching vanilla's real "Return to
// Sender" condition (killed BY ITS OWN deflected fireball), not just
// "a ghast died somehow while a fireball was involved."
void achvOnGhastReturnToSender();
void achvOnItemCrafted(short itemId);
void achvOnItemObtained(short itemId);
void achvOnBiomeEntered(int biomeId);          // overworld BiomeId
void achvOnNetherBiomeEntered(int netherBiomeId);
void achvOnNetherEntered();
void achvOnOverworldReturned();
void achvOnVillageDiscovered();
void achvOnSlept();
void achvOnCropHarvested();

// --- Query / persistence --------------------------------------------

bool achievementUnlocked(AchievementId id);
// Progress counters exposed for the menu (e.g. "37 / 100 blocks mined").
// Returns -1 if the achievement has no counter (binary unlock only).
int  achievementProgress(AchievementId id);
int  achievementProgressTarget(AchievementId id);

int  achievementsUnlockedCount();

void achievementsSave(); // normally automatic on unlock; exposed for a
                          // manual "sync now" style call if ever needed

// --- Notification queue ----------------------------------------------
//
// The HUD toast (hud.cpp) polls this once per frame. A small queue (not
// just "the last one") so two achievements unlocking on the same tick
// (e.g. mining a diamond triggers both Diamonds! and Diamond Miner) both
// get shown in turn rather than one silently overwriting the other.

bool achievementsPollNotification(char* outName, int cap);

#endif
