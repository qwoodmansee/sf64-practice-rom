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
    VOPT_BACK,
    VOPT_MAX,
} VisualizerOption;

static s32 sSelectedOption = 0;
static bool sStateMenuOpen = false;
static PracticeSubMenu sActiveSubMenu;

static const char* sLaserNames[] = { "SINGLE", "TWIN", "HYPER" };
static const char* sWingNames[] = { "NONE", "BROKEN", "INTACT" };
static const char* sHealthNames[] = { "SHORT", "LONG" };

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
    sSelectedOption = 0;
    sActiveSubMenu = subMenu;
}

void Practice_StateMenu_Close(void) {
    sStateMenuOpen = false;
}

static s32 StateMenu_GetOptionCount(void) {
    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:      return LOPT_MAX;
        case PSUBMENU_DISPLAY:      return DOPT_MAX;
        case PSUBMENU_STATS:        return SOPT_MAX;
        case PSUBMENU_VISUALIZERS:  return VOPT_MAX;
        case PSUBMENU_PREV_PLANETS: return PREV_PLANETS_COUNT + 1;
        case PSUBMENU_MACRO:        return MOPT_MAX;
        default:                    return 0;
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
        }
    }

    StateMenu_ApplyLoadoutLive();
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
        }
    }
}

static void StateMenu_UpdateMacro(u16 buttons) {
    if ((buttons & A_BUTTON) || (buttons & R_JPAD) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case MOPT_RECORD:
                if (Practice_Macro_IsArmed() || Practice_Macro_IsRecording()) {
                    Practice_Macro_StopRecord();
                } else {
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
            Practice_StateMenu_Close();
            return;
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
                Practice_DrawTextColor(120, y, gPracticeConfig.expertMode ? "ON" : "OFF",
                    gPracticeConfig.expertMode ? 0 : 255, gPracticeConfig.expertMode ? 255 : 100, 0);
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
                Practice_DrawTextColor(120, y, gPracticeConfig.showInputDisplay ? "ON" : "OFF",
                    gPracticeConfig.showInputDisplay ? 0 : 255, gPracticeConfig.showInputDisplay ? 255 : 100, 0);
                break;
            case DOPT_MINIMAP:
                Practice_DrawText(54, y, "MINIMAP:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showPauseMinimap ? "ON" : "OFF",
                    gPracticeConfig.showPauseMinimap ? 0 : 255, gPracticeConfig.showPauseMinimap ? 255 : 100, 0);
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
                Practice_DrawTextColor(120, y, gPracticeConfig.showHudOverlay ? "ON" : "OFF",
                    gPracticeConfig.showHudOverlay ? 0 : 255, gPracticeConfig.showHudOverlay ? 255 : 100, 0);
                break;
            case SOPT_LAG_FRAMES:
                Practice_DrawText(54, y, "  LAG:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showLagFrames ? "ON" : "OFF",
                    gPracticeConfig.showLagFrames ? 0 : 255, gPracticeConfig.showLagFrames ? 255 : 100, 0);
                break;
            case SOPT_SPEED:
                Practice_DrawText(54, y, "  SPEED:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showSpeed ? "ON" : "OFF",
                    gPracticeConfig.showSpeed ? 0 : 255, gPracticeConfig.showSpeed ? 255 : 100, 0);
                break;
            case SOPT_CHARGE_TIMING:
                Practice_DrawText(54, y, "  CHARGE:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showChargeTiming ? "ON" : "OFF",
                    gPracticeConfig.showChargeTiming ? 0 : 255, gPracticeConfig.showChargeTiming ? 255 : 100, 0);
                break;
            case SOPT_MISSED_INPUTS:
                Practice_DrawText(54, y, "  MISSED:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showMissedInputs ? "ON" : "OFF",
                    gPracticeConfig.showMissedInputs ? 0 : 255, gPracticeConfig.showMissedInputs ? 255 : 100, 0);
                break;
            case SOPT_HIT_TRACKING:
                Practice_DrawText(54, y, "  HITS:");
                Practice_DrawTextColor(120, y, gPracticeConfig.showHitTracking ? "ON" : "OFF",
                    gPracticeConfig.showHitTracking ? 0 : 255, gPracticeConfig.showHitTracking ? 255 : 100, 0);
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
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case VOPT_HITBOXES:
                Practice_DrawText(54, y, "HITBOXES:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxes ? "ON" : "OFF",
                    gPracticeConfig.showHitboxes ? 0 : 255, gPracticeConfig.showHitboxes ? 255 : 100, 0);
                break;
            case VOPT_ACTORS:
                Practice_DrawText(54, y, "  ACTORS:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxActors ? "ON" : "OFF",
                    gPracticeConfig.showHitboxActors ? 0 : 255, gPracticeConfig.showHitboxActors ? 255 : 100, 0);
                break;
            case VOPT_SCENERY:
                Practice_DrawText(54, y, "  SCENERY:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxScenery ? "ON" : "OFF",
                    gPracticeConfig.showHitboxScenery ? 0 : 255, gPracticeConfig.showHitboxScenery ? 255 : 100, 0);
                break;
            case VOPT_ITEMS:
                Practice_DrawText(54, y, "  ITEMS:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxItems ? "ON" : "OFF",
                    gPracticeConfig.showHitboxItems ? 0 : 255, gPracticeConfig.showHitboxItems ? 255 : 100, 0);
                break;
            case VOPT_PLAYER:
                Practice_DrawText(54, y, "  PLAYER:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxPlayer ? "ON" : "OFF",
                    gPracticeConfig.showHitboxPlayer ? 0 : 255, gPracticeConfig.showHitboxPlayer ? 255 : 100, 0);
                break;
            case VOPT_FLASH:
                Practice_DrawText(54, y, "  FLASH:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxFlash ? "ON" : "OFF",
                    gPracticeConfig.showHitboxFlash ? 0 : 255, gPracticeConfig.showHitboxFlash ? 255 : 100, 0);
                break;
            case VOPT_SPAWN_ZONES:
                Practice_DrawText(54, y, "SPAWN ZONES:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showSpawnZones ? "ON" : "OFF",
                    gPracticeConfig.showSpawnZones ? 0 : 255, gPracticeConfig.showSpawnZones ? 255 : 100, 0);
                break;
            case VOPT_SPAWN_ACTORS:
                Practice_DrawTextColor(54, y, "  ENEMIES:", 255, 80, 80);
                Practice_DrawTextColor(150, y, gPracticeConfig.showSpawnActors ? "ON" : "OFF",
                    gPracticeConfig.showSpawnActors ? 0 : 255, gPracticeConfig.showSpawnActors ? 255 : 100, 0);
                break;
            case VOPT_SPAWN_ITEMS:
                Practice_DrawTextColor(54, y, "  ITEMS:", 80, 255, 80);
                Practice_DrawTextColor(150, y, gPracticeConfig.showSpawnItems ? "ON" : "OFF",
                    gPracticeConfig.showSpawnItems ? 0 : 255, gPracticeConfig.showSpawnItems ? 255 : 100, 0);
                break;
            case VOPT_SPAWN_SCENERY:
                Practice_DrawTextColor(54, y, "  SCENERY:", 80, 130, 255);
                Practice_DrawTextColor(150, y, gPracticeConfig.showSpawnScenery ? "ON" : "OFF",
                    gPracticeConfig.showSpawnScenery ? 0 : 255, gPracticeConfig.showSpawnScenery ? 255 : 100, 0);
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
                    Practice_DrawTextColor(130, y, "ON", 255, 60, 60);
                } else {
                    Practice_DrawTextColor(130, y, "OFF", 100, 100, 60);
                }
                break;
            case MOPT_PLAY:
                Practice_DrawText(54, y, "PLAY:");
                if (Practice_Macro_IsPlaying()) {
                    Practice_DrawTextColor(130, y, "ON",  60, 220, 255);
                } else if (Practice_Macro_HasData()) {
                    Practice_DrawTextColor(130, y, "OFF", 100, 100, 60);
                } else {
                    Practice_DrawTextColor(130, y, "---", 60,  60,  60);
                }
                break;
            case MOPT_REWIND:
                Practice_DrawTextColor(54, y, "REWIND", 200, 200, 255);
                break;
            case MOPT_BIND_STATE:
                Practice_DrawText(54, y, "BIND STATE:");
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
                Practice_DrawText(54, y, "FRAMES:");
                Practice_DrawNumber(130, y, Practice_Macro_GetHead());
                Practice_DrawText(160, y, "-");
                Practice_DrawNumber(170, y, Practice_Macro_GetLen());
                break;
            case MOPT_BACK:
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
            boxHeight = 124;
            helpY = 156;
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
            boxHeight = 128;
            helpY = 160;
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
    }
}

#endif
