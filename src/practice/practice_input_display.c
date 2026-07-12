#include "practice.h"

#ifdef PRACTICE_ROM

/* Panel spans (baseX-4 .. baseX+114, baseY-14 .. baseY+24). Keep >=12px off
 * the left/bottom screen edges so CRT overscan doesn't clip it. */
#define INPUT_DISP_X 16
#define INPUT_DISP_Y 190

static void Practice_InputDisplay_DrawStick(s32 cx, s32 cy, s32 radius, s8 stickX, s8 stickY) {
    s32 dotX;
    s32 dotY;

    Practice_DrawBox(cx - radius, cy - radius, radius * 2, radius * 2, 40, 40, 40, 160);

    Practice_DrawBox(cx - radius, cy, radius * 2, 1, 80, 80, 80, 160);
    Practice_DrawBox(cx, cy - radius, 1, radius * 2, 80, 80, 80, 160);

    dotX = cx + (stickX * (radius - 2)) / 80;
    dotY = cy - (stickY * (radius - 2)) / 80;
    Practice_DrawBox(dotX - 1, dotY - 1, 3, 3, 255, 50, 50, 255);
}

static void Practice_InputDisplay_DrawButton(s32 x, s32 y, const char* label, bool pressed) {
    if (pressed) {
        Practice_DrawBox(x, y, 8, 8, 255, 255, 255, 200);
        Practice_DrawTextColor(x + 1, y, label, 0, 0, 0);
    } else {
        Practice_DrawBox(x, y, 8, 8, 60, 60, 60, 160);
        Practice_DrawTextColor(x + 1, y, label, 120, 120, 120);
    }
}

static void Practice_InputDisplay_DrawTrigger(s32 x, s32 y, s32 w, const char* label, bool pressed) {
    if (pressed) {
        Practice_DrawBox(x, y, w, 6, 200, 200, 255, 220);
    } else {
        Practice_DrawBox(x, y, w, 6, 60, 60, 60, 160);
    }
    Practice_DrawTextColor(x + 1, y - 1, label, 180, 180, 180);
}

void Practice_InputDisplay_Draw(void) {
    OSContPad* hold = &gControllerHold[gMainController];
    u16 btn = hold->button;

    s32 baseX = INPUT_DISP_X;
    s32 baseY = INPUT_DISP_Y;

    Practice_DrawBox(baseX - 4, baseY - 14, 118, 38, 0, 0, 0, 140);

    Practice_InputDisplay_DrawTrigger(baseX, baseY - 11, 18, "L", (btn & L_TRIG) != 0);
    Practice_InputDisplay_DrawTrigger(baseX + 90, baseY - 11, 18, "R", (btn & R_TRIG) != 0);
    Practice_InputDisplay_DrawTrigger(baseX + 50, baseY - 11, 14, "Z", (btn & Z_TRIG) != 0);

    Practice_InputDisplay_DrawStick(baseX + 14, baseY + 10, 12, hold->stick_x, hold->stick_y);

    Practice_InputDisplay_DrawButton(baseX + 74, baseY + 2, "A", (btn & A_BUTTON) != 0);
    Practice_InputDisplay_DrawButton(baseX + 64, baseY + 10, "B", (btn & B_BUTTON) != 0);

    Practice_InputDisplay_DrawButton(baseX + 50, baseY - 2, "S", (btn & START_BUTTON) != 0);

    Practice_InputDisplay_DrawButton(baseX + 94, baseY - 2, "U", (btn & U_CBUTTONS) != 0);
    Practice_InputDisplay_DrawButton(baseX + 94, baseY + 14, "D", (btn & D_CBUTTONS) != 0);
    Practice_InputDisplay_DrawButton(baseX + 84, baseY + 6, "L", (btn & L_CBUTTONS) != 0);
    Practice_InputDisplay_DrawButton(baseX + 104, baseY + 6, "R", (btn & R_CBUTTONS) != 0);

    Practice_DrawTextColor(baseX + 34, baseY + 2, "U", (btn & U_JPAD) ? 255 : 80, (btn & U_JPAD) ? 255 : 80, (btn & U_JPAD) ? 255 : 80);
    Practice_DrawTextColor(baseX + 34, baseY + 16, "D", (btn & D_JPAD) ? 255 : 80, (btn & D_JPAD) ? 255 : 80, (btn & D_JPAD) ? 255 : 80);
    Practice_DrawTextColor(baseX + 28, baseY + 9, "L", (btn & L_JPAD) ? 255 : 80, (btn & L_JPAD) ? 255 : 80, (btn & L_JPAD) ? 255 : 80);
    Practice_DrawTextColor(baseX + 40, baseY + 9, "R", (btn & R_JPAD) ? 255 : 80, (btn & R_JPAD) ? 255 : 80, (btn & R_JPAD) ? 255 : 80);
}

#endif
