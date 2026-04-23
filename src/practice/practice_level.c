#include "practice.h"

#ifdef PRACTICE_ROM

#include "fox_map.h"

typedef struct LevelEntry {
    const char* name;
    LevelId levelId;
    PlanetId planetId;
    s32 column;
    bool hasWarpPhase;
} LevelEntry;

static LevelEntry sLevelList[] = {
    { "CORNERIA",  LEVEL_CORNERIA,  PLANET_CORNERIA,  1, false },
    { "METEO",     LEVEL_METEO,     PLANET_METEO,     2, true },
    { "SECTOR Y",  LEVEL_SECTOR_Y,  PLANET_SECTOR_Y,  2, false },
    { "FORTUNA",   LEVEL_FORTUNA,   PLANET_FORTUNA,   3, false },
    { "KATINA",    LEVEL_KATINA,    PLANET_KATINA,    3, false },
    { "AQUAS",     LEVEL_AQUAS,     PLANET_AQUAS,     3, false },
    { "SECTOR X",  LEVEL_SECTOR_X,  PLANET_SECTOR_X,  4, true },
    { "SOLAR",     LEVEL_SOLAR,     PLANET_SOLAR,     4, false },
    { "ZONESS",    LEVEL_ZONESS,    PLANET_ZONESS,    4, false },
    { "TITANIA",   LEVEL_TITANIA,   PLANET_TITANIA,   5, false },
    { "MACBETH",   LEVEL_MACBETH,   PLANET_MACBETH,   5, false },
    { "SECTOR Z",  LEVEL_SECTOR_Z,  PLANET_SECTOR_Z,  5, false },
    { "BOLSE",     LEVEL_BOLSE,     PLANET_BOLSE,     6, false },
    { "AREA 6",    LEVEL_AREA_6,    PLANET_AREA_6,    6, false },
    { "VENOM 1",   LEVEL_VENOM_1,   PLANET_VENOM,     7, false },
    { "VENOM 2",   LEVEL_VENOM_2,   PLANET_VENOM,     7, false },
};

#define LEVEL_COUNT (s32)(sizeof(sLevelList) / sizeof(sLevelList[0]))

static s32 sSelectedLevel = 0;
static s32 sSelectedPhase = 0;

void Practice_LevelSelect_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Update();
        return;
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

    if (sLevelList[sSelectedLevel].hasWarpPhase) {
        if ((press->button & L_JPAD) || (press->button & R_JPAD)) {
            sSelectedPhase ^= 1;
        }
    }

    if (press->button & START_BUTTON) {
        Practice_StateMenu_Open();
        return;
    }

    if (press->button & A_BUTTON) {
        Practice_LaunchLevel(sLevelList[sSelectedLevel].levelId, sSelectedPhase);
    }
}

void Practice_LevelSelect_Draw(void) {
    s32 i;
    s32 y;
    s32 startIdx;
    s32 visibleCount = 12;

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

    if (sLevelList[sSelectedLevel].hasWarpPhase) {
        Practice_DrawText(30, 196, "PHASE:");
        if (sSelectedPhase == 0) {
            Practice_DrawTextColor(75, 196, "NORMAL", 255, 255, 255);
        } else {
            Practice_DrawTextColor(75, 196, "WARP ZONE", 0, 200, 255);
        }
    }

    Practice_DrawTextColor(20, 210, "A:START  START:OPTIONS", 150, 150, 150);

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Draw();
    }
}

void Practice_LaunchLevel(LevelId levelId, s32 phase) {
    gCurrentLevel = levelId;
    gLevelPhase = phase;
    gClearPlayerInfo = true;

    Map_LevelStart_AudioSpecSetup(levelId);

    gGameState = GSTATE_PLAY;
    gPlayState = PLAY_STANDBY;
    gDrawMode = DRAW_NONE;
    gNextGameStateTimer = 0;
    Play_Setup();

    if (gPracticeConfig.skipCutscenes) {
        gCsWasNotSkipped = false;
    }

    Practice_ClearCheckpoint();
    gPracticeScreen = PSCREEN_GAMEPLAY;
}

LevelId Practice_GetSelectedLevelId(void) {
    return sLevelList[sSelectedLevel].levelId;
}

s32 Practice_GetSelectedPhase(void) {
    return sSelectedPhase;
}

#endif
