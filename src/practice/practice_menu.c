#include "practice.h"

#ifdef PRACTICE_ROM

#define RADIAL_DEAD_ZONE  20
#define RADIAL_CENTER_X  160
#define RADIAL_CENTER_Y  115
#define RADIAL_STACK_MAX   4
#define START_HOLD_FRAMES 45
#define SLICE_NONE        (-1)

typedef struct RadialEntry {
    const char* label;
    const char* desc;
    s32 x;
    s32 y;
    s32 labelLen;
    u8 panelR;
    u8 panelG;
    u8 panelB;
} RadialEntry;

typedef struct RadialMenuDef {
    const RadialEntry* entries;
    s32 count;
    s32 (*getSlice)(s8 sx, s8 sy);
} RadialMenuDef;

// ── Root radial ───────────────────────────────────────────────────────────────

typedef enum RootSlice {
    RSLICE_RESTART,
    RSLICE_SAVE,
    RSLICE_LOAD,
    RSLICE_LEVELS,
    RSLICE_LOADOUT,
    RSLICE_DISPLAY,
    RSLICE_CAMERA,
    RSLICE_MAX,
} RootSlice;

static const RadialEntry sRootEntries[RSLICE_MAX] = {
    { "RESTART", "RESTART LEVEL",  132, 52,  7,  180, 60,  60  },
    { "SAVE",    "SAVE POSITION",  214, 74,  4,  60,  140, 180 },
    { "LOAD",    "LOAD POSITION",  214, 152, 4,  60,  180, 100 },
    { "LEVELS",  "LEVEL SELECT",   136, 178, 6,  180, 140, 60  },
    { "LOADOUT", "LOADOUT...",      68, 148, 10, 140, 60,  180 },
    { "DISPLAY", "DISPLAY...",      68, 82,  10, 60,  160, 160 },
    { "CAMERA",  "FREE CAMERA",    222, 108, 6,  60,  200, 200 },
};

static s32 Root_GetSlice(s8 stickX, s8 stickY) {
    s32 x = stickX;
    s32 y = stickY;
    s32 ax = (x < 0) ? -x : x;
    s32 ay = (y < 0) ? -y : y;

    if ((ax < RADIAL_DEAD_ZONE) && (ay < RADIAL_DEAD_ZONE)) {
        return SLICE_NONE;
    }
    if ((ay * 100) > (ax * 173)) {
        return y > 0 ? RSLICE_RESTART : RSLICE_LEVELS;
    }
    if (x > 0) {
        if ((ay * 100) < (ax * 58)) {
            return RSLICE_CAMERA;
        }
        return y > 0 ? RSLICE_SAVE : RSLICE_LOAD;
    }
    return y > 0 ? RSLICE_DISPLAY : RSLICE_LOADOUT;
}

// ── Display sub-radial ────────────────────────────────────────────────────────

typedef enum DisplaySlice {
    DSLICE_SKIP_CUTS,   // left
    DSLICE_INPUTS,      // down
    DSLICE_STATS,       // up
    DSLICE_VISUALS,     // right
    DSLICE_MAX,
} DisplaySlice;

static const RadialEntry sDisplayEntries[DSLICE_MAX] = {
    { "SKIP CUTS", "SKIP CUTSCENES",   50, 108, 9, 60, 160, 160 },
    { "INPUTS",    "INPUT DISPLAY",   136, 175, 6, 60, 160, 160 },
    { "STATS",     "STATS OVERLAY...", 140, 55, 5, 60, 160, 160 },
    { "VISUALS",   "VISUALIZERS...",   212, 108, 7, 60, 160, 160 },
};

