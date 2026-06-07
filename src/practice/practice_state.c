#include "practice.h"

#ifdef PRACTICE_ROM

typedef enum LoadoutOption {
    LOPT_LASERS,
    LOPT_BOMBS,
    LOPT_LIVES,
    LOPT_GOLD_RINGS,
    LOPT_HEALTH,
    LOPT_RIGHT_WING,
    LOPT_LEFT_WING,
    LOPT_FALCO,
    LOPT_SLIPPY,
    LOPT_PEPPY,
    LOPT_EXPERT,
    LOPT_HIT_COUNT,
    LOPT_PREV_PLANETS,
    LOPT_BACK,
    LOPT_MAX,
} LoadoutOption;

typedef enum DisplayOption {
    DOPT_SKIP_CUTSCENES,
    DOPT_INPUT_DISPLAY,
    DOPT_MINIMAP,
    DOPT_STATS_MENU,
    DOPT_VIS_MENU,
    DOPT_MACRO_MENU,
    DOPT_BACK,
    DOPT_MAX,
} DisplayOption;

typedef enum MacroOption {
    MOPT_RECORD,
    MOPT_PLAY,
    MOPT_REWIND,
    MOPT_TRIM,
    MOPT_FRAMES,
    MOPT_BIND_STATE,
    MOPT_LOOP,
    MOPT_BACK,
    MOPT_MAX,
} MacroOption;

typedef enum StatsOption {
    SOPT_HUD_OVERLAY,
    SOPT_LAG_FRAMES,
    SOPT_SPEED,
    SOPT_CHARGE_TIMING,
    SOPT_MISSED_INPUTS,
    SOPT_HIT_TRACKING,
    SOPT_LEVEL_TIMERS,
    SOPT_BACK,
    SOPT_MAX,
} StatsOption;

typedef enum VisualizerOption {
    VOPT_HITBOXES,
    VOPT_ACTORS,
    VOPT_SCENERY,
    VOPT_ITEMS,
    VOPT_PLAYER,
    VOPT_FLASH,
    VOPT_SPAWN_ZONES,
    VOPT_SPAWN_ACTORS,
    VOPT_SPAWN_ITEMS,
    VOPT_SPAWN_SCENERY,
    VOPT_ENEMY_HEALTH,
    VOPT_BACK,
    VOPT_MAX,
} VisualizerOption;

typedef enum EnemyHealthOption {
    EHOPT_SHOW,
    EHOPT_SORT,
    EHOPT_MIN_HP,
    EHOPT_BOSS_ONLY,
    EHOPT_HIDE_MODELS,
    EHOPT_BACK,
    EHOPT_MAX,
} EnemyHealthOption;

/* Lane rows map to engine AudioType values via sAudioLaneType[]. The three
 * rows mirror the engine's master-volume layer (MUSIC covers BGM + fanfare). */
typedef enum AudioOption {
    AOPT_MUSIC,
    AOPT_SFX,
    AOPT_VOICE,
    AOPT_RESET_ALL,
    AOPT_BACK,
    AOPT_MAX,
} AudioOption;

#define AOPT_LANE_COUNT AOPT_RESET_ALL  /* number of adjustable volume rows */

static s32 sSelectedOption = 0;
static bool sStateMenuOpen = false;
static bool sStateMenuJustOpened = false;
static PracticeSubMenu sActiveSubMenu;
static bool sConfirmOverwrite = false;

static const char* sLaserNames[] = { "SINGLE", "TWIN", "HYPER" };
static const char* sWingNames[] = { "NONE", "BROKEN", "INTACT" };
static const char* sHealthNames[] = { "SHORT", "LONG" };
static const char* sAudioLaneNames[] = { "MUSIC", "SFX", "VOICE" };
/* Row -> engine AudioType (the enum order is MUSIC, VOICE, SFX). */
static const u8 sAudioLaneType[] = { AUDIO_TYPE_MUSIC, AUDIO_TYPE_SFX, AUDIO_TYPE_VOICE };

#define PREV_PLANETS_COUNT 13
static const LevelId sPrevPlanetIds[PREV_PLANETS_COUNT] = {
    LEVEL_METEO, LEVEL_FORTUNA,  LEVEL_SECTOR_X, LEVEL_TITANIA, LEVEL_BOLSE,
    LEVEL_SECTOR_Y, LEVEL_KATINA, LEVEL_SOLAR,  LEVEL_MACBETH, LEVEL_AQUAS,
    LEVEL_ZONESS,   LEVEL_SECTOR_Z, LEVEL_AREA_6,
};

bool Practice_StateMenuIsOpen(void) {
    return sStateMenuOpen;
}

void Practice_StateMenu_Open(PracticeSubMenu subMenu) {
    sStateMenuOpen = true;
    sStateMenuJustOpened = true;
    sSelectedOption = 0;
    sActiveSubMenu = subMenu;
}

