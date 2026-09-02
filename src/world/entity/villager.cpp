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

void Villager::tick() { Entity::tick(); }

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
