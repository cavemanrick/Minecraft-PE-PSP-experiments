#ifndef MCPSP_WORLD_ITEM_FISHING_ROD_ITEM_H
#define MCPSP_WORLD_ITEM_FISHING_ROD_ITEM_H

#include "world/item/item.h"

class FishingRodItem : public Item {
public:
    explicit FishingRodItem(short id) : Item(id) {
        maxStackSize = 1;
        maxDamage = 64; // vanilla's 65 uses
    }
    virtual bool isHandEquipped() const { return true; }
};

// --- Cast/reel control, called from GameMode::handleInput ---------------
//
// The cast state lives here rather than on Player or the Item, for two
// reasons. Item instances are shared singletons (Item::items[]), so they
// cannot hold per-cast state; and Player is persisted, whereas a cast
// deliberately is not (see EntityTypes::IdFishingBobber). A file-scope
// entity id in fishing_rod_item.cpp is the one place that is neither.

// True if a bobber is currently out.
bool fishingLineIsOut();

// Cast the line. No-op if one is already out.
void fishingCast();

// Reel in. Lands a catch if the bobber is mid-bite, otherwise just
// retracts. Returns true if something was caught.
bool fishingReel();

// Called every tick regardless of what the player is holding. Cuts the line
// when the cast should not survive -- rod deselected, player too far, player
// dead, bobber gone. Without this a bobber can outlive the thing that owns
// it, which is the failure mode the id-not-pointer rule above exists for.
void fishingTick();

// "Cast" / "Reel in" for the crosshair hint, or 0 when not holding a rod.
const char* fishingUseLabel();

#endif
