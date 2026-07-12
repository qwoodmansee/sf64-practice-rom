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

// -- Root radial - 7-item octant layout
// N=RESTART NE=DISPLAY E=AUDIO S=LEVELS SW=LOADOUT W=CHEATS NW=SD CARD
// SE is intentionally empty: SAVE/LOAD were removed from the root radial.
// Position save/load still run from the gameplay hotkeys; SD save/load lives
// in the SD CARD sub-radial.
// Each wedge is 45deg wide (tan 22.5deg ~ 5/12 gives clean integer boundary).

typedef enum RootSlice {
    RSLICE_RESTART,   // N
    RSLICE_DISPLAY,   // NE
    RSLICE_AUDIO,     // E
    RSLICE_LEVELS,    // S
    RSLICE_LOADOUT,   // SW
    RSLICE_CHEATS,    // W
    RSLICE_SD,        // NW
    RSLICE_MAX,
} RootSlice;

static const RadialEntry sRootEntries[RSLICE_MAX] = {
    { "RESTART", "RESTART LEVEL",  134, 48,  7,  180, 60,  60  },  // N
    { "DISPLAY", "DISPLAY...",     204, 58,  7,  60,  160, 160 },  // NE
    { "AUDIO",   "AUDIO...",       216, 108, 5,  90,  140, 220 },  // E
    { "LEVELS",  "LEVEL SELECT",   140, 178, 6,  180, 140, 60  },  // S
    { "LOADOUT", "LOADOUT...",      68, 154, 7,  140, 60,  180 },  // SW
    { "CHEATS",  "CHEATS...",       64, 108, 6,  200, 80,  80  },  // W
    { "SD CARD", "SD CARD...",      68, 58,  7,  80,  200, 120 },  // NW
};

// tan(22.5deg) ~ 5/12.  ax*12 < ay*5 -> near vertical.  ay*12 < ax*5 -> near horizontal.
static s32 Root_GetSlice(s8 stickX, s8 stickY) {
    s32 x = stickX;
    s32 y = stickY;
    s32 ax = x < 0 ? -x : x;
    s32 ay = y < 0 ? -y : y;

    if (ax < RADIAL_DEAD_ZONE && ay < RADIAL_DEAD_ZONE) {
        return SLICE_NONE;
    }
    if (ax * 12 < ay * 5) {           // near vertical
        return y > 0 ? RSLICE_RESTART : RSLICE_LEVELS;
    }
    if (ay * 12 < ax * 5) {           // near horizontal
        return x > 0 ? RSLICE_AUDIO : RSLICE_CHEATS;
    }
    // diagonal quadrants
    if (x > 0) {
        return y > 0 ? RSLICE_DISPLAY : SLICE_NONE;   // SE empty (save/load removed)
    }
    return y > 0 ? RSLICE_SD : RSLICE_LOADOUT;
}

// -- Display sub-radial - 7 items
// Left third split at y=10/-10: upper=MACRO, center=SKIP CUTS, lower=CAMERA.
// Right half split at y=-10: upper=VISUALS, lower=CS METER.
// Vertical: up=STATS, down=INPUTS.
// CAMERA is shown dimmed and unselectable when the game is not frozen.

typedef enum DisplaySlice {
    DSLICE_SKIP_CUTS,    // W center
    DSLICE_CAMERA,       // W lower (frozen only)
    DSLICE_INPUTS,       // S
    DSLICE_STATS,        // N
    DSLICE_VISUALS,      // E upper
    DSLICE_CHARGE_METER, // E lower
    DSLICE_MACRO,        // W upper
    DSLICE_MAX,
} DisplaySlice;

