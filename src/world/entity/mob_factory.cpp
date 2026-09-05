#include "world/entity/mob_factory.h"
#include "world/entity/entity_types.h"
#include "world/entity/mob.h"
#include "world/entity/entity.h"
#include "world/entity/animal/pig.h"
#include "world/entity/animal/cow.h"
#include "world/entity/animal/chicken.h"
#include "world/entity/animal/sheep.h"
#include "world/entity/animal/strider.h"
#include "world/entity/monster/ghast.h"
#include "world/entity/monster/zombie.h"
#include "world/entity/monster/skeleton.h"
#include "world/entity/monster/creeper.h"
#include "world/entity/monster/spider.h"
#include "world/entity/monster/warped_spider.h"
#include "world/entity/monster/pig_zombie.h"
#include <cstdlib>

namespace MobFactory {

// Compile-time check that the mobs this factory builds actually fit an
// entity pool slot. Every mob type is already included above, so this is
// the one place in the codebase that can see all of them at once.
//
// Falling out of the pool is not fatal -- Entity::operator new drops
// through to malloc -- but it is silent, and a mob quietly malloc'd on
// every spawn is exactly the kind of thing that shows up much later as
// heap exhaustion or fragmentation after a few dozen spawns rather than as
// an obvious failure at the point of the mistake. If this line stops
// compiling, either something grew or ENTITY_SLOT needs raising; the array
// size in the error message is the giveaway.
typedef char assert_mob_fits_slot[
    (sizeof(PigZombie) <= Entity::ENTITY_SLOT &&
     sizeof(Zombie)    <= Entity::ENTITY_SLOT &&
     sizeof(Skeleton)  <= Entity::ENTITY_SLOT &&
     sizeof(Creeper)   <= Entity::ENTITY_SLOT &&
     sizeof(Spider)    <= Entity::ENTITY_SLOT &&
     sizeof(WarpedSpider) <= Entity::ENTITY_SLOT &&
     sizeof(Pig)       <= Entity::ENTITY_SLOT &&
     sizeof(Cow)       <= Entity::ENTITY_SLOT &&
     sizeof(Chicken)   <= Entity::ENTITY_SLOT &&
     sizeof(Sheep)     <= Entity::ENTITY_SLOT &&
     sizeof(Strider)   <= Entity::ENTITY_SLOT &&
     sizeof(Ghast)     <= Entity::ENTITY_SLOT) ? 1 : -1];

static const int MOB_SLOT_RESERVE = 24;

Mob* createMob(int mobType, Level* level) {
    if (Entity::freeSlots() <= MOB_SLOT_RESERVE) return 0;
    Mob* r = 0;
    switch (mobType) {
        case EntityTypes::IdPig:      r = new Pig(level); break;
        case EntityTypes::IdCow:      r = new Cow(level); break;
        case EntityTypes::IdChicken:  r = new Chicken(level); break;
        case EntityTypes::IdSheep:    r = new Sheep(level); break;
        case EntityTypes::IdZombie:   r = new Zombie(level); break;
        case EntityTypes::IdSkeleton: r = new Skeleton(level); break;
        case EntityTypes::IdCreeper:  r = new Creeper(level); break;
        case EntityTypes::IdSpider:   r = new Spider(level); break;
        case EntityTypes::IdWarpedSpider: r = new WarpedSpider(level); break;
        case EntityTypes::IdPigZombie:r = new PigZombie(level); break;
        case EntityTypes::IdStrider:   r = new Strider(level); break;
        case EntityTypes::IdGhast:     r = new Ghast(level); break;
    }
    return r;
}

}