void Practice_StateMenu_Close(void) {
    sStateMenuOpen = false;
    sStateMenuJustOpened = false;
}

static s32 StateMenu_GetOptionCount(void) {
    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:       return LOPT_MAX;
        case PSUBMENU_DISPLAY:       return DOPT_MAX;
        case PSUBMENU_STATS:         return SOPT_MAX;
        case PSUBMENU_VISUALIZERS:   return VOPT_MAX;
        case PSUBMENU_PREV_PLANETS:  return PREV_PLANETS_COUNT + 1;
        case PSUBMENU_MACRO:         return MOPT_MAX;
        case PSUBMENU_ENEMY_HEALTH:  return EHOPT_MAX;
        case PSUBMENU_AUDIO:         return AOPT_MAX;
        default:                     return 0;
    }
}

static void StateMenu_ApplyLoadoutLive(void) {
    gLaserStrength[gPlayerNum] = gPracticeConfig.laserStrength;
    gBombCount[gPlayerNum] = gPracticeConfig.bombCount;
    gLifeCount[gPlayerNum] = gPracticeConfig.lifeCount;
    gGoldRingCount[gPlayerNum] = gPracticeConfig.goldRingCount;
    if (gPracticeConfig.longHealth && gGoldRingCount[gPlayerNum] < 3) {
        gGoldRingCount[gPlayerNum] = 3;
    }

    if ((gGameState == GSTATE_PLAY) && (gPlayState == PLAY_UPDATE)) {
        gPlayer[0].arwing.rightWingState = gPracticeConfig.rightWingState;
        gPlayer[0].arwing.leftWingState = gPracticeConfig.leftWingState;
        if (gPracticeConfig.longHealth) {
            gPlayer[0].shields = Play_GetMaxShields();
        }
    }

    if (!gPracticeConfig.falcoAlive) {
        gTeamShields[TEAM_ID_FALCO] = 0;
    } else if (gTeamShields[TEAM_ID_FALCO] == 0) {
        gTeamShields[TEAM_ID_FALCO] = 255;
    }
    if (!gPracticeConfig.slippyAlive) {
        gTeamShields[TEAM_ID_SLIPPY] = 0;
    } else if (gTeamShields[TEAM_ID_SLIPPY] == 0) {
        gTeamShields[TEAM_ID_SLIPPY] = 255;
    }
    if (!gPracticeConfig.peppyAlive) {
        gTeamShields[TEAM_ID_PEPPY] = 0;
    } else if (gTeamShields[TEAM_ID_PEPPY] == 0) {
        gTeamShields[TEAM_ID_PEPPY] = 255;
    }
}

static void StateMenu_UpdateLoadout(u16 buttons) {
    bool loadoutChanged = false;
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
            case LOPT_GOLD_RINGS:
                gPracticeConfig.goldRingCount++;
                if (gPracticeConfig.goldRingCount > 2) {
                    gPracticeConfig.goldRingCount = 0;
                }
                break;
            case LOPT_HEALTH:
                gPracticeConfig.longHealth ^= true;
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
            case LOPT_FALCO:
                gPracticeConfig.falcoAlive ^= true;
                break;
            case LOPT_SLIPPY:
                gPracticeConfig.slippyAlive ^= true;
                break;
            case LOPT_PEPPY:
                gPracticeConfig.peppyAlive ^= true;
                break;
            case LOPT_EXPERT:
                gPracticeConfig.expertMode ^= true;
                break;
            case LOPT_HIT_COUNT:
                gPracticeConfig.hitCount++;
                if (gPracticeConfig.hitCount > 999) {
                    gPracticeConfig.hitCount = 0;
                }
                break;
        }
        loadoutChanged = true;
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
            case LOPT_GOLD_RINGS:
                if (gPracticeConfig.goldRingCount == 0) {
                    gPracticeConfig.goldRingCount = 2;
                } else {
                    gPracticeConfig.goldRingCount--;
                }
                break;
            case LOPT_HEALTH:
                gPracticeConfig.longHealth ^= true;
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
            case LOPT_FALCO:
                gPracticeConfig.falcoAlive ^= true;
                break;
            case LOPT_SLIPPY:
                gPracticeConfig.slippyAlive ^= true;
                break;
            case LOPT_PEPPY:
                gPracticeConfig.peppyAlive ^= true;
                break;
            case LOPT_EXPERT:
                gPracticeConfig.expertMode ^= true;
                break;
            case LOPT_HIT_COUNT:
                gPracticeConfig.hitCount--;
                if (gPracticeConfig.hitCount < 0) {
                    gPracticeConfig.hitCount = 999;
                }
                break;
        }
        loadoutChanged = true;
    }

    if (loadoutChanged) {
        StateMenu_ApplyLoadoutLive();
    }
}