static const RadialEntry sDisplayEntries[DSLICE_MAX] = {
    { "SKIP CUTS", "SKIP CUTSCENES",    50,  95, 9,  60, 160, 160 },
    { "CAMERA",    "FREE CAMERA",       50, 133, 6,  60, 200, 200 },
    { "INPUTS",    "INPUT DISPLAY",    136, 175, 6,  60, 160, 160 },
    { "STATS",     "STATS OVERLAY...", 138,  52, 5,  60, 160, 160 },
    { "VISUALS",   "VISUALIZERS...",   212,  82, 7,  60, 160, 160 },
    { "CS METER",  "CHARGE SHOT METER", 202, 148, 8,  0, 220, 100 },
    { "MACRO",     "MACRO...",          28,  62, 5,  80,  80, 200 },
};

static s32 Display_GetSlice(s8 stickX, s8 stickY) {
    s32 x = stickX;
    s32 y = stickY;
    s32 ax = x < 0 ? -x : x;
    s32 ay = y < 0 ? -y : y;

    if (ax < RADIAL_DEAD_ZONE && ay < RADIAL_DEAD_ZONE) {
        return SLICE_NONE;
    }
    if (ax > ay) {                         // horizontal dominant
        if (x > 0) {
            return y < -10 ? DSLICE_CHARGE_METER : DSLICE_VISUALS;
        }
        if (y > 10) {
            return DSLICE_MACRO;
        }
        if (y < -10) {
            return DSLICE_CAMERA;
        }
        return DSLICE_SKIP_CUTS;
    }
    return y > 0 ? DSLICE_STATS : DSLICE_INPUTS;
}

// -- Cheats sub-radial

typedef enum CheatSlice {
    CSLICE_AUTO_SHOT,
    CSLICE_INF_HP,
    CSLICE_INF_BOMBS,
    CSLICE_INF_LIVES,
    CSLICE_INF_BOOST,
    CSLICE_MAX,
} CheatSlice;

static const RadialEntry sCheatsEntries[CSLICE_MAX] = {
    { "AUTO CS",  "AUTO CHARGE SHOT",  89, 182, 7,  30,  200, 90  },
    { "INF HP",   "INF HEALTH",       140, 55,  6,  240, 210, 50  },
    { "INF BOMB", "INF BOMBS",         50, 108, 8,  210, 55,  55  },
    { "INF LIFE", "INF LIVES",        212, 88,  8,  140, 140, 150 },
    { "INF BST",  "INF BOOST",        238, 168, 7,  110, 210, 250 },
};

static s32 Cheats_GetSlice(s8 stickX, s8 stickY) {
    s32 x = stickX;
    s32 y = stickY;
    s32 ax = x < 0 ? -x : x;
    s32 ay = y < 0 ? -y : y;

    if (ax < RADIAL_DEAD_ZONE && ay < RADIAL_DEAD_ZONE) {
        return SLICE_NONE;
    }
    if (ax > ay) {
        if (x > 0) {
            return y < -10 ? CSLICE_INF_BOOST : CSLICE_INF_LIVES;
        }
        return CSLICE_INF_BOMBS;
    }
    return y > 0 ? CSLICE_INF_HP : CSLICE_AUTO_SHOT;
}

// -- SD sub-radial

typedef enum SdSlice {
    SSLICE_SAVE,
    SSLICE_LOAD,
    SSLICE_MAX,
} SdSlice;

static const RadialEntry sSdEntries[SSLICE_MAX] = {
    { "SD SAVE", "SAVE TO SD CARD", 68,  108, 7, 80, 200, 120 },
    { "SD LOAD", "LOAD FROM SD",   192,  108, 7, 80, 200, 120 },
};

static s32 Sd_GetSlice(s8 stickX, s8 stickY) {
    s32 ax = stickX < 0 ? -stickX : stickX;
    s32 ay = stickY < 0 ? -stickY : stickY;
    if (ax < RADIAL_DEAD_ZONE && ay < RADIAL_DEAD_ZONE) {
        return SLICE_NONE;
    }
    return stickX > 0 ? SSLICE_LOAD : SSLICE_SAVE;
}

