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

    // Item::category defaults to -1 (see Item::Item in item.cpp). Both
    // screen_craft.cpp's recipe list (`if (item->category < 0) continue`)
    // and the Registry Categories-tab enumeration key off this field, not
    // just whether a recipe/creative-palette entry exists -- leaving it
    // at -1 meant the raft's own crafting recipe was silently invisible
    // in the crafting UI even though Recipes::getInstance() had it
    // registered correctly. Category 1 groups building/functional items
    // (planks, bed, crafting table, chest) -- the closest existing
    // grouping to a placeable vehicle item.
    category = 1;
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
