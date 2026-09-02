#include "world/achievement/achievement.h"

#include "world/level/chunk/chunk.h"
#include "world/level/world.h"
#include "world/item/item.h"
#include "world/entity/entity_types.h"
#include "world/level/storage/level_storage.h"
#include "platform/path.h"

#include <cstdio>
#include <cstring>

// --- Static achievement table -----------------------------------------
//
// Order must match AchievementId in the header. Kept as one flat table
// (rather than per-category arrays) so bitmask index == AchievementId
// directly, with no lookup indirection needed anywhere else in the file.

static const AchievementDef kDefs[ACHV_COUNT] = {
    { ACHV_GETTING_STARTED,    ACHV_CAT_PROGRESSION, "Getting Started",     "Craft your first tool", true },
    { ACHV_STONE_AGE,          ACHV_CAT_PROGRESSION, "Stone Age",           "Obtain stone", true },
    { ACHV_GETTING_UPGRADE,    ACHV_CAT_PROGRESSION, "Getting an Upgrade",  "Craft a better pickaxe", true },
    { ACHV_DIAMONDS,           ACHV_CAT_PROGRESSION, "Diamonds!",           "Obtain a diamond", true },
    { ACHV_INTO_THE_NETHER,    ACHV_CAT_PROGRESSION, "Into the Nether",     "Enter the Nether", true },
    { ACHV_RETURN_TO_SENDER,   ACHV_CAT_PROGRESSION, "Return to Sender",    "Kill a Ghast with a deflected fireball", true },

    { ACHV_ADVENTURER,         ACHV_CAT_EXPLORATION, "Adventurer",          "Visit 5 biomes", true },
    { ACHV_EXPLORER,           ACHV_CAT_EXPLORATION, "Explorer",            "Visit 10 biomes", true },
    { ACHV_DEEP_EXPLORER,      ACHV_CAT_EXPLORATION, "Deep Explorer",       "Reach bedrock level", true },
    { ACHV_NETHER_EXPLORER,    ACHV_CAT_EXPLORATION, "Nether Explorer",     "Visit all Nether biomes", true },
    { ACHV_VILLAGE_DISCOVERER, ACHV_CAT_EXPLORATION, "Village Discoverer",  "Find a village", true },
    { ACHV_DUNGEON_DELVER,     ACHV_CAT_EXPLORATION, "Dungeon Delver",      "Find a dungeon", false },

    { ACHV_BUILDER,            ACHV_CAT_BUILDING,    "Builder",             "Place 100 blocks", true },
    { ACHV_ARCHITECT,          ACHV_CAT_BUILDING,    "Architect",           "Place 1,000 blocks", true },
    { ACHV_MASTER_BUILDER,     ACHV_CAT_BUILDING,    "Master Builder",      "Place 10,000 blocks", true },
    { ACHV_HOME_SWEET_HOME,    ACHV_CAT_BUILDING,    "Home Sweet Home",     "Sleep in a bed", true },
    { ACHV_TOWN_BUILDER,       ACHV_CAT_BUILDING,    "Town Builder",        "Build near a village", false },

    { ACHV_FIRST_BLOOD,        ACHV_CAT_COMBAT,      "First Blood",         "Kill your first hostile mob", true },
    { ACHV_MONSTER_HUNTER,     ACHV_CAT_COMBAT,      "Monster Hunter",      "Kill 100 hostile mobs", true },
    { ACHV_SNIPER,             ACHV_CAT_COMBAT,      "Sniper",              "Kill a mob from long range", true },
    { ACHV_SLAYER,             ACHV_CAT_COMBAT,      "Slayer",              "Kill every basic hostile mob", true },

    { ACHV_MINER,              ACHV_CAT_MINING,      "Miner",               "Mine 100 blocks", true },
    { ACHV_DEEP_MINER,         ACHV_CAT_MINING,      "Deep Miner",          "Mine at great depth", true },
    { ACHV_TREASURE_HUNTER,    ACHV_CAT_MINING,      "Treasure Hunter",     "Find a rare resource", true },
    { ACHV_DIAMOND_MINER,      ACHV_CAT_MINING,      "Diamond Miner",       "Mine your first diamond", true },

    { ACHV_VILLAGE_HERO,       ACHV_CAT_VILLAGE,     "Village Hero",        "Complete a village objective", false },
    { ACHV_FARMER,             ACHV_CAT_VILLAGE,     "Farmer",              "Harvest crops", true },
    { ACHV_TRADER,             ACHV_CAT_VILLAGE,     "Trader",              "Trade with a villager", false },

    { ACHV_INTO_THE_FIRE,      ACHV_CAT_NETHER,      "Into the Fire",       "Enter the Nether", true },
    { ACHV_FORTRESS_FOUND,     ACHV_CAT_NETHER,      "Fortress Found",      "Discover a Nether Fortress", false },
    { ACHV_BLAZE_HUNTER,       ACHV_CAT_NETHER,      "Blaze Hunter",        "Kill a Blaze", false },
    { ACHV_NETHER_TREASURE,    ACHV_CAT_NETHER,      "Nether Treasure",     "Open a fortress chest", false },
    { ACHV_RETURN_FROM_HELL,   ACHV_CAT_NETHER,      "Return From Hell",    "Return to the Overworld", true },
};

