#include "practice.h"

#ifdef PRACTICE_ROM

#include "practice_save_config.h"

/* Expansion Pak slot pool only - entire .bss goes in .practice_pool_pak (VMA 0x80400000).
 * practice_save.c keeps gPracticeSaveDisabled and other small globals in .main_bss so stock
 * 4 MB hardware never touches Expansion Pak addresses for control state.
 *
 * The save/load scratch buffer also lives here. Keeping it out of .main_bss matters:
 * Aquas is sensitive to extra resident BSS below the stock dynamic window, and save
 * state work is disabled without Expansion Pak anyway. */
static u8 sSlotPoolPak[MAX_RAM_SLOTS_WITH_PAK * MAX_STATE_SIZE]
    __attribute__((aligned(8)));
static u8 sSaveScratchPak[MAX_STATE_SIZE] __attribute__((aligned(8)));

uintptr_t Practice_Save_SlotPoolBase(void) {
    return (uintptr_t)sSlotPoolPak;
}

void* Practice_Save_ScratchBase(void) {
    return sSaveScratchPak;
}

#endif /* PRACTICE_ROM */
