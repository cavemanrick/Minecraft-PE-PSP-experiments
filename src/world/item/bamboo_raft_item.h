
#ifndef MCPSP_WORLD_ITEM_BAMBOO_RAFT_ITEM_H
#define MCPSP_WORLD_ITEM_BAMBOO_RAFT_ITEM_H

#include "world/item/item.h"

class BambooRaftItem : public Item {
public:
    BambooRaftItem(short id);
    virtual bool useOn(ItemInstance* item, Player* player, World* world, int x, int y, int z, int face,
                       float clickX, float clickY, float clickZ);
    // getIcon() is dead for id >= 256 items -- see the NOTE in item.cpp's
    // registerItems(). The real icon lookup is kItemIcon[id - 256] in
    // gpu/item_icons.h, wired alongside ITEM_BAMBOO_RAFT's registration.
};

#endif
