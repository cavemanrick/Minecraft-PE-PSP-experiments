
#ifndef MCPSP_WORLD_ENTITY_TYPES_H
#define MCPSP_WORLD_ENTITY_TYPES_H

namespace EntityTypes {

    const int BaseEnemy         = 1;
    const int BaseCreature      = 2;
    const int BaseWaterCreature = 3;

    const int IdChicken   = 10;
    const int IdCow       = 11;
    const int IdPig       = 12;
    const int IdSheep     = 13;
    const int IdZombie    = 32;
    const int IdCreeper   = 33;
    const int IdSkeleton  = 34;
    const int IdSpider    = 35;
    const int IdPigZombie = 36;
    const int IdStrider   = 37;
    const int IdGhast     = 38;
    const int IdVillager = 39;
    const int IdWarpedSpider = 40;

    const int IdLocalPlayer = 63;
    const int IdItemEntity  = 64;
    const int IdPrimedTnt   = 65;
    const int IdFallingTile = 66;
    const int IdArrow       = 80;
    const int IdSnowball    = 81;
    const int IdThrownEgg   = 82;
    const int IdPainting    = 83;
    // The fishing bobber. Deliberately never persisted -- FishingBobber
    // overrides save() to refuse, because a bobber reloaded without the
    // cast that produced it is an orphan nothing can reel in. It is
    // therefore also absent from EntityFactory::createEntity: nothing can
    // ask for one by type id, only the rod can create one.
    const int IdFishingBobber = 84;
    const int IdFireball      = 85;
}

#endif
