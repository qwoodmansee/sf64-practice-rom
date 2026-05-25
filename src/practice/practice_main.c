#include "practice.h"
#include "iodev/iodev.h"
#include "practice_late.h"
#include "practice_overlay.h"

#ifdef PRACTICE_ROM

PracticeScreen gPracticeScreen;
PracticeConfig gPracticeConfig;
PracticeMenuState gPracticeMenuState;

void Practice_Init(void) {
    Practice_Late_Init();

    gPracticeScreen = PSCREEN_LEVEL_SELECT;
    Practice_LevelSelect_OnEnter();
    gPracticeMenuState = PMENU_CLOSED;
    Practice_FrameAdvance_Init();

    gPracticeConfig.laserStrength = LASERS_HYPER;
    gPracticeConfig.bombCount = 3;
    gPracticeConfig.lifeCount = 2;
    gPracticeConfig.goldRingCount = 0;
    gPracticeConfig.rightWingState = WINGSTATE_INTACT;
    gPracticeConfig.leftWingState = WINGSTATE_INTACT;
    gPracticeConfig.falcoAlive = true;
    gPracticeConfig.slippyAlive = true;
    gPracticeConfig.peppyAlive = true;
    gPracticeConfig.showInputDisplay = true;
    gPracticeConfig.skipCutscenes = true;
    gPracticeConfig.showHudOverlay = false;
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
    gPracticeConfig.longHealth = false;
    gPracticeConfig.showPauseMinimap = true;
    gPracticeConfig.showChargeShotMeter = false;
    gPracticeConfig.autoFireChargeShot = false;
    gPracticeConfig.infHealth = false;
    gPracticeConfig.infBombs = false;
    gPracticeConfig.infLives = false;
    gPracticeConfig.infBoost = false;
    gPracticeConfig.prevPlanetsMask  = 0;
    gPracticeConfig.macroBindState   = false;
    gPracticeConfig.macroLoop        = false;
    gPracticeConfig.showEnemyHealth = false;
    gPracticeConfig.enemyHealthSort = 0;
    gPracticeConfig.enemyHealthMinHp = 0;
    gPracticeConfig.enemyHealthBossOnly = false;
    gPracticeConfig.enemyHealthHideModels = false;
    gPracticeConfig.hitCount = 0;

    /* Boss-test override flag: runtime-only, reset on every boot.
     * Per-launch resets happen in Practice_LevelSelect_Update's non-boss
     * A-press branch (Task 6) and in Practice_BossTest_Launch (which sets
     * it true after Practice_LaunchLevel returns). */
    gPracticeForceCarrier = false;

    osSyncPrintf("=== PRACTICE ROM boot @ %s %s ===\n", __DATE__, __TIME__);

    /* iodev_* live in .practice_late_core, which is loaded only when an
     * Expansion Pak is present (RAM 0x80720000 is unmapped on stock 4MB).
     * Skip the cart-detect diagnostic on stock carts; SD-backed features
     * are already disabled there. */
    if (osMemSize >= 0x800000U) {
        iodev_id_t cart = iodev_detect();
        iodev_result_t sd = iodev_sd_init();
        osSyncPrintf("[iodev] cart=%d sd_init=%d\n", (int)cart, (int)sd);
    } else {
        osSyncPrintf("[iodev] skipped (stock 4MB, no Expansion Pak)\n");
    }

    osSyncPrintf("[init] Practice_Save_Init enter\n");
    Practice_Save_Init();
    osSyncPrintf("[init] Practice_Save_Init exit\n");
    /* Practice_Sd_Init touches several .practice_late_core symbols
     * (iodev_*, slot_manager_set_sd_scratch) and Practice_Save_ScratchBase()
     * which lives in the Pak-only slot pool. Skip on stock 4MB. */
    if (osMemSize >= 0x800000U) {
        osSyncPrintf("[init] Practice_Sd_Init enter\n");
        Practice_Sd_Init();
        osSyncPrintf("[init] Practice_Sd_Init exit\n");
    } else {
        osSyncPrintf("[init] Practice_Sd_Init skipped (stock 4MB)\n");
    }
    osSyncPrintf("[init] practice_overlay_prime_build_ids enter\n");
    practice_overlay_prime_build_ids();
    osSyncPrintf("[init] practice_overlay_prime_build_ids exit\n");

#ifdef IODEV_DIAG_FATFS
    /* Phase 2 hardware verification probe. Build with IODEV_DIAG_FATFS=1
     * and follow docs/superpowers/plans/HW_VERIFY_phase2.md. */
    Practice_TestFatfs();
#endif
    Practice_Macro_Init();
    osSyncPrintf("[init] Practice_Init returning\n");
}

