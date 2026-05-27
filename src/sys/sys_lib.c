#include "sys.h"

s32 Lib_vsPrintf(char* dst, const char* fmt, va_list args) {
    return vsprintf(dst, fmt, args);
}

void Lib_vTable(s32 index, void (**table)(s32, s32), s32 arg0, s32 arg1) {
    void (*func)(s32, s32) = table[index];

    func(arg0, arg1);
}

void Lib_SwapBuffers(u8* buf1, u8* buf2, s32 len) {
    s32 i;
    u8 temp;

    for (i = 0; i < len; i++) {
        temp = buf2[i];
        buf2[i] = buf1[i];
        buf1[i] = temp;
    }
}

void Lib_QuickSort(u8* first, u32 length, u32 size, CompareFunc cFunc) {
    u32 splitIdx;
    u8* last;
    u8* right;
    u8* left;

    while (true) {
        last = first + (length - 1) * size;

        if (length == 2) {
            if (cFunc(first, last) > 0) {
                Lib_SwapBuffers(first, last, size);
            }
            return;
        }
        if (size && size && size) {} //! FAKE: must be here with at least 3 && operands.
        left = first;
        right = last - size;

        while (true) {
            while (cFunc(left, last) < 0) {
                left += size;
            }
            while ((cFunc(right, last) >= 0) && (left < right)) {
                right -= size;
            }
            if (left >= right) {
                break;
            }
            Lib_SwapBuffers(left, right, size);
            left += size;
            right -= size;
        }
        Lib_SwapBuffers(last, left, size);
        splitIdx = (left - first) / size;
        if (length / 2 < splitIdx) {
            if ((length - splitIdx) > 2) {
                Lib_QuickSort(left + size, length - splitIdx - 1, size, cFunc);
            }

            if (splitIdx < 2) {
                return;
            }
            left = first;
            length = splitIdx;
        } else {
            if (splitIdx >= 2) {
                Lib_QuickSort(first, splitIdx, size, cFunc);
            }

            if ((length - splitIdx) <= 2) {
                return;
            }

            first = left + size;
            length -= splitIdx + 1;
        }
    }
}

void Lib_InitPerspective(Gfx** dList) {
    u16 norm;

    guPerspective(gGfxMtx, &norm, gFovY, (f32) SCREEN_WIDTH / SCREEN_HEIGHT, gProjectNear, gProjectFar, 1.0f);
    gSPPerspNormalize((*dList)++, norm);
    gSPMatrix((*dList)++, gGfxMtx++, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    guLookAt(gGfxMtx, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -12800.0f, 0.0f, 1.0f, 0.0f);
    gSPMatrix((*dList)++, gGfxMtx++, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    Matrix_Copy(gGfxMatrix, &gIdentityMatrix);
}

void Lib_InitOrtho(Gfx** dList) {
    guOrtho(gGfxMtx, -SCREEN_WIDTH / 2, SCREEN_WIDTH / 2, -SCREEN_HEIGHT / 2, SCREEN_HEIGHT / 2, gProjectNear,
            gProjectFar, 1.0f);
    gSPMatrix((*dList)++, gGfxMtx++, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    guLookAt(gGfxMtx, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -12800.0f, 0.0f, 1.0f, 0.0f);
    gSPMatrix((*dList)++, gGfxMtx++, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    Matrix_Copy(gGfxMatrix, &gIdentityMatrix);
}

void Lib_DmaRead(void* src, void* dst, ptrdiff_t size) {
    osInvalICache(dst, size);
    osInvalDCache(dst, size);
    while (size > 0x100) {
        osPiStartDma(&gDmaIOMsg, 0, 0, (uintptr_t) src, dst, 0x100, &gDmaMesgQueue);
        size -= 0x100;
        src = (void*) ((uintptr_t) src + 0x100);
        dst = (void*) ((uintptr_t) dst + 0x100);
        MQ_WAIT_FOR_MESG(&gDmaMesgQueue, NULL);
    }
    if (size != 0) {
        osPiStartDma(&gDmaIOMsg, 0, 0, (uintptr_t) src, dst, size, &gDmaMesgQueue);
        MQ_WAIT_FOR_MESG(&gDmaMesgQueue, NULL);
    }
}

void Lib_FillScreen(u8 setFill) {
    s32 i;

    gFillScreenColor |= 1;
    if (setFill == true) {
        if (gFillScreen == false) {
            if (gFillScreenColor == 1) {
                osViBlack(true);
            } else {
                for (i = 0; i < 3 * SCREEN_WIDTH; i++) {
                    gFillBuffer[i] = gFillScreenColor;
                }
                osWritebackDCacheAll();
                osViSwapBuffer(&gFillBuffer[SCREEN_WIDTH]);
                osViRepeatLine(true);
            }
            gFillScreen = true;
        }
    } else if (gFillScreen == true) {
        osViRepeatLine(false);
        osViBlack(false);
        gFillScreen = false;
    }
}

#ifdef PRACTICE_BOOT_BREADCRUMBS
/* Diagnostic-only progressive breadcrumb. Paints a colored vertical stripe
 * in gFillBuffer's repeat-row at the next cursor slot, leaving prior
 * stripes intact. With osViRepeatLine(true) the row spans full screen
 * height, so each call adds one new vertical bar moving left→right
 * across the screen. A clean boot leaves a visible "rainbow trace" of
 * all stages reached; a hang freezes the trace at the last reached
 * stage. Background is dark navy (matches bootproc's loading sentinel).
 *
 * 16 slots × 20 px wide = 320 px = full SCREEN_WIDTH. */
#define DEBUG_BC_SLOTS       16
#define DEBUG_BC_SLOT_W      (SCREEN_WIDTH / DEBUG_BC_SLOTS)
#define DEBUG_BC_BG_COLOR    0x0011  /* dark navy, matches bootproc loading */

static u8 sDebugBcCursor = 0;

void Lib_DebugFillScreen(u16 color) {
    s32 i;
    s32 slot_x;

    color |= 1;
    gFillScreenColor = color;

    /* First call: paint all 3 rows of gFillBuffer with the navy background.
     * Subsequent calls preserve the buffer so prior stripes remain visible. */
    if (sDebugBcCursor == 0) {
        for (i = 0; i < 3 * SCREEN_WIDTH; i++) {
            gFillBuffer[i] = DEBUG_BC_BG_COLOR;
        }
    }

    if (sDebugBcCursor < DEBUG_BC_SLOTS) {
        /* osViSwapBuffer targets &gFillBuffer[SCREEN_WIDTH] (the middle row),
         * which osViRepeatLine then repeats for all 240 lines. Paint the
         * stripe in that middle row. */
        slot_x = sDebugBcCursor * DEBUG_BC_SLOT_W;
        for (i = 0; i < DEBUG_BC_SLOT_W; i++) {
            gFillBuffer[SCREEN_WIDTH + slot_x + i] = color;
        }
    }
    sDebugBcCursor++;

    osWritebackDCacheAll();
    osViSwapBuffer(&gFillBuffer[SCREEN_WIDTH]);
    osViRepeatLine(true);
    gFillScreen = true;
}
#endif
