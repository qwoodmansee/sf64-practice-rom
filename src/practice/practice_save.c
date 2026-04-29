#include "practice.h"

#ifdef PRACTICE_ROM

#include "variables.h"
#include "bgm.h"
#include "sf64audio_external.h"
#include "sf64thread.h"
#include "practice_save_tags.h"
#include "practice_save_config.h"
#include "practice_overlay.h"

#include "slot_manager.h"
#include "serial.h"

#ifndef PRACTICE_SAVE_SELFTEST
#define PRACTICE_SAVE_SELFTEST 1
#endif

#ifndef PRACTICE_SAVE_TRACE
#define PRACTICE_SAVE_TRACE 0
#endif

#if PRACTICE_SAVE_TRACE
#define SAVE_TR_STAGE(msg) osSyncPrintf("[save_tr] %s\n", (msg))
#else
#define SAVE_TR_STAGE(msg) ((void)0)
#endif

/* RAM slot pool megabyte buffer lives in practice_save_slotpool.c (VMA 0x80400000).
 * This file keeps globals in .main_bss so stock 4 MB never reads/writes Expansion
 * Pak DRAM for gPracticeSaveDisabled / slot_manager state. */

/* Detected at boot from osMemSize. 0 = stock (save disabled), 4 = Pak. */
s32 gPracticeRamSlotCount;

/* Set when save/load is structurally impossible (stock 4 MB). */
s32 gPracticeSaveDisabled;

s32 gPracticeActiveSlot;
s32 gPracticeSlotValidBits;
s32 gPracticeLastSaveResult;
s32 gPracticeLastLoadResult;

typedef struct PracticeScalarState {
    f32 pathProgress;
    f32 savedPathProgress;
    s32 objectLoadIndex;
    s32 savedObjectLoadIndex;
    f32 pathVelZ;
    f32 pathVelX;
    f32 pathVelY;
    f32 pathGroundScroll;
    f32 pathTexScroll;
    f32 groundHeight;
    f32 waterLevel;
    s32 groundClipMode;
    GroundType groundType;
    s32 groundSurface;
    s32 savedGroundSurface;
    LevelMode levelMode;
    s32 levelPhase;
    bool loadLevelObjects;

    LaserStrength laserStrength[4];
    s32 bombCount[4];
    s16 lifeCount[4];
    s32 chargeTimers[4];
    s32 shieldTimer[4];
    s32 hasShield[4];
    s32 playerForms[4];

    s32 hitCount;
    s32 displayedHitCount;
    s32 ringPassCount;

    s32 teamShields[6];
    s32 teamDamage[6];
    s32 starWolfTeamAlive[6];
    s32 savedStarWolfTeamAlive[6];
    s32 rightWingHealth[4];
    s32 leftWingHealth[4];
    s32 formationLeaderIndex;

    Vec3f playCamEye;
    Vec3f playCamAt;
    f32 csCamEyeX;
    f32 csCamEyeY;
    f32 csCamEyeZ;
    f32 csCamAtX;
    f32 csCamAtY;
    f32 csCamAtZ;
    f32 cameraShakeY;
    s32 cameraShake;
    s32 camCount;
    f32 fovY;
    f32 projectNear;
    f32 projectFar;

    s32 gameFrameCount;
    s32 csFrameCount;
    s32 levelClearScreenTimer;
    s32 levelStartStatusScreenTimer;
    s32 bossHealthBar;
    s32 bossActive;
    s32 allRangeEventTimer;
    s32 allRangeFrameCount;
    s32 allRangeSpawnEvent;
    s32 allRangeCheckpoint;
    s32 allRangeCountdown[3];
    bool showAllRangeCountdown;
    s32 bossFrameCount;

    u8 showHud;
    bool showReticles[4];
    s32 fillScreenAlpha;
    s32 fillScreenRed;
    s32 fillScreenGreen;
    s32 fillScreenBlue;
    s32 fillScreenAlphaTarget;
    s32 fillScreenAlphaStep;

    s32 radioState;
    s32 radioStateTimer;
    s32 radioMsgId;

    bool killEventActors;
    s32 prevEventActorIndex;

    u16 bgmSeqId;
} PracticeScalarState;

typedef struct PracticeSnapshot {
    bool valid;
    Player playerData[4];
    Actor actors[60];
    Boss bosses[4];
    Scenery scenery[50];
    Sprite sprites[40];
    Effect effects[100];
    Item items[20];
    PlayerShot playerShots[16];
    TexturedLine texturedLines[100];
    RadarMark radarMarks[65];
    BonusText bonusText[10];
    PracticeScalarState scalars;
} PracticeSnapshot;

/* Too large for the game thread stack; slot_manager never nests save inside load. */
static PracticeSnapshot gPracticeSaveScratch;

static uint32_t Practice_Save_Cb(void *buf, uint32_t buf_size);
static int Practice_Load_Cb(const void *buf, uint32_t size);

/*---------------------------------------------------------------------*/
/* Mirrors Practice_LaunchLevel - packed u16 equivalent to AUDIO_SET_SPEC. */
/*---------------------------------------------------------------------*/

static u16 Practice_AudioSpecPacked(LevelId lid) {
    u8 sfx;
    u8 spec;

    sfx = SFX_LAYOUT_DEFAULT;
    switch (lid) {
        case LEVEL_CORNERIA:
            spec = AUDIOSPEC_CO;
            break;
        case LEVEL_METEO:
            spec = AUDIOSPEC_ME;
            break;
        case LEVEL_TITANIA:
            spec = AUDIOSPEC_TI;
            break;
        case LEVEL_AQUAS:
            spec = AUDIOSPEC_AQ;
            break;
        case LEVEL_BOLSE:
            spec = AUDIOSPEC_BO;
            break;
        case LEVEL_KATINA:
            spec = AUDIOSPEC_KA;
            break;
        case LEVEL_AREA_6:
            spec = AUDIOSPEC_A6;
            break;
        case LEVEL_SECTOR_Z:
            spec = AUDIOSPEC_SZ;
            break;
        case LEVEL_FORTUNA:
            spec = AUDIOSPEC_FO;
            break;
        case LEVEL_SECTOR_X:
            spec = AUDIOSPEC_SX;
            break;
        case LEVEL_MACBETH:
            spec = AUDIOSPEC_MA;
            break;
        case LEVEL_ZONESS:
            spec = AUDIOSPEC_ZO;
            break;
        case LEVEL_SECTOR_Y:
            spec = AUDIOSPEC_SY;
            break;
        case LEVEL_SOLAR:
            sfx = SFX_LAYOUT_SO;
            spec = AUDIOSPEC_SO;
            break;
        case LEVEL_TRAINING:
            spec = AUDIOSPEC_TR;
            break;
        case LEVEL_VENOM_1:
        case LEVEL_VENOM_2:
            spec = AUDIOSPEC_VE;
            break;
        case LEVEL_VENOM_ANDROSS:
            spec = AUDIOSPEC_AND;
            break;
        default:
            spec = AUDIOSPEC_CO;
            break;
    }
    return (u16)(((u16)sfx << 8) | (u16)spec);
}

