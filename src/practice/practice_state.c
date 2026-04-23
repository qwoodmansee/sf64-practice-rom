#include "practice.h"

#ifdef PRACTICE_ROM

typedef enum StateOption {
    SOPT_LASERS,
    SOPT_BOMBS,
    SOPT_LIVES,
    SOPT_RIGHT_WING,
    SOPT_LEFT_WING,
    SOPT_FALCO,
    SOPT_SLIPPY,
    SOPT_PEPPY,
    SOPT_SKIP_CUTSCENES,
    SOPT_INPUT_DISPLAY,
    SOPT_BACK,
    SOPT_MAX,
} StateOption;

static s32 sSelectedOption = 0;
static bool sStateMenuOpen = false;

static const char* sLaserNames[] = { "SINGLE", "TWIN", "HYPER" };
static const char* sWingNames[] = { "NONE", "BROKEN", "INTACT" };

bool Practice_StateMenuIsOpen(void) {
    return sStateMenuOpen;
}

void Practice_StateMenu_Open(void) {
    sStateMenuOpen = true;
    sSelectedOption = 0;
}

void Practice_StateMenu_Close(void) {
    sStateMenuOpen = false;
}

void Practice_StateMenu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];

    if (press->button & B_BUTTON) {
        Practice_StateMenu_Close();
        return;
    }

    if (press->button & U_JPAD) {
        sSelectedOption--;
        if (sSelectedOption < 0) {
            sSelectedOption = SOPT_MAX - 1;
        }
    }
    if (press->button & D_JPAD) {
        sSelectedOption++;
        if (sSelectedOption >= SOPT_MAX) {
            sSelectedOption = 0;
        }
    }

    if (press->button & A_BUTTON) {
        if (sSelectedOption == SOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
    }

    if ((press->button & R_JPAD) || (press->button & A_BUTTON)) {
        switch (sSelectedOption) {
            case SOPT_LASERS:
                gPracticeConfig.laserStrength++;
                if (gPracticeConfig.laserStrength > LASERS_HYPER) {
                    gPracticeConfig.laserStrength = LASERS_SINGLE;
                }
                break;
            case SOPT_BOMBS:
                gPracticeConfig.bombCount++;
                if (gPracticeConfig.bombCount > 9) {
                    gPracticeConfig.bombCount = 0;
                }
                break;
            case SOPT_LIVES:
                gPracticeConfig.lifeCount++;
                if (gPracticeConfig.lifeCount > 99) {
                    gPracticeConfig.lifeCount = 1;
                }
                break;
            case SOPT_RIGHT_WING:
                gPracticeConfig.rightWingState++;
                if (gPracticeConfig.rightWingState > WINGSTATE_INTACT) {
                    gPracticeConfig.rightWingState = WINGSTATE_NONE;
                }
                break;
            case SOPT_LEFT_WING:
                gPracticeConfig.leftWingState++;
                if (gPracticeConfig.leftWingState > WINGSTATE_INTACT) {
                    gPracticeConfig.leftWingState = WINGSTATE_NONE;
                }
                break;
            case SOPT_FALCO:
                gPracticeConfig.falcoAlive ^= true;
                break;
            case SOPT_SLIPPY:
                gPracticeConfig.slippyAlive ^= true;
                break;
            case SOPT_PEPPY:
                gPracticeConfig.peppyAlive ^= true;
                break;
            case SOPT_SKIP_CUTSCENES:
                gPracticeConfig.skipCutscenes ^= true;
                break;
            case SOPT_INPUT_DISPLAY:
                gPracticeConfig.showInputDisplay ^= true;
                break;
        }
    }

    if (press->button & L_JPAD) {
        switch (sSelectedOption) {
            case SOPT_LASERS:
                if (gPracticeConfig.laserStrength == LASERS_SINGLE) {
                    gPracticeConfig.laserStrength = LASERS_HYPER;
                } else {
                    gPracticeConfig.laserStrength--;
                }
                break;
            case SOPT_BOMBS:
                gPracticeConfig.bombCount--;
                if (gPracticeConfig.bombCount < 0) {
                    gPracticeConfig.bombCount = 9;
                }
                break;
            case SOPT_LIVES:
                gPracticeConfig.lifeCount--;
                if (gPracticeConfig.lifeCount < 1) {
                    gPracticeConfig.lifeCount = 99;
                }
                break;
            case SOPT_RIGHT_WING:
                if (gPracticeConfig.rightWingState == WINGSTATE_NONE) {
                    gPracticeConfig.rightWingState = WINGSTATE_INTACT;
                } else {
                    gPracticeConfig.rightWingState--;
                }
                break;
            case SOPT_LEFT_WING:
                if (gPracticeConfig.leftWingState == WINGSTATE_NONE) {
                    gPracticeConfig.leftWingState = WINGSTATE_INTACT;
                } else {
                    gPracticeConfig.leftWingState--;
                }
                break;
            case SOPT_FALCO:
                gPracticeConfig.falcoAlive ^= true;
                break;
            case SOPT_SLIPPY:
                gPracticeConfig.slippyAlive ^= true;
                break;
            case SOPT_PEPPY:
                gPracticeConfig.peppyAlive ^= true;
                break;
            case SOPT_SKIP_CUTSCENES:
                gPracticeConfig.skipCutscenes ^= true;
                break;
            case SOPT_INPUT_DISPLAY:
                gPracticeConfig.showInputDisplay ^= true;
                break;
        }
    }
}

