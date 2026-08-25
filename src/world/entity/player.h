
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

    // --- Nether portal crossing state ------------------------------------
    // Crossing is no longer instantaneous. Standing in a portal charges
    // portalCharge one step per tick; the screen darkens in proportion,
    // and only when it reaches PORTAL_CHARGE_TICKS does the teleport
    // actually fire. Stepping out before then drains the charge back down
    // again, so the fade reverses and nothing happens -- which is both the
    // vanilla behaviour and the thing that makes a portal feel like a
    // portal rather than a trip hazard.
    //
    // After the teleport, portalArrive counts back down to zero and the
    // screen brightens from black, so the far side is revealed rather than
    // cut to.
    //
    // inPortalThisTick is set by netherPortalEntityInside during move().
    // Tile::entityInside is called once per *overlapping block* per tick
    // (see the triple loop at the end of Entity::move in entity.cpp), not
    // once per tick, so it can only ever be a flag -- a counter
    // incremented there would measure "portal blocks touched" rather than
    // time, which depends on how wide the portal is and where in it the
    // player is standing. netherPortalPlayerTick (nether_portal.cpp) is
    // what consumes the flag, exactly once per tick, from the end of
    // LocalPlayer::aiStep.
    //
    // portalLatched is the re-entry guard: once a teleport fires, no
    // further teleport may fire until the player has spent a whole tick
    // not touching any portal block. It stops the ping-pong that would
    // otherwise happen when an arrival lands the player in contact with
    // the portal on the far side.
    //
    // portalForced is the debug teleport's way in (see debug_teleport.cpp):
    // it charges the same counter without requiring a portal block, so the
    // dev shortcut gets the identical fade and arrival treatment the real
    // portal does instead of being a hard cut to a different place.
    //
    // None of this is saved to NBT: a fresh load starting uncharged and
    // unlatched is correct, since the player is by definition not
    // mid-crossing.
    enum { PORTAL_CHARGE_TICKS = 40, PORTAL_ARRIVE_TICKS = 16 };
    bool  inPortalThisTick;
    bool  portalLatched;
    bool  portalForced;
    short portalCharge;
    short portalArrive;
    // The portal block most recently touched. Used at fire time to work
    // out which side of the world this portal is on and which way its
    // plane faces, so the player can be stepped clear of it and turned to
    // face away. havePortalBlock is false for a forced (debug) crossing,
    // where there is no portal involved at all.
    bool  havePortalBlock;
    int   portalBlockX, portalBlockY, portalBlockZ;

    // 0 = clear, 1 = fully black. Drives the screen overlay (see
    // portalRenderFade in main.cpp). The arrival fade takes priority over
    // the charge fade so that the tick the teleport fires reads as one
    // continuous black-out rather than a flash back to clear.
    float portalFadeAlpha() const {
        if (portalArrive > 0) return (float)portalArrive / (float)PORTAL_ARRIVE_TICKS;
        if (portalCharge <= 0) return 0.0f;
        return (float)portalCharge / (float)PORTAL_CHARGE_TICKS;
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
