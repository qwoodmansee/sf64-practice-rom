#include "practice.h"

#ifdef PRACTICE_ROM

#define HUD_X 8
#define HUD_Y 60
#define HUD_LINE_H 10

static bool Practice_FloatIsValid(f32* fp) {
    u32 bits = *(u32*)fp;
    u32 exp = (bits >> 23) & 0xFF;
    return (exp != 0xFF);
}

static s32 sLastGameFrame = -1;
static s32 sLagFrameCount = 0;
static s32 sLastChargeTimer = 0;
static s32 sChargeStartFrame = 0;
static s32 sLastChargeShotGap = 0;
static s32 sChargeCount = 0;
static s32 sMissedInputCount = 0;
static s32 sLastInputPollFrame = -1;
static s32 sCSImpactFrame = -1;
static s32 sLastCSTimingOffset = 0;
static bool sCSTimingValid = false;
static const char* sStatusText = NULL;
static s32 sStatusTimer = 0;
static u8 sStatusR = 255;
static u8 sStatusG = 255;
static u8 sStatusB = 255;
PracticeStats gPracticeStats;

void Practice_Hud_Reset(void) {
    Practice_ChargeAssist_Reset();
    sLastGameFrame = -1;
    sLagFrameCount = 0;
    sLastChargeTimer = 0;
    sChargeStartFrame = 0;
    sLastChargeShotGap = 0;
    sChargeCount = 0;
    sMissedInputCount = 0;
    sLastInputPollFrame = -1;
    sCSImpactFrame = -1;
    sLastCSTimingOffset = 0;
    sCSTimingValid = false;
    gPracticeStats.kills = 0;
    gPracticeStats.csBonus = 0;
    gPracticeStats.directHits = 0;
    gPracticeStats.escapes = 0;
    gPracticeStats.crashes = 0;
    gPracticeStats.teamKills = 0;
}

void Practice_Hud_ShowStatus(const char* text, u8 r, u8 g, u8 b) {
    sStatusText = text;
    sStatusTimer = 90;
    sStatusR = r;
    sStatusG = g;
    sStatusB = b;
}

void Practice_Hud_Update(void) {
    s32 frameDelta;
    Player* player;

    if (sStatusTimer > 0) {
        sStatusTimer--;
    }

    if (gPracticeConfig.showChargeShotMeter && (gGameState == GSTATE_PLAY) && (gPlayState == PLAY_UPDATE)) {
        Practice_ChargeShotHud_Tick();
    }

    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }

    if (gPracticeConfig.showLevelTimers && gCurrentLevel == LEVEL_SECTOR_Z) {
        s32 j;
        Actor* sz_actor;
        for (j = 0, sz_actor = &gActors[0]; j < ARRAY_COUNT(gActors); j++, sz_actor++) {
            if (sz_actor->obj.status != OBJ_ACTIVE) { continue; }
            if (sz_actor->obj.id != OBJ_ACTOR_SZ_SPACE_JUNK) { continue; }
            if (j >= (s32)ARRAY_COUNT(gRadarMarks)) { continue; }
            gRadarMarks[j].enabled = true;
            gRadarMarks[j].type = AI360_ENEMY;
            gRadarMarks[j].pos.x = sz_actor->obj.pos.x;
            gRadarMarks[j].pos.y = sz_actor->obj.pos.y;
            gRadarMarks[j].pos.z = sz_actor->obj.pos.z;
            gRadarMarks[j].yRot = 0.0f;
        }
    }

    if (!gPracticeConfig.showHudOverlay) {
        return;
    }

    player = &gPlayer[0];

    if (sLastGameFrame >= 0) {
        frameDelta = gGameFrameCount - sLastGameFrame;

        if (frameDelta > 1) {
            sLagFrameCount += (frameDelta - 1);

            if (gPracticeConfig.showMissedInputs) {
                OSContPad* hold = &gControllerHold[gMainController];
                if (hold->button & (A_BUTTON | B_BUTTON)) {
                    sMissedInputCount += (frameDelta - 1);
                }
            }
        }
    }
    sLastGameFrame = gGameFrameCount;

    if (gPracticeConfig.showChargeTiming) {
        s32 chargeTimer = gChargeTimers[0];
        PlayerShot* csSlot = &gPlayerShots[14];

        if (csSlot->obj.status == SHOT_ACTIVE &&
            csSlot->obj.id == PLAYERSHOT_LOCK_ON &&
            csSlot->scale > 1.5f &&
            sCSImpactFrame < 0) {
            sCSImpactFrame = gGameFrameCount;
        }

        if ((sLastChargeTimer > 10) && (chargeTimer == 0)) {
            if (sChargeCount > 0 && sChargeStartFrame > 0) {
                sLastChargeShotGap = gGameFrameCount - sChargeStartFrame;
            }
            if (sCSImpactFrame >= 0) {
                sLastCSTimingOffset = gGameFrameCount - sCSImpactFrame - 2;
                sCSTimingValid = true;
            }
            sCSImpactFrame = -1;
            sChargeStartFrame = gGameFrameCount;
            sChargeCount++;
        }
        sLastChargeTimer = chargeTimer;
    }
}

