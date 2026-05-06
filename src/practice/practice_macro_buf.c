#include "practice.h"

#ifdef PRACTICE_ROM

/* 18000 frames * 4 bytes = 72000 bytes (~70 KB).
 * Sits at 0x80680000 (Expansion Pak DRAM, right after the save slot pool +
 * scratch which occupy 0x80400000-0x80680000).  The section is NOLOAD so the
 * OS BSS sweep never touches it; callers guard on osMemSize >= 0x800000. */
#define MACRO_MAX_FRAMES 18000

static MacroFrame sMacroBuf[MACRO_MAX_FRAMES] __attribute__((aligned(8)));

MacroFrame* Practice_Macro_BufBase(void) {
    return sMacroBuf;
}

s32 Practice_Macro_BufCapacity(void) {
    return MACRO_MAX_FRAMES;
}

#endif
