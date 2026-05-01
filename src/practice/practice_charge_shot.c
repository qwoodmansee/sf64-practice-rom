#include "practice.h"

#ifdef PRACTICE_ROM

/* Charge-shot meter (bar under reticle) + tap timing on left HUD ("nF").
 * Timing: first frame timer crosses 10->11 while holding A records sChargeReadyFrame.
 * Minimum frames from that instant to a successful charge shot is 2 (one release
 * frame for a new A press edge, then press). Late = firedFrame - readyFrame - 2. */

/* Bar fills to 100% at the first fireable charge (timer > 10, i.e. 11+). Stock 3D orb
 * uses /20.0f for scale while charging; that is cosmetic headroom, not fire timing. */
#define CHARGE_FIRE_THRESHOLD 10
#define CHARGE_BAR_FIRE_READY (CHARGE_FIRE_THRESHOLD + 1)
#define CHARGE_TIMING_DISPLAY_FRAMES 90

static s32 sChargeReadyFrame = -1;
static s32 sTimingShowFrames = 0;
static s32 sTimingValue = 0; /* early: negative count; late/perfect: nonnegative frame offset */
static bool sTimingIsEarly = false;

static bool sAutoReleasePending = false;
static s32 sAutoPhase = 0; /* 0 idle, 1 pending strip next frame, 2 stripped waiting re-press fire */

static bool Practice_ChargeAssist_PlayerOk(Player* player) {
    if (player->num != 0 || gVersusMode) {
        return false;
    }
    if ((player->form != FORM_ARWING) && (player->form != FORM_LANDMASTER)) {
        return false;
    }
    return true;
}

static void Practice_ChargeAssist_ResetAutoState(void) {
    sAutoReleasePending = false;
    sAutoPhase = 0;
}

void Practice_ChargeAssist_Reset(void) {
    sChargeReadyFrame = -1;
    sTimingShowFrames = 0;
    sTimingValue = 0;
    sTimingIsEarly = false;
    Practice_ChargeAssist_ResetAutoState();
}

void Practice_ChargeAssist_LockOnBegin(Player* player) {
    if (!gPracticeConfig.showChargeShotMeter && !gPracticeConfig.autoFireChargeShot) {
        return;
    }
    if (!Practice_ChargeAssist_PlayerOk(player)) {
        return;
    }

    if (gChargeTimers[0] < 10) {
        sChargeReadyFrame = -1;
    }
    if (gChargeTimers[0] == 0) {
        Practice_ChargeAssist_ResetAutoState();
    }

    if (gPracticeConfig.autoFireChargeShot && sAutoReleasePending && (gInputHold->button & A_BUTTON) &&
        (gChargeTimers[player->num] > CHARGE_FIRE_THRESHOLD)) {
        gInputHold->button &= ~A_BUTTON;
        sAutoReleasePending = false;
        sAutoPhase = 2;
    }
}

void Practice_ChargeAssist_PreChargeInc(Player* player) {
    if (!gPracticeConfig.autoFireChargeShot) {
        return;
    }
    if (!Practice_ChargeAssist_PlayerOk(player)) {
        return;
    }
    if (!(gInputHold->button & A_BUTTON)) {
        return;
    }

    if (gChargeTimers[player->num] == CHARGE_FIRE_THRESHOLD) {
        if (sAutoPhase == 0) {
            sAutoReleasePending = true;
            sAutoPhase = 1;
        }
    }
}

void Practice_ChargeAssist_OnChargeShotFired(Player* player) {
    s32 late;

    Practice_ChargeAssist_ResetAutoState();

    if (gPracticeConfig.autoFireChargeShot && Practice_ChargeAssist_PlayerOk(player) &&
        (player->form == FORM_ARWING)) {
        /* Stop hyper/single laser burst from carrying across an auto charge shot. */
        player->shotTimer = 0;
    }

    if (!gPracticeConfig.showChargeShotMeter) {
        sChargeReadyFrame = -1;
        return;
    }
    if (!Practice_ChargeAssist_PlayerOk(player)) {
        return;
    }

    if (sChargeReadyFrame >= 0) {
        late = gGameFrameCount - sChargeReadyFrame - 2;
        if (late < 0) {
            late = 0;
        }
        sTimingIsEarly = false;
        sTimingValue = late;
        sTimingShowFrames = CHARGE_TIMING_DISPLAY_FRAMES;
    }
    sChargeReadyFrame = -1;
}

void Practice_ChargeAssist_OnChargeShotBlocked(Player* player) {
    if (!gPracticeConfig.autoFireChargeShot || !Practice_ChargeAssist_PlayerOk(player)) {
        return;
    }
    /* Player_UpdateLockOn falls through with timer > 10 but slot 14 busy; same-frame
     * gInputPress would otherwise trigger Player_ArwingLaser + shotTimer burst. */
    gInputPress->button &= ~gShootButton[player->num];
}