static const char* kCategoryNames[ACHV_CAT_COUNT] = {
    "Progression", "Exploration", "Building", "Combat", "Mining", "Village", "Nether",
};

const AchievementDef* achievementDef(AchievementId id) {
    if (id < 0 || id >= ACHV_COUNT) return 0;
    return &kDefs[id];
}

const char* achievementCategoryName(AchievementCategory c) {
    if (c < 0 || c >= ACHV_CAT_COUNT) return "";
    return kCategoryNames[c];
}

// --- Persistent state ---------------------------------------------------
//
// 40 achievements need 5 bytes for the unlocked bitmask. Counters are
// stored as plain ints rather than packed, since the whole file is well
// under 1KB regardless and PSP flash writes don't benefit from shaving a
// few dozen bytes here -- clarity of the on-disk struct matters more than
// squeezing it further.

#define ACHV_SAVE_VERSION 1

struct AchievementCounters {
    int blocksMined;
    int blocksPlaced;
    int hostileKills;
    int biomesVisitedMask;       // bit per BiomeId (11 used of 32)
    int netherBiomesVisitedMask; // bit per NetherBiomeId (3 used of 32)
    int hostileKilledMask;       // bit per EntityTypes hostile id, remapped -- see hostileBit()
    int deepestYReached;         // lowest (smallest) Y the player has stood at, init to a high sentinel
};

static unsigned char s_unlocked[(ACHV_COUNT + 7) / 8];
static AchievementCounters s_counters;
static bool s_loaded = false;
static bool s_dirty = false;

static bool bitGet(const unsigned char* mask, int i) { return (mask[i >> 3] & (1 << (i & 7))) != 0; }
static void bitSet(unsigned char* mask, int i)        { mask[i >> 3] |= (unsigned char)(1 << (i & 7)); }

// Remaps a hostile EntityTypes id to a small dense bit index for
// hostileKilledMask, rather than using the raw id (which runs up to 36+
// and would waste most of a 32-bit mask on ids that are never hostile).
// Returns -1 for a non-hostile or unrecognised type, in which case the
// "kill every basic hostile mob" tracking simply ignores that kill --
// the achievement is deliberately scoped to the four overworld basics.
static int hostileBit(int entityTypeId) {
    switch (entityTypeId) {
        case EntityTypes::IdZombie:   return 0;
        case EntityTypes::IdCreeper:  return 1;
        case EntityTypes::IdSkeleton: return 2;
        case EntityTypes::IdSpider:   return 3;
        default: return -1;
    }
}
#define HOSTILE_BASIC_COUNT 4
#define HOSTILE_BASIC_MASK  ((1 << HOSTILE_BASIC_COUNT) - 1)

static bool isHostileEntityType(int entityTypeId) {
    // PigZombie is Nether-native and neutral unless provoked in real
    // Minecraft; it is deliberately excluded from "basic hostile" combat
    // achievements (First Blood/Monster Hunter/Slayer) so those stay
    // scoped to the four overworld monsters the plan lists, but it still
    // counts toward general hostileKills below via its own check at the
    // call site -- see achvOnMobKilled.
    return hostileBit(entityTypeId) >= 0;
}

// --- Notification queue -------------------------------------------------

#define ACHV_NOTIFY_QUEUE 4
static char s_notifyQueue[ACHV_NOTIFY_QUEUE][40];
static int  s_notifyHead = 0, s_notifyTail = 0;

static void queueNotification(const char* name) {
    int next = (s_notifyTail + 1) % ACHV_NOTIFY_QUEUE;
    if (next == s_notifyHead) return; // queue full; drop rather than block gameplay
    strncpy(s_notifyQueue[s_notifyTail], name, sizeof(s_notifyQueue[0]) - 1);
    s_notifyQueue[s_notifyTail][sizeof(s_notifyQueue[0]) - 1] = '\0';
    s_notifyTail = next;
}

