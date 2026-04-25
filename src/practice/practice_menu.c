#include "practice.h"

#ifdef PRACTICE_ROM

#define RADIAL_DEAD_ZONE 20
#define RADIAL_CENTER_X 160
#define RADIAL_CENTER_Y 115

typedef enum RadialSlice {
    RSLICE_NONE = -1,
    RSLICE_RESTART,
    RSLICE_SAVE,
    RSLICE_LOAD,
    RSLICE_LEVELS,
    RSLICE_LOADOUT,
    RSLICE_DISPLAY,
    RSLICE_CAMERA,
    RSLICE_MAX,
} RadialSlice;

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

static RadialEntry sRadialEntries[RSLICE_MAX] = {
    { "RESTART",  "RESTART LEVEL",     132, 52,  7,  180, 60,  60  },
    { "SAVE",     "SAVE POSITION",     214, 74,  4,  60,  140, 180 },
    { "LOAD",     "LOAD POSITION",     214, 152, 4,  60,  180, 100 },
    { "LEVELS",   "LEVEL SELECT",      136, 178, 6,  180, 140, 60  },
    { "LOADOUT",  "LOADOUT...",         68, 148, 10, 140, 60,  180 },
    { "DISPLAY",  "DISPLAY...",         68, 82,  10, 60,  160, 160 },
    { "CAMERA",   "FREE CAMERA",       222, 108, 6,  60,  200, 200 },
};

static RadialSlice sHoveredSlice = RSLICE_NONE;
static s32 sStartHoldTimer = 0;
#define START_HOLD_FRAMES 45

void Practice_Menu_Open(void) {
    gPracticeMenuState = PMENU_OPEN;
    sHoveredSlice = RSLICE_NONE;
    sStartHoldTimer = 0;
}

void Practice_Menu_OpenFrozen(void) {
    gPracticeMenuState = PMENU_OPEN_FROZEN;
    sHoveredSlice = RSLICE_NONE;
    sStartHoldTimer = 0;
}

void Practice_Menu_Close(void) {
    gPracticeMenuState = PMENU_CLOSED;
}

static RadialSlice RadialMenu_GetSlice(s8 stickX, s8 stickY) {
    s32 x = stickX;
    s32 y = stickY;
    s32 ax = (x < 0) ? -x : x;
    s32 ay = (y < 0) ? -y : y;

    if ((ax < RADIAL_DEAD_ZONE) && (ay < RADIAL_DEAD_ZONE)) {
        return RSLICE_NONE;
    }

    if ((ay * 100) > (ax * 173)) {
        if (y > 0) {
            return RSLICE_RESTART;
        }
        return RSLICE_LEVELS;
    }

    if (x > 0) {
        if ((ay * 100) < (ax * 58)) {
            return RSLICE_CAMERA;
        }
        if (y > 0) {
            return RSLICE_SAVE;
        }
        return RSLICE_LOAD;
    }

    if (y > 0) {
        return RSLICE_DISPLAY;
    }
    return RSLICE_LOADOUT;
}

void Practice_Menu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    OSContPad* hold = &gControllerHold[gMainController];

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Update();
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
        Practice_Menu_Close();
        return;
    }

    sHoveredSlice = RadialMenu_GetSlice(hold->stick_x, hold->stick_y);

    if ((press->button & A_BUTTON) && (sHoveredSlice != RSLICE_NONE)) {
        switch (sHoveredSlice) {
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
                gGameState = GSTATE_MAP;
                gDrawMode = DRAW_NONE;
                Audio_FadeOutAll(1);
                Audio_ClearVoice();
                break;
            case RSLICE_LOADOUT:
                Practice_StateMenu_Open(PSUBMENU_LOADOUT);
                break;
            case RSLICE_DISPLAY:
                Practice_StateMenu_Open(PSUBMENU_DISPLAY);
                break;
            case RSLICE_CAMERA:
                if (gPracticeMenuState == PMENU_OPEN_FROZEN) {
                    Practice_FreeCam_Enter();
                }
                break;
            default:
                break;
        }
    }
}

void Practice_Menu_Draw(void) {
    s32 i;
    s32 panelW;
    s32 panelAlpha;
    bool hasSelection = (sHoveredSlice != RSLICE_NONE);

    Practice_DrawBox(40, 35, 240, 175, 0, 0, 0, 210);

    for (i = 0; i < RSLICE_MAX; i++) {
        panelW = sRadialEntries[i].labelLen * 8 + 6;

        if (i == sHoveredSlice) {
            panelAlpha = 160;
        } else if (hasSelection) {
            panelAlpha = 40;
        } else {
            panelAlpha = 80;
        }

        Practice_DrawBox(
            sRadialEntries[i].x - 3, sRadialEntries[i].y - 3,
            panelW, 16,
            sRadialEntries[i].panelR, sRadialEntries[i].panelG, sRadialEntries[i].panelB,
            panelAlpha);
    }

    for (i = 0; i < RSLICE_MAX; i++) {
        if (i == sHoveredSlice) {
            Practice_DrawTextOutline(
                sRadialEntries[i].x, sRadialEntries[i].y,
                sRadialEntries[i].label, 255, 255, 0);
        } else if (hasSelection) {
            Practice_DrawTextColor(
                sRadialEntries[i].x, sRadialEntries[i].y,
                sRadialEntries[i].label, 140, 140, 140);
        } else {
            Practice_DrawText(
                sRadialEntries[i].x, sRadialEntries[i].y,
                sRadialEntries[i].label);
        }
    }

    if (hasSelection) {
        Practice_DrawTextOutline(
            RADIAL_CENTER_X - 40, RADIAL_CENTER_Y - 5,
            sRadialEntries[sHoveredSlice].desc, 0, 255, 128);
    } else {
        Practice_DrawTextColor(RADIAL_CENTER_X - 36, RADIAL_CENTER_Y - 12, "PRACTICE", 0, 255, 128);
        Practice_DrawText(RADIAL_CENTER_X - 24, RADIAL_CENTER_Y + 2, "HITS:");
        Practice_DrawNumber(RADIAL_CENTER_X + 16, RADIAL_CENTER_Y + 2, gHitCount);
    }

    if (sStartHoldTimer > 0) {
        s32 barW = (sStartHoldTimer * 160) / START_HOLD_FRAMES;
        Practice_DrawBox(80, 210, 160, 6, 50, 50, 50, 160);
        Practice_DrawBox(80, 210, barW, 6, 255, 100, 100, 220);
        Practice_DrawTextColor(92, 218, "HOLD START: TITLE", 255, 100, 100);
    }

    Practice_DrawTextColor(56, 198, "STICK:SELECT  A:GO  B:CLOSE", 150, 150, 150);

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Draw();
    }
}

#endif