static void Snapshot_FillFromGame(PracticeSnapshot *sn) {
    s32 i;
    s32 nplayer;

    SAVE_TR_STAGE("fill begin");
    /* gPlayer points to MEM_ARRAY_ALLOCATE(Player, gCamCount): only [0 .. gCamCount-1] are valid.
     * Normal levels use gCamCount == 1; versus uses 4. Copying past the allocation UB/crashes. */
    nplayer = gCamCount;
    if (nplayer > 4) {
        nplayer = 4;
    }
    if (nplayer < 0) {
        nplayer = 0;
    }
    for (i = 0; i < nplayer; i++) {
        sn->playerData[i] = gPlayer[i];
    }
    for (; i < 4; i++) {
        bzero(&sn->playerData[i], sizeof(Player));
    }
    SAVE_TR_STAGE("fill after players");
    bcopy(gActors, sn->actors, sizeof(sn->actors));
    bcopy(gBosses, sn->bosses, sizeof(sn->bosses));
    bcopy(gScenery, sn->scenery, sizeof(sn->scenery));
    bcopy(gSprites, sn->sprites, sizeof(sn->sprites));
    bcopy(gEffects, sn->effects, sizeof(sn->effects));
    bcopy(gItems, sn->items, sizeof(sn->items));
    bcopy(gPlayerShots, sn->playerShots, sizeof(sn->playerShots));
    bcopy(gTexturedLines, sn->texturedLines, sizeof(sn->texturedLines));
    bcopy(gRadarMarks, sn->radarMarks, sizeof(sn->radarMarks));
    bcopy(gBonusText, sn->bonusText, sizeof(sn->bonusText));
    SAVE_TR_STAGE("fill after world arrays");

    sn->scalars.pathProgress = gPathProgress;
    sn->scalars.savedPathProgress = gSavedPathProgress;
    sn->scalars.objectLoadIndex = gObjectLoadIndex;
    sn->scalars.savedObjectLoadIndex = gSavedObjectLoadIndex;
    sn->scalars.pathVelZ = gPathVelZ;
    sn->scalars.pathVelX = gPathVelX;
    sn->scalars.pathVelY = gPathVelY;
    sn->scalars.pathGroundScroll = gPathGroundScroll;
    sn->scalars.pathTexScroll = gPathTexScroll;
    sn->scalars.groundHeight = gGroundHeight;
    sn->scalars.waterLevel = gWaterLevel;
    sn->scalars.groundClipMode = gGroundClipMode;
    sn->scalars.groundType = gGroundType;
    sn->scalars.groundSurface = gGroundSurface;
    sn->scalars.savedGroundSurface = gSavedGroundSurface;
    sn->scalars.levelMode = gLevelMode;
    sn->scalars.levelPhase = gLevelPhase;
    sn->scalars.loadLevelObjects = gLoadLevelObjects;
    SAVE_TR_STAGE("fill after path/env scalars");

    bcopy(gLaserStrength, sn->scalars.laserStrength, sizeof(sn->scalars.laserStrength));
    bcopy(gBombCount, sn->scalars.bombCount, sizeof(sn->scalars.bombCount));
    bcopy(gLifeCount, sn->scalars.lifeCount, sizeof(sn->scalars.lifeCount));
    bcopy(gChargeTimers, sn->scalars.chargeTimers, sizeof(sn->scalars.chargeTimers));
    bcopy(gShieldTimer, sn->scalars.shieldTimer, sizeof(sn->scalars.shieldTimer));
    bcopy(gHasShield, sn->scalars.hasShield, sizeof(sn->scalars.hasShield));
    bcopy(gPlayerForms, sn->scalars.playerForms, sizeof(sn->scalars.playerForms));
    SAVE_TR_STAGE("fill after loadout arrays");

    sn->scalars.hitCount = gHitCount;
    sn->scalars.displayedHitCount = gDisplayedHitCount;
    sn->scalars.ringPassCount = gRingPassCount;

    bcopy(gTeamShields, sn->scalars.teamShields, sizeof(sn->scalars.teamShields));
    bcopy(gTeamDamage, sn->scalars.teamDamage, sizeof(sn->scalars.teamDamage));
    bcopy(gStarWolfTeamAlive, sn->scalars.starWolfTeamAlive, sizeof(sn->scalars.starWolfTeamAlive));
    bcopy(gSavedStarWolfTeamAlive, sn->scalars.savedStarWolfTeamAlive, sizeof(sn->scalars.savedStarWolfTeamAlive));
    bcopy(gRightWingHealth, sn->scalars.rightWingHealth, sizeof(sn->scalars.rightWingHealth));
    bcopy(gLeftWingHealth, sn->scalars.leftWingHealth, sizeof(sn->scalars.leftWingHealth));
    sn->scalars.formationLeaderIndex = gFormationLeaderIndex;
    SAVE_TR_STAGE("fill after team/wings");

    sn->scalars.playCamEye = gPlayCamEye;
    sn->scalars.playCamAt = gPlayCamAt;
    sn->scalars.csCamEyeX = gCsCamEyeX;
    sn->scalars.csCamEyeY = gCsCamEyeY;
    sn->scalars.csCamEyeZ = gCsCamEyeZ;
    sn->scalars.csCamAtX = gCsCamAtX;
    sn->scalars.csCamAtY = gCsCamAtY;
    sn->scalars.csCamAtZ = gCsCamAtZ;
    sn->scalars.cameraShakeY = gCameraShakeY;
    sn->scalars.cameraShake = gCameraShake;
    sn->scalars.camCount = gCamCount;
    sn->scalars.fovY = gFovY;
    sn->scalars.projectNear = gProjectNear;
    sn->scalars.projectFar = gProjectFar;
    SAVE_TR_STAGE("fill after cam/proj");

    sn->scalars.gameFrameCount = gGameFrameCount;
    sn->scalars.csFrameCount = gCsFrameCount;
    sn->scalars.levelClearScreenTimer = gLevelClearScreenTimer;
    sn->scalars.levelStartStatusScreenTimer = gLevelStartStatusScreenTimer;
    sn->scalars.bossHealthBar = gBossHealthBar;
    sn->scalars.bossActive = gBossActive;
    sn->scalars.allRangeEventTimer = gAllRangeEventTimer;
    sn->scalars.allRangeFrameCount = gAllRangeFrameCount;
    sn->scalars.allRangeSpawnEvent = gAllRangeSpawnEvent;
    sn->scalars.allRangeCheckpoint = gAllRangeCheckpoint;
    bcopy(gAllRangeCountdown, sn->scalars.allRangeCountdown, sizeof(sn->scalars.allRangeCountdown));
    sn->scalars.showAllRangeCountdown = gShowAllRangeCountdown;
    sn->scalars.bossFrameCount = gBossFrameCount;
    SAVE_TR_STAGE("fill after boss/allrange");

    sn->scalars.showHud = gShowHud;
    bcopy(gShowReticles, sn->scalars.showReticles, sizeof(sn->scalars.showReticles));
    sn->scalars.fillScreenAlpha = gFillScreenAlpha;
    sn->scalars.fillScreenRed = gFillScreenRed;
    sn->scalars.fillScreenGreen = gFillScreenGreen;
    sn->scalars.fillScreenBlue = gFillScreenBlue;
    sn->scalars.fillScreenAlphaTarget = gFillScreenAlphaTarget;
    sn->scalars.fillScreenAlphaStep = gFillScreenAlphaStep;
    SAVE_TR_STAGE("fill after hud/fill");

    sn->scalars.radioState = gRadioState;
    sn->scalars.radioStateTimer = gRadioStateTimer;
    sn->scalars.radioMsgId = gRadioMsgId;

    sn->scalars.killEventActors = gKillEventActors;
    sn->scalars.prevEventActorIndex = gPrevEventActorIndex;

    sn->scalars.bgmSeqId = gBgmSeqId;

    sn->valid = true;
    SAVE_TR_STAGE("fill done");
}

#define PUT(szlim, w, tag, ptr, nbytes)                                                                     \
    do {                                                                                                      \
        if (serial_put_tag((w), (tag), (ptr), (uint32_t)(nbytes)) != 0) {                                     \
            return (uint32_t)((szlim) + 1u);                                                                   \
        }                                                                                                     \
    } while (0)

static uint32_t Practice_Save_Cb(void *buf, uint32_t buf_size) {
    serial_writer_t wr;
    PracticeSnapshot *snap = &gPracticeSaveScratch;
    u16 u16_lvl;
    s16 hdr_phase;
    u32 overlay_build_u32;
    u32 overlay_vram_u32;
    void *ovl_vptr;
    u32 ovl_sz;
    u16 audio_seq_pack;
    u16 audio_spec_packed;
    u32 bank_voice_placeholder;
    u32 seg_flat[16];
    uint32_t wr_sz;

    bzero(seg_flat, sizeof(seg_flat));

#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] cb enter buf=%08x cap=%u gpl=%08x cam=%d\n", (u32)(uintptr_t)buf,
                 (unsigned)buf_size, (u32)(uintptr_t)gPlayer, (s32)gCamCount);
#endif
    Snapshot_FillFromGame(snap);
    SAVE_TR_STAGE("cb after Snapshot_FillFromGame");

    serial_writer_init(&wr, buf, buf_size);
    SAVE_TR_STAGE("cb serial_writer_init ok");

    u16_lvl = (u16)gCurrentLevel;
    hdr_phase = (s16)gLevelPhase;
    PUT(buf_size, &wr, TAG_LEVEL_ID, &u16_lvl, sizeof(u16_lvl));
    PUT(buf_size, &wr, TAG_LEVEL_PHASE, &hdr_phase, sizeof(hdr_phase));
    SAVE_TR_STAGE("cb after hdr lvl+phase");

    overlay_build_u32 = practice_overlay_build_id(gCurrentLevel);
    PUT(buf_size, &wr, TAG_OVERLAY_BUILD_ID, &overlay_build_u32, sizeof(u32));
    SAVE_TR_STAGE("cb after TAG_OVERLAY_BUILD_ID");

    ovl_sz = 0;
    ovl_vptr = NULL;
    practice_overlay_get_region(gCurrentLevel, &ovl_vptr, &ovl_sz);
    if (ovl_vptr != NULL) {
        overlay_vram_u32 = (u32)((uintptr_t)ovl_vptr);
    } else {
        overlay_vram_u32 = 0;
        ovl_sz = 0;
    }
    PUT(buf_size, &wr, TAG_OVERLAY_VRAM, &overlay_vram_u32, sizeof(u32));
    if ((ovl_sz > 0) && (ovl_vptr != NULL)) {
        PUT(buf_size, &wr, TAG_OVERLAY_BYTES, ovl_vptr, ovl_sz);
    } else {
        PUT(buf_size, &wr, TAG_OVERLAY_BYTES, NULL, 0);
    }
