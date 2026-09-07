
#include <pspctrl.h>
#include <pspgu.h>
#include <psputility.h>
#include <cstdio>
#include <cstring>

#include "client/player/player.h"
#include "client/gui/screens/screen.h"
#include "gpu/sprite.h"
#include "platform/audio/sound.h"
#include "gpu/gui_atlas.h"
#include "world/level/world.h"
#include "world/level/level.h"
#include "client/debug_teleport.h"

bool g_paused        = false;
int  g_pauseSel      = 0;
bool g_thirdPerson   = false;
bool g_quitConfirm   = false;
int  g_quitConfirmSel = 1;
bool g_optionsOpen   = false;

extern World g_world;

// TEMPORARY dev/testing entry -- see client/debug_teleport.*. Only shown
// on a genuine 1024x1024-preset world (the only size with a reserved
// Nether region to jump into at all); every other world size gets the
// normal 4-button menu unchanged. This lives in the pause menu rather
// than as a raw in-game button combo specifically because every single
// button on the device is already bound to something during normal
// gameplay (checked exhaustively against player.cpp and gamemode.cpp --
// there is no free button or even a safely combinable pair, since
// existing handlers check bit-membership, not exact combos, so any
// button used here would still also trigger its normal action). The
// pause menu is already a dedicated, deliberately-entered context with
// no such collision risk.
static bool debugNetherEntryAvailable() {
    return worldHasReservedRegions(&g_world);
}

static const char* const kPauseButtons[] = {
    "Back to game", "Achievements", "Controls", "Options", "Save", "Quit to title", "Enter Nether (test)",
};
static const int PAUSE_BUTTONS_BASE = 6; // everything except the debug entry
static const int PAUSE_BUTTONS_MAX  = (int)(sizeof(kPauseButtons) / sizeof(kPauseButtons[0]));

static int pauseButtonCount() {
    return debugNetherEntryAvailable() ? PAUSE_BUTTONS_MAX : PAUSE_BUTTONS_BASE;
}

// Two columns instead of one long vertical stack. With up to 7 buttons
// (the debug Nether entry pushes a normally-6-button menu to 7) a single
// column at the old 25-unit pitch ran to y=194 on a 136-unit-tall screen
// -- more than the bottom third of the menu was drawn off the visible
// area entirely, with no scrolling to reach it. Splitting into two
// columns of up to 4 rows each keeps the tallest case within y=119,
// comfortably on screen.
//
// Left column gets the extra button when the count is odd, same
// convention as optionColumnSplit in screen_options.cpp and the create-
// world toggle grid in screen_create.cpp, so this stays correct
// automatically if a button is ever added or removed rather than
// depending on a hand-picked split index that could go stale.
static int pauseColumnSplit() {
    return (pauseButtonCount() + 1) / 2;
}
static bool pauseButtonInRightColumn(int i) {
    return i >= pauseColumnSplit();
}

static const float PAUSE_V2    = VW / 20.0f;

// MCPSP_MULTIPLAYER is 0 in this build (see menu.h), so the player-list
// panel below is compiled out and the button grid can use the screen's
// full width. If multiplayer is ever turned on, PAUSE_LIST_X/W still
// carve out the same right-hand strip they always have, and the two
// button columns need to fit to the left of it instead -- avail below
// picks the correct span for whichever configuration is compiled.
#if MCPSP_MULTIPLAYER
static const float PAUSE_LIST_W = 8.0f * PAUSE_V2;
static const float PAUSE_LIST_X = VW - PAUSE_LIST_W - PAUSE_V2;
static const float PAUSE_GRID_AVAIL_W = PAUSE_LIST_X - PAUSE_V2;
#else
static const float PAUSE_GRID_AVAIL_W = VW - 2.0f * PAUSE_V2;
#endif

