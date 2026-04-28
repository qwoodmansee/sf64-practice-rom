#include "practice.h"
#include "iodev/iodev.h"

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
    gPracticeConfig.showHudOverlay = true;
    gPracticeConfig.showLagFrames = true;
    gPracticeConfig.showSpeed = true;
    gPracticeConfig.showChargeTiming = false;
    gPracticeConfig.showMissedInputs = false;
    gPracticeConfig.showHitTracking = true;
    gPracticeConfig.showHitboxes = false;
    gPracticeConfig.showHitboxActors = false;
    gPracticeConfig.showHitboxScenery = false;
    gPracticeConfig.showHitboxItems = false;
    gPracticeConfig.showHitboxPlayer = false;
    gPracticeConfig.showHitboxFlash = false;
    gPracticeConfig.showSpawnZones = false;
    gPracticeConfig.showSpawnActors = true;
    gPracticeConfig.showSpawnItems = true;
    gPracticeConfig.showSpawnScenery = false;
    gPracticeConfig.expertMode = false;

    osSyncPrintf("=== PRACTICE ROM boot @ %s %s ===\n", __DATE__, __TIME__);

    {
        iodev_id_t cart = iodev_detect();
        iodev_result_t sd = iodev_sd_init();
        osSyncPrintf("[iodev] cart=%d sd_init=%d\n", (int)cart, (int)sd);
    }

    Practice_SlotTest_Run();

#ifdef IODEV_DIAG_FATFS
    /* Phase 2 hardware verification probe. Build with IODEV_DIAG_FATFS=1
     * and follow docs/superpowers/plans/HW_VERIFY_phase2.md. */
    Practice_TestFatfs();
#endif
}

void Practice_Update(void) {
    switch (gPracticeScreen) {
        case PSCREEN_LEVEL_SELECT:
            Practice_LevelSelect_Update();
            break;
        case PSCREEN_GAMEPLAY:
            Practice_Hud_Update();
            if (Practice_FreeCam_IsActive()) {
                Practice_FreeCam_Update();
            } else if (gPracticeMenuState != PMENU_CLOSED) {
                Practice_Menu_Update();
            } else {
                if (Practice_InputTriggered(PACTION_OPEN_MENU)) {
                    Practice_Menu_OpenFrozen();
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
            if (!Practice_FreeCam_IsActive() || Practice_FreeCam_OverlayVisible()) {
                Practice_Hud_Draw();
                if (gPracticeConfig.showInputDisplay) {
                    Practice_InputDisplay_Draw();
                }
            }
            if (Practice_FreeCam_IsActive()) {
                Practice_FreeCam_Draw();
            } else if (gPracticeMenuState != PMENU_CLOSED) {
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

    gExpertMode = gPracticeConfig.expertMode;

    if (gPracticeConfig.skipCutscenes) {
        switch (gCurrentLevel) {
            case LEVEL_CORNERIA:  AUDIO_PLAY_BGM(NA_BGM_STAGE_CO); break;
            case LEVEL_METEO:     AUDIO_PLAY_BGM(NA_BGM_STAGE_ME); break;
            case LEVEL_SECTOR_Y:  AUDIO_PLAY_BGM(NA_BGM_STAGE_SY); break;
            case LEVEL_FORTUNA:   AUDIO_PLAY_BGM(NA_BGM_STAGE_FO); break;
            case LEVEL_KATINA:    AUDIO_PLAY_BGM(NA_BGM_STAGE_KA); break;
            case LEVEL_AQUAS:     AUDIO_PLAY_BGM(NA_BGM_STAGE_AQ); break;
            case LEVEL_SECTOR_X:  AUDIO_PLAY_BGM(NA_BGM_STAGE_SX); break;
            case LEVEL_SOLAR:     AUDIO_PLAY_BGM(NA_BGM_STAGE_SO); break;
            case LEVEL_ZONESS:    AUDIO_PLAY_BGM(NA_BGM_STAGE_ZO); break;
            case LEVEL_TITANIA:   AUDIO_PLAY_BGM(NA_BGM_STAGE_TI); break;
            case LEVEL_MACBETH:   AUDIO_PLAY_BGM(NA_BGM_STAGE_MA); break;
            case LEVEL_SECTOR_Z:  AUDIO_PLAY_BGM(NA_BGM_STAGE_SZ); break;
            case LEVEL_BOLSE:     AUDIO_PLAY_BGM(NA_BGM_STAGE_BO); break;
            case LEVEL_AREA_6:    AUDIO_PLAY_BGM(NA_BGM_STAGE_A6); break;
            case LEVEL_VENOM_1:
            case LEVEL_VENOM_2:   AUDIO_PLAY_BGM(NA_BGM_STAGE_VE1); break;
            case LEVEL_TRAINING:  AUDIO_PLAY_BGM(NA_BGM_TRAINING); break;
            default: break;
        }
    }
}

#endif