#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] cb after TAG_OVERLAY_BYTES ovl_sz=%u vram=%08x\n", (unsigned)ovl_sz,
                 (unsigned)overlay_vram_u32);
#endif

    bcopy(gSegments, seg_flat, sizeof(gSegments));
    PUT(buf_size, &wr, TAG_SEGMENTS, seg_flat, sizeof(gSegments));
    SAVE_TR_STAGE("cb after TAG_SEGMENTS");

    audio_seq_pack = (u16)gBgmSeqId;
    PUT(buf_size, &wr, TAG_AUDIO_SEQ_ID, &audio_seq_pack, sizeof(u16));
    audio_spec_packed = Practice_AudioSpecPacked(gCurrentLevel);
    PUT(buf_size, &wr, TAG_AUDIO_SPEC_PACKED, &audio_spec_packed, sizeof(u16));
    bank_voice_placeholder = 0;
    PUT(buf_size, &wr, TAG_AUDIO_BANK_VOICE, &bank_voice_placeholder, sizeof(u32));
    SAVE_TR_STAGE("cb after audio tags");

    PUT(buf_size, &wr, TAG_PLAYER_ARRAY, snap->playerData, sizeof(snap->playerData));
    SAVE_TR_STAGE("cb after TAG_PLAYER_ARRAY");
    PUT(buf_size, &wr, TAG_ACTORS, snap->actors, sizeof(snap->actors));
    SAVE_TR_STAGE("cb after TAG_ACTORS");
    PUT(buf_size, &wr, TAG_BOSSES, snap->bosses, sizeof(snap->bosses));
    PUT(buf_size, &wr, TAG_SCENERY, snap->scenery, sizeof(snap->scenery));
    PUT(buf_size, &wr, TAG_SPRITES, snap->sprites, sizeof(snap->sprites));
    SAVE_TR_STAGE("cb after TAG_BOSSES_SCENERY_SPRITES");
    PUT(buf_size, &wr, TAG_EFFECTS, snap->effects, sizeof(snap->effects));
    PUT(buf_size, &wr, TAG_ITEMS, snap->items, sizeof(snap->items));
    PUT(buf_size, &wr, TAG_PLAYER_SHOTS, snap->playerShots, sizeof(snap->playerShots));
    SAVE_TR_STAGE("cb after TAG_EFFECTS_ITEMS_SHOTS");
    PUT(buf_size, &wr, TAG_TEXTURED_LINES, snap->texturedLines, sizeof(snap->texturedLines));
    PUT(buf_size, &wr, TAG_RADAR_MARKS, snap->radarMarks, sizeof(snap->radarMarks));
    PUT(buf_size, &wr, TAG_BONUS_TEXT, snap->bonusText, sizeof(snap->bonusText));
    SAVE_TR_STAGE("cb after TAG_LINES_RADAR_BONUS");

#define PUT_SCALAR(szlim, w, tg, fld) PUT((szlim), (w), (tg), &(snap->scalars.fld), sizeof(snap->scalars.fld))

    PUT_SCALAR(buf_size, &wr, TAG_PATH_PROGRESS, pathProgress);
    PUT_SCALAR(buf_size, &wr, TAG_SAVED_PATH_PROGRESS, savedPathProgress);
    PUT_SCALAR(buf_size, &wr, TAG_OBJECT_LOAD_INDEX, objectLoadIndex);
    PUT_SCALAR(buf_size, &wr, TAG_SAVED_OBJECT_LOAD_INDEX, savedObjectLoadIndex);
    PUT_SCALAR(buf_size, &wr, TAG_PATH_VEL_Z, pathVelZ);
    PUT_SCALAR(buf_size, &wr, TAG_PATH_VEL_X, pathVelX);
    PUT_SCALAR(buf_size, &wr, TAG_PATH_VEL_Y, pathVelY);
    PUT_SCALAR(buf_size, &wr, TAG_PATH_GROUND_SCROLL, pathGroundScroll);
    PUT_SCALAR(buf_size, &wr, TAG_PATH_TEX_SCROLL, pathTexScroll);
    PUT_SCALAR(buf_size, &wr, TAG_GROUND_HEIGHT, groundHeight);
    PUT_SCALAR(buf_size, &wr, TAG_WATER_LEVEL, waterLevel);
    PUT_SCALAR(buf_size, &wr, TAG_GROUND_CLIP_MODE, groundClipMode);
    PUT_SCALAR(buf_size, &wr, TAG_GROUND_TYPE, groundType);
    PUT_SCALAR(buf_size, &wr, TAG_GROUND_SURFACE, groundSurface);
    PUT_SCALAR(buf_size, &wr, TAG_SAVED_GROUND_SURFACE, savedGroundSurface);
    PUT_SCALAR(buf_size, &wr, TAG_LEVEL_MODE, levelMode);
    PUT_SCALAR(buf_size, &wr, TAG_SCALAR_LEVEL_PHASE, levelPhase);
    PUT_SCALAR(buf_size, &wr, TAG_LOAD_LEVEL_OBJECTS, loadLevelObjects);
    SAVE_TR_STAGE("cb after scalar path..loadobjs");
    PUT(buf_size, &wr, TAG_LASER_STRENGTH, snap->scalars.laserStrength, sizeof(snap->scalars.laserStrength));
    PUT(buf_size, &wr, TAG_BOMB_COUNT, snap->scalars.bombCount, sizeof(snap->scalars.bombCount));
    PUT(buf_size, &wr, TAG_LIFE_COUNT, snap->scalars.lifeCount, sizeof(snap->scalars.lifeCount));
    PUT(buf_size, &wr, TAG_CHARGE_TIMERS, snap->scalars.chargeTimers, sizeof(snap->scalars.chargeTimers));
    PUT(buf_size, &wr, TAG_SHIELD_TIMER, snap->scalars.shieldTimer, sizeof(snap->scalars.shieldTimer));
    PUT(buf_size, &wr, TAG_HAS_SHIELD, snap->scalars.hasShield, sizeof(snap->scalars.hasShield));
    PUT(buf_size, &wr, TAG_PLAYER_FORMS, snap->scalars.playerForms, sizeof(snap->scalars.playerForms));
    PUT_SCALAR(buf_size, &wr, TAG_HIT_COUNT, hitCount);
    PUT_SCALAR(buf_size, &wr, TAG_DISPLAYED_HIT_COUNT, displayedHitCount);
    PUT_SCALAR(buf_size, &wr, TAG_RING_PASS_COUNT, ringPassCount);
    SAVE_TR_STAGE("cb after scalar combat/hit");
    PUT(buf_size, &wr, TAG_TEAM_SHIELDS, snap->scalars.teamShields, sizeof(snap->scalars.teamShields));
    PUT(buf_size, &wr, TAG_TEAM_DAMAGE, snap->scalars.teamDamage, sizeof(snap->scalars.teamDamage));
    PUT(buf_size, &wr, TAG_STAR_WOLF_TEAM_ALIVE, snap->scalars.starWolfTeamAlive, sizeof(snap->scalars.starWolfTeamAlive));
    PUT(buf_size, &wr, TAG_SAVED_STAR_WOLF_TEAM_ALIVE, snap->scalars.savedStarWolfTeamAlive,
        sizeof(snap->scalars.savedStarWolfTeamAlive));
    PUT(buf_size, &wr, TAG_RIGHT_WING_HEALTH, snap->scalars.rightWingHealth, sizeof(snap->scalars.rightWingHealth));
    PUT(buf_size, &wr, TAG_LEFT_WING_HEALTH, snap->scalars.leftWingHealth, sizeof(snap->scalars.leftWingHealth));
    PUT_SCALAR(buf_size, &wr, TAG_FORMATION_LEADER_INDEX, formationLeaderIndex);
    SAVE_TR_STAGE("cb after team/wing arrays");
    PUT_SCALAR(buf_size, &wr, TAG_PLAY_CAM_EYE, playCamEye);
    PUT_SCALAR(buf_size, &wr, TAG_PLAY_CAM_AT, playCamAt);
    PUT_SCALAR(buf_size, &wr, TAG_CS_CAM_EYE_X, csCamEyeX);
    PUT_SCALAR(buf_size, &wr, TAG_CS_CAM_EYE_Y, csCamEyeY);
    PUT_SCALAR(buf_size, &wr, TAG_CS_CAM_EYE_Z, csCamEyeZ);
    PUT_SCALAR(buf_size, &wr, TAG_CS_CAM_AT_X, csCamAtX);
    PUT_SCALAR(buf_size, &wr, TAG_CS_CAM_AT_Y, csCamAtY);
    PUT_SCALAR(buf_size, &wr, TAG_CS_CAM_AT_Z, csCamAtZ);
    PUT_SCALAR(buf_size, &wr, TAG_CAMERA_SHAKE_Y, cameraShakeY);
    PUT_SCALAR(buf_size, &wr, TAG_CAMERA_SHAKE, cameraShake);
    PUT_SCALAR(buf_size, &wr, TAG_CAM_COUNT, camCount);
    PUT_SCALAR(buf_size, &wr, TAG_FOV_Y, fovY);
    PUT_SCALAR(buf_size, &wr, TAG_PROJECT_NEAR, projectNear);
    PUT_SCALAR(buf_size, &wr, TAG_PROJECT_FAR, projectFar);
    SAVE_TR_STAGE("cb after scalar cam/proj");
    PUT_SCALAR(buf_size, &wr, TAG_GAME_FRAME_COUNT, gameFrameCount);
    PUT_SCALAR(buf_size, &wr, TAG_CS_FRAME_COUNT, csFrameCount);
    PUT_SCALAR(buf_size, &wr, TAG_LEVEL_CLEAR_SCREEN_TIMER, levelClearScreenTimer);
    PUT_SCALAR(buf_size, &wr, TAG_LEVEL_START_STATUS_SCREEN_TIMER, levelStartStatusScreenTimer);
    PUT_SCALAR(buf_size, &wr, TAG_BOSS_HEALTH_BAR, bossHealthBar);
    PUT_SCALAR(buf_size, &wr, TAG_BOSS_ACTIVE, bossActive);
    PUT_SCALAR(buf_size, &wr, TAG_ALL_RANGE_EVENT_TIMER, allRangeEventTimer);
    PUT_SCALAR(buf_size, &wr, TAG_ALL_RANGE_FRAME_COUNT, allRangeFrameCount);
    PUT_SCALAR(buf_size, &wr, TAG_ALL_RANGE_SPAWN_EVENT, allRangeSpawnEvent);
    PUT_SCALAR(buf_size, &wr, TAG_ALL_RANGE_CHECKPOINT, allRangeCheckpoint);
    PUT(buf_size, &wr, TAG_ALL_RANGE_COUNTDOWN, snap->scalars.allRangeCountdown, sizeof(snap->scalars.allRangeCountdown));
    PUT_SCALAR(buf_size, &wr, TAG_SHOW_ALL_RANGE_COUNTDOWN, showAllRangeCountdown);
    PUT_SCALAR(buf_size, &wr, TAG_BOSS_FRAME_COUNT, bossFrameCount);
    SAVE_TR_STAGE("cb after scalar boss/allrange");
    PUT_SCALAR(buf_size, &wr, TAG_SHOW_HUD, showHud);
    PUT(buf_size, &wr, TAG_SHOW_RETICLES, snap->scalars.showReticles, sizeof(snap->scalars.showReticles));
    PUT_SCALAR(buf_size, &wr, TAG_FILL_SCREEN_ALPHA, fillScreenAlpha);
    PUT_SCALAR(buf_size, &wr, TAG_FILL_SCREEN_RED, fillScreenRed);
    PUT_SCALAR(buf_size, &wr, TAG_FILL_SCREEN_GREEN, fillScreenGreen);
    PUT_SCALAR(buf_size, &wr, TAG_FILL_SCREEN_BLUE, fillScreenBlue);
    PUT_SCALAR(buf_size, &wr, TAG_FILL_SCREEN_ALPHA_TARGET, fillScreenAlphaTarget);
    PUT_SCALAR(buf_size, &wr, TAG_FILL_SCREEN_ALPHA_STEP, fillScreenAlphaStep);
    SAVE_TR_STAGE("cb after scalar hud/fill");
    PUT_SCALAR(buf_size, &wr, TAG_RADIO_STATE, radioState);
    PUT_SCALAR(buf_size, &wr, TAG_RADIO_STATE_TIMER, radioStateTimer);
    PUT_SCALAR(buf_size, &wr, TAG_RADIO_MSG_ID, radioMsgId);
    PUT_SCALAR(buf_size, &wr, TAG_KILL_EVENT_ACTORS, killEventActors);
    PUT_SCALAR(buf_size, &wr, TAG_PREV_EVENT_ACTOR_INDEX, prevEventActorIndex);
    PUT_SCALAR(buf_size, &wr, TAG_BGM_SEQ_ID, bgmSeqId);
    SAVE_TR_STAGE("cb after scalar radio/kill/bgm");

