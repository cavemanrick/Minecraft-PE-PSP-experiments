#pragma once
#include "client/gui/screens/menu.h"
#include "world/item/item_instance.h"

void hotbarDraw(MenuState& s);
void guiFill(float x, float y, float w, float h, unsigned int color);
void guiFillGradient(float x, float y, float w, float h, unsigned int topColor, unsigned int botColor);

void guiScrollbar(float x, float y, float w, float h, float contentH, float scroll,
                  unsigned int alpha = 255);
void drawBlockIcon(short id, unsigned char data, float x, float y, float sizePx, unsigned int colorTint = 0xFFFFFFFFu);
void drawStackCount(Font& font, int count, float slotX, float slotY, float size);

void drawDurabilityBar(short id, short data, float x, float y, float sizePx);

void drawGuiItem(Font& font, const ItemInstance& item, float x, float y, float sizePx,
                 unsigned int tint = 0xFFFFFFFFu, bool showCount = true);
int getGuiBlockIcon(short id, unsigned char data);
const char* getBlockName(short id, unsigned char data);
const char* getBlockDescription(short id, unsigned char data);

int  itemFlatIcon(short id, unsigned char data);
void drawFlatIcon(int icon, float x, float y, float sizePx, unsigned int tint);

void hudChatMessage(const char* msg);

// Upper-right corner toast queue. hudAchievementToast is exposed mainly
// for symmetry / potential direct use elsewhere; achievement.cpp itself
// doesn't need to call it, since hud.cpp already drains
// achievementsPollNotification() into the same queue every frame.
//
// hudKeyHintToast currently has no caller: it drove a context-sensitive
// "Button: Action" toast (see gameHintsDraw in hud.cpp) that has been
// switched off for reading as constant reminders of basic controls
// rather than genuine notifications -- see the comment above ToastKind
// in hud.cpp. Left declared and implemented for a possible future
// tutorial system to reuse rather than removed.
void hudAchievementToast(const char* name);
void hudKeyHintToast(const char* button, const char* action);
