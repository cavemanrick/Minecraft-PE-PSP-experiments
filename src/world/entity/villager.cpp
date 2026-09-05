#include "world/entity/villager.h"
#include "world/entity/player.h"
#include "world/inventory/inventory.h"
#include "world/item/item.h"
#include "world/level/level.h"
#include "world/level/levelgen/village_gen.h"
#include "world/entity/villager_trade_hooks.h"
#include "nbt/compound_tag.h"

Villager::Villager(Level* level) : Mob(level) {
    setSize(0.6f, 1.8f);
    heightOffset = 0.0f;
    walkingSpeed = 0.0f;
    health = getMaxHealth();
    for (int i = 0; i < TRADE_COUNT; ++i) tradeUses[i] = 0;
    entityRendererId = ER_VILLAGER_RENDERER;
    canRemove = false;
}

// Must go through Mob::tick(), not Entity::tick() directly. Mob::tick() is
// what sets xOld/yOld/zOld every tick (before calling Entity::tick() itself),
// and the renderer interpolates render position as
// xOld + (x - xOld) * partialTick (entity_render_dispatcher.cpp). Villager is
// spawned with setPos() (village_gen.cpp), which sets x/y/z but never touches
// xOld/yOld/zOld -- those stay at the constructor's (0,0,0) default. Calling
// only Entity::tick() here meant xOld/yOld/zOld were NEVER updated after
// spawn, so every frame interpolated between world origin (0,0,0) and the
// real house position: the "flickering, barely rendering, flies up and
// away" symptom.
//
// Mob::tick() also runs aiStep(), which is where gravity and ground
// collision live (Mob::travel(), called from aiStep()). Villager was never
// settling onto the floor or having its motion state (xd/yd/zd, written to
// directly by Entity::push() whenever another entity bumps into it)
// consumed or decayed -- aiStep()'s isImmobile() branch zeroes the *input*
// axes (xxa/yya) and skips updateAi(), it does not skip travel()/gravity,
// so an immobile mob still settles and still clears pushes correctly.
void Villager::tick() { Mob::tick(); }

bool Villager::playerInteract() {
    if (!level || !level->player) return false;
    villageTradeOpen(this);
    return true;
}

bool Villager::save(CompoundTag*) { return false; }
int Villager::getEntityTypeId() const { return EntityTypes::IdVillager; }
bool Villager::hurt(Entity*, int) { return false; }

void Villager::setTradeUses(const unsigned char* uses) {
    if (!uses) return;
    for (int i = 0; i < TRADE_COUNT; ++i)
        tradeUses[i] = uses[i] > MAX_TRADE_USES ? MAX_TRADE_USES : uses[i];
}
void Villager::getTradeUses(unsigned char* uses) const {
    if (!uses) return;
    for (int i = 0; i < TRADE_COUNT; ++i) uses[i] = tradeUses[i];
}
void Villager::addAdditonalSaveData(CompoundTag* tag) { Mob::addAdditonalSaveData(tag); }
void Villager::readAdditionalSaveData(CompoundTag* tag) { Mob::readAdditionalSaveData(tag); }