#undef PUT_SCALAR
#undef PUT

    wr_sz = (uint32_t)serial_writer_size(&wr);
#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] cb done wr_sz=%u\n", (unsigned)wr_sz);
#endif
    return wr_sz;
}

static void Snapshot_ApplyToGame(const PracticeSnapshot *sn) {
    s32 i;
    s32 nplayer;

    SAVE_TR_STAGE("apply begin");
    nplayer = gCamCount;
    if (nplayer > 4) {
        nplayer = 4;
    }
    if (nplayer < 0) {
        nplayer = 0;
    }
    for (i = 0; i < nplayer; i++) {
        gPlayer[i] = sn->playerData[i];
    }
    SAVE_TR_STAGE("apply after players");
    bcopy(sn->actors, gActors, sizeof(sn->actors));
    bcopy(sn->bosses, gBosses, sizeof(sn->bosses));
    bcopy(sn->scenery, gScenery, sizeof(sn->scenery));
    bcopy(sn->sprites, gSprites, sizeof(sn->sprites));
    bcopy(sn->effects, gEffects, sizeof(sn->effects));
    bcopy(sn->items, gItems, sizeof(sn->items));
    bcopy(sn->playerShots, gPlayerShots, sizeof(sn->playerShots));
    bcopy(sn->texturedLines, gTexturedLines, sizeof(sn->texturedLines));
    bcopy(sn->radarMarks, gRadarMarks, sizeof(sn->radarMarks));
    bcopy(sn->bonusText, gBonusText, sizeof(sn->bonusText));
    SAVE_TR_STAGE("apply after world arrays");

    gPathProgress = sn->scalars.pathProgress;
    gSavedPathProgress = sn->scalars.savedPathProgress;
    gObjectLoadIndex = sn->scalars.objectLoadIndex;
    gSavedObjectLoadIndex = sn->scalars.savedObjectLoadIndex;
    gPathVelZ = sn->scalars.pathVelZ;
    gPathVelX = sn->scalars.pathVelX;
    gPathVelY = sn->scalars.pathVelY;
    gPathGroundScroll = sn->scalars.pathGroundScroll;
    gPathTexScroll = sn->scalars.pathTexScroll;
    gGroundHeight = sn->scalars.groundHeight;
    gWaterLevel = sn->scalars.waterLevel;
    gGroundClipMode = sn->scalars.groundClipMode;
    gGroundType = sn->scalars.groundType;
    gGroundSurface = sn->scalars.groundSurface;
    gSavedGroundSurface = sn->scalars.savedGroundSurface;
    gLevelMode = sn->scalars.levelMode;
    gLevelPhase = sn->scalars.levelPhase;
    gLoadLevelObjects = sn->scalars.loadLevelObjects;
    SAVE_TR_STAGE("apply after path..loadobjs");

    bcopy(sn->scalars.laserStrength, gLaserStrength, sizeof(sn->scalars.laserStrength));
    bcopy(sn->scalars.bombCount, gBombCount, sizeof(sn->scalars.bombCount));
    bcopy(sn->scalars.lifeCount, gLifeCount, sizeof(sn->scalars.lifeCount));
    bcopy(sn->scalars.chargeTimers, gChargeTimers, sizeof(sn->scalars.chargeTimers));
    bcopy(sn->scalars.shieldTimer, gShieldTimer, sizeof(sn->scalars.shieldTimer));
    bcopy(sn->scalars.hasShield, gHasShield, sizeof(sn->scalars.hasShield));
    bcopy(sn->scalars.playerForms, gPlayerForms, sizeof(sn->scalars.playerForms));
    SAVE_TR_STAGE("apply after loadout bcopy");

    gHitCount = sn->scalars.hitCount;
    gDisplayedHitCount = sn->scalars.displayedHitCount;
    gRingPassCount = sn->scalars.ringPassCount;
    SAVE_TR_STAGE("apply after combat scalars");

    bcopy(sn->scalars.teamShields, gTeamShields, sizeof(sn->scalars.teamShields));
    bcopy(sn->scalars.teamDamage, gTeamDamage, sizeof(sn->scalars.teamDamage));
    bcopy(sn->scalars.starWolfTeamAlive, gStarWolfTeamAlive, sizeof(sn->scalars.starWolfTeamAlive));
    bcopy(sn->scalars.savedStarWolfTeamAlive, gSavedStarWolfTeamAlive, sizeof(sn->scalars.savedStarWolfTeamAlive));
    bcopy(sn->scalars.rightWingHealth, gRightWingHealth, sizeof(sn->scalars.rightWingHealth));
    bcopy(sn->scalars.leftWingHealth, gLeftWingHealth, sizeof(sn->scalars.leftWingHealth));
    gFormationLeaderIndex = sn->scalars.formationLeaderIndex;
    SAVE_TR_STAGE("apply after team/wings");

    gPlayCamEye = sn->scalars.playCamEye;
    gPlayCamAt = sn->scalars.playCamAt;
    gCsCamEyeX = sn->scalars.csCamEyeX;
    gCsCamEyeY = sn->scalars.csCamEyeY;
    gCsCamEyeZ = sn->scalars.csCamEyeZ;
    gCsCamAtX = sn->scalars.csCamAtX;
    gCsCamAtY = sn->scalars.csCamAtY;
    gCsCamAtZ = sn->scalars.csCamAtZ;
    gCameraShakeY = sn->scalars.cameraShakeY;
    gCameraShake = sn->scalars.cameraShake;
    gCamCount = sn->scalars.camCount;
    gFovY = sn->scalars.fovY;
    gProjectNear = sn->scalars.projectNear;
    gProjectFar = sn->scalars.projectFar;
    SAVE_TR_STAGE("apply after cam/proj");

    gGameFrameCount = sn->scalars.gameFrameCount;
    gCsFrameCount = sn->scalars.csFrameCount;
    gLevelClearScreenTimer = sn->scalars.levelClearScreenTimer;
    gLevelStartStatusScreenTimer = sn->scalars.levelStartStatusScreenTimer;
    gBossHealthBar = sn->scalars.bossHealthBar;
    gBossActive = sn->scalars.bossActive;
    gAllRangeEventTimer = sn->scalars.allRangeEventTimer;
    gAllRangeFrameCount = sn->scalars.allRangeFrameCount;
    gAllRangeSpawnEvent = sn->scalars.allRangeSpawnEvent;
    gAllRangeCheckpoint = sn->scalars.allRangeCheckpoint;
    bcopy(sn->scalars.allRangeCountdown, gAllRangeCountdown, sizeof(sn->scalars.allRangeCountdown));
    gShowAllRangeCountdown = sn->scalars.showAllRangeCountdown;
    gBossFrameCount = sn->scalars.bossFrameCount;
    SAVE_TR_STAGE("apply after boss/allrange");

    gShowHud = sn->scalars.showHud;
    bcopy(sn->scalars.showReticles, gShowReticles, sizeof(sn->scalars.showReticles));
    gFillScreenAlpha = sn->scalars.fillScreenAlpha;
    gFillScreenRed = sn->scalars.fillScreenRed;
    gFillScreenGreen = sn->scalars.fillScreenGreen;
    gFillScreenBlue = sn->scalars.fillScreenBlue;
    gFillScreenAlphaTarget = sn->scalars.fillScreenAlphaTarget;
    gFillScreenAlphaStep = sn->scalars.fillScreenAlphaStep;

    gRadioState = 0;
    gRadioStateTimer = 0;
    gRadioMsgId = 0;

    gKillEventActors = sn->scalars.killEventActors;
    gPrevEventActorIndex = sn->scalars.prevEventActorIndex;
    SAVE_TR_STAGE("apply after hud/fill/kill");

    gPlayer[0].state = PLAYERSTATE_ACTIVE;
    SAVE_TR_STAGE("apply after gPlayer[0].state=ACTIVE");

    Practice_Hud_Reset();
    SAVE_TR_STAGE("apply after Practice_Hud_Reset");

    Audio_ClearVoice();
    SAVE_TR_STAGE("apply after Audio_ClearVoice");
    /* Phase 5: apply TAG_AUDIO_SPEC_PACKED via Audio_SetAudioSpec (emit-only TLV in Phase 4). */
    AUDIO_PLAY_BGM(sn->scalars.bgmSeqId);
    SAVE_TR_STAGE("apply after AUDIO_PLAY_BGM");
}

