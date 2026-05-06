#include "practice.h"
#include "practice_save_config.h"

#ifdef PRACTICE_ROM

/* Macro snapshot buffer: one PracticeSnapshot-sized slot in Expansion Pak DRAM.
 * Sits at 0x80691940 (right after the 18000-frame macro frame buffer).
 * NOLOAD -- callers guard on osMemSize >= 0x800000 before touching it. */
static u8 sMacroSnapBuf[MAX_STATE_SIZE] __attribute__((aligned(8)));

u8* Practice_Macro_SnapBase(void) {
    return sMacroSnapBuf;
}

#endif
