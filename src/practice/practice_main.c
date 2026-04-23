#include "practice.h"

#ifdef PRACTICE_ROM

PracticeScreen gPracticeScreen;
PracticeConfig gPracticeConfig;
PracticeMenuState gPracticeMenuState;

void Practice_Init(void) {
    gPracticeScreen = PSCREEN_LEVEL_SELECT;
    gPracticeMenuState = PMENU_CLOSED;

    gPracticeConfig.laserStrength = LASERS_SINGLE;
    gPracticeConfig.bombCount = 3;
    gPracticeConfig.lifeCount = 2;
    gPracticeConfig.rightWingState = WINGSTATE_INTACT;
    gPracticeConfig.leftWingState = WINGSTATE_INTACT;
    gPracticeConfig.falcoAlive = true;
    gPracticeConfig.slippyAlive = true;
    gPracticeConfig.peppyAlive = true;
    gPracticeConfig.showInputDisplay = false;
    gPracticeConfig.skipCutscenes = true;
}

void Practice_Update(void) {
    switch (gPracticeScreen) {
        case PSCREEN_LEVEL_SELECT:
            Practice_LevelSelect_Update();
            break;
        case PSCREEN_GAMEPLAY:
            if (gPracticeMenuState == PMENU_OPEN) {
                Practice_Menu_Update();
            } else {
                if (Practice_InputTriggered(PACTION_OPEN_MENU)) {
                    Practice_Menu_Open();
                } else if (Practice_InputTriggered(PACTION_QUICK_RESTART)) {
                    Practice_LaunchLevel(gCurrentLevel, gLevelPhase);
                } else if (Practice_InputTriggered(PACTION_SAVE_POS)) {
                    Practice_SaveState();
                } else if (Practice_InputTriggered(PACTION_RESTORE_POS)) {
                    Practice_LoadState();
                }
            }
            break;
    }
}

void Practice_Draw(void) {
    switch (gPracticeScreen) {
        case PSCREEN_LEVEL_SELECT:
            Practice_LevelSelect_Draw();
            break;
        case PSCREEN_GAMEPLAY:
            if (gPracticeConfig.showInputDisplay) {
                Practice_InputDisplay_Draw();
            }
            if (gPracticeMenuState == PMENU_OPEN) {
                Practice_Menu_Draw();
            }
            break;
    }
}

void Practice_ApplyStartConditions(void) {
    gLaserStrength[gPlayerNum] = gPracticeConfig.laserStrength;
    gBombCount[gPlayerNum] = gPracticeConfig.bombCount;
    gLifeCount[gPlayerNum] = gPracticeConfig.lifeCount;

    gPlayer[0].arwing.rightWingState = gPracticeConfig.rightWingState;
    gPlayer[0].arwing.leftWingState = gPracticeConfig.leftWingState;

    if (!gPracticeConfig.falcoAlive) {
        gTeamShields[TEAM_ID_FALCO] = 0;
    }
    if (!gPracticeConfig.slippyAlive) {
        gTeamShields[TEAM_ID_SLIPPY] = 0;
    }
    if (!gPracticeConfig.peppyAlive) {
        gTeamShields[TEAM_ID_PEPPY] = 0;
    }
}

#endif