static int Practice_Load_Cb(const void *buf, uint32_t size) {
    serial_reader_t r;
    PracticeSnapshot *sn = &gPracticeSaveScratch;
    uint16_t raw_tag;
    uint32_t len;
    const void *data;
    serial_status_t st;
    u32 tlv_trace_n;
    u32 tlv_ov_build_id;
    u32 tlv_ov_vram_u32;
    const uint8_t *overlay_src;
    uint32_t overlay_len;
    bool have_overlay_meta;
    u32 segments_copy[16];
    bool have_segments;

    tlv_ov_build_id = 0;
    tlv_ov_vram_u32 = 0;
    overlay_src = NULL;
    overlay_len = 0;
    have_overlay_meta = false;
    have_segments = false;

    bzero(sn, sizeof(*sn));
    bzero(segments_copy, sizeof(segments_copy));
    tlv_trace_n = 0;

#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] load_cb enter size=%u buf=%08x\n", (unsigned)size, (u32)(uintptr_t)buf);
#endif
    serial_reader_init(&r, buf, size);
    SAVE_TR_STAGE("load tlv loop start");

    for (;;) {
        st = serial_get_next(&r, &raw_tag, &len, &data);
        if (st == SERIAL_OK_END) {
            break;
        }
        if (st != SERIAL_OK_TAG_READ) {
#if PRACTICE_SAVE_TRACE
            osSyncPrintf("[save_tr] load tlv serial_get_next st=%d (not TAG_READ)\n", (s32)st);
#endif
            return -1;
        }

        tlv_trace_n++;
#if PRACTICE_SAVE_TRACE
        if (tlv_trace_n <= 36U) {
            osSyncPrintf("[save_tr] load tlv #%u tag=%u len=%u\n", (unsigned)tlv_trace_n, (unsigned)raw_tag,
                         (unsigned)len);
        }
#endif

        switch ((practice_save_tag_t)raw_tag) {
            case TAG_LEVEL_ID:
                if (len != sizeof(u16)) {
                    return -1;
                }
                break;

            case TAG_LEVEL_PHASE:
                if (len != sizeof(s16)) {
                    return -1;
                }
                break;

            case TAG_OVERLAY_BUILD_ID:
                if (len != sizeof(u32)) {
                    return -1;
                }
                tlv_ov_build_id = *(const u32 *)data;
                have_overlay_meta = true;
                break;

            case TAG_OVERLAY_VRAM:
                if (len != sizeof(u32)) {
                    return -1;
                }
                tlv_ov_vram_u32 = *(const u32 *)data;
                have_overlay_meta = true;
                break;

            case TAG_OVERLAY_BYTES:
                overlay_src = (const uint8_t *)data;
                overlay_len = len;
                break;

            case TAG_SEGMENTS:
                if (len != sizeof(segments_copy)) {
                    return -1;
                }
                bcopy((void *)data, segments_copy, sizeof(segments_copy));
                have_segments = true;
                break;

            case TAG_AUDIO_SEQ_ID:
                if (len != sizeof(u16)) {
                    return -1;
                }
                sn->scalars.bgmSeqId = *(const u16 *)data;
                break;

            case TAG_AUDIO_SPEC_PACKED:
                /* Phase 4 emit-only - Phase 5: Audio_SetAudioSpec. */
                if (len != sizeof(u16)) {
                    return -1;
                }
                break;

            case TAG_AUDIO_BANK_VOICE:
                if (len != sizeof(u32)) {
                    return -1;
                }
                break;

            case TAG_PLAYER_ARRAY:
                if (len != sizeof(sn->playerData)) {
                    return -1;
                }
                bcopy((void *)data, sn->playerData, sizeof(sn->playerData));
                break;

            case TAG_ACTORS:
                if (len != sizeof(sn->actors)) {
                    return -1;
                }
                bcopy((void *)data, sn->actors, sizeof(sn->actors));
                break;

            case TAG_BOSSES:
                if (len != sizeof(sn->bosses)) {
                    return -1;
                }
                bcopy((void *)data, sn->bosses, sizeof(sn->bosses));
                break;

            case TAG_SCENERY:
                if (len != sizeof(sn->scenery)) {
                    return -1;
                }
                bcopy((void *)data, sn->scenery, sizeof(sn->scenery));
                break;

            case TAG_SPRITES:
                if (len != sizeof(sn->sprites)) {
                    return -1;
                }
                bcopy((void *)data, sn->sprites, sizeof(sn->sprites));
                break;

            case TAG_EFFECTS:
                if (len != sizeof(sn->effects)) {
                    return -1;
                }
                bcopy((void *)data, sn->effects, sizeof(sn->effects));
                break;

            case TAG_ITEMS:
                if (len != sizeof(sn->items)) {
                    return -1;
                }
                bcopy((void *)data, sn->items, sizeof(sn->items));
                break;

            case TAG_PLAYER_SHOTS:
                if (len != sizeof(sn->playerShots)) {
                    return -1;
                }
                bcopy((void *)data, sn->playerShots, sizeof(sn->playerShots));
                break;

            case TAG_TEXTURED_LINES:
                if (len != sizeof(sn->texturedLines)) {
                    return -1;
                }
                bcopy((void *)data, sn->texturedLines, sizeof(sn->texturedLines));
                break;

            case TAG_RADAR_MARKS:
                if (len != sizeof(sn->radarMarks)) {
                    return -1;
                }
                bcopy((void *)data, sn->radarMarks, sizeof(sn->radarMarks));
                break;

            case TAG_BONUS_TEXT:
                if (len != sizeof(sn->bonusText)) {
                    return -1;
                }
                bcopy((void *)data, sn->bonusText, sizeof(sn->bonusText));
                break;

#define LD(field)                                                                                              \
                if (len != sizeof(sn->scalars.field))                                                        \
                    return -1;                                                                                \
                bcopy((void *)data, &(sn->scalars.field), sizeof(sn->scalars.field));                            \
                break


            case TAG_PATH_PROGRESS:
                LD(pathProgress);
            case TAG_SAVED_PATH_PROGRESS:
                LD(savedPathProgress);
            case TAG_OBJECT_LOAD_INDEX:
                LD(objectLoadIndex);
            case TAG_SAVED_OBJECT_LOAD_INDEX:
                LD(savedObjectLoadIndex);
            case TAG_PATH_VEL_Z:
                LD(pathVelZ);
            case TAG_PATH_VEL_X:
                LD(pathVelX);
            case TAG_PATH_VEL_Y:
                LD(pathVelY);
            case TAG_PATH_GROUND_SCROLL:
                LD(pathGroundScroll);
            case TAG_PATH_TEX_SCROLL:
                LD(pathTexScroll);
            case TAG_GROUND_HEIGHT:
                LD(groundHeight);
            case TAG_WATER_LEVEL:
                LD(waterLevel);
            case TAG_GROUND_CLIP_MODE:
                LD(groundClipMode);
            case TAG_GROUND_TYPE:
                LD(groundType);
            case TAG_GROUND_SURFACE:
                LD(groundSurface);
            case TAG_SAVED_GROUND_SURFACE:
                LD(savedGroundSurface);
            case TAG_LEVEL_MODE:
                LD(levelMode);
            case TAG_SCALAR_LEVEL_PHASE:
                LD(levelPhase);
            case TAG_LOAD_LEVEL_OBJECTS:
                LD(loadLevelObjects);
            case TAG_HIT_COUNT:
                LD(hitCount);
            case TAG_DISPLAYED_HIT_COUNT:
                LD(displayedHitCount);
            case TAG_RING_PASS_COUNT:
                LD(ringPassCount);
            case TAG_FORMATION_LEADER_INDEX:
                LD(formationLeaderIndex);
            case TAG_PLAY_CAM_EYE:
                LD(playCamEye);
            case TAG_PLAY_CAM_AT:
                LD(playCamAt);
            case TAG_CS_CAM_EYE_X:
                LD(csCamEyeX);
            case TAG_CS_CAM_EYE_Y:
                LD(csCamEyeY);
            case TAG_CS_CAM_EYE_Z:
                LD(csCamEyeZ);
            case TAG_CS_CAM_AT_X:
                LD(csCamAtX);
            case TAG_CS_CAM_AT_Y:
                LD(csCamAtY);
            case TAG_CS_CAM_AT_Z:
                LD(csCamAtZ);
            case TAG_CAMERA_SHAKE_Y:
                LD(cameraShakeY);
            case TAG_CAMERA_SHAKE:
                LD(cameraShake);
            case TAG_CAM_COUNT:
                LD(camCount);
            case TAG_FOV_Y:
                LD(fovY);
            case TAG_PROJECT_NEAR:
                LD(projectNear);
            case TAG_PROJECT_FAR:
                LD(projectFar);
            case TAG_GAME_FRAME_COUNT:
                LD(gameFrameCount);
            case TAG_CS_FRAME_COUNT:
                LD(csFrameCount);
            case TAG_LEVEL_CLEAR_SCREEN_TIMER:
                LD(levelClearScreenTimer);
            case TAG_LEVEL_START_STATUS_SCREEN_TIMER:
                LD(levelStartStatusScreenTimer);
            case TAG_BOSS_HEALTH_BAR:
                LD(bossHealthBar);
            case TAG_BOSS_ACTIVE:
                LD(bossActive);
            case TAG_ALL_RANGE_EVENT_TIMER:
                LD(allRangeEventTimer);
            case TAG_ALL_RANGE_FRAME_COUNT:
                LD(allRangeFrameCount);
            case TAG_ALL_RANGE_SPAWN_EVENT:
                LD(allRangeSpawnEvent);
            case TAG_ALL_RANGE_CHECKPOINT:
                LD(allRangeCheckpoint);
            case TAG_SHOW_ALL_RANGE_COUNTDOWN:
                LD(showAllRangeCountdown);
            case TAG_BOSS_FRAME_COUNT:
                LD(bossFrameCount);
            case TAG_SHOW_HUD:
                LD(showHud);

#undef LD

            case TAG_LASER_STRENGTH:
                if (len != sizeof(sn->scalars.laserStrength)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.laserStrength, sizeof(sn->scalars.laserStrength));
                break;

            case TAG_BOMB_COUNT:
                if (len != sizeof(sn->scalars.bombCount)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.bombCount, sizeof(sn->scalars.bombCount));
                break;

            case TAG_LIFE_COUNT:
                if (len != sizeof(sn->scalars.lifeCount)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.lifeCount, sizeof(sn->scalars.lifeCount));
                break;

            case TAG_CHARGE_TIMERS:
                if (len != sizeof(sn->scalars.chargeTimers)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.chargeTimers, sizeof(sn->scalars.chargeTimers));
                break;

            case TAG_SHIELD_TIMER:
                if (len != sizeof(sn->scalars.shieldTimer)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.shieldTimer, sizeof(sn->scalars.shieldTimer));
                break;

            case TAG_HAS_SHIELD:
                if (len != sizeof(sn->scalars.hasShield)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.hasShield, sizeof(sn->scalars.hasShield));
                break;

            case TAG_PLAYER_FORMS:
                if (len != sizeof(sn->scalars.playerForms)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.playerForms, sizeof(sn->scalars.playerForms));
                break;

            case TAG_TEAM_SHIELDS:
                if (len != sizeof(sn->scalars.teamShields)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.teamShields, sizeof(sn->scalars.teamShields));
                break;

            case TAG_TEAM_DAMAGE:
                if (len != sizeof(sn->scalars.teamDamage)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.teamDamage, sizeof(sn->scalars.teamDamage));
                break;

            case TAG_STAR_WOLF_TEAM_ALIVE:
                if (len != sizeof(sn->scalars.starWolfTeamAlive)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.starWolfTeamAlive, sizeof(sn->scalars.starWolfTeamAlive));
                break;

            case TAG_SAVED_STAR_WOLF_TEAM_ALIVE:
                if (len != sizeof(sn->scalars.savedStarWolfTeamAlive)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.savedStarWolfTeamAlive, sizeof(sn->scalars.savedStarWolfTeamAlive));
                break;

            case TAG_RIGHT_WING_HEALTH:
                if (len != sizeof(sn->scalars.rightWingHealth)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.rightWingHealth, sizeof(sn->scalars.rightWingHealth));
                break;

            case TAG_LEFT_WING_HEALTH:
                if (len != sizeof(sn->scalars.leftWingHealth)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.leftWingHealth, sizeof(sn->scalars.leftWingHealth));
                break;

            case TAG_ALL_RANGE_COUNTDOWN:
                if (len != sizeof(sn->scalars.allRangeCountdown)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.allRangeCountdown, sizeof(sn->scalars.allRangeCountdown));
                break;

            case TAG_SHOW_RETICLES:
                if (len != sizeof(sn->scalars.showReticles)) {
                    return -1;
                }
                bcopy((void *)data, sn->scalars.showReticles, sizeof(sn->scalars.showReticles));
                break;

            case TAG_FILL_SCREEN_ALPHA:
                if (len != sizeof(sn->scalars.fillScreenAlpha)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.fillScreenAlpha), sizeof(sn->scalars.fillScreenAlpha));
                break;

            case TAG_FILL_SCREEN_RED:
                if (len != sizeof(sn->scalars.fillScreenRed)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.fillScreenRed), sizeof(sn->scalars.fillScreenRed));
                break;

            case TAG_FILL_SCREEN_GREEN:
                if (len != sizeof(sn->scalars.fillScreenGreen)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.fillScreenGreen), sizeof(sn->scalars.fillScreenGreen));
                break;

            case TAG_FILL_SCREEN_BLUE:
                if (len != sizeof(sn->scalars.fillScreenBlue)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.fillScreenBlue), sizeof(sn->scalars.fillScreenBlue));
                break;

            case TAG_FILL_SCREEN_ALPHA_TARGET:
                if (len != sizeof(sn->scalars.fillScreenAlphaTarget)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.fillScreenAlphaTarget), sizeof(sn->scalars.fillScreenAlphaTarget));
                break;

            case TAG_FILL_SCREEN_ALPHA_STEP:
                if (len != sizeof(sn->scalars.fillScreenAlphaStep)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.fillScreenAlphaStep), sizeof(sn->scalars.fillScreenAlphaStep));
                break;

            case TAG_RADIO_STATE:
                if (len != sizeof(sn->scalars.radioState)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.radioState), sizeof(sn->scalars.radioState));
                break;

            case TAG_RADIO_STATE_TIMER:
                if (len != sizeof(sn->scalars.radioStateTimer)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.radioStateTimer), sizeof(sn->scalars.radioStateTimer));
                break;

            case TAG_RADIO_MSG_ID:
                if (len != sizeof(sn->scalars.radioMsgId)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.radioMsgId), sizeof(sn->scalars.radioMsgId));
                break;

            case TAG_KILL_EVENT_ACTORS:
                if (len != sizeof(sn->scalars.killEventActors)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.killEventActors), sizeof(sn->scalars.killEventActors));
                break;

            case TAG_PREV_EVENT_ACTOR_INDEX:
                if (len != sizeof(sn->scalars.prevEventActorIndex)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.prevEventActorIndex), sizeof(sn->scalars.prevEventActorIndex));
                break;

            case TAG_BGM_SEQ_ID:
                if (len != sizeof(sn->scalars.bgmSeqId)) {
                    return -1;
                }
                bcopy((void *)data, &(sn->scalars.bgmSeqId), sizeof(sn->scalars.bgmSeqId));
                break;

            default:
                break;
        }
    }

