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
s32 gPracticeDirectHits = 0;
s32 gPracticeIndirectCount = 0;
s32 gPracticeIndirectBonus = 0;
s32 gPracticeDespawns = 0;

void Practice_Hud_Reset(void) {
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
    gPracticeDirectHits = 0;
    gPracticeIndirectCount = 0;
    gPracticeIndirectBonus = 0;
    gPracticeDespawns = 0;
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

    if (!gPracticeConfig.showHudOverlay) {
        return;
    }
    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
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

    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }

    if ((sStatusTimer > 0) && (sStatusText != NULL)) {
        Practice_DrawBox(74, 36, 172, 14, 0, 0, 0, 170);
        Practice_DrawTextColor(80, 39, sStatusText, sStatusR, sStatusG, sStatusB);
    }

    if (!gPracticeConfig.showHudOverlay) {
        return;
    }

    labelX = HUD_X + 4;
    valueX = HUD_X + 80;

    if (gPracticeConfig.showLagFrames) { lineCount++; }
    if (gPracticeConfig.showSpeed) { lineCount++; }
    if (gPracticeConfig.showChargeTiming) { lineCount += 3; }
    if (gPracticeConfig.showMissedInputs) { lineCount++; }
    if (gPracticeConfig.showHitTracking) { lineCount += 4; }

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

    if (gPracticeConfig.showMissedInputs) {
        Practice_DrawTextColor(labelX, y, "MISSED:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, sMissedInputCount);
        y += HUD_LINE_H;
    }

    if (gPracticeConfig.showHitTracking) {
        Practice_DrawTextColor(labelX, y, "DIRECT:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeDirectHits);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "INDRCT:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeIndirectCount);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, " BONUS:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeIndirectBonus);
        y += HUD_LINE_H;

        Practice_DrawTextColor(labelX, y, "DESPWN:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, gPracticeDespawns);
        y += HUD_LINE_H;
    }

}

#endif
