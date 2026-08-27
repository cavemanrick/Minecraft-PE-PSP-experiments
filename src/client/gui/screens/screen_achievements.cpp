#include <pspctrl.h>
#include <pspgu.h>
#include <cstdio>
#include <cstring>

#include "client/gui/screens/menu.h"
#include "client/gui/screens/screen.h"
#include "client/gui/hud.h"
#include "platform/audio/sound.h"
#include "gpu/gui_atlas.h"
#include "world/achievement/achievement.h"

bool g_achievementsOpen = false;

void achievementsOpen() {
    g_achievementsOpen = true;
}

// --- Row layout -------------------------------------------------------
//
// One flat scrollable list, grouped by category header, the same
// grouping idiom screen_options.cpp uses for its "Game"/"Interface" etc.
// row groups -- a category header is just a row with no widgets of its
// own, drawn slightly differently, immediately before the first row of
// that category. Built fresh each time the achievements screen renders
// rather than cached, since the list itself never changes at runtime
// (only unlocked/progress state does) and 33 rows is cheap to walk every
// frame on top of everything else this screen already draws.

struct AchvRow {
    bool isHeader;
    AchievementCategory category; // valid when isHeader
    AchievementId id;             // valid when !isHeader
};

#define ACHV_ROWS_MAX (ACHV_COUNT + ACHV_CAT_COUNT)

static int buildRows(AchvRow* rows) {
    int n = 0;
    for (int c = 0; c < ACHV_CAT_COUNT; ++c) {
        bool wroteHeader = false;
        for (int i = 0; i < ACHV_COUNT; ++i) {
            const AchievementDef* d = achievementDef((AchievementId)i);
            if (!d || d->category != c) continue;
            if (!wroteHeader) {
                rows[n].isHeader = true;
                rows[n].category = (AchievementCategory)c;
                n++;
                wroteHeader = true;
            }
            rows[n].isHeader = false;
            rows[n].id = (AchievementId)i;
            n++;
        }
    }
    return n;
}

static const float ACHV_HEADER_H = 14.0f;
static const float ACHV_ROW_H    = 24.0f;
static const float ACHV_LIST_X   = 8.0f;
static const float ACHV_LIST_Y0  = MENU_BAR_H + 4.0f;

static float achvContentHeight(const AchvRow* rows, int n) {
    float h = 0.0f;
    for (int i = 0; i < n; ++i) h += rows[i].isHeader ? ACHV_HEADER_H : ACHV_ROW_H;
    return h;
}

// Y offset (from the top of the list, before scroll is subtracted) of the
// given row index -- used both to draw and to keep the selected row
// scrolled into view, same approach as screen_options.cpp's
// optionRowY/selY scroll-clamp logic.
static float achvRowY(const AchvRow* rows, int index) {
    float y = 0.0f;
    for (int i = 0; i < index; ++i) y += rows[i].isHeader ? ACHV_HEADER_H : ACHV_ROW_H;
    return y;
}

// Index of the next/previous *non-header* row from a starting index,
// clamped to stay in range -- keeps Up/Down navigation from ever landing
// selection on a category header, which has nothing to select.
static int skipToSelectable(const AchvRow* rows, int n, int from, int dir) {
    int i = from;
    while (i >= 0 && i < n && rows[i].isHeader) i += dir;
    if (i < 0 || i >= n) return from; // no selectable row that direction; stay put
    return i;
}

struct AchievementsScreen : Screen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};

void AchievementsScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int) {
    AchvRow rows[ACHV_ROWS_MAX];
    int n = buildRows(rows);
    if (s.achvSelected < 0 || s.achvSelected >= n) s.achvSelected = skipToSelectable(rows, n, 0, +1);

    if (pressed & PSP_CTRL_UP) {
        int next = skipToSelectable(rows, n, s.achvSelected - 1, -1);
        if (next != s.achvSelected) { s.achvSelected = next; soundPlay("random.click", 1.0f, 1.0f); }
    }
    if (pressed & PSP_CTRL_DOWN) {
        int next = skipToSelectable(rows, n, s.achvSelected + 1, +1);
        if (next != s.achvSelected) { s.achvSelected = next; soundPlay("random.click", 1.0f, 1.0f); }
    }
    if (pressed & PSP_CTRL_CIRCLE) {
        soundPlay("random.click", 1.0f, 1.0f);
        g_achievementsOpen = false;
    }
}

