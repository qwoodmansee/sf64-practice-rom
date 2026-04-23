#include "practice.h"

#ifdef PRACTICE_ROM

#include "variables.h"
#include "bgm.h"

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

static PracticeSnapshot sSnapshot;

void Practice_SaveState(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        sSnapshot.playerData[i] = gPlayer[i];
    }
    bcopy(gActors, sSnapshot.actors, sizeof(sSnapshot.actors));
    bcopy(gBosses, sSnapshot.bosses, sizeof(sSnapshot.bosses));
    bcopy(gScenery, sSnapshot.scenery, sizeof(sSnapshot.scenery));
    bcopy(gSprites, sSnapshot.sprites, sizeof(sSnapshot.sprites));
    bcopy(gEffects, sSnapshot.effects, sizeof(sSnapshot.effects));
    bcopy(gItems, sSnapshot.items, sizeof(sSnapshot.items));
    bcopy(gPlayerShots, sSnapshot.playerShots, sizeof(sSnapshot.playerShots));
    bcopy(gTexturedLines, sSnapshot.texturedLines, sizeof(sSnapshot.texturedLines));
    bcopy(gRadarMarks, sSnapshot.radarMarks, sizeof(sSnapshot.radarMarks));
    bcopy(gBonusText, sSnapshot.bonusText, sizeof(sSnapshot.bonusText));

    sSnapshot.scalars.pathProgress = gPathProgress;
    sSnapshot.scalars.savedPathProgress = gSavedPathProgress;
    sSnapshot.scalars.objectLoadIndex = gObjectLoadIndex;
    sSnapshot.scalars.savedObjectLoadIndex = gSavedObjectLoadIndex;
    sSnapshot.scalars.pathVelZ = gPathVelZ;
    sSnapshot.scalars.pathVelX = gPathVelX;
    sSnapshot.scalars.pathVelY = gPathVelY;
    sSnapshot.scalars.pathGroundScroll = gPathGroundScroll;
    sSnapshot.scalars.pathTexScroll = gPathTexScroll;
    sSnapshot.scalars.groundHeight = gGroundHeight;
    sSnapshot.scalars.waterLevel = gWaterLevel;
    sSnapshot.scalars.groundClipMode = gGroundClipMode;
    sSnapshot.scalars.groundType = gGroundType;
    sSnapshot.scalars.groundSurface = gGroundSurface;
    sSnapshot.scalars.savedGroundSurface = gSavedGroundSurface;
    sSnapshot.scalars.levelMode = gLevelMode;
    sSnapshot.scalars.levelPhase = gLevelPhase;
    sSnapshot.scalars.loadLevelObjects = gLoadLevelObjects;

    bcopy(gLaserStrength, sSnapshot.scalars.laserStrength, sizeof(sSnapshot.scalars.laserStrength));
    bcopy(gBombCount, sSnapshot.scalars.bombCount, sizeof(sSnapshot.scalars.bombCount));
    bcopy(gLifeCount, sSnapshot.scalars.lifeCount, sizeof(sSnapshot.scalars.lifeCount));
    bcopy(gChargeTimers, sSnapshot.scalars.chargeTimers, sizeof(sSnapshot.scalars.chargeTimers));
    bcopy(gShieldTimer, sSnapshot.scalars.shieldTimer, sizeof(sSnapshot.scalars.shieldTimer));
    bcopy(gHasShield, sSnapshot.scalars.hasShield, sizeof(sSnapshot.scalars.hasShield));
    bcopy(gPlayerForms, sSnapshot.scalars.playerForms, sizeof(sSnapshot.scalars.playerForms));

    sSnapshot.scalars.hitCount = gHitCount;
    sSnapshot.scalars.displayedHitCount = gDisplayedHitCount;
    sSnapshot.scalars.ringPassCount = gRingPassCount;

    bcopy(gTeamShields, sSnapshot.scalars.teamShields, sizeof(sSnapshot.scalars.teamShields));
    bcopy(gTeamDamage, sSnapshot.scalars.teamDamage, sizeof(sSnapshot.scalars.teamDamage));
    bcopy(gStarWolfTeamAlive, sSnapshot.scalars.starWolfTeamAlive, sizeof(sSnapshot.scalars.starWolfTeamAlive));
    bcopy(gSavedStarWolfTeamAlive, sSnapshot.scalars.savedStarWolfTeamAlive, sizeof(sSnapshot.scalars.savedStarWolfTeamAlive));
    bcopy(gRightWingHealth, sSnapshot.scalars.rightWingHealth, sizeof(sSnapshot.scalars.rightWingHealth));
    bcopy(gLeftWingHealth, sSnapshot.scalars.leftWingHealth, sizeof(sSnapshot.scalars.leftWingHealth));
    sSnapshot.scalars.formationLeaderIndex = gFormationLeaderIndex;

    sSnapshot.scalars.playCamEye = gPlayCamEye;
    sSnapshot.scalars.playCamAt = gPlayCamAt;
    sSnapshot.scalars.csCamEyeX = gCsCamEyeX;
    sSnapshot.scalars.csCamEyeY = gCsCamEyeY;
    sSnapshot.scalars.csCamEyeZ = gCsCamEyeZ;
    sSnapshot.scalars.csCamAtX = gCsCamAtX;
    sSnapshot.scalars.csCamAtY = gCsCamAtY;
    sSnapshot.scalars.csCamAtZ = gCsCamAtZ;
    sSnapshot.scalars.cameraShakeY = gCameraShakeY;
    sSnapshot.scalars.cameraShake = gCameraShake;
    sSnapshot.scalars.camCount = gCamCount;
    sSnapshot.scalars.fovY = gFovY;
    sSnapshot.scalars.projectNear = gProjectNear;
    sSnapshot.scalars.projectFar = gProjectFar;

    sSnapshot.scalars.gameFrameCount = gGameFrameCount;
    sSnapshot.scalars.csFrameCount = gCsFrameCount;
    sSnapshot.scalars.levelClearScreenTimer = gLevelClearScreenTimer;
    sSnapshot.scalars.levelStartStatusScreenTimer = gLevelStartStatusScreenTimer;
    sSnapshot.scalars.bossHealthBar = gBossHealthBar;
    sSnapshot.scalars.bossActive = gBossActive;
    sSnapshot.scalars.allRangeEventTimer = gAllRangeEventTimer;
    sSnapshot.scalars.allRangeFrameCount = gAllRangeFrameCount;
    sSnapshot.scalars.allRangeSpawnEvent = gAllRangeSpawnEvent;
    sSnapshot.scalars.allRangeCheckpoint = gAllRangeCheckpoint;
    bcopy(gAllRangeCountdown, sSnapshot.scalars.allRangeCountdown, sizeof(sSnapshot.scalars.allRangeCountdown));
    sSnapshot.scalars.showAllRangeCountdown = gShowAllRangeCountdown;
    sSnapshot.scalars.bossFrameCount = gBossFrameCount;

    sSnapshot.scalars.showHud = gShowHud;
    bcopy(gShowReticles, sSnapshot.scalars.showReticles, sizeof(sSnapshot.scalars.showReticles));
    sSnapshot.scalars.fillScreenAlpha = gFillScreenAlpha;
    sSnapshot.scalars.fillScreenRed = gFillScreenRed;
    sSnapshot.scalars.fillScreenGreen = gFillScreenGreen;
    sSnapshot.scalars.fillScreenBlue = gFillScreenBlue;
    sSnapshot.scalars.fillScreenAlphaTarget = gFillScreenAlphaTarget;
    sSnapshot.scalars.fillScreenAlphaStep = gFillScreenAlphaStep;

    sSnapshot.scalars.radioState = gRadioState;
    sSnapshot.scalars.radioStateTimer = gRadioStateTimer;
    sSnapshot.scalars.radioMsgId = gRadioMsgId;

    sSnapshot.scalars.killEventActors = gKillEventActors;
    sSnapshot.scalars.prevEventActorIndex = gPrevEventActorIndex;

    sSnapshot.scalars.bgmSeqId = gBgmSeqId;

    sSnapshot.valid = true;
}