#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] load tlv loop done tags=%u\n", (unsigned)tlv_trace_n);
#endif
    (void)have_overlay_meta;

    SAVE_TR_STAGE("load before overlay restore");
    if ((overlay_src != NULL) && (overlay_len > 0)) {
        void *dst;
        u32 ds;
        u32 cur_build;
        u32 cur_va;

        if (practice_overlay_get_region(gCurrentLevel, &dst, &ds) == 0) {
            cur_va = (u32)(uintptr_t)dst;
            cur_build = practice_overlay_build_id(gCurrentLevel);
            if ((tlv_ov_build_id == cur_build) && (tlv_ov_vram_u32 == cur_va) && (overlay_len == ds)) {
                bcopy((void *)overlay_src, dst, ds);
            } else if (overlay_len != 0) {
                osSyncPrintf("[practice_save] WARN partial overlay skipped (tlv_build=%08x cur=%08x)\n",
                             tlv_ov_build_id, cur_build);
            }
        }
    }
    SAVE_TR_STAGE("load after overlay restore");

    if (have_segments) {
        SAVE_TR_STAGE("load apply TAG_SEGMENTS");
        bcopy(segments_copy, gSegments, sizeof(gSegments));
    }

    SAVE_TR_STAGE("load before Snapshot_ApplyToGame");
    Snapshot_ApplyToGame(sn);
    SAVE_TR_STAGE("load_cb ok return 0");
    return 0;
}

