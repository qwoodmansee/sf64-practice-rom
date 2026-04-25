#include "practice.h"

#ifdef PRACTICE_ROM

typedef struct PracticeBinding {
    u16 modifier;
    u16 button;
} PracticeBinding;

static PracticeBinding sPracticeBindings[PACTION_MAX] = {
    { R_TRIG, R_JPAD },  /* PACTION_OPEN_MENU:   R + D-Pad Right */
    { L_TRIG, L_JPAD },  /* PACTION_SAVE_POS:    L + D-Pad Left */
    { L_TRIG, R_JPAD },  /* PACTION_RESTORE_POS: L + D-Pad Right */
};

static const char* sPracticeActionNames[PACTION_MAX] = {
    "MENU",
    "SAVE POS",
    "LOAD POS",
};

bool Practice_InputTriggered(PracticeAction action) {
    OSContPad* press = &gControllerPress[gMainController];
    OSContPad* hold = &gControllerHold[gMainController];
    u16 mod = sPracticeBindings[action].modifier;
    u16 btn = sPracticeBindings[action].button;

    if ((hold->button & mod) && (press->button & btn)) {
        return true;
    }
    if ((hold->button & btn) && (press->button & mod)) {
        return true;
    }
    return false;
}

u16 Practice_GetBinding(PracticeAction action) {
    return sPracticeBindings[action].button;
}

void Practice_SetBinding(PracticeAction action, u16 button) {
    sPracticeBindings[action].button = button;
}

const char* Practice_GetActionName(PracticeAction action) {
    return sPracticeActionNames[action];
}

const char* Practice_GetDPadName(u16 button) {
    switch (button) {
        case U_JPAD: return "D-UP";
        case D_JPAD: return "D-DOWN";
        case L_JPAD: return "D-LEFT";
        case R_JPAD: return "D-RIGHT";
        default:     return "???";
    }
}

#endif