bool achievementsPollNotification(char* outName, int cap) {
    if (s_notifyHead == s_notifyTail) return false;
    strncpy(outName, s_notifyQueue[s_notifyHead], cap - 1);
    outName[cap - 1] = '\0';
    s_notifyHead = (s_notifyHead + 1) % ACHV_NOTIFY_QUEUE;
    return true;
}

// --- Unlock core ----------------------------------------------------

static void unlock(AchievementId id) {
    if (id < 0 || id >= ACHV_COUNT) return;
    if (bitGet(s_unlocked, id)) return; // already unlocked; no re-notify
    bitSet(s_unlocked, id);
    s_dirty = true;
    queueNotification(kDefs[id].name);
    achievementsSave();
}

bool achievementUnlocked(AchievementId id) {
    if (id < 0 || id >= ACHV_COUNT) return false;
    return bitGet(s_unlocked, id);
}

int achievementsUnlockedCount() {
    int n = 0;
    for (int i = 0; i < ACHV_COUNT; ++i) if (bitGet(s_unlocked, i)) n++;
    return n;
}

// --- Progress reporting for the menu -------------------------------
//
// Only counter-backed achievements report meaningful progress; everything
// else (single-event unlocks like "sleep in a bed") returns -1 for both
// current and target, which the menu renders as a plain locked/unlocked
// row with no progress bar.

int achievementProgressTarget(AchievementId id) {
    switch (id) {
        case ACHV_BUILDER:         return 100;
        case ACHV_ARCHITECT:       return 1000;
        case ACHV_MASTER_BUILDER:  return 10000;
        case ACHV_MONSTER_HUNTER:  return 100;
        case ACHV_MINER:           return 100;
        case ACHV_ADVENTURER:      return 5;
        case ACHV_EXPLORER:        return 10;
        case ACHV_NETHER_EXPLORER: return 3;
        case ACHV_SLAYER:          return HOSTILE_BASIC_COUNT;
        default: return -1;
    }
}

static int popcount11(int mask) { // biomesVisitedMask only uses 11 bits
    int n = 0;
    for (int i = 0; i < 11; ++i) if (mask & (1 << i)) n++;
    return n;
}
static int popcountN(int mask, int n) {
    int c = 0;
    for (int i = 0; i < n; ++i) if (mask & (1 << i)) c++;
    return c;
}

int achievementProgress(AchievementId id) {
    switch (id) {
        case ACHV_BUILDER: case ACHV_ARCHITECT: case ACHV_MASTER_BUILDER:
            return s_counters.blocksPlaced;
        case ACHV_MONSTER_HUNTER:
            return s_counters.hostileKills;
        case ACHV_MINER:
            return s_counters.blocksMined;
        case ACHV_ADVENTURER: case ACHV_EXPLORER:
            return popcount11(s_counters.biomesVisitedMask);
        case ACHV_NETHER_EXPLORER:
            return popcountN(s_counters.netherBiomesVisitedMask, 3);
        case ACHV_SLAYER:
            return popcountN(s_counters.hostileKilledMask, HOSTILE_BASIC_COUNT);
        default: return -1;
    }
}

// --- Save / load ---------------------------------------------------
//
// Flat binary, not NBT: this is a small fixed-shape record (a version tag,
// a bitmask, and a handful of ints), and the project's own precedent for
// this kind of compact global state is options.txt's plain fopen/fwrite
// path (see optionsSave/optionsLoad in screen_options.cpp), not the NBT
// tree used for per-world saves. NBT would be pure overhead here.
//
// Per-world, not global: achievements.dat lives inside the active world's
// own save directory (LevelStorage::getActiveDir(), the same "saves/<name>"
// folder level.dat/level.txt/icon.png already live in), not the shared
// install-directory assetPath() every world used to read the same file
// from. That single shared file was the actual bug being fixed here --
// unlocking an achievement in one world silently unlocked it "in" every
// other world too, since there was only ever one save slot for all of
// them.
static const char* saveFilePath() {
    static char buf[352];
    const char* dir = LevelStorage::getActiveDir();
    // No active world (e.g. the very first achievementsInit() call at
    // process startup, before any world has been created or loaded --
    // see main.cpp) has nowhere per-world to read or write, so fall back
    // to the old global path rather than fopen("achievements.dat", ...)
    // against a relative/empty directory, which would land wherever the
    // process's current working directory happens to be. This path is
    // only ever actually used before a real world is active; once one is,
    // achievementsInit() is called again (see the setActiveWorld call
    // site in render.cpp) and every subsequent save/load goes through the
    // real per-world path below.
    if (!dir || !dir[0]) return assetPath("achievements.dat");
    snprintf(buf, sizeof(buf), "%s/achievements.dat", dir);
    return buf;
}

