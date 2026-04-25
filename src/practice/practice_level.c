#include "practice.h"

#ifdef PRACTICE_ROM

#include "fox_map.h"

#define MAX_PHASES 3

typedef struct PhaseEntry {
    const char* name;
    s32 phase;
} PhaseEntry;

typedef struct LevelEntry {
    const char* name;
    LevelId levelId;
    PlanetId planetId;
    s32 column;
    s32 phaseCount;
    PhaseEntry phases[MAX_PHASES];
} LevelEntry;

static LevelEntry sLevelList[] = {
    { "CORNERIA", LEVEL_CORNERIA, PLANET_CORNERIA, 1, 1,
      { { "START", 0 } } },
    { "METEO",    LEVEL_METEO,    PLANET_METEO,    2, 2,
      { { "START", 0 }, { "WARP", 1 } } },
    { "SECTOR Y", LEVEL_SECTOR_Y, PLANET_SECTOR_Y, 2, 1,
      { { "START", 0 } } },
    { "FORTUNA",  LEVEL_FORTUNA,  PLANET_FORTUNA,  3, 1,
      { { "START", 0 } } },
    { "KATINA",   LEVEL_KATINA,   PLANET_KATINA,   3, 1,
      { { "START", 0 } } },
    { "AQUAS",    LEVEL_AQUAS,    PLANET_AQUAS,    3, 1,
      { { "START", 0 } } },
    { "SECTOR X", LEVEL_SECTOR_X, PLANET_SECTOR_X, 4, 2,
      { { "START", 0 }, { "WARP", 1 } } },
    { "SOLAR",    LEVEL_SOLAR,    PLANET_SOLAR,    4, 1,
      { { "START", 0 } } },
    { "ZONESS",   LEVEL_ZONESS,   PLANET_ZONESS,   4, 1,
      { { "START", 0 } } },
    { "TITANIA",  LEVEL_TITANIA,  PLANET_TITANIA,  5, 1,
      { { "START", 0 } } },
    { "MACBETH",  LEVEL_MACBETH,  PLANET_MACBETH,  5, 1,
      { { "START", 0 } } },
    { "SECTOR Z", LEVEL_SECTOR_Z, PLANET_SECTOR_Z, 5, 1,
      { { "START", 0 } } },
    { "BOLSE",    LEVEL_BOLSE,    PLANET_BOLSE,    6, 1,
      { { "START", 0 } } },
    { "AREA 6",   LEVEL_AREA_6,   PLANET_AREA_6,   6, 1,
      { { "START", 0 } } },
    { "VENOM 1",  LEVEL_VENOM_1,  PLANET_VENOM,    7, 1,
      { { "START", 0 } } },
    { "VENOM 2",  LEVEL_VENOM_2,  PLANET_VENOM,    7, 2,
      { { "START", 0 }, { "GREAT FOX", 2 } } },
};

#define LEVEL_COUNT (s32)(sizeof(sLevelList) / sizeof(sLevelList[0]))

typedef struct {
    const char* name;
    u16 bgmId;
    u8 audioSpec;
    u8 sfxLayout;
} BgmEntry;