static const float PAUSE_COL_GAP = PAUSE_V2;
static const float PAUSE_BTN_W  = (PAUSE_GRID_AVAIL_W - PAUSE_COL_GAP) / 2.0f;
static const float PAUSE_BTN_H  = 20.0f;
static const float PAUSE_PITCH  = 25.0f;
static const float PAUSE_BTN_Y  = 24.0f;
static const float PAUSE_COL0_X = PAUSE_V2;
static const float PAUSE_COL1_X = PAUSE_COL0_X + PAUSE_BTN_W + PAUSE_COL_GAP;

#if MCPSP_MULTIPLAYER
static const float PAUSE_LIST_Y = VH / 10.0f;
static const float PAUSE_LIST_H = 8.0f * (VH / 10.0f);
#endif

static const float PAUSE_ROW_H   = 15.0f;
static const float PAUSE_ROW_Y0  = 2.0f;
static const float PAUSE_NAME_X  = 3.0f;
static const float PAUSE_NAME_Y  = 4.0f;

const char* pausePlayerName() {
    static char s_name[64];
    static bool s_read = false;
    if (!s_read) {
        s_read = true;
        s_name[0] = '\0';
        sceUtilityGetSystemParamString(PSP_SYSTEMPARAM_ID_STRING_NICKNAME, s_name, sizeof(s_name));
        if (!s_name[0]) std::snprintf(s_name, sizeof(s_name), "Player");
    }
    return s_name;
}

int pausePlayerList(const char** outNames, bool* outIsLocal, int max) {
    if (max <= 0) return 0;
    outNames[0] = pausePlayerName();
    outIsLocal[0] = true;
    return 1;
}
struct PauseScreen : Screen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};

void PauseScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int ) {

    if (g_quitConfirm) {
        int before = g_quitConfirmSel;
        if (pressed & PSP_CTRL_LEFT)  g_quitConfirmSel = 0;
        if (pressed & PSP_CTRL_RIGHT) g_quitConfirmSel = 1;
        if (g_quitConfirmSel != before) soundPlay("random.click", 1.0f, 1.0f);
        if (pressed & PSP_CTRL_CIRCLE) { soundPlay("random.click", 1.0f, 1.0f); g_quitConfirm = false; return; }
        if (pressed & PSP_CTRL_CROSS) {
            soundPlay("random.click", 1.0f, 1.0f);
            g_quitConfirm = false;

            if (g_quitConfirmSel == 0) { quitToMenuNoSave(s); g_paused = false; }
        }
        return;
    }

    const int selBefore = g_pauseSel;
    int split = pauseColumnSplit();
    int count = pauseButtonCount();
    bool inRight = pauseButtonInRightColumn(g_pauseSel);
    int colStart = inRight ? split : 0;
    int colEnd   = inRight ? count : split; // exclusive
    int rowInCol = g_pauseSel - colStart;

    if ((pressed & PSP_CTRL_UP)   && g_pauseSel > colStart) g_pauseSel--;
    if ((pressed & PSP_CTRL_DOWN) && g_pauseSel < colEnd - 1) g_pauseSel++;

    // Left/Right hop to the same row in the other column. The right
    // column can be one shorter than the left (split favours the left
    // column on an odd count -- see pauseColumnSplit), so jumping to a
    // row that doesn't exist over there clamps to that column's last
    // row instead of landing past its end.
    if (pressed & PSP_CTRL_RIGHT) {
        if (!inRight) {
            int target = split + rowInCol;
            g_pauseSel = (target < count) ? target : count - 1;
        }
    }
    if (pressed & PSP_CTRL_LEFT) {
        if (inRight) {
            int target = rowInCol; // left column always has >= right column's row count
            g_pauseSel = target;
        }
    }

    if (g_pauseSel != selBefore) soundPlay("random.click", 1.0f, 1.0f);

    if (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_SELECT)) {
        soundPlay("random.click", 1.0f, 1.0f);
        g_paused = false;
        return;
    }
    if (pressed & PSP_CTRL_CROSS) {
        soundPlay("random.click", 1.0f, 1.0f);
        switch (g_pauseSel) {
            case 0: g_paused = false; break;
            case 1:
                achievementsOpen();
                g_paused = false;
                break;
            case 2:
                controlsOpen();
                g_paused = false;
                break;
            case 3:

                g_optionsOpen = true;
                g_paused = false;
                break;
            case 4: g_saveRequested = true; g_paused = false; break;
            case 5: g_quitConfirm = true; g_quitConfirmSel = 1; break;
            case 6:
                if (debugNetherEntryAvailable()) {
                    debugToggleNetherTeleport(&g_world, g_level.player);
                    g_paused = false;
                }
                break;
        }
    }
}