static s32 Display_GetSlice(s8 stickX, s8 stickY) {
    s32 x = stickX;
    s32 y = stickY;
    s32 ax = (x < 0) ? -x : x;
    s32 ay = (y < 0) ? -y : y;

    if ((ax < RADIAL_DEAD_ZONE) && (ay < RADIAL_DEAD_ZONE)) {
        return SLICE_NONE;
    }
    if (ax > ay) {
        return x > 0 ? DSLICE_VISUALS : DSLICE_SKIP_CUTS;
    }
    return y > 0 ? DSLICE_STATS : DSLICE_INPUTS;
}

// ── Menu stack ────────────────────────────────────────────────────────────────

static const RadialMenuDef sMenuDefs[] = {
    { sRootEntries,    RSLICE_MAX, Root_GetSlice    },
    { sDisplayEntries, DSLICE_MAX, Display_GetSlice },
};

static s32 sMenuDepth = 0;
static s32 sHovered[RADIAL_STACK_MAX];
static s32 sStartHoldTimer = 0;

void Practice_Menu_Open(void) {
    s32 i;
    gPracticeMenuState = PMENU_OPEN;
    sMenuDepth = 0;
    for (i = 0; i < RADIAL_STACK_MAX; i++) {
        sHovered[i] = SLICE_NONE;
    }
    sStartHoldTimer = 0;
}

void Practice_Menu_OpenFrozen(void) {
    s32 i;
    gPracticeMenuState = PMENU_OPEN_FROZEN;
    sMenuDepth = 0;
    for (i = 0; i < RADIAL_STACK_MAX; i++) {
        sHovered[i] = SLICE_NONE;
    }
    sStartHoldTimer = 0;
}

void Practice_Menu_Close(void) {
    gPracticeMenuState = PMENU_CLOSED;
}

void Practice_Menu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    OSContPad* hold = &gControllerHold[gMainController];
    const RadialMenuDef* def;

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Update();
        return;
    }

    /* Slot cycle: L-only / R-only shoulder (distinct from L+DPAD save/load combos). */
    if ((press->button & L_TRIG) && !(press->button & (U_JPAD | D_JPAD | L_JPAD | R_JPAD))) {
        Practice_CycleSlot(-1);
        return;
    }
    if ((press->button & R_TRIG) && !(press->button & (U_JPAD | D_JPAD | L_JPAD | R_JPAD))) {
        Practice_CycleSlot(1);
        return;
    }

    if (hold->button & START_BUTTON) {
        sStartHoldTimer++;
        if (sStartHoldTimer >= START_HOLD_FRAMES) {
            Practice_Menu_Close();
            gGameState = GSTATE_MENU;
            gDrawMode = DRAW_NONE;
            Audio_FadeOutAll(1);
            Audio_ClearVoice();
            return;
        }
    } else {
        sStartHoldTimer = 0;
    }

    /* Z = SD save/load shortcuts when radial is open at depth 0 */
    /* Must come before B_BUTTON check so Z+B is caught here, not by the close-menu path */
    if (sMenuDepth == 0 && (press->button & Z_TRIG)) {
        if (press->button & B_BUTTON) {
            Practice_Sd_StartLoad();
        } else {
            Practice_Sd_StartSave();
        }
        return;
    }

    if (press->button & B_BUTTON) {
        if (sMenuDepth > 0) {
            sMenuDepth--;
            sHovered[sMenuDepth] = SLICE_NONE;
        } else {
            Practice_Menu_Close();
        }
        return;
    }

    def = &sMenuDefs[sMenuDepth];
    sHovered[sMenuDepth] = def->getSlice(hold->stick_x, hold->stick_y);

    if ((press->button & A_BUTTON) && (sHovered[sMenuDepth] != SLICE_NONE)) {
        if (sMenuDepth == 0) {
            switch (sHovered[0]) {
                case RSLICE_RESTART:
                    Practice_Menu_Close();
                    Practice_LaunchLevel(gCurrentLevel, gLevelPhase, 0.0f);
                    break;
                case RSLICE_SAVE:
                    Practice_SaveState();
                    break;
                case RSLICE_LOAD:
                    Practice_Menu_Close();
                    Practice_LoadState();
                    break;
                case RSLICE_LEVELS:
                    Practice_Menu_Close();
                    gPracticeScreen = PSCREEN_LEVEL_SELECT;
                    Practice_LevelSelect_OnEnter();
                    gGameState = GSTATE_MAP;
                    gDrawMode = DRAW_NONE;
                    Audio_FadeOutAll(1);
                    Audio_ClearVoice();
                    break;
                case RSLICE_LOADOUT:
                    Practice_StateMenu_Open(PSUBMENU_LOADOUT);
                    break;
                case RSLICE_DISPLAY:
                    sMenuDepth = 1;
                    sHovered[1] = SLICE_NONE;
                    break;
                case RSLICE_CAMERA:
                    if (gPracticeMenuState == PMENU_OPEN_FROZEN) {
                        Practice_FreeCam_Enter();
                    }
                    break;
                default:
                    break;
            }
        } else if (sMenuDepth == 1) {
            switch (sHovered[1]) {
                case DSLICE_SKIP_CUTS:
                    gPracticeConfig.skipCutscenes ^= true;
                    break;
                case DSLICE_INPUTS:
                    gPracticeConfig.showInputDisplay ^= true;
                    break;
                case DSLICE_STATS:
                    Practice_StateMenu_Open(PSUBMENU_STATS);
                    break;
                case DSLICE_VISUALS:
                    Practice_StateMenu_Open(PSUBMENU_VISUALIZERS);
                    break;
                default:
                    break;
            }
        }
    }
}

