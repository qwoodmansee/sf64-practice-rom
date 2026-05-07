#include "practice.h"
#include "practice_overlay.h"

#ifdef PRACTICE_ROM

typedef enum MacroState {
    MACRO_IDLE,
    MACRO_ARMED,
    MACRO_RECORDING,
    MACRO_PLAYING,
} MacroState;

static MacroState sMacroState = MACRO_IDLE;
static s32        sMacroHead  = 0;
static s32        sMacroLen   = 0;
static u16        sPrevButton = 0;

/* State-bind: level/phase recorded alongside the macro snap. */
static bool       sMacroSnapValid       = false;
static s32        sMacroSnapLevel       = 0;
static s32        sMacroSnapPhase       = 0;
static bool       sMacroRestorePending  = false;
static bool       sMacroBufFull         = false;

static bool Macro_HasPak(void) {
    return osMemSize >= 0x00800000U;
}

void Practice_Macro_Init(void) {
    sMacroState          = MACRO_IDLE;
    sMacroHead           = 0;
    sMacroLen            = 0;
    sPrevButton          = 0;
    sMacroSnapValid      = false;
    sMacroSnapLevel      = 0;
    sMacroSnapPhase      = 0;
    sMacroRestorePending = false;
    sMacroBufFull        = false;
}

/* Called from fox_game.c immediately before Play_Main() so injected inputs
 * are visible to game logic on the same frame. */
void Practice_Macro_PrePlay(void) {
    MacroFrame* buf;
    s32 cap;
    OSContPad* hold;
    OSContPad* press;
    MacroFrame f;

    if (!Macro_HasPak()) {
        return;
    }
    if (gPracticeMenuState != PMENU_CLOSED) {
        return;
    }
    if (gGameState != GSTATE_PLAY || gPlayState != PLAY_UPDATE) {
        return;
    }

    buf = Practice_Macro_BufBase();
    cap = Practice_Macro_BufCapacity();

    if (sMacroState == MACRO_ARMED) {
        hold = &gControllerHold[gMainController];
        if (hold->button != 0 || hold->stick_x != 0 || hold->stick_y != 0) {
            if (sMacroHead >= cap) {
                /* Append-from-full: nothing to write. Drop back to IDLE and
                 * surface the FULL indicator so the user knows why nothing
                 * recorded. Without this guard we would write past the end
                 * of the macro frame buffer. */
                sMacroState   = MACRO_IDLE;
                sMacroBufFull = true;
                return;
            }
            if (sMacroHead == 0) {
                /* Fresh record: optionally snap state at this start point. */
                if (gPracticeConfig.macroBindState &&
                    practice_overlay_is_saveable((LevelId)gCurrentLevel)) {
                    Practice_Save_MacroSnap();
                    sMacroSnapLevel = (s32)gCurrentLevel;
                    sMacroSnapPhase = (s32)gLevelPhase;
                    sMacroSnapValid = true;
                }
            }
            /* Append mode (head > 0 after trim): keep existing snap and frames. */
            sMacroState = MACRO_RECORDING;
            buf[sMacroHead].button  = hold->button;
            buf[sMacroHead].stick_x = hold->stick_x;
            buf[sMacroHead].stick_y = hold->stick_y;
            sMacroHead++;
            sMacroLen = sMacroHead;
        }
        /* If no input yet, caller sees IsArmed() == true and skips Play_Main. */
        return;
    } else if (sMacroState == MACRO_PLAYING) {
        /* If a state restore is pending, wait for the target scene before
         * injecting frames. Apply once the destination reaches PLAY_UPDATE. */
        if (sMacroRestorePending) {
            if ((s32)gCurrentLevel == sMacroSnapLevel &&
                gPlayer != NULL &&
                !gPracticeSaveDisabled) {
                Practice_Save_MacroApply();
                sMacroRestorePending = false;
            }
            return;
        }
    }

    if (sMacroState == MACRO_RECORDING) {
        if (sMacroHead < cap) {
            hold = &gControllerHold[gMainController];
            buf[sMacroHead].button  = hold->button;
            buf[sMacroHead].stick_x = hold->stick_x;
            buf[sMacroHead].stick_y = hold->stick_y;
            sMacroHead++;
            sMacroLen = sMacroHead;
        } else {
            /* Buffer full -- stop recording automatically. */
            sMacroState  = MACRO_IDLE;
            sMacroBufFull = true;
        }
    } else if (sMacroState == MACRO_PLAYING) {
        /* The caller (fox_game.c GSTATE_PLAY) only invokes PrePlay when the
         * frame is actually going to advance, so we don't re-check
         * Practice_FrameAdvance_IsFrozen() here -- doing so would consume an
         * additional queued frame-step and starve Play_Main on the same tick. */
        if (sMacroHead < sMacroLen) {
            f = buf[sMacroHead];
            hold  = &gControllerHold[gMainController];
            press = &gControllerPress[gMainController];

            hold->button  = f.button;
            hold->stick_x = f.stick_x;
            hold->stick_y = f.stick_y;

            /* Recompute press from the previous injected frame, not the real
             * hardware read, so button-press events fire correctly on playback. */
            press->button  = (f.button ^ sPrevButton) & f.button;
            press->stick_x = f.stick_x;
            press->stick_y = f.stick_y;

            sPrevButton = f.button;
            sMacroHead++;
        } else if (sMacroHead >= sMacroLen) {
            if (gPracticeConfig.macroLoop) {
                sMacroHead  = 0;
                sPrevButton = 0;
                if (gPracticeConfig.macroBindState && sMacroSnapValid) {
                    sMacroRestorePending = true;
                    if ((s32)gCurrentLevel != sMacroSnapLevel ||
                        (s32)gLevelPhase   != sMacroSnapPhase) {
                        practice_overlay_request_load((LevelId)sMacroSnapLevel, sMacroSnapPhase);
                    }
                }
            } else {
                sMacroState = MACRO_IDLE;
                sPrevButton = 0;
            }
        }
    }
}

