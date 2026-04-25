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
    OOPT_HUD_OVERLAY,
    OOPT_LAG_FRAMES,
    OOPT_SPEED,
    OOPT_CHARGE_TIMING,
    OOPT_MISSED_INPUTS,
    OOPT_HIT_TRACKING,
    OOPT_HITBOX_MENU,
    OOPT_BACK,
    OOPT_MAX,
} OptionsOption;

typedef enum HitboxOption {
    HOPT_MASTER,
    HOPT_ACTORS,
    HOPT_SCENERY,
    HOPT_ITEMS,
    HOPT_PLAYER,
    HOPT_FLASH,
    HOPT_BACK,
    HOPT_MAX,
} HitboxOption;

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
    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT: return LOPT_MAX;
        case PSUBMENU_OPTIONS: return OOPT_MAX;
        case PSUBMENU_HITBOX:  return HOPT_MAX;
        default:               return 0;
    }
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
            case OOPT_HUD_OVERLAY:
                gPracticeConfig.showHudOverlay ^= true;
                break;
            case OOPT_LAG_FRAMES:
                gPracticeConfig.showLagFrames ^= true;
                break;
            case OOPT_SPEED:
                gPracticeConfig.showSpeed ^= true;
                break;
            case OOPT_CHARGE_TIMING:
                gPracticeConfig.showChargeTiming ^= true;
                break;
            case OOPT_MISSED_INPUTS:
                gPracticeConfig.showMissedInputs ^= true;
                break;
            case OOPT_HIT_TRACKING:
                gPracticeConfig.showHitTracking ^= true;
                break;
        }
    }
}

static void StateMenu_UpdateHitbox(u16 buttons) {
    if ((buttons & R_JPAD) || (buttons & A_BUTTON) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case HOPT_MASTER:
                gPracticeConfig.showHitboxes ^= true;
                break;
            case HOPT_ACTORS:
                gPracticeConfig.showHitboxActors ^= true;
                break;
            case HOPT_SCENERY:
                gPracticeConfig.showHitboxScenery ^= true;
                break;
            case HOPT_ITEMS:
                gPracticeConfig.showHitboxItems ^= true;
                break;
            case HOPT_PLAYER:
                gPracticeConfig.showHitboxPlayer ^= true;
                break;
            case HOPT_FLASH:
                gPracticeConfig.showHitboxFlash ^= true;
                break;
        }
    }
}