static void StateMenu_UpdateDisplay(u16 buttons) {
    if ((buttons & R_JPAD) || (buttons & A_BUTTON) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case DOPT_SKIP_CUTSCENES:
                gPracticeConfig.skipCutscenes ^= true;
                break;
            case DOPT_INPUT_DISPLAY:
                gPracticeConfig.showInputDisplay ^= true;
                break;
            case DOPT_MINIMAP:
                gPracticeConfig.showPauseMinimap ^= true;
                break;
        }
    }
}

static void StateMenu_UpdateStats(u16 buttons) {
    if ((buttons & R_JPAD) || (buttons & A_BUTTON) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case SOPT_HUD_OVERLAY:
                gPracticeConfig.showHudOverlay ^= true;
                break;
            case SOPT_LAG_FRAMES:
                gPracticeConfig.showLagFrames ^= true;
                break;
            case SOPT_SPEED:
                gPracticeConfig.showSpeed ^= true;
                break;
            case SOPT_CHARGE_TIMING:
                gPracticeConfig.showChargeTiming ^= true;
                break;
            case SOPT_MISSED_INPUTS:
                gPracticeConfig.showMissedInputs ^= true;
                break;
            case SOPT_HIT_TRACKING:
                gPracticeConfig.showHitTracking ^= true;
                break;
            case SOPT_LEVEL_TIMERS:
                gPracticeConfig.showLevelTimers ^= true;
                break;
        }
    }
}

static void StateMenu_UpdateVisualizers(u16 buttons) {
    if ((buttons & R_JPAD) || (buttons & A_BUTTON) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case VOPT_HITBOXES:
                gPracticeConfig.showHitboxes ^= true;
                break;
            case VOPT_ACTORS:
                gPracticeConfig.showHitboxActors ^= true;
                break;
            case VOPT_SCENERY:
                gPracticeConfig.showHitboxScenery ^= true;
                break;
            case VOPT_ITEMS:
                gPracticeConfig.showHitboxItems ^= true;
                break;
            case VOPT_PLAYER:
                gPracticeConfig.showHitboxPlayer ^= true;
                break;
            case VOPT_FLASH:
                gPracticeConfig.showHitboxFlash ^= true;
                break;
            case VOPT_SPAWN_ZONES:
                gPracticeConfig.showSpawnZones ^= true;
                break;
            case VOPT_SPAWN_ACTORS:
                gPracticeConfig.showSpawnActors ^= true;
                break;
            case VOPT_SPAWN_ITEMS:
                gPracticeConfig.showSpawnItems ^= true;
                break;
            case VOPT_SPAWN_SCENERY:
                gPracticeConfig.showSpawnScenery ^= true;
                break;
            case VOPT_ENEMY_HEALTH:
                sActiveSubMenu = PSUBMENU_ENEMY_HEALTH;
                sSelectedOption = 0;
                return;
        }
    }
}

static void StateMenu_UpdateMacro(u16 buttons) {
    /* Confirm-overwrite dialog: A confirms, B cancels (B handled before this). */
    if (sConfirmOverwrite) {
        if (buttons & A_BUTTON) {
            Practice_Macro_StartRecord();
            sConfirmOverwrite = false;
        }
        return;
    }

    if ((buttons & A_BUTTON) || (buttons & R_JPAD) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case MOPT_RECORD:
                if (Practice_Macro_IsArmed() || Practice_Macro_IsRecording()) {
                    Practice_Macro_StopRecord();
                } else if (Practice_Macro_HasData() &&
                           Practice_Macro_GetHead() < Practice_Macro_GetLen()) {
                    /* Head is before end: would clobber existing data. */
                    sConfirmOverwrite = true;
                } else {
                    /* Fresh (no data) or at trim point (head==len): start/append. */
                    Practice_Macro_StartRecord();
                }
                break;
            case MOPT_PLAY:
                if (Practice_Macro_IsPlaying()) {
                    Practice_Macro_StopPlay();
                } else {
                    Practice_Macro_StartPlay();
                }
                break;
            case MOPT_REWIND:
                Practice_Macro_Rewind();
                break;
            case MOPT_TRIM:
                Practice_Macro_Trim();
                break;
            case MOPT_BIND_STATE:
                gPracticeConfig.macroBindState = !gPracticeConfig.macroBindState;
                break;
            case MOPT_LOOP:
                gPracticeConfig.macroLoop = !gPracticeConfig.macroLoop;
                break;
        }
    }
}

static void StateMenu_UpdatePrevPlanets(u16 buttons) {
    if ((buttons & A_BUTTON) || (buttons & R_JPAD) || (buttons & L_JPAD)) {
        if (sSelectedOption < PREV_PLANETS_COUNT) {
            u32 bit = 1u << sPrevPlanetIds[sSelectedOption];
            gPracticeConfig.prevPlanetsMask ^= bit;
        }
    }
}