static void resetToDefaults() {
    memset(s_unlocked, 0, sizeof(s_unlocked));
    memset(&s_counters, 0, sizeof(s_counters));
    s_counters.deepestYReached = WORLD_H; // sentinel "not yet measured"; any real Y is lower
}

void achievementsSave() {
    FILE* f = fopen(saveFilePath(), "wb");
    if (!f) return; // best-effort; loss of achievement progress is not
                     // worth crashing or blocking gameplay over
    int version = ACHV_SAVE_VERSION;
    fwrite(&version, sizeof(version), 1, f);
    fwrite(s_unlocked, sizeof(s_unlocked), 1, f);
    fwrite(&s_counters, sizeof(s_counters), 1, f);
    fclose(f);
    s_dirty = false;
}

// s_loadedDir remembers which world's save directory the in-memory
// s_unlocked/s_counters state actually belongs to, the same "cache plus
// an identity check" shape ensureBiomeSeeds already uses for
// s_seedsForWorldSeed. Without this, calling achievementsInit() again
// after switching worlds (see the setActiveWorld call site in
// render.cpp) would have no way to tell "a different world is now
// active, reload" apart from "the same world is still active, this call
// is redundant" -- and getting that wrong either loses the ability to
// ever pick up a second world's progress, or repeatedly reloads (and
// therefore silently discards any not-yet-saved s_dirty progress) every
// time init happens to be called again for the world already active.
static char s_loadedDir[352] = "";

void achievementsInit() {
    const char* dir = LevelStorage::getActiveDir();
    const char* effectiveDir = (dir && dir[0]) ? dir : "";

    if (s_loaded && strcmp(s_loadedDir, effectiveDir) == 0) return;

    // Switching to a different world (or to/from "no world active") while
    // unsaved progress exists: flush it to the world that was previously
    // loaded before overwriting the in-memory state with the new world's
    // data, or that progress is lost with no save prompt or warning --
    // achievements are the kind of state a player would never think to
    // check is unsaved before backing out to the world list.
    if (s_loaded && s_dirty) achievementsSave();

    resetToDefaults();

    FILE* f = fopen(saveFilePath(), "rb");
    if (f) {
        int version = 0;
        bool ok = fread(&version, sizeof(version), 1, f) == 1 && version == ACHV_SAVE_VERSION;
        if (ok) ok = fread(s_unlocked, sizeof(s_unlocked), 1, f) == 1;
        if (ok) ok = fread(&s_counters, sizeof(s_counters), 1, f) == 1;
        fclose(f);
        if (!ok) resetToDefaults(); // corrupt/old-version file: start clean
                                     // rather than trust a partial read
    }

    strncpy(s_loadedDir, effectiveDir, sizeof(s_loadedDir) - 1);
    s_loadedDir[sizeof(s_loadedDir) - 1] = '\0';
    s_loaded = true;
}

void achievementsShutdown() {
    if (s_dirty) achievementsSave();
}

// --- Event handlers ---------------------------------------------------

void achvOnBlockMined(unsigned char blockId, int y) {
    s_counters.blocksMined++;
    s_dirty = true;
    if (s_counters.blocksMined >= 100) unlock(ACHV_MINER);

    if (blockId == BLOCK_STONE || blockId == BLOCK_COBBLESTONE) unlock(ACHV_STONE_AGE);

    if (blockId == BLOCK_ORE_DIAMOND) {
        unlock(ACHV_DIAMOND_MINER);
        unlock(ACHV_TREASURE_HUNTER);
    }

    if (y <= 5) unlock(ACHV_DEEP_MINER);

    if (y < s_counters.deepestYReached) s_counters.deepestYReached = y;
    if (s_counters.deepestYReached <= 1) unlock(ACHV_DEEP_EXPLORER); // bedrock band
}

void achvOnBlockPlaced(unsigned char) {
    s_counters.blocksPlaced++;
    s_dirty = true;
    if (s_counters.blocksPlaced >= 100)   unlock(ACHV_BUILDER);
    if (s_counters.blocksPlaced >= 1000)  unlock(ACHV_ARCHITECT);
    if (s_counters.blocksPlaced >= 10000) unlock(ACHV_MASTER_BUILDER);
}