static void RadialMenu_DrawLayer(const RadialMenuDef* def, s32 hoveredSlice, bool dimmed) {
    s32 i;
    s32 panelW;
    s32 panelAlpha;
    bool hasSelection = (hoveredSlice != SLICE_NONE);
    u8 pr, pg, pb;

    for (i = 0; i < def->count; i++) {
        const RadialEntry* e = &def->entries[i];
        panelW = e->labelLen * 8 + 6;

        pr = e->panelR;
        pg = e->panelG;
        pb = e->panelB;

        if (!dimmed && def == &sMenuDefs[1]) {
            if (i == DSLICE_SKIP_CUTS && gPracticeConfig.skipCutscenes) {
                pr = 0; pg = 180; pb = 80;
            } else if (i == DSLICE_INPUTS && gPracticeConfig.showInputDisplay) {
                pr = 0; pg = 180; pb = 80;
            }
        }

        if (dimmed) {
            panelAlpha = 20;
        } else if (i == hoveredSlice) {
            panelAlpha = 160;
        } else if (hasSelection) {
            panelAlpha = 40;
        } else {
            panelAlpha = 80;
        }

        Practice_DrawBox(e->x - 3, e->y - 3, panelW, 16, pr, pg, pb, panelAlpha);
    }

    for (i = 0; i < def->count; i++) {
        const RadialEntry* e = &def->entries[i];
        if (dimmed) {
            Practice_DrawTextColor(e->x, e->y, e->label, 55, 55, 55);
        } else if (i == hoveredSlice) {
            Practice_DrawTextOutline(e->x, e->y, e->label, 255, 255, 0);
        } else if (hasSelection) {
            Practice_DrawTextColor(e->x, e->y, e->label, 140, 140, 140);
        } else {
            Practice_DrawText(e->x, e->y, e->label);
        }
    }
}