#if PRACTICE_SAVE_SELFTEST
static u8 sSelftestStorage[SLOT_MANAGER_HEADER_SIZE + 16];
static s32 sSelftestLoadedValue;

static uint32_t Practice_Save_SelftestSaveCb(void *buf, uint32_t buf_size) {
    u32 value;

    if (buf_size < sizeof(value)) {
        return buf_size + 1U;
    }

    value = 0x53545631U; /* STV1 */
    bcopy(&value, buf, sizeof(value));
    return sizeof(value);
}

static int Practice_Save_SelftestLoadCb(const void *buf, uint32_t size) {
    u32 value;

    if (size != sizeof(value)) {
        return -1;
    }

    bcopy((void *)buf, &value, sizeof(value));
    sSelftestLoadedValue = (s32)value;
    return 0;
}

static void Practice_Save_CorruptSlotProbe(void) {
    s32 r;

    /* Exercise slot_manager's header validation without touching live game state.
     * Practice_Save_Init reinitializes the real slot manager immediately after this probe. */
    bzero(sSelftestStorage, sizeof(sSelftestStorage));
    sSelftestLoadedValue = 0;

    slot_manager_init(STATE_VERSION, LIB_VERSION, Practice_Save_SelftestSaveCb, Practice_Save_SelftestLoadCb, 1);
    r = slot_manager_set_ram_storage(sSelftestStorage, sizeof(sSelftestStorage), sizeof(sSelftestStorage));
    if (r != SLOT_MANAGER_OK) {
        osSyncPrintf("[practice_save] WARN corrupt-slot probe storage: got %d\n", r);
        return;
    }

    r = slot_manager_save_ram(0);
    if (r != SLOT_MANAGER_OK) {
        osSyncPrintf("[practice_save] WARN corrupt-slot probe save: got %d\n", r);
        return;
    }

    sSelftestStorage[0] ^= 0xFF;
    r = slot_manager_load_ram(0);
    if (r != SLOT_MANAGER_ERR_MAGIC) {
        osSyncPrintf("[practice_save] WARN corrupt-slot probe magic: got %d\n", r);
    }
    if (sSelftestLoadedValue != 0) {
        osSyncPrintf("[practice_save] WARN corrupt-slot probe mutated state\n");
    }

    r = slot_manager_load_ram(99);
    if (r != SLOT_MANAGER_ERR_INVALID_SLOT) {
        osSyncPrintf("[practice_save] WARN invalid-slot probe: got %d\n", r);
    }
}
#endif

static void Practice_SaveTrace_CanSaveFields(void) {
#if PRACTICE_SAVE_TRACE
    osSyncPrintf(
        "[save_tr] gates dis=%d gs=%d ps=%d menu=%d ovl_ok=%d gpl=%08x p0st=%d\n", (s32)gPracticeSaveDisabled,
        (s32)gGameState, (s32)gPlayState, (s32)gPracticeMenuState,
        practice_overlay_is_saveable(gCurrentLevel) ? 1 : 0, (u32)(uintptr_t)gPlayer,
        (gPlayer != NULL) ? (s32)gPlayer[0].state : -99);
#endif
}

s32 Practice_CanSaveHere(void) {
    if (gPracticeSaveDisabled) {
        return 0;
    }
    if (gGameState != GSTATE_PLAY) {
        return 0;
    }
    if (gPlayState != PLAY_UPDATE) {
        return 0;
    }
    if (!practice_overlay_is_saveable(gCurrentLevel)) {
        return 0;
    }
    if (gPlayer == NULL) {
        return 0;
    }
    if (gPlayer[0].state != PLAYERSTATE_ACTIVE) {
        return 0;
    }
    return 1;
}