void achvOnMobKilled(int entityTypeId, float killDistance) {
    bool basicHostile = isHostileEntityType(entityTypeId);
    // Hostile-in-general also covers the Nether pig zombie for the raw
    // kill counter (a real threat the player fought), just not for the
    // "basic hostile" first-blood/slayer set, which is deliberately
    // scoped to the four overworld monsters named in the plan.
    bool countsAsHostile = basicHostile || entityTypeId == EntityTypes::IdPigZombie;
    if (!countsAsHostile) return;

    s_counters.hostileKills++;
    s_dirty = true;
    unlock(ACHV_FIRST_BLOOD);
    if (s_counters.hostileKills >= 100) unlock(ACHV_MONSTER_HUNTER);
    if (killDistance >= 20.0f) unlock(ACHV_SNIPER);

    if (basicHostile) {
        int bit = hostileBit(entityTypeId);
        s_counters.hostileKilledMask |= (1 << bit);
        if ((s_counters.hostileKilledMask & HOSTILE_BASIC_MASK) == HOSTILE_BASIC_MASK)
            unlock(ACHV_SLAYER);
    }
}

void achvOnGhastReturnToSender() {
    unlock(ACHV_RETURN_TO_SENDER);
}

void achvOnItemCrafted(short itemId) {
    switch (itemId) {
        case ITEM_PICKAXE_WOOD: case ITEM_PICKAXE_STONE: case ITEM_PICKAXE_IRON:
        case ITEM_PICKAXE_GOLD: case ITEM_PICKAXE_DIAMOND:
        case ITEM_SWORD_WOOD:   case ITEM_SWORD_STONE:   case ITEM_SWORD_IRON:
        case ITEM_SWORD_GOLD:   case ITEM_SWORD_DIAMOND:
        case ITEM_HATCHET_WOOD: case ITEM_HATCHET_STONE: case ITEM_HATCHET_IRON:
        case ITEM_HATCHET_GOLD: case ITEM_HATCHET_DIAMOND:
        case ITEM_SHOVEL_WOOD:  case ITEM_SHOVEL_STONE:  case ITEM_SHOVEL_IRON:
        case ITEM_SHOVEL_GOLD:  case ITEM_SHOVEL_DIAMOND:
        case ITEM_HOE_WOOD:     case ITEM_HOE_STONE:     case ITEM_HOE_IRON:
        case ITEM_HOE_GOLD:     case ITEM_HOE_DIAMOND:
            unlock(ACHV_GETTING_STARTED);
            break;
        default: break;
    }
    switch (itemId) {
        case ITEM_PICKAXE_STONE: case ITEM_PICKAXE_IRON:
        case ITEM_PICKAXE_GOLD:  case ITEM_PICKAXE_DIAMOND:
            unlock(ACHV_GETTING_UPGRADE);
            break;
        default: break;
    }
}

void achvOnItemObtained(short itemId) {
    if (itemId == ITEM_DIAMOND) unlock(ACHV_DIAMONDS);
}

void achvOnBiomeEntered(int biomeId) {
    if (biomeId < 0 || biomeId >= 11) return;
    int bit = 1 << biomeId;
    if (s_counters.biomesVisitedMask & bit) return; // already counted
    s_counters.biomesVisitedMask |= bit;
    s_dirty = true;
    int n = popcount11(s_counters.biomesVisitedMask);
    if (n >= 5)  unlock(ACHV_ADVENTURER);
    if (n >= 10) unlock(ACHV_EXPLORER);
}

void achvOnNetherBiomeEntered(int netherBiomeId) {
    if (netherBiomeId < 0 || netherBiomeId >= 3) return;
    int bit = 1 << netherBiomeId;
    if (s_counters.netherBiomesVisitedMask & bit) return;
    s_counters.netherBiomesVisitedMask |= bit;
    s_dirty = true;
    if (popcountN(s_counters.netherBiomesVisitedMask, 3) >= 3) unlock(ACHV_NETHER_EXPLORER);
}

void achvOnNetherEntered() {
    unlock(ACHV_INTO_THE_NETHER);
    unlock(ACHV_INTO_THE_FIRE);
}

void achvOnOverworldReturned() {
    unlock(ACHV_RETURN_FROM_HELL);
}

void achvOnVillageDiscovered() {
    unlock(ACHV_VILLAGE_DISCOVERER);
}

void achvOnSlept() {
    unlock(ACHV_HOME_SWEET_HOME);
}

void achvOnCropHarvested() {
    unlock(ACHV_FARMER);
}