void AchievementsScreen::renderContent(MenuState& s) {
    Font& font = s.font; bool haveFont = s.haveFont;
    if (!haveFont) return;

    sceGuDisable(GU_DEPTH_TEST);

    AchvRow rows[ACHV_ROWS_MAX];
    int n = buildRows(rows);
    if (s.achvSelected < 0 || s.achvSelected >= n) s.achvSelected = skipToSelectable(rows, n, 0, +1);

    {
        float lb = 4.0f * MENU_PX + menuBarButtonW(s, "Back");
        char title[40];
        std::snprintf(title, sizeof(title), "Achievements (%d/%d)",
                      achievementsUnlockedCount(), ACHV_COUNT);
        drawMenuHeader(s, title, 0.0f, VW, MENU_BAR_H, MENU_BAR_TEXT, lb, VW - lb);
    }
    {
        float bw = menuBarButtonW(s, "Back");
        menuBarButton(s, 4.0f * MENU_PX, bw, "Back", true);
    }

    float listX = ACHV_LIST_X, listW = VW - ACHV_LIST_X * 2.0f - 4.0f;
    float paneY0 = ACHV_LIST_Y0;
    float paneH  = (UI_HINTS_Y / UI_SCALE - 1.0f) - paneY0;
    float contentH = achvContentHeight(rows, n);

    // Same scroll-into-view clamp shape as screen_options.cpp: find the
    // selected row's on-screen span, then adjust scroll only enough to
    // bring it fully into the visible pane, never re-centring it.
    float selY = achvRowY(rows, s.achvSelected);
    float selH = ACHV_ROW_H;
    float scroll = s.achvScroll;
    if (selY < scroll)                scroll = selY;
    if (selY + selH > scroll + paneH) scroll = selY + selH - paneH;
    float maxScroll = contentH - paneH; if (maxScroll < 0.0f) maxScroll = 0.0f;
    if (scroll > maxScroll) scroll = maxScroll;
    if (scroll < 0.0f) scroll = 0.0f;
    s.achvScroll = scroll;
    float rowY0 = paneY0 - scroll;

    sceGuScissor((int)(listX * UI_SCALE), (int)(paneY0 * UI_SCALE),
                 (int)(listW * UI_SCALE), (int)(paneH * UI_SCALE));

    float y = rowY0;
    for (int i = 0; i < n; ++i) {
        float rowH = rows[i].isHeader ? ACHV_HEADER_H : ACHV_ROW_H;
        if (y > paneY0 + paneH || y + rowH < paneY0 - rowH) { y += rowH; continue; }

        if (rows[i].isHeader) {
            fontDrawTextShadow(&font, (listX + 2.0f) * UI_SCALE, (y + 2.0f) * UI_SCALE,
                               achievementCategoryName(rows[i].category), 0xFFFFD700u, UI_SCALE * 0.85f);
        } else {
            const AchievementDef* d = achievementDef(rows[i].id);
            bool unlocked = achievementUnlocked(rows[i].id);
            bool selected = (i == s.achvSelected);

            unsigned int rowBg = selected ? 0x50FFFFFFu : 0x25000000u;
            drawRect(listX * UI_SCALE, y * UI_SCALE, listW * UI_SCALE, (ACHV_ROW_H - 1.0f) * UI_SCALE, rowBg);

            // A defined-but-not-yet-reachable achievement (no Ghast/
            // fortress/villager in the game yet) is shown, not hidden --
            // omitting it would make completionists think the list itself
            // is incomplete or buggy -- but dimmed and explicitly labelled
            // so it doesn't read as a locked-but-earnable goal.
            unsigned int nameCol;
            if (!d->implemented)  nameCol = 0xFF707070u;
            else if (unlocked)    nameCol = 0xFF60FF60u;
            else                  nameCol = 0xFFE0E0E0u;

            fontDrawTextClipped(&font, (listX + 4.0f) * UI_SCALE, (y + 1.0f) * UI_SCALE,
                                d->name, nameCol, UI_SCALE, listW - 8.0f);

            const char* desc = d->description;
            char descBuf[64];
            if (!d->implemented) {
                std::snprintf(descBuf, sizeof(descBuf), "%s (not yet available)", d->description);
                desc = descBuf;
            }
            fontDrawTextClipped(&font, (listX + 4.0f) * UI_SCALE, (y + 10.0f) * UI_SCALE,
                                desc, unlocked ? 0xFFA0FFA0u : 0xFF909090u, UI_SCALE * 0.7f, listW - 8.0f);

            int target = achievementProgressTarget(rows[i].id);
            if (target > 0 && !unlocked) {
                int prog = achievementProgress(rows[i].id);
                if (prog < 0) prog = 0;
                if (prog > target) prog = target;
                char progBuf[24];
                std::snprintf(progBuf, sizeof(progBuf), "%d/%d", prog, target);
                float pw = fontTextWidth(&font, progBuf) * UI_SCALE * 0.7f;
                fontDrawTextShadow(&font, (listX + listW - pw - 4.0f) * UI_SCALE, (y + 10.0f) * UI_SCALE,
                                   progBuf, 0xFFC0C0C0u, UI_SCALE * 0.7f);
            } else if (unlocked) {
                const char* tick = "DONE";
                float tw = fontTextWidth(&font, tick) * UI_SCALE * 0.7f;
                fontDrawTextShadow(&font, (listX + listW - tw - 4.0f) * UI_SCALE, (y + 10.0f) * UI_SCALE,
                                   tick, 0xFF60FF60u, UI_SCALE * 0.7f);
            }
        }
        y += rowH;
    }
    sceGuScissor(0, 0, 480, 272);

    guiScrollbar((VW - 3.0f) * UI_SCALE, paneY0 * UI_SCALE, 2.0f * UI_SCALE,
                 paneH * UI_SCALE, contentH * UI_SCALE, scroll * UI_SCALE);

    {
        ButtonHint h[1];
        int n2 = 0;
        h[n2++] = (ButtonHint){ BTN_ICON_CIRCLE, PSP_CTRL_CIRCLE, "Back" };
        buttonHintsDraw(s, h, n2);
    }

    sceGuEnable(GU_DEPTH_TEST);
}

static AchievementsScreen s_achievementsScreen;
Screen& achievementsScreen() { return s_achievementsScreen; }