void Practice_StateMenu_Draw(void) {
    s32 y;
    s32 i;

    Practice_DrawBox(40, 40, 240, 160, 0, 0, 60, 200);
    Practice_DrawTextColor(50, 44, "STARTING CONDITIONS", 0, 255, 128);

    for (i = 0; i < SOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case SOPT_LASERS:
                Practice_DrawText(54, y, "LASER:");
                Practice_DrawTextColor(120, y, sLaserNames[gPracticeConfig.laserStrength], 255, 255, 0);
                break;
            case SOPT_BOMBS:
                Practice_DrawText(54, y, "BOMBS:");
                Practice_DrawNumber(120, y, gPracticeConfig.bombCount);
                break;
            case SOPT_LIVES:
                Practice_DrawText(54, y, "LIVES:");
                Practice_DrawNumber(120, y, gPracticeConfig.lifeCount);
                break;
            case SOPT_RIGHT_WING:
                Practice_DrawText(54, y, "R WING:");
                Practice_DrawTextColor(120, y, sWingNames[gPracticeConfig.rightWingState], 255, 255, 0);
                break;
            case SOPT_LEFT_WING:
                Practice_DrawText(54, y, "L WING:");
                Practice_DrawTextColor(120, y, sWingNames[gPracticeConfig.leftWingState], 255, 255, 0);
                break;
            case SOPT_FALCO:
                Practice_DrawText(54, y, "FALCO:");
                Practice_DrawTextColor(120, y, gPracticeConfig.falcoAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.falcoAlive ? 0 : 255, gPracticeConfig.falcoAlive ? 255 : 100, 0);
                break;
            case SOPT_SLIPPY:
                Practice_DrawText(54, y, "SLIPPY:");
                Practice_DrawTextColor(120, y, gPracticeConfig.slippyAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.slippyAlive ? 0 : 255, gPracticeConfig.slippyAlive ? 255 : 100, 0);
                break;
            case SOPT_PEPPY:
                Practice_DrawText(54, y, "PEPPY:");
                Practice_DrawTextColor(120, y, gPracticeConfig.peppyAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.peppyAlive ? 0 : 255, gPracticeConfig.peppyAlive ? 255 : 100, 0);
                break;
            case SOPT_SKIP_CUTSCENES:
                Practice_DrawText(54, y, "SCENES:");
                Practice_DrawTextColor(120, y, gPracticeConfig.skipCutscenes ? "SKIP" : "PLAY",
                    gPracticeConfig.skipCutscenes ? 0 : 255, gPracticeConfig.skipCutscenes ? 255 : 100, 0);
                break;
            case SOPT_INPUT_DISPLAY:
                Practice_DrawText(54, y, "INPUT:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showInputDisplay ? "ON" : "OFF",
                    gPracticeConfig.showInputDisplay ? 0 : 255, gPracticeConfig.showInputDisplay ? 255 : 100, 0);
                break;
            case SOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }

    Practice_DrawTextColor(50, 190, "D-PAD:CHANGE  B:BACK", 150, 150, 150);
}

#endif
