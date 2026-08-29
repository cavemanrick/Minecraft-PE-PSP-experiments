
#include <pspctrl.h>
#include <pspgu.h>
#include <cstdio>
#include <cstring>

#include "client/gui/screens/menu.h"
#include "client/gui/screens/screen.h"
#include "client/gui/hud.h"
#include "gpu/sprite.h"
#include "gpu/gui_atlas.h"
#include "world/level/levelgen/level_source.h"
#include "world/level/levelgen/gen_features.h"
#include "world/level/levelgen/cheat_spawn_content.h"
#include "world/level/storage/worldlist.h"
#include "world/level/world.h"

struct CreateScreen : Screen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};

namespace {

const float PX     = 0.625f;
const float TEXT_S = 1.0f;

const float BEVEL = 2.0f * PX;

struct CreateRowDef {
    const char* label;
    const char* placeholder;
    int         oskTarget;
    const char* oskPrompt;
    bool        advancedOnly;
};

const CreateRowDef kFieldRows[] = {
    { "Name", "New world", 1, "Enter World Name:", false },

    { "Seed", 0,           2, "Enter World Seed:", true  },
};
const int FIELD_COUNT = (int)(sizeof(kFieldRows) / sizeof(kFieldRows[0]));
const int ROW_NAME = 0, ROW_SEED = 1;

const int ROW_COUNT = FIELD_COUNT + GEN_FEATURE_COUNT;

bool rowIsToggle(int row)   { return row >= FIELD_COUNT; }
int  rowFeature(int row)    { return row - FIELD_COUNT; }
const char* rowLabel(int row) {
    return rowIsToggle(row) ? kGenFeatures[rowFeature(row)].label : kFieldRows[row].label;
}

enum { FOCUS_TYPE_OLD = ROW_COUNT, FOCUS_TYPE_FLAT,
       FOCUS_SIZE_512, FOCUS_SIZE_1024, FOCUS_SIZE_INFINITE,
       FOCUS_SURVIVAL, FOCUS_CREATIVE, FOCUS_CREATE,
       FOCUS_BACK, FOCUS_ADVANCED, FOCUS_COUNT };

// World size presets shown in the advanced panel. Values here are the
// actual chunk-count sizeX/sizeZ passed to worldInitTerrain/worldListCreate
// (0 meaning infinite, matching the same convention worldChunkInBounds
// uses) -- see world.h's WORLD_PRESET_* / WORLD_PRESET_1024_TOTAL_*
// constants for where these numbers come from and why the 1024 preset's
// actual sizeX isn't simply "1024/16": it reserves extra width beyond the
// overworld's own 1024x1024 extent for the Nether/End regions, so the
// overworld itself is genuinely 1024x1024 rather than being shrunk to fit
// inside a 1024 total.
//
// Infinite worlds have no reserved regions at all (worldHasReservedRegions
// returns false for sizeX == 0), so they have no Nether -- portals and the
// debug Nether-entry both already gate on worldHasReservedRegions and
// correctly refuse on an Infinite world rather than carving a fake Nether
// pocket into real overworld terrain (see nether_portal.cpp's
// netherPortalsSupported for the bug this guards against). Villages and
// dungeons have no such dependency -- they generate purely from live
// terrain at the chunk being populated -- so both remain fully available
// on Infinite worlds; only the Nether Fortress toggle is meaningless here,
// since chunkGenerateNether is never reached when there is no reserved
// Nether strip to route a chunk into.
//
// The 512 and 1024 presets both reserve extra width for their Nether/End
// strips, so their two values here are the TOTAL logical bound rather
// than the player-facing size. The overworld is still a full 512x512 or
// 1024x1024 -- the strips are appended past it, not carved out.
enum { WORLD_SIZE_PRESET_512 = 1, WORLD_SIZE_PRESET_1024 = 2, WORLD_SIZE_PRESET_INFINITE = 3 };

void sizePresetChunks(int preset, int* outX, int* outZ) {
    if (preset == WORLD_SIZE_PRESET_1024) {
        *outX = WORLD_PRESET_1024_TOTAL_X_CHUNKS;
        *outZ = WORLD_PRESET_1024_TOTAL_Z_CHUNKS;
    } else if (preset == WORLD_SIZE_PRESET_INFINITE) {
        *outX = 0;
        *outZ = 0;
    } else {
        *outX = WORLD_PRESET_512_TOTAL_X_CHUNKS;
        *outZ = WORLD_PRESET_512_TOTAL_Z_CHUNKS;
    }
}

const char* sizePresetLabel(int preset) {
    if (preset == WORLD_SIZE_PRESET_1024)     return "1024x1024";
    if (preset == WORLD_SIZE_PRESET_INFINITE) return "Infinite";
    return "512x512";
}

const float ROW_LABEL_H = 16.0f * PX;
const float ROW_BOX_H   = 18.0f * PX;

const float LABEL_GAP = 5.0f;

float rowHeight() {
    return ROW_LABEL_H + ROW_BOX_H + 13.0f * PX;
}

bool s_advanced = false;

// Flat worlds are disabled for now. Kept as a single named constant
// (checked from both handleInput and renderContent, hence file scope
// rather than a local in either function) so re-enabling this later is a
// one-line change, same shape as toggleRowUsable's per-row gate above.
const bool kFlatDisabled = true;

int  s_lastHeader = FOCUS_BACK;

char* rowText(MenuState& s, int row) {
    return (row == ROW_NAME) ? s.newWorldName : s.newWorldSeed;
}

bool rowVisible(int row) { return s_advanced || (!rowIsToggle(row) && !kFieldRows[row].advancedOnly); }
bool rowInLeftColumn(int row) { return !rowIsToggle(row) && row != ROW_SEED; }

bool genFeaturesUsable(const MenuState& s) {
    return levelSourceFor(s.newWorldType).supportsGenFeatures();
}
// Nether Fortress specifically (not the other toggles) is meaningless on
// an Infinite world: chunkGenerateNether is only ever reached through a
// reserved Nether strip, and Infinite worlds have none (see the
// WORLD_SIZE_PRESET_INFINITE comment above). Villages/dungeons/caves have
// no such dependency and stay fully togglable. Kept separate from
// genFeaturesUsable, which is a whole-panel gate keyed on world TYPE
// (flat worlds disable every toggle) -- this is a single-row exception
// keyed on world SIZE, and conflating the two would make either one
// harder to reason about.
bool toggleRowUsable(const MenuState& s, int row) {
    if (!genFeaturesUsable(s)) return false;
    if (rowFeature(row) == GEN_FEATURE_NETHER_FORTRESS &&
        s.newWorldSizePreset == WORLD_SIZE_PRESET_INFINITE)
        return false;
    return true;
}
bool rowFocusable(const MenuState& s, int row) {
    if (!rowVisible(row)) return false;
    return !rowIsToggle(row) || toggleRowUsable(s, row);
}

int toggleStep(const MenuState& s, int from, int dir) {
    for (int r = from + dir; r >= FIELD_COUNT && r < ROW_COUNT; r += dir)
        if (rowFocusable(s, r)) return r;
    return -1;
}

const float TOG_W   = 38.0f * PX;
const float TOG_H   = 20.0f * PX;
const float TOG_ROW = ROW_BOX_H + 5.0f * PX;

const char* modeDescription(int gamemode) {
    return gamemode == 1
        ? "Easily destroy and place blocks. No damage, flying and other cool stuff."
        : "Limited resources, you'll need tools. You may get hurt. Watch out for Monsters.";
}

void drawFieldLabel(Font& font, float x, float widgetY, const char* text) {
    fontDrawTextShadow(&font, x * UI_SCALE,
                       widgetY * UI_SCALE - 8.0f * TEXT_S - LABEL_GAP,
                       text, 0xFFFFFFFFu, TEXT_S);
}

struct Layout {
    float headerH, btnH, hdrBtnY, backX, backW, advX, advW;
    float panelX, panelY, panelW, panelH;
    float formX, formY, formW, formH, boxW, contentH;
    float typeY, sizeY, modeY, pillW, pillH, pill0X, pill1X;
    float pillW3, pill0X3, pill1X3, pill2X3;
    float descX, descY, descW;
    float createX, createY, createW, createH;
};

Layout layout(MenuState& s) {
    Layout L;

    L.btnH    = 18.0f * MENU_PX;
    L.headerH = L.btnH + 8.0f * MENU_PX;
    L.hdrBtnY = (L.headerH - L.btnH) / 2.0f;
    L.backW   = menuBarButtonW(s, "Back");
    L.backX   = 4.0f * MENU_PX;
    L.advW    = menuBarButtonW(s, "Advanced");
    L.advX    = VW - L.advW - 4.0f * MENU_PX;

    L.panelX = 5.0f * PX;
    L.panelY = L.headerH + 8.0f * PX;
    L.panelW = VW - 10.0f * PX;

    L.panelH = (UI_HINTS_Y / UI_SCALE - 1.0f) - L.panelY;

    L.formX = L.panelX + 5.0f * PX;
    L.formY = L.panelY + 6.0f * PX;
    L.boxW  = VW * 0.28f;
    L.formW = VW * 0.5f - L.formX;

    L.pillH  = 26.0f * PX;
    L.pillW  = VW * 0.205f;
    L.pill0X = L.formX;
    L.pill1X = L.formX + L.pillW + 6.0f * PX;

    // Size row has three options, not two -- narrower pills so all three
    // fit the same span the two-pill Type/Mode rows use (pill0X to
    // pill1X+pillW), rather than reusing pillW verbatim and overflowing.
    L.pillW3  = (L.pillW * 2.0f - 4.0f * PX) / 3.0f;
    L.pill0X3 = L.formX;
    L.pill1X3 = L.pill0X3 + L.pillW3 + 3.0f * PX;
    L.pill2X3 = L.pill1X3 + L.pillW3 + 3.0f * PX;

    L.descX = VW * 0.52f;
    L.descW = VW * 0.44f;

    L.createW = L.descW;
    L.createH = 26.0f * PX;
    L.createX = L.descX;

    if (s_advanced) {

        L.typeY   = L.panelY + L.panelH * 0.34f;
        L.sizeY   = L.panelY + L.panelH * 0.52f;
        L.modeY   = L.panelY + L.panelH * 0.78f;
        L.createY = L.modeY;
        L.descY   = 0.0f;
        L.formH   = L.typeY - LABEL_GAP / UI_SCALE - 4.0f * PX - L.formY;
    } else {
        L.typeY   = 0.0f;
        L.sizeY   = 0.0f;
        L.modeY   = L.panelY + L.panelH * 0.52f;
        L.createY = L.panelY + L.panelH - L.createH - 6.0f * PX;
        L.descY   = L.modeY;
        L.formH   = L.modeY - LABEL_GAP / UI_SCALE - 4.0f * PX - L.formY;
    }

    L.contentH = 0.0f;
    for (int i = 0; i < ROW_COUNT; i++)
        if (rowVisible(i) && rowInLeftColumn(i)) L.contentH += rowHeight();
    return L;
}

int effectiveGameMode(const MenuState& s) {
    int forced = levelSourceFor(s.newWorldType).forcedGameType();
    return (forced >= 0) ? forced : s.newWorldGamemode;
}
bool gameModeLocked(const MenuState& s) {
    return levelSourceFor(s.newWorldType).forcedGameType() >= 0;
}

}

void CreateScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int ) {
    int& sel = s.createSelected;
    if (sel < 0 || sel >= FOCUS_COUNT) sel = 0;

    if (pressed & PSP_CTRL_TRIANGLE) {
        s_advanced = !s_advanced;
        if (!s_advanced && (sel == ROW_SEED || rowIsToggle(sel) ||
                            sel == FOCUS_TYPE_OLD || sel == FOCUS_TYPE_FLAT ||
                            sel == FOCUS_SIZE_512 || sel == FOCUS_SIZE_1024 ||
                            sel == FOCUS_SIZE_INFINITE))
            sel = ROW_NAME;
    }

    const bool locked = gameModeLocked(s);
    const int  modePill = effectiveGameMode(s) ? FOCUS_CREATIVE : FOCUS_SURVIVAL;
    const int  typePill = (s.newWorldType == WORLD_TYPE_FLAT) ? FOCUS_TYPE_FLAT : FOCUS_TYPE_OLD;
    const int  sizePill = (s.newWorldSizePreset == WORLD_SIZE_PRESET_1024) ? FOCUS_SIZE_1024
                        : (s.newWorldSizePreset == WORLD_SIZE_PRESET_INFINITE) ? FOCUS_SIZE_INFINITE
                                                                          : FOCUS_SIZE_512;
    const bool onHeader = (sel == FOCUS_BACK || sel == FOCUS_ADVANCED);
    const bool onType   = (sel == FOCUS_TYPE_OLD || sel == FOCUS_TYPE_FLAT);
    const bool onSize   = (sel == FOCUS_SIZE_512 || sel == FOCUS_SIZE_1024 || sel == FOCUS_SIZE_INFINITE);
    const bool onMode   = (sel == FOCUS_SURVIVAL || sel == FOCUS_CREATIVE);

    const int belowType  = locked ? FOCUS_CREATE : modePill;
    const int aboveCreate = locked ? (s_advanced ? sizePill : ROW_NAME) : modePill;

    const int firstToggle = toggleStep(s, FIELD_COUNT - 1, +1);
    const bool onToggle   = (sel >= FIELD_COUNT && sel < ROW_COUNT);

    if (pressed & PSP_CTRL_DOWN) {
        if (onHeader)                sel = ROW_NAME;
        else if (sel == ROW_NAME)    sel = s_advanced ? typePill : belowType;
        else if (sel == ROW_SEED)    sel = (firstToggle >= 0) ? firstToggle : typePill;
        else if (onToggle)           { int n = toggleStep(s, sel, +1);
                                       sel = (n >= 0) ? n : typePill; }
        else if (onType)             sel = sizePill;
        else if (onSize)             sel = belowType;
        else if (onMode)             sel = FOCUS_CREATE;
    }
    if (pressed & PSP_CTRL_UP) {
        if (sel == FOCUS_CREATE)  sel = aboveCreate;
        else if (onMode)          sel = s_advanced ? sizePill : ROW_NAME;
        else if (onSize)          sel = typePill;
        else if (onType)          sel = ROW_NAME;
        else if (onToggle)        { int p = toggleStep(s, sel, -1);
                                    sel = (p >= 0) ? p : ROW_SEED; }
        else if (sel == ROW_SEED) sel = ROW_NAME;
        else if (sel == ROW_NAME) sel = s_lastHeader;
    }
    if (pressed & PSP_CTRL_RIGHT) {
        if (sel == FOCUS_BACK)             sel = FOCUS_ADVANCED;
        else if (sel == ROW_NAME && s_advanced) sel = ROW_SEED;
        else if (sel == FOCUS_TYPE_OLD && !kFlatDisabled) { sel = FOCUS_TYPE_FLAT; s.newWorldType = WORLD_TYPE_FLAT; }
        else if (sel == FOCUS_SIZE_512)      { sel = FOCUS_SIZE_1024; s.newWorldSizePreset = WORLD_SIZE_PRESET_1024; }
        else if (sel == FOCUS_SIZE_1024)     { sel = FOCUS_SIZE_INFINITE; s.newWorldSizePreset = WORLD_SIZE_PRESET_INFINITE; }
        else if (sel == FOCUS_SURVIVAL && !locked) { sel = FOCUS_CREATIVE; s.newWorldGamemode = 1; }
        else if (sel == FOCUS_CREATIVE || (sel == FOCUS_SURVIVAL && locked)) sel = FOCUS_CREATE;
    }
    if (pressed & PSP_CTRL_LEFT) {
        if (sel == FOCUS_ADVANCED)       sel = FOCUS_BACK;
        else if (sel == ROW_SEED)        sel = ROW_NAME;
        else if (onToggle)               sel = ROW_NAME;
        else if (sel == FOCUS_TYPE_FLAT) { sel = FOCUS_TYPE_OLD; s.newWorldType = WORLD_TYPE_OLD; }
        else if (sel == FOCUS_SIZE_INFINITE) { sel = FOCUS_SIZE_1024; s.newWorldSizePreset = WORLD_SIZE_PRESET_1024; }
        else if (sel == FOCUS_SIZE_1024)   { sel = FOCUS_SIZE_512;      s.newWorldSizePreset = WORLD_SIZE_PRESET_512; }
        else if (sel == FOCUS_CREATIVE && !locked) { sel = FOCUS_SURVIVAL; s.newWorldGamemode = 0; }
        else if (sel == FOCUS_CREATE)    sel = aboveCreate;
    }
    if (sel == FOCUS_BACK || sel == FOCUS_ADVANCED) s_lastHeader = sel;

    if (gameModeLocked(s) && (sel == FOCUS_SURVIVAL || sel == FOCUS_CREATIVE))
        sel = FOCUS_CREATE;

    if (pressed & PSP_CTRL_CIRCLE) s.screen = SCREEN_WORLDS;

    if (pressed & PSP_CTRL_CROSS) {
        if (sel < ROW_COUNT && rowIsToggle(sel)) {
            if (toggleRowUsable(s, sel))
                s.newWorldGenMask = genFeatureToggled(s.newWorldGenMask, rowFeature(sel));
        } else if (sel < ROW_COUNT) {
            const CreateRowDef& row = kFieldRows[sel];
            startOsk(row.oskTarget, row.oskPrompt, rowText(s, sel));
        } else if (sel == FOCUS_TYPE_OLD)  { s.newWorldType = WORLD_TYPE_OLD;
        } else if (sel == FOCUS_TYPE_FLAT) { if (!kFlatDisabled) s.newWorldType = WORLD_TYPE_FLAT;
        } else if (sel == FOCUS_SIZE_512)      { s.newWorldSizePreset = WORLD_SIZE_PRESET_512;
        } else if (sel == FOCUS_SIZE_1024)     { s.newWorldSizePreset = WORLD_SIZE_PRESET_1024;
        } else if (sel == FOCUS_SIZE_INFINITE) { s.newWorldSizePreset = WORLD_SIZE_PRESET_INFINITE;
        } else if (sel == FOCUS_SURVIVAL)  { if (!locked) s.newWorldGamemode = 0;
        } else if (sel == FOCUS_CREATIVE)  { if (!locked) s.newWorldGamemode = 1;
        } else if (sel == FOCUS_BACK)      { s.screen = SCREEN_WORLDS;
        } else if (sel == FOCUS_ADVANCED)  { s_advanced = !s_advanced;
        } else if (sel == FOCUS_CREATE) {
            char created[64];

            // Dev-only escape hatch: typing "cheat" as the seed (case
            // sensitive, exact match) creates a genuinely random world
            // (same seed roll an empty seed box would produce, not a hash
            // of the literal word "cheat" -- a hash would make every
            // cheat world identical, which isn't "random") on the 512
            // preset specifically, since that's the smaller of the two
            // presets that actually carries a reserved Nether strip
            // (worldHasReservedRegions in world.h) -- without one, the
            // portal placeCheatSpawnContent builds would have nowhere to
            // lead. Gamemode is likewise forced to Survival (0) rather
            // than whatever the pill was last set to: FillingContainer::
            // add() is a silent no-op in Creative (line 121, the item is
            // deleted and true is returned since Creative doesn't need
            // granted stacks), so a Creative cheat world would otherwise
            // report success while the promised pickaxe/saddle never
            // actually appear. See cheat_spawn_content.cpp for what
            // actually gets placed at spawn.
            bool cheatSeed = (strcmp(s.newWorldSeed, "cheat") == 0);
            long seed = cheatSeed ? worldSeedFromString("") : worldSeedFromString(s.newWorldSeed);
            // Dev-only escape hatch: typing "debug" as the seed (case
            // sensitive, exact match) creates a WORLD_TYPE_DEBUG world
            // instead of whatever's selected in the type toggle. This
            // isn't wired into the type pill UI since it's a personal
            // testing tool, not something meant for the normal
            // create-world flow -- see debug_spawn_content.cpp for what
            // actually gets placed at spawn.
            int worldType = s.newWorldType;
            if (strcmp(s.newWorldSeed, "debug") == 0) worldType = WORLD_TYPE_DEBUG;

            int sizeX, sizeZ;
            sizePresetChunks(cheatSeed ? WORLD_SIZE_PRESET_512 : s.newWorldSizePreset, &sizeX, &sizeZ);

            g_cheatWorldPending = cheatSeed;

            if (worldListCreate(&s.worlds, s.newWorldName, created,
                                cheatSeed ? 0 : effectiveGameMode(s), seed, worldType,
                                s.newWorldGenMask, sizeX, sizeZ)) {
                snprintf(s.statusMsg, sizeof(s.statusMsg), "Loading: %s", created);
                s.worldSelected = s.worlds.count - 1;
                s.screen = SCREEN_GAME;
            } else {
                g_cheatWorldPending = false; // creation failed; nothing will
                                              // ever reach the spawn hook to
                                              // consume this, so clear it
                                              // rather than leave it armed
                                              // for a later, unrelated world
                s.screen = SCREEN_WORLDS;
            }
            s.uiRow = 1;
        }
    }
}

