#include "practice.h"

#ifdef PRACTICE_ROM

/* sIsPaused:
 *   false = running normally
 *   true  = paused; Play_Main is skipped each tick unless D-Up has queued a
 *           frame step
 *
 * D-Down press  : toggle sIsPaused on/off
 * D-Up press    : while paused, let exactly one Play_Main tick execute.
 * D-Up hold     : after a short delay, continue stepping frames at a repeat
 *                 cadence.
 * D-Up input    : no effect when not paused.
 */
#define FRAME_ADVANCE_REPEAT_DELAY 12
#define FRAME_ADVANCE_REPEAT_RATE 2

static bool sIsPaused = false;
static s32 sQueuedFrameSteps = 0;
static s32 sFrameAdvanceHoldTimer = 0;
static s32 sFrameAdvanceRepeatTimer = 0;

void Practice_FrameAdvance_Init(void) {
    sIsPaused = false;
    sQueuedFrameSteps = 0;
    sFrameAdvanceHoldTimer = 0;
    sFrameAdvanceRepeatTimer = 0;
}

void Practice_FrameAdvance_Update(void) {
    if (gGameState != GSTATE_PLAY) {
        return;
    }
    if (gPracticeMenuState != PMENU_CLOSED) {
        sIsPaused = false;
        sQueuedFrameSteps = 0;
        sFrameAdvanceHoldTimer = 0;
        sFrameAdvanceRepeatTimer = 0;
        return;
    }

    /* D-Down press: toggle pause */
    if (gControllerPress[gMainController].button & D_JPAD) {
        sIsPaused = !sIsPaused;
        sQueuedFrameSteps = 0;
        sFrameAdvanceHoldTimer = 0;
        sFrameAdvanceRepeatTimer = 0;
    }

    /* D-Up press: step one frame while paused */
    if (sIsPaused && (gControllerPress[gMainController].button & U_JPAD)) {
        sQueuedFrameSteps = 1;
        sFrameAdvanceHoldTimer = 0;
        sFrameAdvanceRepeatTimer = FRAME_ADVANCE_REPEAT_RATE;
    }

    if (!sIsPaused || !(gControllerHold[gMainController].button & U_JPAD)) {
        sFrameAdvanceHoldTimer = 0;
        sFrameAdvanceRepeatTimer = 0;
        return;
    }

    sFrameAdvanceHoldTimer++;
    if (sFrameAdvanceHoldTimer <= FRAME_ADVANCE_REPEAT_DELAY) {
        return;
    }

    sFrameAdvanceRepeatTimer--;
    if (sFrameAdvanceRepeatTimer <= 0) {
        sQueuedFrameSteps = 1;
        sFrameAdvanceRepeatTimer = FRAME_ADVANCE_REPEAT_RATE;
    }
}

bool Practice_FrameAdvance_IsFrozen(void) {
    if (!sIsPaused) {
        return false;
    }
    if (sQueuedFrameSteps > 0) {
        sQueuedFrameSteps--;
        return false;
    }
    return true;
}

bool Practice_FrameAdvance_IsPaused(void) {
    return sIsPaused;
}

#endif
