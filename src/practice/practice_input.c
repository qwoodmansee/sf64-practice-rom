#include "practice.h"

#ifdef PRACTICE_ROM

static u16 sPracticeBindings[PACTION_MAX] = {
    U_JPAD,  /* PACTION_OPEN_MENU_FROZEN: L + D-Pad Up */
    D_JPAD,  /* PACTION_OPEN_MENU:        L + D-Pad Down */
    L_JPAD,  /* PACTION_SAVE_POS:         L + D-Pad Left */
    R_JPAD,  /* PACTION_RESTORE_POS:      L + D-Pad Right */
};

static const char* sPracticeActionNames[PACTION_MAX] = {
    "MENU (FROZEN)",
    "MENU",
    "SAVE POS",
    "LOAD POS",
};

bool Practice_InputTriggered(PracticeAction action) {
    OSContPad* press = &gControllerPress[gMainController];
    OSContPad* hold = &gControllerHold[gMainController];

    if (!(hold->button & L_TRIG)) {
        return false;
    }
    return (press->button & sPracticeBindings[action]) != 0;
}

u16 Practice_GetBinding(PracticeAction action) {
    return sPracticeBindings[action];
}

void Practice_SetBinding(PracticeAction action, u16 button) {
    sPracticeBindings[action] = button;
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
