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
static s32 sLastCsState = 0;
static s32 sChargeStartFrame = 0;
static s32 sLastChargeShotGap = 0;
static s32 sChargeCount = 0;
static s32 sMissedInputCount = 0;
static s32 sLastInputPollFrame = -1;

void Practice_Hud_Reset(void) {
    sLastGameFrame = -1;
    sLagFrameCount = 0;
    sLastCsState = 0;
    sChargeStartFrame = 0;
    sLastChargeShotGap = 0;
    sChargeCount = 0;
    sMissedInputCount = 0;
    sLastInputPollFrame = -1;
}

void Practice_Hud_Update(void) {
    s32 frameDelta;
    Player* player;

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
        if (player->csState != 0 && sLastCsState == 0) {
            if (sChargeCount > 0 && sChargeStartFrame > 0) {
                sLastChargeShotGap = gGameFrameCount - sChargeStartFrame;
            }
            sChargeStartFrame = gGameFrameCount;
            sChargeCount++;
        }
        sLastCsState = player->csState;
    }
}

void Practice_Hud_Draw(void) {
    s32 y;
    s32 labelX;
    s32 valueX;
    s32 lineCount = 0;

    if (!gPracticeConfig.showHudOverlay) {
        return;
    }
    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }

    labelX = HUD_X + 4;
    valueX = HUD_X + 80;

    if (gPracticeConfig.showLagFrames) { lineCount++; }
    if (gPracticeConfig.showSpeed) { lineCount++; }
    if (gPracticeConfig.showChargeTiming) { lineCount += 2; }
    if (gPracticeConfig.showMissedInputs) { lineCount++; }

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
    }

    if (gPracticeConfig.showMissedInputs) {
        Practice_DrawTextColor(labelX, y, "MISSED:", 180, 180, 180);
        Practice_DrawNumber(valueX, y, sMissedInputCount);
        y += HUD_LINE_H;
    }
}

#endif
