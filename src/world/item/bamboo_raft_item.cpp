#include "world/item/bamboo_raft_item.h"
#include "world/item/item_instance.h"
#include "world/entity/player.h"
#include "world/entity/vehicle/raft.h"
#include "world/entity/entity_types.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/inventory/inventory.h"

extern Level g_level;

BambooRaftItem::BambooRaftItem(short id) : Item(id) {
    maxStackSize = 1; // matches vanilla: a raft/boat item never stacks
}

bool BambooRaftItem::useOn(ItemInstance* item, Player* player, World* world,
                           int x, int y, int z, int face, float, float, float) {
    if (!item || item->isNull()) return false;

    // Target the water block itself, same convention a bucket uses --
    // isReplaceable() already covers liquids (see rawReplaceable in
    // tile.cpp), so no neighbor-offset step is needed the way TileItem's
    // solid-block placement requires.
    unsigned char id = worldBlock(world, x, y, z);
    if (!isWaterId(id)) return false;

    Raft* raft = new Raft(&g_level);
    raft->setPos((float)x + 0.5f, (float)y + 1.0f, (float)z + 0.5f);
    raft->yRot = player ? player->yRot : 0.0f;

    // A spot already occupied by another raft/entity shouldn't stack a
    // second one on top -- same spirit as HangingEntityItem's survives()
    // check, though Raft has no equivalent yet; the level's own entity
    // collision (see Entity::isFree) is not consulted here deliberately,
    // since a raft has no solid collision box interaction with itself
    // defined. Flagged rather than silently assumed safe.
    g_level.addEntity(raft);

    if (player && !player->inventory->isCreative()) player->inventory->consumeSelected();
    return true;
}
