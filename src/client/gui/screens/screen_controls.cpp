#include <pspctrl.h>
#include <pspgu.h>
#include <cstring>

#include "client/gui/screens/menu.h"
#include "client/gui/screens/screen.h"
#include "client/gui/hud.h"
#include "platform/audio/sound.h"
#include "gpu/gui_atlas.h"

bool g_controlsOpen = false;

void controlsOpen() {
    g_controlsOpen = true;
}

// --- Row layout ---------------------------------------------------------
//
// A flat, read-only reference list -- there is nothing to select or edit
// here, just a static legend of what every button currently does, grouped
// under headers the same way screen_achievements.cpp and
// screen_options.cpp group their own rows. Built as a fixed table rather
// than mirrored live off gameHintsDraw's per-context branches: this
// screen's job is to be the complete reference a player opens once to
// learn the game, not a live readout of the one or two hints relevant to
// whatever they're standing next to right now -- that job belongs to the
// upper-right toast (see hudKeyHintToast in hud.cpp), and the two are
// deliberately independent so a change to one doesn't silently drift the
// other out of sync content-wise; both simply need to be kept accurate
// against player.cpp and hud.cpp by hand when bindings change.

struct CtrlRow {
    bool        isHeader;
    const char* header;  // valid when isHeader
    ButtonIcon  icon;     // valid when !isHeader
    const char* action;   // valid when !isHeader
    const char* note;     // optional second line, valid when !isHeader; 0 if none
};

// A pseudo-icon count of -1 marks the rows (analog stick, D-pad, Select)
// that don't correspond to a single face/shoulder button glyph in
// button_icons.h -- they draw a small text badge instead of a ButtonIcon
// sprite.
enum { CTRL_ICON_STICK = BTN_ICON_COUNT, CTRL_ICON_DPAD, CTRL_ICON_SELECT };

static const CtrlRow kControlRows[] = {
    { true, "Movement", BTN_ICON_CROSS, 0, 0 },
    { false, 0, (ButtonIcon)CTRL_ICON_STICK, "Move / Look", "Analog stick" },
    { false, 0, BTN_ICON_CROSS, "Jump", "Hold in Creative to start flying (double-tap)" },
    { false, 0, (ButtonIcon)CTRL_ICON_DPAD, "Cycle Hotbar", "Left / Right" },
    { false, 0, BTN_ICON_UP, "Drop Item", "Tap to drop one, hold to drop the stack" },
    { false, 0, (ButtonIcon)CTRL_ICON_SELECT, "Pause", 0 },

    { true, "World Interaction", BTN_ICON_CROSS, 0, 0 },
    { false, 0, BTN_ICON_R, "Break / Attack", 0 },
    { false, 0, BTN_ICON_L, "Use / Place", "Also opens Inventory when the hand slot is selected" },
    { false, 0, BTN_ICON_UP, "Toggle Camera", "First / Third Person -- hand slot selected" },

    { true, "Inventory", BTN_ICON_CROSS, 0, 0 },
    { false, 0, BTN_ICON_CROSS, "Take / Place", 0 },
    { false, 0, BTN_ICON_SQUARE, "Crafting", 0 },
    { false, 0, BTN_ICON_TRIANGLE, "Armour", 0 },
    { false, 0, BTN_ICON_CIRCLE, "Back / Exit", 0 },

    { true, "Chest & Furnace", BTN_ICON_CROSS, 0, 0 },
    { false, 0, BTN_ICON_CROSS, "Take / Move", 0 },
    { false, 0, BTN_ICON_TRIANGLE, "Quick Move", 0 },
    { false, 0, BTN_ICON_SQUARE, "Take / Move Half", 0 },
    { false, 0, BTN_ICON_CIRCLE, "Exit", 0 },

    { true, "Crafting Table", BTN_ICON_CROSS, 0, 0 },
    { false, 0, BTN_ICON_CROSS, "Create", 0 },
    { false, 0, BTN_ICON_R, "Change Category", "When more than one is available" },
    { false, 0, BTN_ICON_CIRCLE, "Exit", 0 },

    { true, "Sleeping", BTN_ICON_CROSS, 0, 0 },
    { false, 0, BTN_ICON_L, "Wake Up", 0 },
};
#define CTRL_ROWS_MAX (int)(sizeof(kControlRows) / sizeof(kControlRows[0]))

static const float CTRL_HEADER_H = 14.0f;
static const float CTRL_ROW_H    = 20.0f;
static const float CTRL_LIST_X   = 8.0f;
static const float CTRL_LIST_Y0  = MENU_BAR_H + 4.0f;

static float ctrlContentHeight() {
    float h = 0.0f;
    for (int i = 0; i < CTRL_ROWS_MAX; ++i) h += kControlRows[i].isHeader ? CTRL_HEADER_H : CTRL_ROW_H;
    return h;
}

struct ControlsScreen : Screen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};

void ControlsScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int) {
    float contentH = ctrlContentHeight();
    float paneH = (UI_HINTS_Y / UI_SCALE - 1.0f) - CTRL_LIST_Y0;
    float maxScroll = contentH - paneH; if (maxScroll < 0.0f) maxScroll = 0.0f;

    // No per-row selection to move -- Up/Down just page the list, same
    // step as one row, since there's nothing here to land a highlight on.
    if (pressed & PSP_CTRL_UP)   s.ctrlScroll -= CTRL_ROW_H;
    if (pressed & PSP_CTRL_DOWN) s.ctrlScroll += CTRL_ROW_H;
    if (s.ctrlScroll < 0.0f) s.ctrlScroll = 0.0f;
    if (s.ctrlScroll > maxScroll) s.ctrlScroll = maxScroll;

    if (pressed & PSP_CTRL_CIRCLE) {
        soundPlay("random.click", 1.0f, 1.0f);
        g_controlsOpen = false;
    }
}

// Small square text badge for the two multi-button rows (analog stick,
// D-pad) that have no single ButtonIcon glyph of their own.
static void drawTextBadge(MenuState& s, float x, float y, const char* text) {
    const float w = 20.0f, h = 13.0f;
    drawRect(x * UI_SCALE, y * UI_SCALE, w * UI_SCALE, h * UI_SCALE, 0x60FFFFFFu);
    float tw = fontTextWidth(&s.font, text) * UI_SCALE * 0.6f;
    fontDrawTextShadow(&s.font, (x + (w - tw / UI_SCALE) / 2.0f) * UI_SCALE,
                       (y + 2.0f) * UI_SCALE, text, 0xFFFFFFFFu, UI_SCALE * 0.6f);
}

void ControlsScreen::renderContent(MenuState& s) {
    Font& font = s.font; bool haveFont = s.haveFont;
    if (!haveFont) return;

    sceGuDisable(GU_DEPTH_TEST);

    {
        float lb = 4.0f * MENU_PX + menuBarButtonW(s, "Back");
        drawMenuHeader(s, "Controls", 0.0f, VW, MENU_BAR_H, MENU_BAR_TEXT, lb, VW - lb);
    }
    {
        float bw = menuBarButtonW(s, "Back");
        menuBarButton(s, 4.0f * MENU_PX, bw, "Back", true);
    }

    float listX = CTRL_LIST_X, listW = VW - CTRL_LIST_X * 2.0f - 4.0f;
    float paneY0 = CTRL_LIST_Y0;
    float paneH  = (UI_HINTS_Y / UI_SCALE - 1.0f) - paneY0;
    float contentH = ctrlContentHeight();

    float maxScroll = contentH - paneH; if (maxScroll < 0.0f) maxScroll = 0.0f;
    if (s.ctrlScroll > maxScroll) s.ctrlScroll = maxScroll;
    if (s.ctrlScroll < 0.0f) s.ctrlScroll = 0.0f;
    float scroll = s.ctrlScroll;
    float rowY0 = paneY0 - scroll;

    sceGuScissor((int)(listX * UI_SCALE), (int)(paneY0 * UI_SCALE),
                 (int)(listW * UI_SCALE), (int)(paneH * UI_SCALE));

    const float iconColX = listX + 4.0f;
    const float textColX = listX + 28.0f;

    float y = rowY0;
    for (int i = 0; i < CTRL_ROWS_MAX; ++i) {
        const CtrlRow& r = kControlRows[i];
        float rowH = r.isHeader ? CTRL_HEADER_H : CTRL_ROW_H;
        if (y > paneY0 + paneH || y + rowH < paneY0 - rowH) { y += rowH; continue; }

        if (r.isHeader) {
            fontDrawTextShadow(&font, (listX + 2.0f) * UI_SCALE, (y + 2.0f) * UI_SCALE,
                               r.header, 0xFFFFD700u, UI_SCALE * 0.85f);
        } else {
            if ((int)r.icon == CTRL_ICON_STICK)       drawTextBadge(s, iconColX, y + 2.0f, "STK");
            else if ((int)r.icon == CTRL_ICON_DPAD)   drawTextBadge(s, iconColX, y + 2.0f, "D-P");
            else if ((int)r.icon == CTRL_ICON_SELECT) drawTextBadge(s, iconColX, y + 2.0f, "SEL");
            else buttonIconDraw(&s.guiAtlas, r.icon, iconColX * UI_SCALE, (y + 2.0f) * UI_SCALE, UI_SCALE);

            fontDrawTextClipped(&font, textColX * UI_SCALE, (y + 1.0f) * UI_SCALE,
                                r.action, 0xFFE0E0E0u, UI_SCALE, listW - (textColX - listX) - 4.0f);
            if (r.note)
                fontDrawTextWrapped(&font, textColX * UI_SCALE, (y + 10.0f) * UI_SCALE,
                                    r.note, 0xFFA0A0A0u, UI_SCALE * 0.7f, listW - (textColX - listX) - 4.0f);
        }
        y += rowH;
    }
    sceGuScissor(0, 0, 480, 272);

    guiScrollbar((VW - 3.0f) * UI_SCALE, paneY0 * UI_SCALE, 2.0f * UI_SCALE,
                 paneH * UI_SCALE, contentH * UI_SCALE, scroll * UI_SCALE);

    {
        ButtonHint h[1];
        int n = 0;
        h[n++] = (ButtonHint){ BTN_ICON_CIRCLE, PSP_CTRL_CIRCLE, "Back" };
        buttonHintsDraw(s, h, n);
    }

    sceGuEnable(GU_DEPTH_TEST);
}

static ControlsScreen s_controlsScreen;
Screen& controlsScreen() { return s_controlsScreen; }