static const s32 sMinHpValues[] = { 0, 1, 5, 10, 25, 50 };
#define MIN_HP_COUNT 6

static void StateMenu_UpdateEnemyHealth(u16 buttons) {
    s32 i;
    if ((buttons & R_JPAD) || (buttons & A_BUTTON) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case EHOPT_SHOW:
                gPracticeConfig.showEnemyHealth ^= true;
                break;
            case EHOPT_SORT:
                gPracticeConfig.enemyHealthSort ^= 1;
                break;
            case EHOPT_MIN_HP:
                if (buttons & L_JPAD) {
                    /* cycle backward */
                    for (i = 0; i < MIN_HP_COUNT; i++) {
                        if (sMinHpValues[i] == gPracticeConfig.enemyHealthMinHp) {
                            gPracticeConfig.enemyHealthMinHp = sMinHpValues[(i + MIN_HP_COUNT - 1) % MIN_HP_COUNT];
                            break;
                        }
                    }
                } else {
                    /* cycle forward */
                    for (i = 0; i < MIN_HP_COUNT; i++) {
                        if (sMinHpValues[i] == gPracticeConfig.enemyHealthMinHp) {
                            gPracticeConfig.enemyHealthMinHp = sMinHpValues[(i + 1) % MIN_HP_COUNT];
                            break;
                        }
                    }
                }
                break;
            case EHOPT_BOSS_ONLY:
                gPracticeConfig.enemyHealthBossOnly ^= true;
                break;
            case EHOPT_HIDE_MODELS:
                gPracticeConfig.enemyHealthHideModels ^= true;
                break;
        }
    }
}

#define AUDIO_VOL_STEP 9   /* 0..99 in 11 steps; reaches both 0 and 99 exactly */

/* Audio submenu: L/R adjust the hovered lane's volume; A resets it (or RESET
 * ALL). Volume rows map to an engine AudioType via sAudioLaneType[]. */
static void StateMenu_UpdateAudio(u16 buttons) {
    s32 row = sSelectedOption;

    if (row < AOPT_LANE_COUNT) {
        s32 type = sAudioLaneType[row];

        if (buttons & L_JPAD) {
            Practice_Audio_AdjustVolume(type, -AUDIO_VOL_STEP);
        }
        if (buttons & R_JPAD) {
            Practice_Audio_AdjustVolume(type, AUDIO_VOL_STEP);
        }
        if (buttons & A_BUTTON) {
            Practice_Audio_ResetLane(type);
        }
    } else if ((row == AOPT_RESET_ALL) && (buttons & A_BUTTON)) {
        Practice_Audio_ResetAll();
    }
}

