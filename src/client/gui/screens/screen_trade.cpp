#include "client/gui/screens/screen.h"
#include "client/gui/hud.h"
#include "world/entity/villager.h"
#include "world/entity/local_player.h"
#include "world/entity/player.h"
#include "world/inventory/inventory.h"
#include "world/item/item.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "gpu/gui_atlas.h"
#include "platform/audio/sound.h"
#include <pspctrl.h>
#include <pspgu.h>
#include <cstdio>
#include <cmath>

extern Level g_level;
bool g_tradeOpen = false;
static Villager* s_villager = 0;
static int s_cursor = 0;

struct TradeDef {
    short inId, inCount, outId, outCount;
    const char* name;
};
static const TradeDef s_trades[Villager::TRADE_COUNT] = {
    { ITEM_WHEAT, 8, ITEM_BREAD, 1, "Wheat -> Bread" },
    { ITEM_COAL, 10, ITEM_IRON_INGOT, 1, "Coal -> Iron" },
    { ITEM_IRON_INGOT, 5, ITEM_GOLD_INGOT, 1, "Iron -> Gold" }
};

static bool tradeHasInput(const TradeDef& t) {
    Player* p = g_level.player;
    if (!p || !p->inventory) return false;
    if (p->inventory->isCreative()) return true;
    int total = 0;
    for (int i = 0; i < p->inventory->getContainerSize(); ++i) {
        ItemInstance* it = p->inventory->getItem(i);
        if (it && !it->isNull() && it->id == t.inId && it->data == 0) total += it->count;
    }
    return total >= t.inCount;
}

static bool tradeDo(int idx) {
    if (!s_villager || idx < 0 || idx >= Villager::TRADE_COUNT) return false;
    const TradeDef& t = s_trades[idx];
    if (s_villager->tradeUses[idx] >= Villager::MAX_TRADE_USES) return false;
    if (!tradeHasInput(t)) return false;
    Player* p = g_level.player;
    if (!p || !p->inventory) return false;

    if (!p->inventory->isCreative()) {
        ItemInstance cost(t.inId, t.inCount, 0);
        if (p->inventory->removeResource(cost, true) != 0) return false;
    }
    ItemInstance* reward = new ItemInstance(t.outId, t.outCount, 0);
    if (!p->inventory->add(reward)) p->drop(reward);
    ++s_villager->tradeUses[idx];

    LevelChunk* c = worldSlot(g_level.w, (int)floorf(s_villager->x) >> 4,
                              (int)floorf(s_villager->z) >> 4);
    if (c && c->resident) c->unsaved = true;

    soundPlay("random.pop", 0.5f, 1.1f);
    return true;
}

void tradeOpen(Villager* villager) {
    s_villager = villager;
    s_cursor = 0;
    g_tradeOpen = true;
    soundPlay("random.click", 1.0f, 1.0f);
}
void tradeClose() {
    s_villager = 0;
    g_tradeOpen = false;
}
static bool tradeStillValid() {
    if (!s_villager || s_villager->removed || !g_level.player) return false;
    return s_villager->distanceTo(g_level.player) <= 4.0f;
}

struct TradeScreen : ContainerScreen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};
void TradeScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int held) {
    (void)s; (void)held;
    if (!tradeStillValid()) { tradeClose(); return; }
    if (pressed & PSP_CTRL_UP) {
        if (s_cursor > 0) --s_cursor;
        soundPlay("random.click", 0.6f, 1.0f);
    }
    if (pressed & PSP_CTRL_DOWN) {
        if (s_cursor < Villager::TRADE_COUNT - 1) ++s_cursor;
        soundPlay("random.click", 0.6f, 1.0f);
    }
    if (pressed & PSP_CTRL_CROSS) tradeDo(s_cursor);
    if (pressed & PSP_CTRL_CIRCLE) {
        tradeClose();
        soundPlay("random.click", 1.0f, 1.0f);
    }
}
static void drawTradeItem(MenuState& s, const ItemInstance& it, float x, float y) {
    drawNinePatch(s, GA_SS_SLOT_X, GA_SS_SLOT_Y, 8, 8, 2, x, y, 24, 24);
    if (!it.isNull()) drawGuiItem(s.font, it, G(x + 4), G(y + 4), G(16), WHITE, s.haveFont);
}
void TradeScreen::renderContent(MenuState& s) {
    if (!s_villager || !tradeStillValid()) return;
    sceGuDisable(GU_DEPTH_TEST);
    drawHeaderTitle(s, "Villager");
    const float x = 38.0f, y0 = 38.0f, rowH = 58.0f;
    for (int i = 0; i < Villager::TRADE_COUNT; ++i) {
        float y = y0 + i * rowH;
        bool selected = (i == s_cursor);
        drawNinePatch(s, GA_SS_PANE_X, GA_SS_PANE_Y, 8, 8, 2, x - 6, y - 4, 404, 50);
        if (selected)
            drawNinePatch(s, GA_SS_SLOT_X, GA_SS_SLOT_Y, 8, 8, 2, x - 3, y - 1, 398, 44);
        ItemInstance cost(s_trades[i].inId, s_trades[i].inCount, 0);
        ItemInstance reward(s_trades[i].outId, s_trades[i].outCount, 0);
        drawTradeItem(s, cost, x, y + 8);
        drawTradeItem(s, reward, x + 92, y + 8);
        if (s.haveFont) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%d / %d", s_villager->tradeUses[i], Villager::MAX_TRADE_USES);
            fontDrawTextShadow(&s.font, G(x + 128), G(y + 10), s_trades[i].name, 0xFFE0E0E0u, UI_SCALE);
            fontDrawTextShadow(&s.font, G(x + 128), G(y + 26), buf, 0xFFB0B0B0u, UI_SCALE);
        }
    }
    if (s.haveFont)
        fontDrawTextShadow(&s.font, G(42), G(218), "X: Trade   O: Close", 0xFFFFFFFFu, UI_SCALE);
    sceGuEnable(GU_DEPTH_TEST);
}
Screen& tradeScreen() { static TradeScreen s; return s; }