typedef enum PracticeRadialSub1 {
    PRADIALSUB_DISPLAY,
    PRADIALSUB_CHEATS,
    PRADIALSUB_SD,
} PracticeRadialSub1;

static PracticeRadialSub1 sRadialSub1 = PRADIALSUB_DISPLAY;

// -- Menu stack

static s32 sMenuDepth = 0;
static s32 sHovered[RADIAL_STACK_MAX];
static s32 sStartHoldTimer = 0;

static const RadialMenuDef sMenuDefs[] = {
    { sRootEntries,    RSLICE_MAX, Root_GetSlice    },
    { sDisplayEntries, DSLICE_MAX, Display_GetSlice },
    { sCheatsEntries,  CSLICE_MAX, Cheats_GetSlice  },
    { sSdEntries,      SSLICE_MAX, Sd_GetSlice      },
};

static const RadialMenuDef* Menu_ActiveDef(void) {
    if (sMenuDepth == 0) {
        return &sMenuDefs[0];
    }
    if (sRadialSub1 == PRADIALSUB_CHEATS) {
        return &sMenuDefs[2];
    }
    if (sRadialSub1 == PRADIALSUB_SD) {
        return &sMenuDefs[3];
    }
    return &sMenuDefs[1];
}

void Practice_Menu_Open(void) {
    s32 i;
    gPracticeMenuState = PMENU_OPEN;
    sMenuDepth = 0;
    sRadialSub1 = PRADIALSUB_DISPLAY;
    for (i = 0; i < RADIAL_STACK_MAX; i++) {
        sHovered[i] = SLICE_NONE;
    }
    sStartHoldTimer = 0;
}

void Practice_Menu_OpenFrozen(void) {
    s32 i;
    gPracticeMenuState = PMENU_OPEN_FROZEN;
    sMenuDepth = 0;
    sRadialSub1 = PRADIALSUB_DISPLAY;
    for (i = 0; i < RADIAL_STACK_MAX; i++) {
        sHovered[i] = SLICE_NONE;
    }
    sStartHoldTimer = 0;
}