void Practice_Save_Init(void) {
    /* Phase 3 -- runtime slot pool selection based on Expansion Pak detection.
     *
     * osMemSize is set by the N64 OS during boot:
     *   0x00400000 (4 MB)  -- stock RAM, no Expansion Pak
     *   0x00800000 (8 MB)  -- Expansion Pak installed
     *
     * On stock 4 MB hardware:
     *   - The Expansion Pak DRAM window (starts 0x80400000) is not wired; touching
     *     it faults. Save-state control globals stay in practice_save BSS (.main_bss).
     *   - We set gPracticeSaveDisabled=1 and skip slot_manager_init entirely.
     *
     * On Expansion Pak:
     *   - Slot pool only (practice_save_slotpool.c): VMA at 0x80400000, above the
     *     overlay load window (worst case ~0x8028a210 for Titania setup 5), below 0x80800000.
     *   - Four slots times 256 KB fits with ample headroom.
     *
     * We do NOT bzero() the slot pool here. The .practice_pool_pak NOLOAD BSS is cleared
     * at cold boot. Stock N64 skips that region safely because we never pass its pointer
     * into slot_manager. */

    gPracticeRamSlotCount =
        (osMemSize >= 0x00800000U) ? MAX_RAM_SLOTS_WITH_PAK : MAX_RAM_SLOTS_NO_PAK;

    if (gPracticeRamSlotCount == 0) {
        /* Stock 4 MB: save/load not supported. */
        gPracticeSaveDisabled = 1;
        osSyncPrintf("[practice_save] stock 4MB RAM: save/load disabled (no Expansion Pak)\n");
        gPracticeActiveSlot = 0;
        gPracticeSlotValidBits = 0;
        gPracticeLastSaveResult = 0;
        gPracticeLastLoadResult = 0;
        return;
    }

    gPracticeSaveDisabled = 0;

#if PRACTICE_SAVE_SELFTEST
    Practice_Save_CorruptSlotProbe();
#endif

    slot_manager_init(STATE_VERSION, LIB_VERSION, Practice_Save_Cb, Practice_Load_Cb, gPracticeRamSlotCount);

    if (slot_manager_set_ram_storage(
            (void*)Practice_Save_SlotPoolBase(),
            (uint32_t)((MAX_RAM_SLOTS_WITH_PAK * MAX_STATE_SIZE)),
            MAX_STATE_SIZE) != SLOT_MANAGER_OK) {
        osSyncPrintf("[practice_save] Practice_Save_Init slot_manager_set_ram_storage failed\n");
        gPracticeSaveDisabled = 1;
        slot_manager_init(0U, 0U, NULL, NULL, 0U);
        gPracticeRamSlotCount = MAX_RAM_SLOTS_NO_PAK;
        gPracticeActiveSlot = 0;
        gPracticeSlotValidBits = 0;
        gPracticeLastSaveResult = 0;
        gPracticeLastLoadResult = 0;
        return;
    }

    osSyncPrintf("[practice_save] Expansion Pak: %d slots at pool=0x%08x (osMemSize=0x%08x)\n",
                 gPracticeRamSlotCount,
                 (unsigned)(u32)(uintptr_t)Practice_Save_SlotPoolBase(),
                 (unsigned)osMemSize);

    gPracticeActiveSlot = 0;
    gPracticeSlotValidBits = 0;
    gPracticeLastSaveResult = 0;
    gPracticeLastLoadResult = 0;

}

void Practice_SaveTrace_HotkeyIsv(void) {
#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] hotkey SAVE lvl=%d gs=%d ps=%d menu=%d scr=%d\n", (s32)gCurrentLevel, (s32)gGameState,
                 (s32)gPlayState, (s32)gPracticeMenuState, (s32)gPracticeScreen);
#endif
}

void Practice_SaveTrace_LoadHotkeyIsv(void) {
#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] hotkey LOAD lvl=%d gs=%d ps=%d menu=%d scr=%d\n", (s32)gCurrentLevel, (s32)gGameState,
                 (s32)gPlayState, (s32)gPracticeMenuState, (s32)gPracticeScreen);
#endif
}

void Practice_ClearCheckpoint(void) {
    s32 i;

    if (gPracticeSaveDisabled) {
        return;
    }
    for (i = 0; i < gPracticeRamSlotCount; i++) {
        slot_manager_clear_ram(i);
    }
    gPracticeSlotValidBits = 0;
}

s32 Practice_HasCheckpoint(void) {
    if (gPracticeSaveDisabled) {
        return 0;
    }
    return slot_manager_ram_valid(gPracticeActiveSlot) ? 1 : 0;
}

static void SyncValidBits(void) {
    s32 i;
    u32 bits;

    bits = 0;
    for (i = 0; i < gPracticeRamSlotCount; i++) {
        if (slot_manager_ram_valid(i)) {
            bits |= (u32)((u32)1u << i);
        }
    }
    gPracticeSlotValidBits = (s32)bits;
}

void Practice_SaveStateSlot(s32 slot) {
    s32 rr;

    gPracticeLastSaveResult = SLOT_MANAGER_ERR_INVALID_SLOT;

#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] SaveStateSlot enter slot=%d dis=%d gs=%d ps=%d menu=%d scr=%d\n", slot,
                 (s32)gPracticeSaveDisabled, (s32)gGameState, (s32)gPlayState, (s32)gPracticeMenuState,
                 (s32)gPracticeScreen);
#endif

    if (gPracticeSaveDisabled) {
        osSyncPrintf("[save] disabled: no Expansion Pak (stock 4MB)\n");
        Practice_Hud_ShowStatus("SAVE DIS", 255, 120, 80);
        return;
    }

    if ((slot < 0) || (slot >= gPracticeRamSlotCount)) {
        osSyncPrintf("[save] refuse: bad slot=%d\n", slot);
        Practice_Hud_ShowStatus("BAD SLOT", 255, 120, 80);
        return;
    }

    Practice_SaveTrace_CanSaveFields();
    if (!Practice_CanSaveHere()) {
        gPracticeLastSaveResult = SLOT_MANAGER_ERR_INVALID_SLOT;
        osSyncPrintf("[save] refuse: not saveable (level=%d play=%d menu=%d)\n", (s32)gCurrentLevel,
                     (s32)gPlayState, (s32)gPracticeMenuState);
        Practice_Hud_ShowStatus("SAVE REF", 255, 120, 80);
        return;
    }

#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] call slot_manager_save_ram(slot=%d)\n", slot);
#endif
    rr = slot_manager_save_ram(slot);
#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] slot_manager_save_ram ret=%d\n", rr);
#endif
    gPracticeLastSaveResult = rr;
    SyncValidBits();
    if (rr == SLOT_MANAGER_OK) {
        Practice_Hud_ShowStatus("SAVE OK", 80, 255, 120);
    } else {
        Practice_Hud_ShowStatus("SAVE FAIL", 255, 120, 80);
    }
}

void Practice_SaveState(void) {
    Practice_SaveStateSlot(gPracticeActiveSlot);
}

void Practice_LoadStateSlot(s32 slot) {
    s32 rr;

    gPracticeLastLoadResult = SLOT_MANAGER_ERR_INVALID_SLOT;

#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] LoadStateSlot enter slot=%d dis=%d scr=%d\n", slot, (s32)gPracticeSaveDisabled,
                 (s32)gPracticeScreen);
#endif

    if (gPracticeSaveDisabled) {
        osSyncPrintf("[load] disabled: no Expansion Pak (stock 4MB)\n");
        Practice_Hud_ShowStatus("LOAD DIS", 255, 120, 80);
        return;
    }

    if ((slot < 0) || (slot >= gPracticeRamSlotCount)) {
#if PRACTICE_SAVE_TRACE
        osSyncPrintf("[save_tr] LoadStateSlot refuse bad slot=%d\n", slot);
#endif
        Practice_Hud_ShowStatus("BAD SLOT", 255, 120, 80);
        return;
    }
    if (!slot_manager_ram_valid(slot)) {
        gPracticeLastLoadResult = SLOT_MANAGER_ERR_INVALID_SLOT;
#if PRACTICE_SAVE_TRACE
        osSyncPrintf("[save_tr] LoadStateSlot refuse slot empty\n");
#endif
        Practice_Hud_ShowStatus("LOAD EMPTY", 255, 180, 80);
        return;
    }

#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] call slot_manager_load_ram(%d)\n", slot);
#endif
    rr = slot_manager_load_ram(slot);
#if PRACTICE_SAVE_TRACE
    osSyncPrintf("[save_tr] slot_manager_load_ram ret=%d\n", rr);
#endif
    gPracticeLastLoadResult = rr;
    if (rr == SLOT_MANAGER_OK) {
        Practice_Hud_ShowStatus("LOAD OK", 80, 255, 120);
    } else {
        Practice_Hud_ShowStatus("LOAD FAIL", 255, 120, 80);
    }
}

void Practice_LoadState(void) {
    Practice_LoadStateSlot(gPracticeActiveSlot);
}

s32 Practice_GetActiveSlot(void) {
    return gPracticeActiveSlot;
}

void Practice_CycleSlot(s32 delta) {
    s32 n;

    if (gPracticeSaveDisabled) {
        return;
    }

    if (delta <= 0) {
        n = slot_manager_prev_slot(gPracticeActiveSlot);
    } else {
        n = slot_manager_next_slot(gPracticeActiveSlot);
    }
    if (n >= 0) {
        gPracticeActiveSlot = n;
    }
}

#endif /* PRACTICE_ROM */