void guiTButton(MenuState& s, float x, float y, float w, float h, bool pressed,
                float destCorner) {
    drawNinePatch(s, GA_SS_SLOT_X + (pressed ? 0.0f : 8.0f), GA_SS_SLOT_Y,
                  8.0f, 8.0f, 2.0f, x, y, w, h, destCorner);
}

void guiTButtonLabel(MenuState& s, float x, float y, float w, float h,
                     const char* label, bool hovered, bool active, float scale) {
    if (!s.haveFont) return;

    unsigned int col = !active ? GUI_DISABLED : hovered ? 0xFFA0FFFFu : 0xFFE0E0E0u;
    float lw = fontTextWidth(&s.font, label) * scale;
    fontDrawTextShadow(&s.font, x * UI_SCALE + (w * UI_SCALE - lw) / 2.0f,
                       (y + h / 2.0f) * UI_SCALE - 4.0f * scale, label, col, scale);
}

void PauseScreen::renderContent(MenuState& s) {
    Font& font = s.font; bool haveFont = s.haveFont;
    bool haveGui = s.haveGui;

    sceGuDisable(GU_DEPTH_TEST);

    if (haveGui && haveFont) {

        const char* title = "Game menu";
        float tw = fontTextWidth(&font, title) * UI_SCALE;
        // Centred over the whole two-column grid (from PAUSE_COL0_X to
        // the right edge of the right column) rather than just the left
        // column's width, now that there are two columns to span.
        float gridSpan = (PAUSE_COL1_X + PAUSE_BTN_W) - PAUSE_COL0_X;
        fontDrawTextShadow(&font,
                           (PAUSE_COL0_X + gridSpan / 2.0f) * UI_SCALE - tw / 2.0f,
                           (PAUSE_BTN_Y - 11.0f) * UI_SCALE, title, 0xFFFFFFFFu, UI_SCALE);

        int split = pauseColumnSplit();
        for (int i = 0; i < pauseButtonCount(); i++) {
            bool hover = (g_pauseSel == i);
            bool rightCol = pauseButtonInRightColumn(i);
            int rowInCol = rightCol ? (i - split) : i;
            float bx = rightCol ? PAUSE_COL1_X : PAUSE_COL0_X;
            float by = PAUSE_BTN_Y + rowInCol * PAUSE_PITCH;
            guiTButton(s, bx, by, PAUSE_BTN_W, PAUSE_BTN_H, hover);
            guiTButtonLabel(s, bx, by, PAUSE_BTN_W, PAUSE_BTN_H,
                            kPauseButtons[i], hover, true);
        }

        // Player list. This is the in-world roster from MCPE's game menu,
        // which is why it is sized for 16 rows; with the multiplayer
        // front-end off there is never more than one name in it, so the
        // panel is a mostly-empty box showing the PSP system nickname
        // (which reads "PPSSPP" under the emulator, from
        // sceUtilityGetSystemParamString above). Hidden until there is
        // something to list.
#if MCPSP_MULTIPLAYER
        const unsigned int LIST_EDGE = 0x69000000u;
        const unsigned int LIST_FILL = 0x452D2D2Du;
        drawRect(PAUSE_LIST_X * UI_SCALE, PAUSE_LIST_Y * UI_SCALE,
                 PAUSE_LIST_W * UI_SCALE, PAUSE_LIST_H * UI_SCALE, LIST_EDGE);
        drawRect((PAUSE_LIST_X + 1.0f) * UI_SCALE, (PAUSE_LIST_Y + 1.0f) * UI_SCALE,
                 (PAUSE_LIST_W - 2.0f) * UI_SCALE, (PAUSE_LIST_H - 2.0f) * UI_SCALE, LIST_FILL);

        const char* names[16]; bool isLocal[16];
        int n = pausePlayerList(names, isLocal, 16);
        for (int i = 0; i < n; i++) {
            float ry = PAUSE_LIST_Y + PAUSE_ROW_Y0 + i * PAUSE_ROW_H;
            if (ry + PAUSE_ROW_H > PAUSE_LIST_Y + PAUSE_LIST_H) break;

            drawRect(PAUSE_LIST_X * UI_SCALE, ry * UI_SCALE,
                     PAUSE_LIST_W * UI_SCALE, PAUSE_ROW_H * UI_SCALE, LIST_FILL);
            drawRect(PAUSE_LIST_X * UI_SCALE, ry * UI_SCALE,
                     1.0f * UI_SCALE, PAUSE_ROW_H * UI_SCALE, LIST_EDGE);
            drawRect((PAUSE_LIST_X + PAUSE_LIST_W - 1.0f) * UI_SCALE, ry * UI_SCALE,
                     1.0f * UI_SCALE, PAUSE_ROW_H * UI_SCALE, LIST_EDGE);
            drawRect(PAUSE_LIST_X * UI_SCALE, (ry + PAUSE_ROW_H - 1.0f) * UI_SCALE,
                     PAUSE_LIST_W * UI_SCALE, 1.0f * UI_SCALE, LIST_EDGE);

            unsigned int col = isLocal[i] ? 0xFFFFFFFFu : 0xFF777777u;
            fontDrawTextClipped(&font, (PAUSE_LIST_X + PAUSE_NAME_X) * UI_SCALE,
                                (ry + PAUSE_NAME_Y) * UI_SCALE, names[i], col, UI_SCALE,
                                PAUSE_LIST_W - PAUSE_NAME_X * 2.0f);
        }
#endif
    }

    if (g_quitConfirm && haveGui && haveFont) {
        drawRect(0.0f, 0.0f, 480.0f, 272.0f, 0xB0000000u);
        const char* line = "Are you sure you want to exit without saving?";
        float lw = fontTextWidth(&font, line) * UI_SCALE;
        fontDrawTextShadow(&font, (VW * UI_SCALE - lw) / 2.0f, 48.0f * UI_SCALE,
                           line, 0xFFFFFFFFu, UI_SCALE);

        const float btnW = 90.0f, btnH = 20.0f, gap = 12.0f;
        const float totalW = btnW * 2 + gap;
        const float bx0 = (VW - totalW) / 2.0f, by = 66.0f;
        const char* clabels[2] = { "Quit", "Cancel" };
        for (int i = 0; i < 2; ++i) {
            bool hover = (g_quitConfirmSel == i);
            float bx = bx0 + i * (btnW + gap);
            guiTButton(s, bx, by, btnW, btnH, hover);
            guiTButtonLabel(s, bx, by, btnW, btnH, clabels[i], hover, true);
        }
    }

    {
        ButtonHint h[2];
        int n = 0;
        h[n++] = (ButtonHint){ BTN_ICON_CROSS,  PSP_CTRL_CROSS,  "Select" };
        h[n++] = (ButtonHint){ BTN_ICON_CIRCLE, PSP_CTRL_CIRCLE,
                               g_quitConfirm ? "Cancel" : "Back to game" };
        buttonHintsDraw(s, h, n);
    }

    sceGuEnable(GU_DEPTH_TEST);
}

static PauseScreen s_pauseScreen;
Screen& pauseScreen() { return s_pauseScreen; }
