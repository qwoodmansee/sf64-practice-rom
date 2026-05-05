#include "practice.h"

#ifdef PRACTICE_ROM

/* sIsPaused:
 *   false = running normally
 *   true  = paused; Play_Main is skipped each tick unless D-Up has queued a
 *           frame step
 *
 * D-Down press  : toggle sIsPaused on/off
 * D-Up press    : if running, pause; if already paused, step one frame
 * D-Up hold     : after a short delay, continue stepping frames at a repeat
 *                 cadence
 * D-Left press  : save to active slot (only without L held)
 * D-Right press : load from active slot (only without L or Z held)
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
    OSContPad* press;
    OSContPad* hold;

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

    press = &gControllerPress[gMainController];
    hold  = &gControllerHold[gMainController];

    /* D-Down press: toggle pause */
    if (press->button & D_JPAD) {
        sIsPaused = !sIsPaused;
        sQueuedFrameSteps = 0;
        sFrameAdvanceHoldTimer = 0;
        sFrameAdvanceRepeatTimer = 0;
    }

    /* D-Up press: pause if running; step one frame if already paused */
    if (press->button & U_JPAD) {
        if (!sIsPaused) {
            sIsPaused = true;
            sQueuedFrameSteps = 0;
            sFrameAdvanceHoldTimer = 0;
            sFrameAdvanceRepeatTimer = 0;
        } else {
            sQueuedFrameSteps = 1;
            sFrameAdvanceHoldTimer = 0;
            sFrameAdvanceRepeatTimer = FRAME_ADVANCE_REPEAT_RATE;
        }
    }

    /* D-Left (no L): save to active slot */
    if ((press->button & L_JPAD) && !(hold->button & L_TRIG)) {
        Practice_SaveState();
    }

    /* D-Right (no L, no Z): load from active slot */
    if ((press->button & R_JPAD) && !(hold->button & L_TRIG) && !(hold->button & Z_TRIG)) {
        Practice_LoadState();
    }

    if (!sIsPaused || !(hold->button & U_JPAD)) {
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
