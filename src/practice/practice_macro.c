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
            /* First real input -- optionally snap state, then start recording. */
            if (gPracticeConfig.macroBindState &&
                practice_overlay_is_saveable((LevelId)gCurrentLevel)) {
                Practice_Save_MacroSnap();
                sMacroSnapLevel = (s32)gCurrentLevel;
                sMacroSnapPhase = (s32)gLevelPhase;
                sMacroSnapValid = true;
            }
            sMacroState = MACRO_RECORDING;
            buf[0].button  = hold->button;
            buf[0].stick_x = hold->stick_x;
            buf[0].stick_y = hold->stick_y;
            sMacroHead = 1;
            sMacroLen  = 1;
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
            sMacroState = MACRO_IDLE;
        }
    } else if (sMacroState == MACRO_PLAYING) {
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
        } else {
            sMacroState = MACRO_IDLE;
            sPrevButton = 0;
        }
    }
}

void Practice_Macro_Update(void) {
    /* Reserved for future per-frame work. */
}

void Practice_Macro_Draw(void) {
    if (!Macro_HasPak()) {
        return;
    }
    if (gPracticeScreen != PSCREEN_GAMEPLAY) {
        return;
    }
    if (sMacroState == MACRO_ARMED) {
        Practice_DrawTextColor(240, 8, "REC:", 255, 140, 0);
        Practice_DrawNumber(272, 8, 0);
    } else if (sMacroState == MACRO_RECORDING) {
        Practice_DrawTextColor(240, 8, "REC:", 255, 60, 60);
        Practice_DrawNumber(272, 8, sMacroHead);
    } else if (sMacroState == MACRO_PLAYING) {
        Practice_DrawTextColor(240, 8, "PLAY:", 60, 220, 255);
        Practice_DrawNumber(276, 8, sMacroHead);
    }
}

void Practice_Macro_StartRecord(void) {
    if (!Macro_HasPak()) {
        return;
    }
    sMacroState = MACRO_ARMED;
    sMacroHead  = 0;
    sMacroLen   = 0;
    sPrevButton = 0;
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

#endif