void Practice_Macro_Update(void) {
    OSContPad* hold;
    OSContPad* press;

    if (!Macro_HasPak()) {
        /* No Expansion Pak: macro buffers are unmapped. Skip the hotkey
         * handler entirely so we never swallow Start, toggle macroLoop,
         * etc. on stock 4 MB systems. */
        return;
    }
    if (gGameState != GSTATE_PLAY) {
        return;
    }
    if (gPracticeMenuState != PMENU_CLOSED) {
        return;
    }

    hold  = &gControllerHold[gMainController];
    press = &gControllerPress[gMainController];

    if (!(hold->button & L_TRIG)) {
        return;
    }

    /* L + D-Up : arm/stop recording */
    if (press->button & U_JPAD) {
        if (Practice_Macro_IsArmed() || Practice_Macro_IsRecording()) {
            Practice_Macro_StopRecord();
        } else {
            Practice_Macro_StartRecord();
        }
        return;
    }

    /* L + D-Right : start/stop playback */
    if (press->button & R_JPAD) {
        if (Practice_Macro_IsPlaying()) {
            Practice_Macro_StopPlay();
        } else {
            Practice_Macro_StartPlay();
        }
        return;
    }

    /* L + D-Left : rewind to frame 0 */
    if (press->button & L_JPAD) {
        Practice_Macro_Rewind();
        return;
    }

    /* L + D-Down : toggle loop */
    if (press->button & D_JPAD) {
        gPracticeConfig.macroLoop = !gPracticeConfig.macroLoop;
        return;
    }

    /* L + Start : replay (rewind + play); swallow Start so game doesn't pause */
    if (press->button & START_BUTTON) {
        press->button &= ~(u16)START_BUTTON;
        Practice_Macro_StartPlay();
        return;
    }
}