void Practice_Hud_Draw(void) {
    s32 y;
    s32 labelX;
    s32 valueX;
    s32 lineCount = 0;
    bool levelTimerHasContent = false;

    /* Draw before the PLAY_UPDATE guard so both remain visible while frozen
     * or during PLAY_PAUSE (e.g. SD save/load results from the pause menu). */
    if ((gGameState == GSTATE_PLAY) && Practice_FrameAdvance_IsPaused()) {
        Practice_DrawTextColor(8, 8, "PAUSED", 255, 220, 60);
    }

    if ((sStatusTimer > 0) && (sStatusText != NULL)) {
        Practice_DrawBox(74, 36, 172, 14, 0, 0, 0, 170);
        Practice_DrawTextColor(80, 39, sStatusText, sStatusR, sStatusG, sStatusB);
    }

    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }

    if (!gPracticeConfig.showHudOverlay) {
        return;
    }

    labelX = HUD_X + 4;
    valueX = HUD_X + 80;

    if (gPracticeConfig.showLagFrames) { lineCount++; }
    if (gPracticeConfig.showSpeed) { lineCount++; }
    if (gPracticeConfig.showChargeTiming) { lineCount += 3; }
    if (gPracticeConfig.showChargeShotMeter) { lineCount++; }
    if (gPracticeConfig.showMissedInputs) { lineCount++; }
    if (gPracticeConfig.showHitTracking) { lineCount += 6; }
    if (gPracticeConfig.showLevelTimers) {
        if (gCurrentLevel == LEVEL_FORTUNA) {
            levelTimerHasContent = ((gAllRangeSpawnEvent - 500) - gAllRangeEventTimer > 0);
        } else if (gCurrentLevel == LEVEL_SECTOR_Z) {
            s32 i;
            for (i = 10; i <= 12; i++) {
                if (gActors[i].obj.status != OBJ_FREE && gActors[i].aiType == AI360_MISSILE) {
                    levelTimerHasContent = true;
                    break;
                }
            }
        }
        if (levelTimerHasContent) { lineCount++; }
    }

    if (lineCount == 0) {
        return;
    }

    Practice_DrawBox(HUD_X, HUD_Y, 120, 4 + (lineCount * HUD_LINE_H), 0, 0, 0, 160);

    y = HUD_Y + 2;

    if (gPracticeConfig.showSpeed) {
        Practice_DrawTextColor(labelX, y, "SPEED:", 180, 180, 180);
        if (Practice_FloatIsValid(&gPlayer[0].baseSpeed) && Practice_FloatIsValid(&gPlayer[0].boostSpeed)) {
            Practice_DrawFloat(valueX, y, gPlayer[0].baseSpeed + gPlayer[0].boostSpeed, 1);
        } else {
            Practice_DrawText(valueX, y, "---");
        }
        y += HUD_LINE_H;
    }

    if (gPracticeConfig.showLagFrames) {
        Practice_DrawTextColor(labelX, y, "LAG:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, sLagFrameCount);
        y += HUD_LINE_H;
    }

    if (gPracticeConfig.showChargeTiming) {
        Practice_DrawTextColor(labelX, y, "CS GAP:", 180, 180, 180);
        if (sLastChargeShotGap > 0) {
            Practice_DrawNumber(valueX, y, sLastChargeShotGap);
            Practice_DrawText(valueX + 30, y, "F");
        } else {
            Practice_DrawTextColor(valueX, y, "--", 120, 120, 120);
        }
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "CS CNT:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, sChargeCount);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "CS TIM:", 180, 180, 180);
        if (sCSTimingValid) {
            s32 off = sLastCSTimingOffset;
            char buf[8];
            s32 bi = 0;
            s32 absOff = off < 0 ? -off : off;
            u8 tr, tg, tb;

            if (off < 0) {
                buf[bi++] = '-';
                tr = 255; tg = 80; tb = 80;
            } else if (off == 0) {
                tr = 80; tg = 255; tb = 80;
            } else {
                tr = 255; tg = 200; tb = 0;
            }

            if (absOff == 0) {
                buf[bi++] = '0';
            } else {
                char tmp[6];
                s32 ti = 0;
                s32 v = absOff;
                while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                while (ti > 0) { buf[bi++] = tmp[--ti]; }
            }
            buf[bi++] = 'F';
            buf[bi] = '\0';

            Practice_DrawTextColor(valueX, y, buf, tr, tg, tb);
        } else {
            Practice_DrawTextColor(valueX, y, "----", 80, 80, 80);
        }
        y += HUD_LINE_H;
    }

    if (gPracticeConfig.showChargeShotMeter) {
        Practice_ChargeShotHud_DrawLine(labelX, valueX, y);
        y += HUD_LINE_H;
    }

    if (gPracticeConfig.showMissedInputs) {
        Practice_DrawTextColor(labelX, y, "MISSED:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, sMissedInputCount);
        y += HUD_LINE_H;
    }

    if (gPracticeConfig.showHitTracking) {
        Practice_DrawTextColor(labelX, y, "KILLS:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeStats.kills);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "BONUS:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeStats.csBonus);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "DIRECT:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeStats.directHits);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "ESCAPE:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeStats.escapes);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "CRASH:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeStats.crashes);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "TEAM:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeStats.teamKills);
        y += HUD_LINE_H;
    }

    if (levelTimerHasContent) {
        if (gCurrentLevel == LEVEL_FORTUNA) {
            Practice_DrawTextColor(labelX, y, "SPAWN:", 180, 180, 180);
            Practice_DrawNumber(valueX, y, (gAllRangeSpawnEvent - 500) - gAllRangeEventTimer);
            y += HUD_LINE_H;
        } else if (gCurrentLevel == LEVEL_SECTOR_Z) {
            s32 i;
            f32 minDist = -1.0f;
            for (i = 10; i <= 12; i++) {
                if (gActors[i].obj.status != OBJ_FREE && gActors[i].aiType == AI360_MISSILE) {
                    f32 raw = gActors[i].obj.pos.z - gBosses[0].obj.pos.z - 800.0f;
                    if (raw < 0.0f) { raw = 0.0f; }
                    if (minDist < 0.0f || raw < minDist) {
                        minDist = raw;
                    }
                }
            }
            Practice_DrawTextColor(labelX, y, "MISS:", 180, 180, 180);
            Practice_DrawNumber(valueX, y, minDist >= 0.0f ? (s32)minDist : 0);
            y += HUD_LINE_H;
        }
    }

}

#endif