void Practice_LoadState(void) {
    s32 i;

    if (!sSnapshot.valid) {
        return;
    }

    for (i = 0; i < 4; i++) {
        gPlayer[i] = sSnapshot.playerData[i];
    }
    bcopy(sSnapshot.actors, gActors, sizeof(sSnapshot.actors));
    bcopy(sSnapshot.bosses, gBosses, sizeof(sSnapshot.bosses));
    bcopy(sSnapshot.scenery, gScenery, sizeof(sSnapshot.scenery));
    bcopy(sSnapshot.sprites, gSprites, sizeof(sSnapshot.sprites));
    bcopy(sSnapshot.effects, gEffects, sizeof(sSnapshot.effects));
    bcopy(sSnapshot.items, gItems, sizeof(sSnapshot.items));
    bcopy(sSnapshot.playerShots, gPlayerShots, sizeof(sSnapshot.playerShots));
    bcopy(sSnapshot.texturedLines, gTexturedLines, sizeof(sSnapshot.texturedLines));
    bcopy(sSnapshot.radarMarks, gRadarMarks, sizeof(sSnapshot.radarMarks));
    bcopy(sSnapshot.bonusText, gBonusText, sizeof(sSnapshot.bonusText));

    gPathProgress = sSnapshot.scalars.pathProgress;
    gSavedPathProgress = sSnapshot.scalars.savedPathProgress;
    gObjectLoadIndex = sSnapshot.scalars.objectLoadIndex;
    gSavedObjectLoadIndex = sSnapshot.scalars.savedObjectLoadIndex;
    gPathVelZ = sSnapshot.scalars.pathVelZ;
    gPathVelX = sSnapshot.scalars.pathVelX;
    gPathVelY = sSnapshot.scalars.pathVelY;
    gPathGroundScroll = sSnapshot.scalars.pathGroundScroll;
    gPathTexScroll = sSnapshot.scalars.pathTexScroll;
    gGroundHeight = sSnapshot.scalars.groundHeight;
    gWaterLevel = sSnapshot.scalars.waterLevel;
    gGroundClipMode = sSnapshot.scalars.groundClipMode;
    gGroundType = sSnapshot.scalars.groundType;
    gGroundSurface = sSnapshot.scalars.groundSurface;
    gSavedGroundSurface = sSnapshot.scalars.savedGroundSurface;
    gLevelMode = sSnapshot.scalars.levelMode;
    gLevelPhase = sSnapshot.scalars.levelPhase;
    gLoadLevelObjects = sSnapshot.scalars.loadLevelObjects;

    bcopy(sSnapshot.scalars.laserStrength, gLaserStrength, sizeof(sSnapshot.scalars.laserStrength));
    bcopy(sSnapshot.scalars.bombCount, gBombCount, sizeof(sSnapshot.scalars.bombCount));
    bcopy(sSnapshot.scalars.lifeCount, gLifeCount, sizeof(sSnapshot.scalars.lifeCount));
    bcopy(sSnapshot.scalars.chargeTimers, gChargeTimers, sizeof(sSnapshot.scalars.chargeTimers));
    bcopy(sSnapshot.scalars.shieldTimer, gShieldTimer, sizeof(sSnapshot.scalars.shieldTimer));
    bcopy(sSnapshot.scalars.hasShield, gHasShield, sizeof(sSnapshot.scalars.hasShield));
    bcopy(sSnapshot.scalars.playerForms, gPlayerForms, sizeof(sSnapshot.scalars.playerForms));

    gHitCount = sSnapshot.scalars.hitCount;
    gDisplayedHitCount = sSnapshot.scalars.displayedHitCount;
    gRingPassCount = sSnapshot.scalars.ringPassCount;

    bcopy(sSnapshot.scalars.teamShields, gTeamShields, sizeof(sSnapshot.scalars.teamShields));
    bcopy(sSnapshot.scalars.teamDamage, gTeamDamage, sizeof(sSnapshot.scalars.teamDamage));
    bcopy(sSnapshot.scalars.starWolfTeamAlive, gStarWolfTeamAlive, sizeof(sSnapshot.scalars.starWolfTeamAlive));
    bcopy(sSnapshot.scalars.savedStarWolfTeamAlive, gSavedStarWolfTeamAlive, sizeof(sSnapshot.scalars.savedStarWolfTeamAlive));
    bcopy(sSnapshot.scalars.rightWingHealth, gRightWingHealth, sizeof(sSnapshot.scalars.rightWingHealth));
    bcopy(sSnapshot.scalars.leftWingHealth, gLeftWingHealth, sizeof(sSnapshot.scalars.leftWingHealth));
    gFormationLeaderIndex = sSnapshot.scalars.formationLeaderIndex;

    gPlayCamEye = sSnapshot.scalars.playCamEye;
    gPlayCamAt = sSnapshot.scalars.playCamAt;
    gCsCamEyeX = sSnapshot.scalars.csCamEyeX;
    gCsCamEyeY = sSnapshot.scalars.csCamEyeY;
    gCsCamEyeZ = sSnapshot.scalars.csCamEyeZ;
    gCsCamAtX = sSnapshot.scalars.csCamAtX;
    gCsCamAtY = sSnapshot.scalars.csCamAtY;
    gCsCamAtZ = sSnapshot.scalars.csCamAtZ;
    gCameraShakeY = sSnapshot.scalars.cameraShakeY;
    gCameraShake = sSnapshot.scalars.cameraShake;
    gCamCount = sSnapshot.scalars.camCount;
    gFovY = sSnapshot.scalars.fovY;
    gProjectNear = sSnapshot.scalars.projectNear;
    gProjectFar = sSnapshot.scalars.projectFar;

    gGameFrameCount = sSnapshot.scalars.gameFrameCount;
    gCsFrameCount = sSnapshot.scalars.csFrameCount;
    gLevelClearScreenTimer = sSnapshot.scalars.levelClearScreenTimer;
    gLevelStartStatusScreenTimer = sSnapshot.scalars.levelStartStatusScreenTimer;
    gBossHealthBar = sSnapshot.scalars.bossHealthBar;
    gBossActive = sSnapshot.scalars.bossActive;
    gAllRangeEventTimer = sSnapshot.scalars.allRangeEventTimer;
    gAllRangeFrameCount = sSnapshot.scalars.allRangeFrameCount;
    gAllRangeSpawnEvent = sSnapshot.scalars.allRangeSpawnEvent;
    gAllRangeCheckpoint = sSnapshot.scalars.allRangeCheckpoint;
    bcopy(sSnapshot.scalars.allRangeCountdown, gAllRangeCountdown, sizeof(sSnapshot.scalars.allRangeCountdown));
    gShowAllRangeCountdown = sSnapshot.scalars.showAllRangeCountdown;
    gBossFrameCount = sSnapshot.scalars.bossFrameCount;

    gShowHud = sSnapshot.scalars.showHud;
    bcopy(sSnapshot.scalars.showReticles, gShowReticles, sizeof(sSnapshot.scalars.showReticles));
    gFillScreenAlpha = sSnapshot.scalars.fillScreenAlpha;
    gFillScreenRed = sSnapshot.scalars.fillScreenRed;
    gFillScreenGreen = sSnapshot.scalars.fillScreenGreen;
    gFillScreenBlue = sSnapshot.scalars.fillScreenBlue;
    gFillScreenAlphaTarget = sSnapshot.scalars.fillScreenAlphaTarget;
    gFillScreenAlphaStep = sSnapshot.scalars.fillScreenAlphaStep;

    gRadioState = 0;
    gRadioStateTimer = 0;
    gRadioMsgId = 0;

    gKillEventActors = sSnapshot.scalars.killEventActors;
    gPrevEventActorIndex = sSnapshot.scalars.prevEventActorIndex;

    gPlayer[0].state = PLAYERSTATE_ACTIVE;

    Audio_ClearVoice();
    AUDIO_PLAY_BGM(sSnapshot.scalars.bgmSeqId);
}

bool Practice_HasCheckpoint(void) {
    return sSnapshot.valid;
}

void Practice_ClearCheckpoint(void) {
    sSnapshot.valid = false;
}

#endif
