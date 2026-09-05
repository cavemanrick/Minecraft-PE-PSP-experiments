#ifndef MCPSP_WORLD_ENTITY_MONSTER_WARPED_SPIDER_H
#define MCPSP_WORLD_ENTITY_MONSTER_WARPED_SPIDER_H

#include "world/entity/monster/spider.h"

// A Nether-native spider variant. Identical AI, movement, and combat to the
// ordinary Spider -- this class only overrides identity (entity type id,
// renderer, sound pitch) and survival (fire/lava immunity). Deliberately a
// thin subclass rather than a fork: any future AI/behavior fix made to
// Spider applies here automatically, and the only places this class exists
// at all are the handful of things that are genuinely different about the
// Nether variant.
//
// Replaces the ordinary Skeleton that used to spawn from the Nether
// fortress's monster spawner block (see the placeSpawner call in
// nether_fortress_gen.cpp) -- see WarpedSpider::getVoicePitch below for the
// one other behavioral difference (a lower ambient/hurt/death sound pitch,
// reusing Spider's existing sound names rather than needing new sound
// assets).
class WarpedSpider : public Spider {
public:
    WarpedSpider(Level* level);

    virtual int getEntityTypeId() const;
    virtual float getVoicePitch();
};

#endif
