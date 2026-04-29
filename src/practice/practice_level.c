#include "practice.h"

#ifdef PRACTICE_ROM

#define MAX_PHASES 3

typedef struct PhaseEntry {
    const char* name;
    s32 phase;
    f32 checkpointProgress;
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
    { "CORNERIA", LEVEL_CORNERIA, PLANET_CORNERIA, 1, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 93610.3f } } },
    { "METEO",    LEVEL_METEO,    PLANET_METEO,    2, 3,
      { { "START", 0 }, { "WARP", 1 }, { "CHECKPOINT", 0, 223651.3f } } },
    { "SECTOR Y", LEVEL_SECTOR_Y, PLANET_SECTOR_Y, 2, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 96148.4f } } },
    { "FORTUNA",  LEVEL_FORTUNA,  PLANET_FORTUNA,  3, 1,
      { { "START", 0 } } },
    { "KATINA",   LEVEL_KATINA,   PLANET_KATINA,   3, 1,
      { { "START", 0 } } },
    { "AQUAS",    LEVEL_AQUAS,    PLANET_AQUAS,    3, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 52268.8f } } },
    { "SECTOR X", LEVEL_SECTOR_X, PLANET_SECTOR_X, 4, 3,
      { { "START", 0 }, { "WARP", 1 }, { "CHECKPOINT", 0, 85788.5f } } },
    { "SOLAR",    LEVEL_SOLAR,    PLANET_SOLAR,    4, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 81850.0f } } },
    { "ZONESS",   LEVEL_ZONESS,   PLANET_ZONESS,   4, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 140850.0f } } },
    { "TITANIA",  LEVEL_TITANIA,  PLANET_TITANIA,  5, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 48150.0f } } },
    { "MACBETH",  LEVEL_MACBETH,  PLANET_MACBETH,  5, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 69015.0f } } },
    { "SECTOR Z", LEVEL_SECTOR_Z, PLANET_SECTOR_Z, 5, 1,
      { { "START", 0 } } },
    { "BOLSE",    LEVEL_BOLSE,    PLANET_BOLSE,    6, 1,
      { { "START", 0 } } },
    { "AREA 6",   LEVEL_AREA_6,   PLANET_AREA_6,   6, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 152984.0f } } },
    { "VENOM 1",  LEVEL_VENOM_1,  PLANET_VENOM,    7, 2,
      { { "START", 0 }, { "CHECKPOINT", 0, 150379.9f } } },
    { "VENOM 2",  LEVEL_VENOM_2,  PLANET_VENOM,    7, 2,
      { { "START", 0 }, { "GREAT FOX", 2 } } },
};

#define LEVEL_COUNT (s32)(sizeof(sLevelList) / sizeof(sLevelList[0]))

/* Audio_SetAudioSpec queues SEQCMD_RESET_AUDIO_HEAP. While sAudioResetStatus
 * is not AUDIORESET_READY (0), Audio_Update skips Audio_ProcessSeqCmds;
 * stacking another reset wedges BGM. Defer until Audio_HandleReset() == 0. */

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
static bool sBgmAudioDirty = false;
static bool sBgmPlayPending = false;
static u16 sBgmLastSpecPacked = 0xFFFF;

f32 gPracticeCheckpointProgress = 0.0f;

static u16 Practice_BgmSpecPacked(const BgmEntry* e) {
    return (u16)(((u16)e->sfxLayout << 8) | (u16)e->audioSpec);
}

static void Practice_ServiceLevelSelectBgm(void) {
    const BgmEntry* e = &sBgmList[sBgmIndex];
    u16 packed = Practice_BgmSpecPacked(e);

    if (!sBgmPlaying) {
        sBgmAudioDirty = true;
    }

    if (!sBgmAudioDirty && !sBgmPlayPending) {
        return;
    }

    if (Audio_HandleReset() != 0) {
        return;
    }

    if (packed != sBgmLastSpecPacked) {
        AUDIO_SET_SPEC(e->sfxLayout, e->audioSpec);
        sBgmLastSpecPacked = packed;
        sBgmPlayPending = true;
        sBgmPlaying = true;
        sBgmAudioDirty = false;
        return;
    }

    AUDIO_PLAY_BGM(e->bgmId);
    sBgmPlayPending = false;
    sBgmPlaying = true;
    sBgmAudioDirty = false;
}

void Practice_LevelSelect_OnEnter(void) {
    f32 r;

    r = Rand_ZeroOne();
    sBgmIndex = (s32)(r * (f32)BGM_COUNT);
    if (sBgmIndex >= BGM_COUNT) {
        sBgmIndex = BGM_COUNT - 1;
    }
    if (sBgmIndex < 0) {
        sBgmIndex = 0;
    }
    sBgmPlaying = false;
    sBgmAudioDirty = false;
    sBgmPlayPending = false;
    sBgmLastSpecPacked = 0xFFFF;
}

