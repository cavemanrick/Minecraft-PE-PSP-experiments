#ifndef MCPSP_WORLD_ENTITY_VILLAGER_H
#define MCPSP_WORLD_ENTITY_VILLAGER_H
#include "world/entity/mob.h"
#include "world/entity/entity_types.h"
class Villager : public Mob {
public:
    enum { TRADE_COUNT = 3, MAX_TRADE_USES = 12 };
    Villager(Level* level);
    virtual void tick();
    virtual bool playerInteract();
    virtual bool save(CompoundTag* tag);
    virtual int getEntityTypeId() const;
    virtual int getCreatureBaseType() const { return EntityTypes::BaseCreature; }
    virtual bool removeWhenFarAway() { return false; }
    virtual bool isImmobile() { return true; }
    virtual bool hurt(Entity* source, int damage);
    unsigned char tradeUses[TRADE_COUNT];
    void setTradeUses(const unsigned char* uses);
    void getTradeUses(unsigned char* uses) const;
protected:
    virtual void readAdditionalSaveData(CompoundTag* tag);
    virtual void addAdditonalSaveData(CompoundTag* tag);
};
#endif