void Practice_Macro_Draw(void) {
    s32 recR;
    s32 recG;
    s32 recB;

    if (!Macro_HasPak()) {
        return;
    }
    if (gPracticeScreen != PSCREEN_GAMEPLAY) {
        return;
    }
    if (sMacroState == MACRO_ARMED) {
        Practice_DrawTextColor(240, 8, "REC:", 255, 140, 0);
        Practice_DrawNumber(272, 8, sMacroHead);
    } else if (sMacroState == MACRO_RECORDING) {
        /* Amber when >80% full, red otherwise. */
        if (sMacroHead > Practice_Macro_BufCapacity() * 4 / 5) {
            recR = 255; recG = 140; recB = 0;
        } else {
            recR = 255; recG = 60; recB = 60;
        }
        Practice_DrawTextColor(240, 8, "REC:", (u8)recR, (u8)recG, (u8)recB);
        Practice_DrawNumber(272, 8, sMacroHead);
    } else if (sMacroState == MACRO_PLAYING) {
        Practice_DrawTextColor(232, 8, "PLAY:", 60, 220, 255);
        Practice_DrawNumber(264, 8, sMacroHead);
        Practice_DrawTextColor(282, 8, "-", 60, 220, 255);
        Practice_DrawNumber(288, 8, sMacroLen);
    } else if (sMacroBufFull) {
        Practice_DrawTextColor(240, 8, "FULL", 255, 60, 60);
    }
}

void Practice_Macro_StartRecord(void) {
    if (!Macro_HasPak()) {
        return;
    }
    if (sMacroHead == sMacroLen && sMacroLen > 0) {
        /* Append mode: at trim point, keep existing frames and snap. */
        sMacroState   = MACRO_ARMED;
        sPrevButton   = 0;
        sMacroBufFull = false;
    } else {
        /* Fresh record: discard everything. */
        sMacroState   = MACRO_ARMED;
        sMacroHead    = 0;
        sMacroLen     = 0;
        sPrevButton   = 0;
        sMacroBufFull = false;
    }
}

void Practice_Macro_Trim(void) {
    if (sMacroLen == 0 || sMacroHead > sMacroLen) {
        return;
    }
    sMacroLen = sMacroHead;
    if (sMacroLen == 0) {
        /* Trimmed to zero = full clear; snap is no longer valid. */
        sMacroSnapValid = false;
        sMacroBufFull   = false;
    }
}

void Practice_Macro_StopRecord(void) {
    if (sMacroState == MACRO_ARMED || sMacroState == MACRO_RECORDING) {
        sMacroState = MACRO_IDLE;
    }
}

void Practice_Macro_StartPlay(void) {
    if (!Macro_HasPak() || sMacroLen == 0) {
        return;
    }
    sMacroState = MACRO_PLAYING;
    sMacroHead  = 0;
    sPrevButton = 0;

    if (gPracticeConfig.macroBindState && sMacroSnapValid) {
        sMacroRestorePending = true;
        if ((s32)gCurrentLevel != sMacroSnapLevel ||
            (s32)gLevelPhase   != sMacroSnapPhase) {
            practice_overlay_request_load((LevelId)sMacroSnapLevel, sMacroSnapPhase);
        }
    }
}

void Practice_Macro_StopPlay(void) {
    if (sMacroState == MACRO_PLAYING) {
        sMacroState          = MACRO_IDLE;
        sPrevButton          = 0;
        sMacroRestorePending = false;
    }
}

void Practice_Macro_Rewind(void) {
    sMacroHead  = 0;
    sPrevButton = 0;
}

bool Practice_Macro_IsArmed(void) {
    return sMacroState == MACRO_ARMED;
}

bool Practice_Macro_IsRecording(void) {
    return sMacroState == MACRO_RECORDING;
}

bool Practice_Macro_IsPlaying(void) {
    return sMacroState == MACRO_PLAYING;
}

bool Practice_Macro_HasData(void) {
    return sMacroLen > 0;
}

s32 Practice_Macro_GetHead(void) {
    return sMacroHead;
}

s32 Practice_Macro_GetLen(void) {
    return sMacroLen;
}

s32 Practice_Macro_GetSnapLevel(void) {
    return sMacroSnapValid ? sMacroSnapLevel : -1;
}

#endif