void Practice_Update(void) {
    Practice_Cheats_Apply();
    Practice_FrameAdvance_Update();
    /* Pak-gated at call site for visibility. The function also guards
     * internally; this call-site check makes the dependency obvious here. */
    if (osMemSize >= 0x00800000U) {
        Practice_Macro_Update();
    }

    if (Practice_Sd_IsActive()) {
        Practice_Sd_Update();
        return;
    }
    Practice_Save_Tick();

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
                    Practice_SaveTrace_HotkeyIsv();
                    Practice_SaveState();
                } else if (Practice_InputTriggered(PACTION_RESTORE_POS)) {
                    Practice_SaveTrace_LoadHotkeyIsv();
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
                Practice_ChargeMeter_Draw();
                if (gPracticeConfig.showInputDisplay) {
                    Practice_InputDisplay_Draw();
                }
                Practice_EnemyHealth_Draw();
            }
            Practice_Macro_Draw();
            Practice_Minimap_Draw();
            if (Practice_Sd_IsActive()) {
                Practice_Sd_Draw();
            } else if (Practice_FreeCam_IsActive()) {
                Practice_FreeCam_Draw();
            } else if (gPracticeMenuState != PMENU_CLOSED) {
                Practice_Menu_Draw();
            }
            break;
    }
}

void Practice_ApplyStartConditions(void) {
    s32 i;

    for (i = 0; i < 30; i++) {
        gLeveLClearStatus[i] = (gPracticeConfig.prevPlanetsMask >> i) & 1;
    }

    /* Bill and Katt are not covered by gClearPlayerInfo, so a -1 shield value
     * from dying in a prior run persists into the next restart and can kill
     * them the moment they spawn. Reset explicitly from the mask. */
    gTeamShields[TEAM_ID_BILL] = (gPracticeConfig.prevPlanetsMask >> LEVEL_KATINA) & 1 ? 255 : 0;
    gTeamShields[TEAM_ID_KATT] = (((gPracticeConfig.prevPlanetsMask >> LEVEL_AQUAS) & 1) ||
                                   ((gPracticeConfig.prevPlanetsMask >> LEVEL_ZONESS) & 1)) ? 255 : 0;

    gLaserStrength[gPlayerNum] = gPracticeConfig.laserStrength;
    gBombCount[gPlayerNum] = gPracticeConfig.bombCount;
    gLifeCount[gPlayerNum] = gPracticeConfig.lifeCount;
    gGoldRingCount[gPlayerNum] = gPracticeConfig.goldRingCount;
    if (gPracticeConfig.longHealth && gGoldRingCount[gPlayerNum] < 3) {
        gGoldRingCount[gPlayerNum] = 3;
    }

    gPlayer[0].arwing.rightWingState = gPracticeConfig.rightWingState;
    gPlayer[0].arwing.leftWingState = gPracticeConfig.leftWingState;
    if (gPracticeConfig.longHealth) {
        gPlayer[0].shields = Play_GetMaxShields();
    }

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
    gHitCount = gPracticeConfig.hitCount;

    if (gPracticeConfig.skipCutscenes) {
        osSyncPrintf("[bgm_dbg] ApplyStart lvl=%d gBgmSeqId=0x%04X\n",
                     (s32)gCurrentLevel, (u32)gBgmSeqId);
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