void Practice_Menu_Close_PakImpl(void) {
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

    if (press->button & B_BUTTON) {
        if (sMenuDepth > 0) {
            sMenuDepth--;
            sHovered[sMenuDepth] = SLICE_NONE;
        } else {
            Practice_Menu_Close();
        }
        return;
    }

    def = Menu_ActiveDef();
    sHovered[sMenuDepth] = def->getSlice(hold->stick_x, hold->stick_y);

    if ((press->button & A_BUTTON) && (sHovered[sMenuDepth] != SLICE_NONE)) {
        if (sMenuDepth == 0) {
            switch (sHovered[0]) {
                case RSLICE_RESTART:
                    Practice_Menu_Close();
                    Practice_InputGrace_Start();
                    Practice_LaunchLevel(gCurrentLevel, gLevelPhase, 0.0f);
                    break;
                case RSLICE_AUDIO:
                    Practice_StateMenu_Open(PSUBMENU_AUDIO);
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
                    sRadialSub1 = PRADIALSUB_DISPLAY;
                    sMenuDepth = 1;
                    sHovered[1] = SLICE_NONE;
                    break;
                case RSLICE_CHEATS:
                    sRadialSub1 = PRADIALSUB_CHEATS;
                    sMenuDepth = 1;
                    sHovered[1] = SLICE_NONE;
                    break;
                case RSLICE_SD:
                    sRadialSub1 = PRADIALSUB_SD;
                    sMenuDepth = 1;
                    sHovered[1] = SLICE_NONE;
                    break;
                default:
                    break;
            }
        } else if (sMenuDepth == 1) {
            if (sRadialSub1 == PRADIALSUB_SD) {
                switch (sHovered[1]) {
                    case SSLICE_SAVE:
                        Practice_Sd_StartSave();
                        break;
                    case SSLICE_LOAD:
                        Practice_Sd_StartLoad();
                        break;
                    default:
                        break;
                }
            } else if (sRadialSub1 == PRADIALSUB_CHEATS) {
                switch (sHovered[1]) {
                    case CSLICE_AUTO_SHOT:
                        gPracticeConfig.autoFireChargeShot ^= true;
                        break;
                    case CSLICE_INF_HP:
                        gPracticeConfig.infHealth ^= true;
                        break;
                    case CSLICE_INF_BOMBS:
                        gPracticeConfig.infBombs ^= true;
                        break;
                    case CSLICE_INF_LIVES:
                        gPracticeConfig.infLives ^= true;
                        break;
                    case CSLICE_INF_BOOST:
                        gPracticeConfig.infBoost ^= true;
                        break;
                    default:
                        break;
                }
            } else {
                /* Display sub-radial */
                switch (sHovered[1]) {
                    case DSLICE_SKIP_CUTS:
                        gPracticeConfig.skipCutscenes ^= true;
                        break;
                    case DSLICE_CAMERA:
                        if (gPracticeMenuState == PMENU_OPEN_FROZEN) {
                            Practice_FreeCam_Enter();
                        }
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
                    case DSLICE_CHARGE_METER:
                        gPracticeConfig.showChargeShotMeter ^= true;
                        break;
                    case DSLICE_MACRO:
                        Practice_StateMenu_Open(PSUBMENU_MACRO);
                        break;
                    default:
                        break;
                }
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
        /* CAMERA is only reachable when frozen; grey it out otherwise. */
        bool entryUnavail = (def == &sMenuDefs[1] && i == DSLICE_CAMERA &&
                             gPracticeMenuState != PMENU_OPEN_FROZEN);
        panelW = e->labelLen * 8 + 6;

        pr = e->panelR;
        pg = e->panelG;
        pb = e->panelB;

        if (!dimmed && !entryUnavail) {
            if (def == &sMenuDefs[1]) {
                if (i == DSLICE_SKIP_CUTS && gPracticeConfig.skipCutscenes) {
                    pr = 0; pg = 180; pb = 80;
                } else if (i == DSLICE_INPUTS && gPracticeConfig.showInputDisplay) {
                    pr = 0; pg = 180; pb = 80;
                } else if (i == DSLICE_CHARGE_METER && gPracticeConfig.showChargeShotMeter) {
                    pr = 0; pg = 180; pb = 80;
                }
            } else if (def == &sMenuDefs[2]) {
                if (i == CSLICE_AUTO_SHOT && gPracticeConfig.autoFireChargeShot) {
                    pr = 80; pg = 255; pb = 130;
                } else if (i == CSLICE_INF_HP && gPracticeConfig.infHealth) {
                    pr = 255; pg = 250; pb = 120;
                } else if (i == CSLICE_INF_BOMBS && gPracticeConfig.infBombs) {
                    pr = 255; pg = 90; pb = 90;
                } else if (i == CSLICE_INF_LIVES && gPracticeConfig.infLives) {
                    pr = 210; pg = 210; pb = 225;
                } else if (i == CSLICE_INF_BOOST && gPracticeConfig.infBoost) {
                    pr = 160; pg = 235; pb = 255;
                }
            }
        }

        if (dimmed || entryUnavail) {
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
        bool entryUnavail = (def == &sMenuDefs[1] && i == DSLICE_CAMERA &&
                             gPracticeMenuState != PMENU_OPEN_FROZEN);

        if (dimmed || entryUnavail) {
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
    const RadialMenuDef* def = Menu_ActiveDef();
    s32 hoveredSlice = sHovered[sMenuDepth];
    bool hasSelection = (hoveredSlice != SLICE_NONE);
    bool isCameraUnavail;

    Practice_DrawBox(40, 35, 240, 175, 0, 0, 0, 210);

    RadialMenu_DrawLayer(def, hoveredSlice, false);

    if (hasSelection) {
        isCameraUnavail = (sMenuDepth == 1 && sRadialSub1 == PRADIALSUB_DISPLAY &&
                           hoveredSlice == DSLICE_CAMERA &&
                           gPracticeMenuState != PMENU_OPEN_FROZEN);

        if (isCameraUnavail) {
            Practice_DrawTextColor(RADIAL_CENTER_X - 44, RADIAL_CENTER_Y - 5,
                "FREEZE GAME FIRST", 160, 160, 160);
        } else if (sMenuDepth == 1 && sRadialSub1 == PRADIALSUB_DISPLAY &&
                   (hoveredSlice == DSLICE_SKIP_CUTS || hoveredSlice == DSLICE_INPUTS ||
                    hoveredSlice == DSLICE_CHARGE_METER)) {
            bool state;
            const char* name;

            if (hoveredSlice == DSLICE_SKIP_CUTS) {
                state = gPracticeConfig.skipCutscenes;
                name = "SKIP CUTSCENES:";
            } else if (hoveredSlice == DSLICE_INPUTS) {
                state = gPracticeConfig.showInputDisplay;
                name = "INPUT DISPLAY:";
            } else {
                state = gPracticeConfig.showChargeShotMeter;
                name = "CHARGE SHOT METER:";
            }
            Practice_DrawTextOutline(RADIAL_CENTER_X - 44, RADIAL_CENTER_Y - 10, name, 0, 255, 128);
            Practice_DrawTextColor(RADIAL_CENTER_X - 44, RADIAL_CENTER_Y + 4,
                state ? "ON" : "OFF",
                state ? 0 : 255, state ? 255 : 100, 0);
        } else if (sMenuDepth == 1 && sRadialSub1 == PRADIALSUB_CHEATS) {
            bool st = false;
            const char* nm = NULL;

            switch (hoveredSlice) {
                case CSLICE_AUTO_SHOT:
                    st = gPracticeConfig.autoFireChargeShot;
                    nm = "AUTO CHARGE SHOT:";
                    break;
                case CSLICE_INF_HP:
                    st = gPracticeConfig.infHealth;
                    nm = "INF HEALTH:";
                    break;
                case CSLICE_INF_BOMBS:
                    st = gPracticeConfig.infBombs;
                    nm = "INF BOMBS:";
                    break;
                case CSLICE_INF_LIVES:
                    st = gPracticeConfig.infLives;
                    nm = "INF LIVES:";
                    break;
                case CSLICE_INF_BOOST:
                    st = gPracticeConfig.infBoost;
                    nm = "INF BOOST:";
                    break;
                default:
                    nm = NULL;
                    break;
            }
            if (nm != NULL) {
                Practice_DrawTextOutline(RADIAL_CENTER_X - 44, RADIAL_CENTER_Y - 10, nm, 0, 255, 128);
                Practice_DrawTextColor(RADIAL_CENTER_X - 44, RADIAL_CENTER_Y + 4, st ? "ON" : "OFF",
                    st ? 0 : 255, st ? 255 : 100, 0);
            }
        } else {
            Practice_DrawTextOutline(
                RADIAL_CENTER_X - 40, RADIAL_CENTER_Y - 5,
                def->entries[hoveredSlice].desc, 0, 255, 128);
        }
    } else if (sMenuDepth > 0) {
        const char* sub = (sRadialSub1 == PRADIALSUB_CHEATS) ? "CHEATS"
                        : (sRadialSub1 == PRADIALSUB_SD)     ? "SD CARD"
                        : "DISPLAY";
        Practice_DrawTextColor(RADIAL_CENTER_X - 28, RADIAL_CENTER_Y - 5, sub, 0, 255, 128);
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
        Practice_DrawTextColor(56, 198, "STICK:SELECT A:GO B:BACK", 150, 150, 150);
    } else {
        Practice_DrawTextColor(74, 198, "L-R:SLOT  B:CLOSE", 150, 150, 150);
    }

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Draw();
    }
}

#endif