void Practice_StateMenu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    s32 optCount = StateMenu_GetOptionCount();

    if (press->button & B_BUTTON) {
        if (sActiveSubMenu == PSUBMENU_HITBOX) {
            sActiveSubMenu = PSUBMENU_OPTIONS;
            sSelectedOption = OOPT_HITBOX_MENU;
            return;
        }
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
        if (sActiveSubMenu == PSUBMENU_LOADOUT && sSelectedOption == LOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
        if (sActiveSubMenu == PSUBMENU_OPTIONS && sSelectedOption == OOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
        if (sActiveSubMenu == PSUBMENU_OPTIONS && sSelectedOption == OOPT_HITBOX_MENU) {
            sActiveSubMenu = PSUBMENU_HITBOX;
            sSelectedOption = 0;
            return;
        }
        if (sActiveSubMenu == PSUBMENU_HITBOX && sSelectedOption == HOPT_BACK) {
            sActiveSubMenu = PSUBMENU_OPTIONS;
            sSelectedOption = OOPT_HITBOX_MENU;
            return;
        }
    }

    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:
            StateMenu_UpdateLoadout(press->button);
            break;
        case PSUBMENU_OPTIONS:
            StateMenu_UpdateOptions(press->button);
            break;
        case PSUBMENU_HITBOX:
            StateMenu_UpdateHitbox(press->button);
            break;
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
            case OOPT_HUD_OVERLAY:
                Practice_DrawText(54, y, "HUD:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showHudOverlay ? "ON" : "OFF",
                    gPracticeConfig.showHudOverlay ? 0 : 255, gPracticeConfig.showHudOverlay ? 255 : 100, 0);
                break;
            case OOPT_LAG_FRAMES:
                Practice_DrawText(54, y, "  LAG:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showLagFrames ? "ON" : "OFF",
                    gPracticeConfig.showLagFrames ? 0 : 255, gPracticeConfig.showLagFrames ? 255 : 100, 0);
                break;
            case OOPT_SPEED:
                Practice_DrawText(54, y, "  SPEED:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showSpeed ? "ON" : "OFF",
                    gPracticeConfig.showSpeed ? 0 : 255, gPracticeConfig.showSpeed ? 255 : 100, 0);
                break;
            case OOPT_CHARGE_TIMING:
                Practice_DrawText(54, y, "  CHARGE:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showChargeTiming ? "ON" : "OFF",
                    gPracticeConfig.showChargeTiming ? 0 : 255, gPracticeConfig.showChargeTiming ? 255 : 100, 0);
                break;
            case OOPT_MISSED_INPUTS:
                Practice_DrawText(54, y, "  MISSED:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showMissedInputs ? "ON" : "OFF",
                    gPracticeConfig.showMissedInputs ? 0 : 255, gPracticeConfig.showMissedInputs ? 255 : 100, 0);
                break;
            case OOPT_HIT_TRACKING:
                Practice_DrawText(54, y, "  HITS:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showHitTracking ? "ON" : "OFF",
                    gPracticeConfig.showHitTracking ? 0 : 255, gPracticeConfig.showHitTracking ? 255 : 100, 0);
                break;
            case OOPT_HITBOX_MENU:
                Practice_DrawTextColor(54, y, "HITBOX VIEWER...", 200, 200, 255);
                break;
            case OOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

static void StateMenu_DrawHitbox(void) {
    s32 y;
    s32 i;

    for (i = 0; i < HOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case HOPT_MASTER:
                Practice_DrawText(54, y, "HITBOXES:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxes ? "ON" : "OFF",
                    gPracticeConfig.showHitboxes ? 0 : 255, gPracticeConfig.showHitboxes ? 255 : 100, 0);
                break;
            case HOPT_ACTORS:
                Practice_DrawText(54, y, "  ACTORS:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxActors ? "ON" : "OFF",
                    gPracticeConfig.showHitboxActors ? 0 : 255, gPracticeConfig.showHitboxActors ? 255 : 100, 0);
                break;
            case HOPT_SCENERY:
                Practice_DrawText(54, y, "  SCENERY:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxScenery ? "ON" : "OFF",
                    gPracticeConfig.showHitboxScenery ? 0 : 255, gPracticeConfig.showHitboxScenery ? 255 : 100, 0);
                break;
            case HOPT_ITEMS:
                Practice_DrawText(54, y, "  ITEMS:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxItems ? "ON" : "OFF",
                    gPracticeConfig.showHitboxItems ? 0 : 255, gPracticeConfig.showHitboxItems ? 255 : 100, 0);
                break;
            case HOPT_PLAYER:
                Practice_DrawText(54, y, "  PLAYER:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxPlayer ? "ON" : "OFF",
                    gPracticeConfig.showHitboxPlayer ? 0 : 255, gPracticeConfig.showHitboxPlayer ? 255 : 100, 0);
                break;
            case HOPT_FLASH:
                Practice_DrawText(54, y, "  FLASH:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxFlash ? "ON" : "OFF",
                    gPracticeConfig.showHitboxFlash ? 0 : 255, gPracticeConfig.showHitboxFlash ? 255 : 100, 0);
                break;
            case HOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

void Practice_StateMenu_Draw(void) {
    const char* title;
    s32 boxHeight;
    s32 helpY;

    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:
            title = "LOADOUT";
            boxHeight = 115;
            helpY = 148;
            break;
        case PSUBMENU_OPTIONS:
            title = "OPTIONS";
            boxHeight = 213;
            helpY = 246;
            break;
        case PSUBMENU_HITBOX:
            title = "HITBOX VIEWER";
            boxHeight = 143;
            helpY = 176;
            break;
        default:
            return;
    }

    Practice_DrawBox(40, 40, 240, boxHeight, 0, 0, 60, 200);
    Practice_DrawTextColor(50, 44, title, 0, 255, 128);

    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:
            StateMenu_DrawLoadout();
            Practice_DrawTextColor(50, helpY, "D-PAD:CHANGE  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_OPTIONS:
            StateMenu_DrawOptions();
            Practice_DrawTextColor(50, helpY, "A:TOGGLE  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_HITBOX:
            StateMenu_DrawHitbox();
            Practice_DrawTextColor(50, helpY, "A:TOGGLE  B:BACK", 150, 150, 150);
            break;
    }
}

#endif
