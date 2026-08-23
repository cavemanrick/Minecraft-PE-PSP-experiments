
#ifndef MCPSP_WORLD_ENTITY_PLAYER_H
#define MCPSP_WORLD_ENTITY_PLAYER_H

#include "world/entity/mob.h"
#include "world/item/item_instance.h"

class Inventory;

class Player : public Mob {
public:
    Player(Level* level);
    virtual ~Player();

    virtual float getHeadHeight() { return 0.12f; }

    void drop(ItemInstance* item);
    void drop(ItemInstance* item, bool randomly);

    Inventory* inventory;

    static const int NUM_ARMOR = 4;
    ItemInstance armor[NUM_ARMOR];

    ItemInstance* getArmor(int slot);
    void setArmor(int slot, const ItemInstance* item);
    virtual int getArmorValue();
    void hurtArmor(int dmg);

    float bob, oBob, tilt, oTilt;
    float bowPull, bowTimeHeld;
    float eatAnim;

    virtual bool isPlayer() { return true; }
    virtual int  getEntityTypeId() const;
    virtual int  getMaxHealth() { return 20; }

    int score;
    int getScore() const { return score; }
    virtual void awardKillScore(Entity* victim, int amount) { score += amount; }

    enum { BED_OK = 0, BED_NOT_POSSIBLE_HERE = 1, BED_NOT_POSSIBLE_NOW = 2,
           BED_TOO_FAR_AWAY = 3, BED_OTHER_PROBLEM = 4, BED_NOT_SAFE = 5 };
    static const int SLEEP_DURATION = 100;
    bool  sleeping;
    short sleepCounter;
    int   bedX, bedY, bedZ;

    int   respawnX, respawnY, respawnZ;
    bool hasRespawnPosition() const { return respawnY >= 0; }
    void setRespawnPosition(int x, int y, int z) { respawnX = x; respawnY = y; respawnZ = z; }

    // Where to send this player back to when they use the Nether-side
    // portal to return -- the Overworld position/facing they had at the
    // moment they last entered a portal going the other way. Saved/loaded
    // through the same Player NBT compound as bed/respawn position (see
    // buildPlayerTag/level_storage.cpp) rather than living only in memory
    // (see nether_portal.cpp's earlier in-memory-only version), so it
    // survives a save/quit/reload the same way bed and respawn position
    // already do. netherReturnY < 0 means "none recorded yet", same
    // sentinel convention as hasRespawnPosition() above -- a player who's
    // never used a portal has no return position, and hasNetherReturnPosition()
    // simply mirrors hasRespawnPosition()'s own -1-means-unset check
    // exactly rather than introducing a second, different convention for
    // what is otherwise the same shape of "optional saved position" data.
    float netherReturnX, netherReturnY, netherReturnZ;
    float netherReturnYRot, netherReturnXRot;
    bool hasNetherReturnPosition() const { return netherReturnY >= 0.0f; }
    void setNetherReturnPosition(float x, float y, float z, float yRot, float xRot) {
        netherReturnX = x; netherReturnY = y; netherReturnZ = z;
        netherReturnYRot = yRot; netherReturnXRot = xRot;
    }

    // Portal re-entry latch. Replaces the old tick-counter cooldown in
    // nether_portal.cpp, which did not work: Tile::entityInside is called
    // once per *overlapping block* per tick (see the triple loop at the end
    // of Entity::move in entity.cpp), not once per tick, so a counter
    // incremented there measured "portal blocks touched" rather than time.
    // Its value therefore depended on how wide the portal was and where in
    // it the player stood, and it froze entirely the moment the player
    // stepped out of a portal.
    //
    // The latch is the vanilla rule instead: once a teleport fires, no
    // further teleport may fire until the player has spent a whole tick not
    // touching any portal block. This is what stops the ping-pong when the
    // return trip drops the player straight back inside the Overworld
    // portal they left from (their recorded return position is, by
    // definition, a spot inside a portal).
    //
    // inPortalThisTick is set by netherPortalEntityInside during move();
    // portalTickEnd() is called once per tick afterwards (end of
    // LocalPlayer::aiStep) to consume it. Not saved to NBT -- a fresh load
    // starting unlatched is correct, since the player is not mid-crossing.
    bool inPortalThisTick;
    bool portalLatched;
    void portalTickEnd() {
        if (!inPortalThisTick) portalLatched = false;
        inPortalThisTick = false;
    }

    bool isSleeping() const { return sleeping; }
    bool isSleepingLongEnough() const { return sleeping && sleepCounter >= SLEEP_DURATION; }
    int  startSleepInBed(int x, int y, int z);
    void stopSleepInBed(bool forcefulWakeUp, bool saveRespawnPoint);
    void sleepTick();
    bool checkBed();

    virtual bool hurt(Entity* source, int dmg);

    virtual void causeFallDamage(float dist);

protected:
    virtual void addAdditonalSaveData(CompoundTag* tag);
    virtual void readAdditionalSaveData(CompoundTag* tag);
};

struct World;
extern const int BED_HEAD_OFF[4][2];
bool bedFindStandUpPosition(World* w, int x, int y, int z, int dir, int* ox, int* oy, int* oz);

#endif