void CreateScreen::renderContent(MenuState& s) {
    if (!s.haveFont || !s.haveGui) return;
    Font& font = s.font;
    const int sel = s.createSelected;
    const Layout L = layout(s);
    const bool locked = gameModeLocked(s);
    const int  mode   = effectiveGameMode(s);

    sceGuDisable(GU_DEPTH_TEST);

    {
        float lb = L.backX + L.backW, rb = L.advX;
        drawMenuHeader(s, "Create a World", 0.0f, VW, L.headerH, MENU_BAR_TEXT, lb, rb - lb);
    }
    menuBarButton(s, L.backX, L.backW, "Back", sel == FOCUS_BACK);

    guiTButton(s, L.advX, L.hdrBtnY, L.advW, L.btnH, s_advanced, MENU_BEVEL);

    guiTButtonLabel(s, L.advX, L.hdrBtnY, L.advW, L.btnH, "Advanced",
                    sel == FOCUS_ADVANCED, true, MENU_BAR_TEXT);

    drawNinePatch(s, GA_SS_PANEL, 3.0f, L.panelX, L.panelY, L.panelW, L.panelH, 3.0f * PX);

    float scroll = s.createScroll;

    if (sel < ROW_COUNT && rowInLeftColumn(sel) && rowVisible(sel)) {
        float selY = 0.0f;
        for (int i = 0; i < sel; i++)
            if (rowVisible(i) && rowInLeftColumn(i)) selY += rowHeight();
        if (selY < scroll) scroll = selY;
        if (selY + rowHeight() > scroll + L.formH) scroll = selY + rowHeight() - L.formH;
    }
    float maxScroll = L.contentH - L.formH;
    if (maxScroll < 0.0f) maxScroll = 0.0f;
    if (scroll < 0.0f) scroll = 0.0f; else if (scroll > maxScroll) scroll = maxScroll;
    s.createScroll = scroll;

    sceGuScissor((int)(L.panelX * UI_SCALE), (int)(L.formY * UI_SCALE),
                 (int)(L.formW * UI_SCALE), (int)(L.formH * UI_SCALE));
    {
        float rowY = L.formY - scroll;
        for (int i = 0; i < ROW_COUNT; i++) {
            if (!rowVisible(i) || !rowInLeftColumn(i)) continue;
            float boxY = rowY + ROW_LABEL_H;
            drawFieldLabel(font, L.formX, boxY, rowLabel(i));
            drawTextField(s, L.formX, boxY, L.boxW, ROW_BOX_H,
                          rowText(s, i), kFieldRows[i].placeholder, sel == i, TEXT_S);
            rowY += rowHeight();
        }
    }
    sceGuScissor(0, 0, 480, 272);

    guiScrollbar((L.formX + L.boxW + 2.0f) * UI_SCALE, L.formY * UI_SCALE,
                 2.0f * PX * UI_SCALE, L.formH * UI_SCALE,
                 L.contentH * UI_SCALE, scroll * UI_SCALE);

    if (s_advanced) {

        drawFieldLabel(font, L.descX, L.formY + ROW_LABEL_H, rowLabel(ROW_SEED));
        drawTextField(s, L.descX, L.formY + ROW_LABEL_H, L.boxW, ROW_BOX_H,
                      s.newWorldSeed, kFieldRows[ROW_SEED].placeholder, sel == ROW_SEED, TEXT_S);

        {
            float togRowY = L.formY + ROW_LABEL_H + ROW_BOX_H + 6.0f * PX;
            for (int i = FIELD_COUNT; i < ROW_COUNT; i++, togRowY += TOG_ROW) {
                const bool rowUsable = toggleRowUsable(s, i);
                const bool on = rowUsable && genFeatureEnabled(s.newWorldGenMask, rowFeature(i));

                fontDrawTextClipped(&font, L.descX * UI_SCALE,
                                    (togRowY + (ROW_BOX_H - 8.0f * TEXT_S) / 2.0f) * UI_SCALE,
                                    rowLabel(i),
                                    !rowUsable ? GUI_DISABLED
                                               : (sel == i ? 0xFFFFFFFFu : 0xFFE0E0E0u), TEXT_S,
                                    (L.boxW - TOG_W - 2.0f) * UI_SCALE / TEXT_S);
                guiOptionSwitch(s, L.descX + L.boxW - TOG_W,
                                togRowY + (ROW_BOX_H - TOG_H) / 2.0f, TOG_W, TOG_H,
                                on, sel == i, !rowUsable ? GUI_DISABLED : 0xFFFFFFFFu);
            }
        }

        drawFieldLabel(font, L.formX, L.typeY, "World Type");
        {
            // Flat worlds are disabled for now -- shown, not hidden, same
            // reasoning as the not-yet-available achievements in
            // screen_achievements.cpp: hiding it would make the pill row
            // look incomplete rather than communicating "not available
            // yet". Only the label dims (active=false, same mechanism the
            // Survival/Creative pills already use for gameModeLocked);
            // the pill background keeps rendering normally since Old is
            // still the selected/active choice either way.
            const bool flat = (s.newWorldType == WORLD_TYPE_FLAT);
            guiTButton(s, L.pill0X, L.typeY, L.pillW, L.pillH, !flat, BEVEL);
            guiTButtonLabel(s, L.pill0X, L.typeY, L.pillW, L.pillH,
                            levelSourceFor(WORLD_TYPE_OLD).label(),
                            sel == FOCUS_TYPE_OLD, true, TEXT_S);
            guiTButton(s, L.pill1X, L.typeY, L.pillW, L.pillH, flat, BEVEL);
            guiTButtonLabel(s, L.pill1X, L.typeY, L.pillW, L.pillH,
                            levelSourceFor(WORLD_TYPE_FLAT).label(),
                            sel == FOCUS_TYPE_FLAT, !kFlatDisabled, TEXT_S);
        }

        drawFieldLabel(font, L.formX, L.sizeY, "World Size");
        {
            static const int kPresets[3] = { WORLD_SIZE_PRESET_512, WORLD_SIZE_PRESET_1024, WORLD_SIZE_PRESET_INFINITE };
            static const int kFocus[3]   = { FOCUS_SIZE_512, FOCUS_SIZE_1024, FOCUS_SIZE_INFINITE };
            const float xs[3] = { L.pill0X3, L.pill1X3, L.pill2X3 };
            for (int i = 0; i < 3; i++) {
                const bool active = (s.newWorldSizePreset == kPresets[i]);
                guiTButton(s, xs[i], L.sizeY, L.pillW3, L.pillH, active, BEVEL);
                guiTButtonLabel(s, xs[i], L.sizeY, L.pillW3, L.pillH,
                                sizePresetLabel(kPresets[i]), sel == kFocus[i], true, TEXT_S);
            }
        }
    }

    drawFieldLabel(font, L.formX, L.modeY, "Game Mode");
    {
        const bool creative = (mode == 1);

        guiTButton(s, L.pill0X, L.modeY, L.pillW, L.pillH, !creative, BEVEL);
        guiTButtonLabel(s, L.pill0X, L.modeY, L.pillW, L.pillH, "Survival",
                        sel == FOCUS_SURVIVAL, !locked, TEXT_S);
        guiTButton(s, L.pill1X, L.modeY, L.pillW, L.pillH, creative, BEVEL);
        guiTButtonLabel(s, L.pill1X, L.modeY, L.pillW, L.pillH, "Creative",
                        sel == FOCUS_CREATIVE, !locked, TEXT_S);
    }

    if (!s_advanced)
        fontDrawTextWrapped(&font, L.descX * UI_SCALE, L.descY * UI_SCALE,
                            modeDescription(mode), 0xFFFFFFFFu, TEXT_S,
                            L.descW * UI_SCALE / TEXT_S);

    guiTButton(s, L.createX, L.createY, L.createW, L.createH, sel == FOCUS_CREATE, BEVEL);
    guiTButtonLabel(s, L.createX, L.createY, L.createW, L.createH, "Create World!",
                    sel == FOCUS_CREATE, true, TEXT_S);
}

void createFormReset(MenuState& s) {
    s.createSelected = 0;

    strcpy(s.newWorldName, "My World");
    s.newWorldSeed[0] = '\0';
    s.newWorldGamemode = 0;
    s.newWorldType = WORLD_TYPE_OLD;
    s.newWorldGenMask = genFeaturesDefaultMask();
    s.newWorldSizePreset = WORLD_SIZE_PRESET_512;
    s.createScroll = 0.0f;
}

static CreateScreen s_createScreen;
Screen& createScreen() { return s_createScreen; }