void Practice_Menu_Draw(void) {
    const RadialMenuDef* def = &sMenuDefs[sMenuDepth];
    s32 hoveredSlice = sHovered[sMenuDepth];
    bool hasSelection = (hoveredSlice != SLICE_NONE);

    Practice_DrawBox(40, 35, 240, 175, 0, 0, 0, 210);

    RadialMenu_DrawLayer(def, hoveredSlice, false);

    if (hasSelection) {
        if (sMenuDepth == 1 && (hoveredSlice == DSLICE_SKIP_CUTS || hoveredSlice == DSLICE_INPUTS)) {
            bool state = (hoveredSlice == DSLICE_SKIP_CUTS)
                ? gPracticeConfig.skipCutscenes
                : gPracticeConfig.showInputDisplay;
            const char* name = (hoveredSlice == DSLICE_SKIP_CUTS) ? "SKIP CUTSCENES:" : "INPUT DISPLAY:";
            Practice_DrawTextOutline(RADIAL_CENTER_X - 44, RADIAL_CENTER_Y - 10, name, 0, 255, 128);
            Practice_DrawTextColor(RADIAL_CENTER_X - 44, RADIAL_CENTER_Y + 4,
                state ? "ON" : "OFF",
                state ? 0 : 255, state ? 255 : 100, 0);
        } else {
            Practice_DrawTextOutline(
                RADIAL_CENTER_X - 40, RADIAL_CENTER_Y - 5,
                def->entries[hoveredSlice].desc, 0, 255, 128);
        }
    } else if (sMenuDepth > 0) {
        Practice_DrawTextColor(RADIAL_CENTER_X - 28, RADIAL_CENTER_Y - 5, "DISPLAY", 0, 255, 128);
    } else {
        s32 slotCount = Practice_GetRamSlotCount();
        s32 active    = Practice_GetActiveSlot();
        s32 row;
        s32 baseY = RADIAL_CENTER_Y - 30;
        s32 leftX = RADIAL_CENTER_X - 56;

        Practice_DrawTextColor(RADIAL_CENTER_X - 36, baseY, "PRACTICE", 0, 255, 128);

        if (slotCount <= 0) {
            Practice_DrawTextColor(leftX, baseY + 18, "SAVE DISABLED", 200, 200, 200);
            Practice_DrawTextColor(leftX, baseY + 30, "NEEDS PAK", 160, 160, 160);
            Practice_DrawText(leftX, baseY + 50, "HITS:");
            Practice_DrawNumber(leftX + 40, baseY + 50, gHitCount);
        } else {
            for (row = 0; row < slotCount && row < 4; row++) {
                const PracticeSlotMeta* m  = Practice_GetSlotMeta(row);
                bool            isActive   = (row == active);
                s32             y          = baseY + 14 + row * 11;

                if (isActive) {
                    /* Filled chevron to the left of the row -- '>' is not in
                     * the small-text glyph set, so we render a box. */
                    Practice_DrawBox(leftX, y + 1, 4, 6, 255, 220, 80, 220);
                }
                Practice_DrawNumber(leftX + 10, y, row);
                if (m != NULL && m->valid) {
                    Practice_DrawText (leftX + 26,  y, Practice_LevelAbbrev(m->level));
                    Practice_DrawText (leftX + 44,  y, "P");
                    Practice_DrawNumber(leftX + 52, y, m->phase);
                    Practice_DrawTextColor(leftX + 70, y, "SAVED", 0, 255, 80);
                } else {
                    Practice_DrawTextColor(leftX + 26, y, "EMPTY", 120, 120, 120);
                }
            }
        }
    }

    if (sStartHoldTimer > 0) {
        s32 barW = (sStartHoldTimer * 160) / START_HOLD_FRAMES;
        Practice_DrawBox(80, 210, 160, 6, 50, 50, 50, 160);
        Practice_DrawBox(80, 210, barW, 6, 255, 100, 100, 220);
        Practice_DrawTextColor(92, 218, "HOLD START: TITLE", 255, 100, 100);
    }

    if (sMenuDepth > 0) {
        Practice_DrawTextColor(56, 198, "STICK:SELECT  A:GO  B:BACK", 150, 150, 150);
    } else {
        Practice_DrawTextColor(40, 198, "L:R B:CLOSE Z:SD SAVE ZB:SD LOAD", 150, 150, 150);
    }

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Draw();
    }
}

#endif
