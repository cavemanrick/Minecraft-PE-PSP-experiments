#include "world/entity/monster/warped_spider.h"
#include "world/entity/entity_types.h"

WarpedSpider::WarpedSpider(Level* level) : Spider(level) {
    // Everything else (size, runSpeed, attackDamage, health) is inherited
    // from Spider's constructor unchanged -- only identity and survival
    // differ for this variant.
    fireImmune = true;
    entityRendererId = ER_WARPED_SPIDER_RENDERER;
}

int WarpedSpider::getEntityTypeId() const { return EntityTypes::IdWarpedSpider; }

float WarpedSpider::getVoicePitch() {
    // Same shape as Mob::getVoicePitch (small +/-0.2 random jitter around a
    // base), just a lower base so the ordinary "mob.spider"/"mob.spiderdeath"
    // sounds (reused as-is -- see spider.h's getAmbientSound/getHurtSound/
    // getDeathSound, none of which are overridden here) play back slower
    // and deeper rather than needing new sound assets. soundPlay clamps
    // pitch to a minimum of 0.1 (platform/audio/sound.cpp), so 0.7 stays
    // comfortably inside the supported range with room for the jitter on
    // either side.
    float base = 0.7f;
    return (sharedRandom.nextFloat() - sharedRandom.nextFloat()) * 0.2f + base;
}