static BgmEntry sBgmList[] = {
    { "MAP",       NA_BGM_MAP,        AUDIOSPEC_MAP, SFX_LAYOUT_MAP },
    { "CORNERIA",  NA_BGM_STAGE_CO,   AUDIOSPEC_CO,  SFX_LAYOUT_DEFAULT },
    { "METEO",     NA_BGM_STAGE_ME,   AUDIOSPEC_ME,  SFX_LAYOUT_DEFAULT },
    { "SECTOR Y",  NA_BGM_STAGE_SY,   AUDIOSPEC_SY,  SFX_LAYOUT_DEFAULT },
    { "FORTUNA",   NA_BGM_STAGE_FO,   AUDIOSPEC_FO,  SFX_LAYOUT_DEFAULT },
    { "KATINA",    NA_BGM_STAGE_KA,   AUDIOSPEC_KA,  SFX_LAYOUT_DEFAULT },
    { "AQUAS",     NA_BGM_STAGE_AQ,   AUDIOSPEC_AQ,  SFX_LAYOUT_DEFAULT },
    { "SECTOR X",  NA_BGM_STAGE_SX,   AUDIOSPEC_SX,  SFX_LAYOUT_DEFAULT },
    { "SOLAR",     NA_BGM_STAGE_SO,   AUDIOSPEC_SO,  SFX_LAYOUT_SO },
    { "ZONESS",    NA_BGM_STAGE_ZO,   AUDIOSPEC_ZO,  SFX_LAYOUT_DEFAULT },
    { "TITANIA",   NA_BGM_STAGE_TI,   AUDIOSPEC_TI,  SFX_LAYOUT_DEFAULT },
    { "MACBETH",   NA_BGM_STAGE_MA,   AUDIOSPEC_MA,  SFX_LAYOUT_DEFAULT },
    { "SECTOR Z",  NA_BGM_STAGE_SZ,   AUDIOSPEC_SZ,  SFX_LAYOUT_DEFAULT },
    { "BOLSE",     NA_BGM_STAGE_BO,   AUDIOSPEC_BO,  SFX_LAYOUT_DEFAULT },
    { "AREA 6",    NA_BGM_STAGE_A6,   AUDIOSPEC_A6,  SFX_LAYOUT_DEFAULT },
    { "VENOM",     NA_BGM_STAGE_VE1,  AUDIOSPEC_VE,  SFX_LAYOUT_DEFAULT },
    { "STAR WOLF", NA_BGM_STARWOLF,   AUDIOSPEC_FO,  SFX_LAYOUT_DEFAULT },
    { "TRAINING",  NA_BGM_TRAINING,   AUDIOSPEC_TR,  SFX_LAYOUT_DEFAULT },
};

#define BGM_COUNT (s32)(sizeof(sBgmList) / sizeof(sBgmList[0]))

static s32 sSelectedLevel = 0;
static s32 sSelectedPhase = 0;
static s32 sBgmIndex = 0;
static bool sBgmPlaying = false;

static void Practice_PlayCurrentBgm(void) {
    AUDIO_SET_SPEC(sBgmList[sBgmIndex].sfxLayout, sBgmList[sBgmIndex].audioSpec);
    AUDIO_PLAY_BGM(sBgmList[sBgmIndex].bgmId);
    sBgmPlaying = true;
}

void Practice_LevelSelect_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    s32 phaseCount;

    if (!sBgmPlaying) {
        Practice_PlayCurrentBgm();
    }

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Update();
        return;
    }

    if (press->button & L_TRIG) {
        sBgmIndex--;
        if (sBgmIndex < 0) {
            sBgmIndex = BGM_COUNT - 1;
        }
        Practice_PlayCurrentBgm();
    }
    if (press->button & R_TRIG) {
        sBgmIndex++;
        if (sBgmIndex >= BGM_COUNT) {
            sBgmIndex = 0;
        }
        Practice_PlayCurrentBgm();
    }

    if (press->button & U_JPAD) {
        sSelectedLevel--;
        if (sSelectedLevel < 0) {
            sSelectedLevel = LEVEL_COUNT - 1;
        }
        sSelectedPhase = 0;
    }
    if (press->button & D_JPAD) {
        sSelectedLevel++;
        if (sSelectedLevel >= LEVEL_COUNT) {
            sSelectedLevel = 0;
        }
        sSelectedPhase = 0;
    }

    phaseCount = sLevelList[sSelectedLevel].phaseCount;
    if (phaseCount > 1) {
        if (press->button & L_JPAD) {
            sSelectedPhase--;
            if (sSelectedPhase < 0) {
                sSelectedPhase = phaseCount - 1;
            }
        }
        if (press->button & R_JPAD) {
            sSelectedPhase++;
            if (sSelectedPhase >= phaseCount) {
                sSelectedPhase = 0;
            }
        }
    }

    if (press->button & B_BUTTON) {
        gPracticeConfig.expertMode ^= true;
    }

    if (press->button & START_BUTTON) {
        Practice_StateMenu_Open(PSUBMENU_LOADOUT);
        return;
    }

    if (press->button & A_BUTTON) {
        s32 gamePhase = sLevelList[sSelectedLevel].phases[sSelectedPhase].phase;
        Practice_LaunchLevel(sLevelList[sSelectedLevel].levelId, gamePhase);
    }
}

