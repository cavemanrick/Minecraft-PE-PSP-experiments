
#ifndef MCPSP_WORLD_ITEM_BAMBOO_RAFT_ITEM_H
#define MCPSP_WORLD_ITEM_BAMBOO_RAFT_ITEM_H

#include "world/item/item.h"

class BambooRaftItem : public Item {
public:
    BambooRaftItem(short id);
    virtual bool useOn(ItemInstance* item, Player* player, World* world, int x, int y, int z, int face,
                       float clickX, float clickY, float clickZ);
    // Without this, worldPick's default clipLiquids=false raycast passes
    // straight through the water surface -- clicking on open water with
    // the raft selected never even reaches useOn below, since hit.hit
    // comes back false (or the ray lands on whatever's under/behind the
    // water instead). BucketItem overrides this the same way for an
    // empty bucket, for the same reason: scooping water needs the water
    // block itself to register as the hit, not whatever's behind it. The
    // raft is placed on water the same way (see useOn's isWaterId check),
    // so it needs the same opt-in.
    virtual bool isLiquidClipItem(short) const { return true; }
    // getIcon() is dead for id >= 256 items -- see the NOTE in item.cpp's
    // registerItems(). The real icon lookup is kItemIcon[id - 256] in
    // gpu/item_icons.h, wired alongside ITEM_BAMBOO_RAFT's registration.
};

#endif