void Practice_LevelSelect_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    s32 phaseCount;

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Update();
        Practice_ServiceLevelSelectBgm();
        return;
    }

    /* Else-if: both shoulders on one edge only move one step (avoids double heap reset). */
    if (press->button & L_TRIG) {
        sBgmIndex--;
        if (sBgmIndex < 0) {
            sBgmIndex = BGM_COUNT - 1;
        }
        sBgmAudioDirty = true;
    } else if (press->button & R_TRIG) {
        sBgmIndex++;
        if (sBgmIndex >= BGM_COUNT) {
            sBgmIndex = 0;
        }
        sBgmAudioDirty = true;
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
        PhaseEntry* phase = &sLevelList[sSelectedLevel].phases[sSelectedPhase];
        Practice_LaunchLevel(sLevelList[sSelectedLevel].levelId, phase->phase, phase->checkpointProgress);
        return;
    }

    Practice_ServiceLevelSelectBgm();
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

const char* Practice_LevelAbbrev(LevelId levelId) {
    switch (levelId) {
        case LEVEL_CORNERIA:      return "CO";
        case LEVEL_METEO:         return "ME";
        case LEVEL_SECTOR_X:      return "SX";
        case LEVEL_AREA_6:        return "A6";
        case LEVEL_SECTOR_Y:      return "SY";
        case LEVEL_VENOM_1:       return "V1";
        case LEVEL_SOLAR:         return "SO";
        case LEVEL_ZONESS:        return "ZO";
        case LEVEL_VENOM_ANDROSS: return "AN";
        case LEVEL_MACBETH:       return "MA";
        case LEVEL_TITANIA:       return "TI";
        case LEVEL_AQUAS:         return "AQ";
        case LEVEL_FORTUNA:       return "FO";
        case LEVEL_KATINA:        return "KA";
        case LEVEL_BOLSE:         return "BO";
        case LEVEL_SECTOR_Z:      return "SZ";
        case LEVEL_VENOM_2:       return "V2";
        case LEVEL_TRAINING:      return "TR";
        default:                  return "..";
    }
}

/* sf64audio_external.h doesn't expose this -- live extern from sf64audio.h
 * which the audio thread updates whenever a spec change actually lands. */
extern u8 gAudioSpecId;

void Practice_AudioApplyForLevel(LevelId levelId) {
    u16 packed = Practice_AudioSpecForLevel(levelId);
    /* Spec lower byte == AudioSpec id; gAudioSpecId reflects the bank the
     * audio thread has actually loaded. If they match, the requested spec
     * is already active and another Audio_SetAudioSpec would queue a
     * pointless heap reset (which can stack with one already in flight). */
    if ((u8)(packed & 0xFFu) != gAudioSpecId) {
        Audio_SetAudioSpec(0, packed);
    }
}

u16 Practice_AudioSpecForLevel(LevelId levelId) {
    u8 sfx = SFX_LAYOUT_DEFAULT;
    u8 spec;

    switch (levelId) {
        case LEVEL_CORNERIA:      spec = AUDIOSPEC_CO;  break;
        case LEVEL_METEO:         spec = AUDIOSPEC_ME;  break;
        case LEVEL_TITANIA:       spec = AUDIOSPEC_TI;  break;
        case LEVEL_AQUAS:         spec = AUDIOSPEC_AQ;  break;
        case LEVEL_BOLSE:         spec = AUDIOSPEC_BO;  break;
        case LEVEL_KATINA:        spec = AUDIOSPEC_KA;  break;
        case LEVEL_AREA_6:        spec = AUDIOSPEC_A6;  break;
        case LEVEL_SECTOR_Z:      spec = AUDIOSPEC_SZ;  break;
        case LEVEL_FORTUNA:       spec = AUDIOSPEC_FO;  break;
        case LEVEL_SECTOR_X:      spec = AUDIOSPEC_SX;  break;
        case LEVEL_MACBETH:       spec = AUDIOSPEC_MA;  break;
        case LEVEL_ZONESS:        spec = AUDIOSPEC_ZO;  break;
        case LEVEL_SECTOR_Y:      spec = AUDIOSPEC_SY;  break;
        case LEVEL_SOLAR:         sfx = SFX_LAYOUT_SO;
                                  spec = AUDIOSPEC_SO;  break;
        case LEVEL_TRAINING:      spec = AUDIOSPEC_TR;  break;
        case LEVEL_VENOM_1:
        case LEVEL_VENOM_2:       spec = AUDIOSPEC_VE;  break;
        case LEVEL_VENOM_ANDROSS: spec = AUDIOSPEC_AND; break;
        default:                  spec = AUDIOSPEC_CO;  break;
    }
    return (u16)(((u16)sfx << 8) | (u16)spec);
}

void Practice_LaunchLevel(LevelId levelId, s32 phase, f32 checkpointProgress) {
    sBgmPlaying = false;
    sBgmPlayPending = false;

    gNextLevel = levelId;
    gNextLevelPhase = phase;
    gClearPlayerInfo = true;
    gPracticeCheckpointProgress = checkpointProgress;

    // Map_LevelStart_AudioSpecSetup lives in the menu overlay and is only callable from
    // GSTATE_MAP. Replicate its logic here so audio banks load correctly on any restart.
    Practice_AudioApplyForLevel(levelId);

    gNextGameState = GSTATE_PLAY;
    gDrawMode = DRAW_NONE;

    /* Saved slots must survive level relaunches so cross-scene load can drive
     * a transition through Practice_LaunchLevel without wiping the source
     * scene's slot. Slots are explicitly cleared via the picker (Phase 5+). */
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