void Practice_LevelSelect_Draw(void) {
    s32 i;
    s32 y;
    s32 startIdx;
    s32 visibleCount = 10;
    s32 phaseCount;
    const char* laserStr;

    Practice_DrawBox(16, 16, 288, 208, 0, 0, 0, 180);

    Practice_DrawTextColor(20, 20, "SF64 PRACTICE ROM", 0, 255, 128);
    Practice_DrawTextColor(20, 30, "SELECT LEVEL", 200, 200, 200);

    startIdx = sSelectedLevel - (visibleCount / 2);
    if (startIdx < 0) {
        startIdx = 0;
    }
    if (startIdx > LEVEL_COUNT - visibleCount) {
        startIdx = LEVEL_COUNT - visibleCount;
    }
    if (startIdx < 0) {
        startIdx = 0;
    }

    for (i = startIdx; (i < LEVEL_COUNT) && (i < startIdx + visibleCount); i++) {
        y = 46 + ((i - startIdx) * 12);

        if (i == sSelectedLevel) {
            Practice_DrawCursor(20, y);
            Practice_DrawTextColor(30, y, sLevelList[i].name, 255, 255, 0);
        } else {
            Practice_DrawText(30, y, sLevelList[i].name);
        }
    }

    Practice_DrawText(20, 168, "BGM:");
    Practice_DrawTextColor(52, 168, sBgmList[sBgmIndex].name, 100, 200, 255);

    phaseCount = sLevelList[sSelectedLevel].phaseCount;
    Practice_DrawText(20, 180, "PHASE:");
    if (phaseCount > 1) {
        Practice_DrawTextColor(72, 180,
            sLevelList[sSelectedLevel].phases[sSelectedPhase].name,
            255, 220, 0);
    } else {
        Practice_DrawTextColor(72, 180, "START", 150, 150, 150);
    }

    Practice_DrawText(20, 192, "EXPERT:");
    if (gPracticeConfig.expertMode) {
        Practice_DrawTextColor(78, 192, "ON", 0, 255, 100);
    } else {
        Practice_DrawTextColor(78, 192, "OFF", 180, 100, 100);
    }

    laserStr = (gPracticeConfig.laserStrength == LASERS_SINGLE) ? "SINGLE" :
               (gPracticeConfig.laserStrength == LASERS_TWIN)   ? "TWIN"   : "HYPER";
    Practice_DrawText(20,  204, "LASER:");
    Practice_DrawTextColor(72,  204, laserStr, 255, 255, 100);
    Practice_DrawText(136, 204, "BOMBS:");
    Practice_DrawNumber(192, 204, gPracticeConfig.bombCount);
    Practice_DrawText(208, 204, "LIVES:");
    Practice_DrawNumber(264, 204, gPracticeConfig.lifeCount);

    Practice_DrawTextColor(20, 215, "A:GO  B:EXP  START:LOAD  LR:BGM", 150, 150, 150);

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Draw();
    }
}

void Practice_LaunchLevel(LevelId levelId, s32 phase) {
    sBgmPlaying = false;

    gNextLevel = levelId;
    gNextLevelPhase = phase;
    gClearPlayerInfo = true;

    // Map_LevelStart_AudioSpecSetup is in the menu overlay -- only callable from GSTATE_MAP.
    if (gGameState != GSTATE_PLAY) {
        Map_LevelStart_AudioSpecSetup(levelId);
    }

    gNextGameState = GSTATE_PLAY;
    gDrawMode = DRAW_NONE;

    Practice_ClearCheckpoint();
    Practice_Hud_Reset();
    gPracticeScreen = PSCREEN_GAMEPLAY;
}

LevelId Practice_GetSelectedLevelId(void) {
    return sLevelList[sSelectedLevel].levelId;
}

s32 Practice_GetSelectedPhase(void) {
    return sLevelList[sSelectedLevel].phases[sSelectedPhase].phase;
}

#endif
