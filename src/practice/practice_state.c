#include "practice.h"

#ifdef PRACTICE_ROM

typedef enum LoadoutOption {
    LOPT_LASERS,
    LOPT_BOMBS,
    LOPT_LIVES,
    LOPT_RIGHT_WING,
    LOPT_LEFT_WING,
    LOPT_BACK,
    LOPT_MAX,
} LoadoutOption;

typedef enum OptionsOption {
    OOPT_FALCO,
    OOPT_SLIPPY,
    OOPT_PEPPY,
    OOPT_SKIP_CUTSCENES,
    OOPT_INPUT_DISPLAY,
    OOPT_BACK,
    OOPT_MAX,
} OptionsOption;

static s32 sSelectedOption = 0;
static bool sStateMenuOpen = false;
static PracticeSubMenu sActiveSubMenu;

static const char* sLaserNames[] = { "SINGLE", "TWIN", "HYPER" };
static const char* sWingNames[] = { "NONE", "BROKEN", "INTACT" };

bool Practice_StateMenuIsOpen(void) {
    return sStateMenuOpen;
}

void Practice_StateMenu_Open(PracticeSubMenu subMenu) {
    sStateMenuOpen = true;
    sSelectedOption = 0;
    sActiveSubMenu = subMenu;
}

void Practice_StateMenu_Close(void) {
    sStateMenuOpen = false;
}

static s32 StateMenu_GetOptionCount(void) {
    return (sActiveSubMenu == PSUBMENU_LOADOUT) ? LOPT_MAX : OOPT_MAX;
}

static void StateMenu_UpdateLoadout(u16 buttons) {
    if ((buttons & R_JPAD) || (buttons & A_BUTTON)) {
        switch (sSelectedOption) {
            case LOPT_LASERS:
                gPracticeConfig.laserStrength++;
                if (gPracticeConfig.laserStrength > LASERS_HYPER) {
                    gPracticeConfig.laserStrength = LASERS_SINGLE;
                }
                break;
            case LOPT_BOMBS:
                gPracticeConfig.bombCount++;
                if (gPracticeConfig.bombCount > 9) {
                    gPracticeConfig.bombCount = 0;
                }
                break;
            case LOPT_LIVES:
                gPracticeConfig.lifeCount++;
                if (gPracticeConfig.lifeCount > 99) {
                    gPracticeConfig.lifeCount = 1;
                }
                break;
            case LOPT_RIGHT_WING:
                gPracticeConfig.rightWingState++;
                if (gPracticeConfig.rightWingState > WINGSTATE_INTACT) {
                    gPracticeConfig.rightWingState = WINGSTATE_NONE;
                }
                break;
            case LOPT_LEFT_WING:
                gPracticeConfig.leftWingState++;
                if (gPracticeConfig.leftWingState > WINGSTATE_INTACT) {
                    gPracticeConfig.leftWingState = WINGSTATE_NONE;
                }
                break;
        }
    }

    if (buttons & L_JPAD) {
        switch (sSelectedOption) {
            case LOPT_LASERS:
                if (gPracticeConfig.laserStrength == LASERS_SINGLE) {
                    gPracticeConfig.laserStrength = LASERS_HYPER;
                } else {
                    gPracticeConfig.laserStrength--;
                }
                break;
            case LOPT_BOMBS:
                gPracticeConfig.bombCount--;
                if (gPracticeConfig.bombCount < 0) {
                    gPracticeConfig.bombCount = 9;
                }
                break;
            case LOPT_LIVES:
                gPracticeConfig.lifeCount--;
                if (gPracticeConfig.lifeCount < 1) {
                    gPracticeConfig.lifeCount = 99;
                }
                break;
            case LOPT_RIGHT_WING:
                if (gPracticeConfig.rightWingState == WINGSTATE_NONE) {
                    gPracticeConfig.rightWingState = WINGSTATE_INTACT;
                } else {
                    gPracticeConfig.rightWingState--;
                }
                break;
            case LOPT_LEFT_WING:
                if (gPracticeConfig.leftWingState == WINGSTATE_NONE) {
                    gPracticeConfig.leftWingState = WINGSTATE_INTACT;
                } else {
                    gPracticeConfig.leftWingState--;
                }
                break;
        }
    }
}

static void StateMenu_UpdateOptions(u16 buttons) {
    if ((buttons & R_JPAD) || (buttons & A_BUTTON) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case OOPT_FALCO:
                gPracticeConfig.falcoAlive ^= true;
                break;
            case OOPT_SLIPPY:
                gPracticeConfig.slippyAlive ^= true;
                break;
            case OOPT_PEPPY:
                gPracticeConfig.peppyAlive ^= true;
                break;
            case OOPT_SKIP_CUTSCENES:
                gPracticeConfig.skipCutscenes ^= true;
                break;
            case OOPT_INPUT_DISPLAY:
                gPracticeConfig.showInputDisplay ^= true;
                break;
        }
    }
}