void Practice_ChargeAssist_OnChargeShotEarlyReset(Player* player, s32 timerBeforeZero) {
    s32 early;

    Practice_ChargeAssist_ResetAutoState();

    if (!gPracticeConfig.showChargeShotMeter) {
        sChargeReadyFrame = -1;
        return;
    }
    if (!Practice_ChargeAssist_PlayerOk(player)) {
        return;
    }

    if (timerBeforeZero <= CHARGE_FIRE_THRESHOLD) {
        early = (CHARGE_FIRE_THRESHOLD + 1) - timerBeforeZero;
        if (early < 1) {
            early = 1;
        }
        sTimingIsEarly = true;
        sTimingValue = -early;
        sTimingShowFrames = CHARGE_TIMING_DISPLAY_FRAMES;
    }
    sChargeReadyFrame = -1;
}

void Practice_ChargeAssist_PostChargeInc(Player* player) {
    if (!gPracticeConfig.showChargeShotMeter) {
        return;
    }
    if (!Practice_ChargeAssist_PlayerOk(player)) {
        return;
    }
    if (!(gInputHold->button & A_BUTTON)) {
        return;
    }
    if (gChargeTimers[player->num] == (CHARGE_FIRE_THRESHOLD + 1)) {
        if (sChargeReadyFrame < 0) {
            sChargeReadyFrame = gGameFrameCount;
        }
    }
}

void Practice_ChargeShotHud_Tick(void) {
    if (sTimingShowFrames > 0) {
        sTimingShowFrames--;
    }
}

static void Practice_ChargeShotHud_AppendDigitsThenF(char* buf, s32* pi, s32 v) {
    char tmp[8];
    s32 ti = 0;

    if (v == 0) {
        buf[(*pi)++] = '0';
    } else {
        while (v > 0) {
            tmp[ti++] = '0' + (v % 10);
            v /= 10;
        }
        while (ti > 0) {
            buf[(*pi)++] = tmp[--ti];
        }
    }
    buf[(*pi)++] = 'F';
    buf[*pi] = '\0';
}

void Practice_ChargeShotHud_DrawLine(s32 labelX, s32 valueX, s32 y) {
    char buf[12];
    s32 bi;
    s32 absEarly;

    if (!gPracticeConfig.showChargeShotMeter) {
        return;
    }

    Practice_DrawTextColor(labelX, y, "CS TAP:", 180, 180, 180);

    if (sTimingShowFrames > 0) {
        bi = 0;
        if (sTimingIsEarly) {
            absEarly = -sTimingValue;
            buf[bi++] = '-';
            Practice_ChargeShotHud_AppendDigitsThenF(buf, &bi, absEarly);
            Practice_DrawTextColor(valueX, y, buf, 255, 80, 80);
        } else {
            Practice_ChargeShotHud_AppendDigitsThenF(buf, &bi, sTimingValue);
            if (sTimingValue == 0) {
                Practice_DrawTextColor(valueX, y, buf, 80, 255, 80);
            } else {
                Practice_DrawTextColor(valueX, y, buf, 255, 200, 0);
            }
        }
    } else {
        Practice_DrawTextColor(valueX, y, "----", 120, 120, 120);
    }
}

void Practice_ChargeMeter_Draw(void) {
    s32 cx;
    s32 barY;
    s32 fillW;
    s32 t;
    s32 barW = 72;
    s32 barH = 6;
    s32 innerPad = 2;

    if (!gPracticeConfig.showChargeShotMeter) {
        return;
    }
    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }
    if (gPlayer == NULL) {
        return;
    }

    cx = SCREEN_WIDTH / 2;
    barY = (SCREEN_HEIGHT / 2) + 12;

    Practice_DrawBox(cx - (barW / 2) - 1, barY - 1, barW + 2, barH + 2, 0, 0, 0, 220);
    Practice_DrawBox(cx - (barW / 2), barY, barW, barH, 30, 30, 30, 240);

    t = gChargeTimers[0];
    if (t < 0) {
        t = 0;
    }
    if (t > CHARGE_BAR_FIRE_READY) {
        t = CHARGE_BAR_FIRE_READY;
    }
    fillW = (t * (barW - innerPad * 2)) / CHARGE_BAR_FIRE_READY;
    if (fillW > 0) {
        Practice_DrawBox(cx - (barW / 2) + innerPad, barY + innerPad, fillW, barH - innerPad * 2, 0, 220, 100, 255);
    }
}

#endif