void Practice_StateMenu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    s32 optCount = StateMenu_GetOptionCount();

    if (press->button & B_BUTTON) {
        /* If confirm-overwrite dialog is showing, B cancels it instead of
         * closing the whole menu. */
        if (sConfirmOverwrite) {
            sConfirmOverwrite = false;
            return;
        }
        if (sActiveSubMenu == PSUBMENU_ENEMY_HEALTH) {
            sActiveSubMenu = PSUBMENU_VISUALIZERS;
            sSelectedOption = 0;
        } else {
            Practice_StateMenu_Close();
        }
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
        if (sActiveSubMenu == PSUBMENU_DISPLAY && sSelectedOption == DOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
        if (sActiveSubMenu == PSUBMENU_DISPLAY && sSelectedOption == DOPT_STATS_MENU) {
            sActiveSubMenu = PSUBMENU_STATS;
            sSelectedOption = 0;
            return;
        }
        if (sActiveSubMenu == PSUBMENU_DISPLAY && sSelectedOption == DOPT_VIS_MENU) {
            sActiveSubMenu = PSUBMENU_VISUALIZERS;
            sSelectedOption = 0;
            return;
        }
        if (sActiveSubMenu == PSUBMENU_DISPLAY && sSelectedOption == DOPT_MACRO_MENU) {
            sActiveSubMenu = PSUBMENU_MACRO;
            sSelectedOption = 0;
            return;
        }
        if (sActiveSubMenu == PSUBMENU_MACRO && sSelectedOption == MOPT_BACK) {
            /* While the overwrite-confirm dialog is up, the cursor can have
             * moved to BACK via D-pad navigation. A on BACK must NOT close
             * the menu -- StateMenu_UpdateMacro owns the A press as the
             * confirmation. Defer to the submenu dispatch below. */
            if (sConfirmOverwrite) {
                /* Fall through to switch (sActiveSubMenu) so the macro
                 * submenu can consume the A as a confirmation. */
            } else {
                Practice_StateMenu_Close();
                return;
            }
        }
        if (sActiveSubMenu == PSUBMENU_STATS && sSelectedOption == SOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
        if (sActiveSubMenu == PSUBMENU_VISUALIZERS && sSelectedOption == VOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
        if (sActiveSubMenu == PSUBMENU_LOADOUT && sSelectedOption == LOPT_PREV_PLANETS) {
            sActiveSubMenu = PSUBMENU_PREV_PLANETS;
            sSelectedOption = 0;
            return;
        }
        if (sActiveSubMenu == PSUBMENU_PREV_PLANETS && sSelectedOption == PREV_PLANETS_COUNT) {
            Practice_StateMenu_Close();
            return;
        }
        if (sActiveSubMenu == PSUBMENU_ENEMY_HEALTH && sSelectedOption == EHOPT_BACK) {
            sActiveSubMenu = PSUBMENU_VISUALIZERS;
            sSelectedOption = 0;
            return;
        }
        if (sActiveSubMenu == PSUBMENU_AUDIO && sSelectedOption == AOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
    }

    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:
            StateMenu_UpdateLoadout(press->button);
            break;
        case PSUBMENU_DISPLAY:
            StateMenu_UpdateDisplay(press->button);
            break;
        case PSUBMENU_STATS:
            StateMenu_UpdateStats(press->button);
            break;
        case PSUBMENU_VISUALIZERS:
            StateMenu_UpdateVisualizers(press->button);
            break;
        case PSUBMENU_PREV_PLANETS:
            StateMenu_UpdatePrevPlanets(press->button);
            break;
        case PSUBMENU_MACRO:
            StateMenu_UpdateMacro(press->button);
            break;
        case PSUBMENU_ENEMY_HEALTH:
            StateMenu_UpdateEnemyHealth(press->button);
            break;
        case PSUBMENU_AUDIO:
            StateMenu_UpdateAudio(press->button);
            break;
    }
}

/* Common pattern: draw "ON" or "OFF" at (x, y), green when on, orange when off.
 * Used 19 times across the submenu draw routines. Pulled out of inline ternary
 * spam to shrink .text. */
static void DrawToggleValue(s32 x, s32 y, s32 value) {
    if (value) {
        Practice_DrawTextColor(x, y, "ON", 0, 255, 0);
    } else {
        Practice_DrawTextColor(x, y, "OFF", 255, 100, 0);
    }
}

static void StateMenu_DrawLoadout(void) {
    s32 y;
    s32 i;

    for (i = 0; i < LOPT_MAX; i++) {
        y = 60 + (i * 12);

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
            case LOPT_GOLD_RINGS:
                Practice_DrawText(54, y, "RINGS:");
                Practice_DrawNumber(120, y, gPracticeConfig.goldRingCount);
                break;
            case LOPT_HEALTH:
                Practice_DrawText(54, y, "HEALTH:");
                Practice_DrawTextColor(120, y, sHealthNames[gPracticeConfig.longHealth], 255, 255, 0);
                break;
            case LOPT_RIGHT_WING:
                Practice_DrawText(54, y, "R WING:");
                Practice_DrawTextColor(120, y, sWingNames[gPracticeConfig.rightWingState], 255, 255, 0);
                break;
            case LOPT_LEFT_WING:
                Practice_DrawText(54, y, "L WING:");
                Practice_DrawTextColor(120, y, sWingNames[gPracticeConfig.leftWingState], 255, 255, 0);
                break;
            case LOPT_FALCO:
                Practice_DrawText(54, y, "FALCO:");
                Practice_DrawTextColor(120, y, gPracticeConfig.falcoAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.falcoAlive ? 0 : 255, gPracticeConfig.falcoAlive ? 255 : 100, 0);
                break;
            case LOPT_SLIPPY:
                Practice_DrawText(54, y, "SLIPPY:");
                Practice_DrawTextColor(120, y, gPracticeConfig.slippyAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.slippyAlive ? 0 : 255, gPracticeConfig.slippyAlive ? 255 : 100, 0);
                break;
            case LOPT_PEPPY:
                Practice_DrawText(54, y, "PEPPY:");
                Practice_DrawTextColor(120, y, gPracticeConfig.peppyAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.peppyAlive ? 0 : 255, gPracticeConfig.peppyAlive ? 255 : 100, 0);
                break;
            case LOPT_EXPERT:
                Practice_DrawText(54, y, "EXPERT:");
                DrawToggleValue(120, y, gPracticeConfig.expertMode);
                break;
            case LOPT_HIT_COUNT:
                Practice_DrawText(54, y, "HITS:");
                Practice_DrawNumber(120, y, gPracticeConfig.hitCount);
                break;
            case LOPT_PREV_PLANETS:
                Practice_DrawTextColor(54, y, "PLANETS...", 200, 200, 255);
                break;
            case LOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

static void StateMenu_DrawDisplay(void) {
    s32 y;
    s32 i;

    for (i = 0; i < DOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case DOPT_SKIP_CUTSCENES:
                Practice_DrawText(54, y, "SCENES:");
                Practice_DrawTextColor(120, y, gPracticeConfig.skipCutscenes ? "SKIP" : "PLAY",
                    gPracticeConfig.skipCutscenes ? 0 : 255, gPracticeConfig.skipCutscenes ? 255 : 100, 0);
                break;
            case DOPT_INPUT_DISPLAY:
                Practice_DrawText(54, y, "INPUT:");
                DrawToggleValue(120, y, gPracticeConfig.showInputDisplay);
                break;
            case DOPT_MINIMAP:
                Practice_DrawText(54, y, "MINIMAP:");
                DrawToggleValue(120, y, gPracticeConfig.showPauseMinimap);
                break;
            case DOPT_STATS_MENU:
                Practice_DrawTextColor(54, y, "STATS...", 200, 200, 255);
                break;
            case DOPT_VIS_MENU:
                Practice_DrawTextColor(54, y, "VISUALIZERS...", 200, 200, 255);
                break;
            case DOPT_MACRO_MENU:
                Practice_DrawTextColor(54, y, "MACRO...", 200, 200, 255);
                break;
            case DOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

static void StateMenu_DrawStats(void) {
    s32 y;
    s32 i;

    for (i = 0; i < SOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case SOPT_HUD_OVERLAY:
                Practice_DrawText(54, y, "HUD:");
                DrawToggleValue(120, y, gPracticeConfig.showHudOverlay);
                break;
            case SOPT_LAG_FRAMES:
                Practice_DrawText(54, y, "  LAG:");
                DrawToggleValue(120, y, gPracticeConfig.showLagFrames);
                break;
            case SOPT_SPEED:
                Practice_DrawText(54, y, "  SPEED:");
                DrawToggleValue(120, y, gPracticeConfig.showSpeed);
                break;
            case SOPT_CHARGE_TIMING:
                Practice_DrawText(54, y, "  CHARGE:");
                DrawToggleValue(120, y, gPracticeConfig.showChargeTiming);
                break;
            case SOPT_MISSED_INPUTS:
                Practice_DrawText(54, y, "  MISSED:");
                DrawToggleValue(120, y, gPracticeConfig.showMissedInputs);
                break;
            case SOPT_HIT_TRACKING:
                Practice_DrawText(54, y, "  HITS:");
                DrawToggleValue(120, y, gPracticeConfig.showHitTracking);
                break;
            case SOPT_LEVEL_TIMERS:
                Practice_DrawText(54, y, "  LVL TIMERS:");
                DrawToggleValue(120, y, gPracticeConfig.showLevelTimers);
                break;
            case SOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

static void StateMenu_DrawVisualizers(void) {
    s32 y;
    s32 i;

    for (i = 0; i < VOPT_MAX; i++) {
        y = 60 + (i * 12);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case VOPT_HITBOXES:
                Practice_DrawText(54, y, "HITBOXES:");
                DrawToggleValue(150, y, gPracticeConfig.showHitboxes);
                break;
            case VOPT_ACTORS:
                Practice_DrawText(54, y, "  ACTORS:");
                DrawToggleValue(150, y, gPracticeConfig.showHitboxActors);
                break;
            case VOPT_SCENERY:
                Practice_DrawText(54, y, "  SCENERY:");
                DrawToggleValue(150, y, gPracticeConfig.showHitboxScenery);
                break;
            case VOPT_ITEMS:
                Practice_DrawText(54, y, "  ITEMS:");
                DrawToggleValue(150, y, gPracticeConfig.showHitboxItems);
                break;
            case VOPT_PLAYER:
                Practice_DrawText(54, y, "  PLAYER:");
                DrawToggleValue(150, y, gPracticeConfig.showHitboxPlayer);
                break;
            case VOPT_FLASH:
                Practice_DrawText(54, y, "  FLASH:");
                DrawToggleValue(150, y, gPracticeConfig.showHitboxFlash);
                break;
            case VOPT_SPAWN_ZONES:
                Practice_DrawText(54, y, "SPAWN ZONES:");
                DrawToggleValue(150, y, gPracticeConfig.showSpawnZones);
                break;
            case VOPT_SPAWN_ACTORS:
                Practice_DrawTextColor(54, y, "  ENEMIES:", 255, 80, 80);
                DrawToggleValue(150, y, gPracticeConfig.showSpawnActors);
                break;
            case VOPT_SPAWN_ITEMS:
                Practice_DrawTextColor(54, y, "  ITEMS:", 80, 255, 80);
                DrawToggleValue(150, y, gPracticeConfig.showSpawnItems);
                break;
            case VOPT_SPAWN_SCENERY:
                Practice_DrawTextColor(54, y, "  SCENERY:", 80, 130, 255);
                DrawToggleValue(150, y, gPracticeConfig.showSpawnScenery);
                break;
            case VOPT_ENEMY_HEALTH:
                Practice_DrawTextColor(54, y, "ENEMY HP...", 200, 200, 255);
                break;
            case VOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

static void StateMenu_DrawPrevPlanets(void) {
    s32 i;

    for (i = 0; i < PREV_PLANETS_COUNT + 1; i++) {
        s32 y = 60 + (i * 12);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        if (i == PREV_PLANETS_COUNT) {
            Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
        } else {
            bool cleared = (gPracticeConfig.prevPlanetsMask >> sPrevPlanetIds[i]) & 1;
            Practice_DrawText(54, y, Practice_LevelAbbrev(sPrevPlanetIds[i]));
            Practice_DrawText(70, y, ":");
            Practice_DrawTextColor(84, y, cleared ? "CLEAR" : "---",
                cleared ? 0 : 100, cleared ? 255 : 100, cleared ? 0 : 100);
        }
    }
}

static void StateMenu_DrawMacro(void) {
    s32 y;
    s32 i;
    s32 len;
    s32 secs;
    s32 remFrames;

    /* Confirm-overwrite dialog replaces the normal menu rows. */
    if (sConfirmOverwrite) {
        Practice_DrawTextColor(54, 70, "OVERWRITE MACRO.", 255, 200, 60);
        Practice_DrawTextColor(54, 90, "A - YES", 100, 255, 100);
        Practice_DrawTextColor(54, 104, "B - NO",  255, 100, 100);
        return;
    }

    for (i = 0; i < MOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case MOPT_RECORD:
                Practice_DrawText(54, y, "RECORD:");
                if (Practice_Macro_IsArmed()) {
                    Practice_DrawTextColor(130, y, "ARMED", 255, 140, 0);
                } else if (Practice_Macro_IsRecording()) {
                    Practice_DrawTextColor(130, y, "STOP", 255, 60, 60);
                } else {
                    Practice_DrawTextColor(130, y, "START", 100, 220, 100);
                }
                break;
            case MOPT_PLAY:
                Practice_DrawText(54, y, "PLAY:");
                if (Practice_Macro_IsPlaying()) {
                    Practice_DrawTextColor(130, y, "STOP", 255, 100, 100);
                } else if (Practice_Macro_HasData()) {
                    Practice_DrawTextColor(130, y, "START", 100, 220, 100);
                } else {
                    Practice_DrawTextColor(130, y, "---", 60, 60, 60);
                }
                break;
            case MOPT_REWIND:
                Practice_DrawTextColor(54, y, "REWIND", 200, 200, 255);
                break;
            case MOPT_TRIM:
                if (Practice_Macro_GetHead() < Practice_Macro_GetLen()) {
                    Practice_DrawTextColor(54, y, "TRIM:", 200, 200, 255);
                    Practice_DrawNumber(98, y, Practice_Macro_GetHead());
                } else {
                    Practice_DrawTextColor(54, y, "TRIM", 60, 60, 60);
                }
                break;
            case MOPT_BIND_STATE:
                Practice_DrawText(54, y, "SAVE START:");
                Practice_DrawTextColor(142, y,
                    gPracticeConfig.macroBindState ? "ON" : "OFF",
                    gPracticeConfig.macroBindState ? 60  : 100,
                    gPracticeConfig.macroBindState ? 220 : 100,
                    gPracticeConfig.macroBindState ? 60  : 60);
                break;
            case MOPT_LOOP:
                Practice_DrawText(54, y, "LOOP:");
                Practice_DrawTextColor(130, y,
                    gPracticeConfig.macroLoop ? "ON" : "OFF",
                    gPracticeConfig.macroLoop ? 60  : 100,
                    gPracticeConfig.macroLoop ? 220 : 100,
                    gPracticeConfig.macroLoop ? 60  : 60);
                break;
            case MOPT_FRAMES:
                /* Show length in frames and seconds (60 fps). */
                len = Practice_Macro_GetLen();
                secs = len / 60;
                remFrames = len % 60;
                Practice_DrawText(54, y, "LEN:");
                Practice_DrawNumber(90, y, len);
                Practice_DrawText(122, y, "F");
                Practice_DrawNumber(134, y, secs);
                Practice_DrawText(154, y, "S");
                Practice_DrawNumber(166, y, remFrames);
                Practice_DrawText(186, y, "F");
                break;
            case MOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

static void StateMenu_DrawEnemyHealth(void) {
    s32 y;
    s32 i;

    for (i = 0; i < EHOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case EHOPT_SHOW:
                Practice_DrawText(54, y, "SHOW:");
                Practice_DrawTextColor(130, y, gPracticeConfig.showEnemyHealth ? "ON" : "OFF",
                    gPracticeConfig.showEnemyHealth ? 0 : 255,
                    gPracticeConfig.showEnemyHealth ? 255 : 100, 0);
                break;
            case EHOPT_SORT:
                Practice_DrawText(54, y, "SORT:");
                Practice_DrawTextColor(130, y, gPracticeConfig.enemyHealthSort == 1 ? "HIGH HP" : "NEAREST",
                    255, 255, 0);
                break;
            case EHOPT_MIN_HP:
                Practice_DrawText(54, y, "MIN HP:");
                if (gPracticeConfig.enemyHealthMinHp == 0) {
                    Practice_DrawTextColor(130, y, "OFF", 255, 255, 0);
                } else {
                    Practice_DrawNumber(130, y, gPracticeConfig.enemyHealthMinHp);
                }
                break;
            case EHOPT_BOSS_ONLY:
                Practice_DrawText(54, y, "FILTER:");
                Practice_DrawTextColor(130, y, gPracticeConfig.enemyHealthBossOnly ? "BOSSES" : "ALL",
                    255, 255, 0);
                break;
            case EHOPT_HIDE_MODELS:
                Practice_DrawText(54, y, "MODELS:");
                Practice_DrawTextColor(130, y, gPracticeConfig.enemyHealthHideModels ? "OFF" : "ON",
                    gPracticeConfig.enemyHealthHideModels ? 255 : 0,
                    gPracticeConfig.enemyHealthHideModels ? 100 : 255, 0);
                break;
            case EHOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}

static void StateMenu_DrawAudio(void) {
    s32 y;
    s32 i;
    s32 vol;
    s32 fill;

    for (i = 0; i < AOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        if (i < AOPT_LANE_COUNT) {
            Practice_DrawText(54, y, sAudioLaneNames[i]);
            vol = Practice_Audio_GetVolume(sAudioLaneType[i]);
            fill = (vol * 96) / 99;
            Practice_DrawBox(120, y + 1, 96, 7, 40, 40, 40, 200);
            Practice_DrawBox(120, y + 1, fill, 7, 0, 200, 255, 255);
            Practice_DrawNumber(222, y, vol);
        } else if (i == AOPT_RESET_ALL) {
            Practice_DrawTextColor(54, y, "RESET ALL", 255, 200, 60);
        } else {
            Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
        }
    }
}

void Practice_StateMenu_Draw(void) {
    const char* title;
    s32 boxHeight;
    s32 helpY;

    if (sStateMenuJustOpened) {
        sStateMenuJustOpened = false;
        return;
    }

    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:
            title = "LOADOUT";
            boxHeight = 200;
            helpY = 232;
            break;
        case PSUBMENU_DISPLAY:
            title = "DISPLAY";
            boxHeight = 124;
            helpY = 156;
            break;
        case PSUBMENU_STATS:
            title = "STATS";
            boxHeight = 138;
            helpY = 170;
            break;
        case PSUBMENU_VISUALIZERS:
            title = "VISUALIZERS";
            boxHeight = 180;
            helpY = 212;
            break;
        case PSUBMENU_PREV_PLANETS:
            title = "PREV PLANETS";
            boxHeight = 196;
            helpY = 226;
            break;
        case PSUBMENU_MACRO:
            title = "MACRO";
            boxHeight = 142;
            helpY = 174;
            break;
        case PSUBMENU_ENEMY_HEALTH:
            title = "ENEMY HP";
            boxHeight = 110;
            helpY = 142;
            break;
        case PSUBMENU_AUDIO:
            title = "AUDIO";
            boxHeight = 96;
            helpY = 128;
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
        case PSUBMENU_DISPLAY:
            StateMenu_DrawDisplay();
            Practice_DrawTextColor(50, helpY, "A:SELECT  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_STATS:
            StateMenu_DrawStats();
            Practice_DrawTextColor(50, helpY, "A:TOGGLE  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_VISUALIZERS:
            StateMenu_DrawVisualizers();
            Practice_DrawTextColor(50, helpY, "A:TOGGLE  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_PREV_PLANETS:
            StateMenu_DrawPrevPlanets();
            Practice_DrawTextColor(50, helpY, "A:TOGGLE  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_MACRO:
            StateMenu_DrawMacro();
            Practice_DrawTextColor(50, helpY, "A:SELECT  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_AUDIO:
            StateMenu_DrawAudio();
            Practice_DrawTextColor(50, helpY, "L-R:VOLUME  A:RESET  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_ENEMY_HEALTH:
            StateMenu_DrawEnemyHealth();
            Practice_DrawTextColor(50, helpY, "A:SELECT  B:BACK", 150, 150, 150);
            break;
    }
}

#endif