void Practice_StateMenu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    s32 optCount = StateMenu_GetOptionCount();

    if (press->button & B_BUTTON) {
        Practice_StateMenu_Close();
        return;
    }

    if (press->button & U_JPAD) {
        sSelectedOption--;
        if (sSelectedOption < 0) {
            sSelectedOption = optCount - 1;
        }
    }
    if (press->button & D_JPAD) {
        sSelectedOption++;
        if (sSelectedOption >= optCount) {
            sSelectedOption = 0;
        }
    }

    if (press->button & A_BUTTON) {
        s32 backOpt = (sActiveSubMenu == PSUBMENU_LOADOUT) ? LOPT_BACK : OOPT_BACK;
        if (sSelectedOption == backOpt) {
            Practice_StateMenu_Close();
            return;
        }
    }

    if (sActiveSubMenu == PSUBMENU_LOADOUT) {
        StateMenu_UpdateLoadout(press->button);
    } else {
        StateMenu_UpdateOptions(press->button);
    }
}

static void StateMenu_DrawLoadout(void) {
    s32 y;
    s32 i;

    for (i = 0; i < LOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case LOPT_LASERS:
                Practice_DrawText(54, y, "LASER:");
                Practice_DrawTextColor(120, y, sLaserNames[gPracticeConfig.laserStrength], 255, 255, 0);
                break;
            case LOPT_BOMBS:
                Practice_DrawText(54, y, "BOMBS:");
                Practice_DrawNumber(120, y, gPracticeConfig.bombCount);
                break;
            case LOPT_LIVES:
                Practice_DrawText(54, y, "LIVES:");
                Practice_DrawNumber(120, y, gPracticeConfig.lifeCount);
                break;
            case LOPT_RIGHT_WING:
                Practice_DrawText(54, y, "R WING:");
                Practice_DrawTextColor(120, y, sWingNames[gPracticeConfig.rightWingState], 255, 255, 0);
                break;
            case LOPT_LEFT_WING:
                Practice_DrawText(54, y, "L WING:");
                Practice_DrawTextColor(120, y, sWingNames[gPracticeConfig.leftWingState], 255, 255, 0);
                break;
            case LOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

static void StateMenu_DrawOptions(void) {
    s32 y;
    s32 i;

    for (i = 0; i < OOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case OOPT_FALCO:
                Practice_DrawText(54, y, "FALCO:");
                Practice_DrawTextColor(120, y, gPracticeConfig.falcoAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.falcoAlive ? 0 : 255, gPracticeConfig.falcoAlive ? 255 : 100, 0);
                break;
            case OOPT_SLIPPY:
                Practice_DrawText(54, y, "SLIPPY:");
                Practice_DrawTextColor(120, y, gPracticeConfig.slippyAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.slippyAlive ? 0 : 255, gPracticeConfig.slippyAlive ? 255 : 100, 0);
                break;
            case OOPT_PEPPY:
                Practice_DrawText(54, y, "PEPPY:");
                Practice_DrawTextColor(120, y, gPracticeConfig.peppyAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.peppyAlive ? 0 : 255, gPracticeConfig.peppyAlive ? 255 : 100, 0);
                break;
            case OOPT_SKIP_CUTSCENES:
                Practice_DrawText(54, y, "SCENES:");
                Practice_DrawTextColor(120, y, gPracticeConfig.skipCutscenes ? "SKIP" : "PLAY",
                    gPracticeConfig.skipCutscenes ? 0 : 255, gPracticeConfig.skipCutscenes ? 255 : 100, 0);
                break;
            case OOPT_INPUT_DISPLAY:
                Practice_DrawText(54, y, "INPUT:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showInputDisplay ? "ON" : "OFF",
                    gPracticeConfig.showInputDisplay ? 0 : 255, gPracticeConfig.showInputDisplay ? 255 : 100, 0);
                break;
            case OOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

void Practice_StateMenu_Draw(void) {
    const char* title = (sActiveSubMenu == PSUBMENU_LOADOUT) ? "LOADOUT" : "OPTIONS";

    Practice_DrawBox(40, 40, 240, 115, 0, 0, 60, 200);
    Practice_DrawTextColor(50, 44, title, 0, 255, 128);

    if (sActiveSubMenu == PSUBMENU_LOADOUT) {
        StateMenu_DrawLoadout();
        Practice_DrawTextColor(50, 148, "D-PAD:CHANGE  B:BACK", 150, 150, 150);
    } else {
        StateMenu_DrawOptions();
        Practice_DrawTextColor(50, 148, "A:TOGGLE  B:BACK", 150, 150, 150);
    }
}

#endif
