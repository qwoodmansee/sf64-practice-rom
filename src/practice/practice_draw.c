#include "practice.h"

#ifdef PRACTICE_ROM

void Practice_DrawBox(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    RCP_SetupDL(&gMasterDisp, SETUPDL_76);
    gDPSetRenderMode(gMasterDisp++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gMasterDisp++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, a);
    gDPFillRectangle(gMasterDisp++, x, y, x + w, y + h);
    gDPPipeSync(gMasterDisp++);
}

void Practice_DrawText(s32 x, s32 y, const char* text) {
    RCP_SetupDL(&gMasterDisp, SETUPDL_83);
    gDPSetPrimColor(gMasterDisp++, 0, 0, 255, 255, 255, 255);
    Graphics_DisplaySmallText(x, y, 1.0f, 1.0f, text);
}

void Practice_DrawTextColor(s32 x, s32 y, const char* text, u8 r, u8 g, u8 b) {
    RCP_SetupDL(&gMasterDisp, SETUPDL_83);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, 255);
    Graphics_DisplaySmallText(x, y, 1.0f, 1.0f, text);
}

void Practice_DrawNumber(s32 x, s32 y, s32 value) {
    char buf[12];
    s32 i = 0;
    s32 neg = 0;
    s32 v;

    if (value < 0) {
        neg = 1;
        value = -value;
    }
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
    } else {
        v = value;
        while (v > 0) {
            i++;
            v /= 10;
        }
        buf[i] = '\0';
        v = value;
        while (v > 0) {
            buf[--i] = '0' + (v % 10);
            v /= 10;
        }
    }
    if (neg) {
        Practice_DrawTextColor(x - 8, y, "-", 255, 255, 255);
    }
    Practice_DrawText(x, y, buf);
}

void Practice_DrawCursor(s32 x, s32 y) {
    Practice_DrawTextColor(x, y, "-", 255, 255, 0);
}

#endif
