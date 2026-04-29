#include "practice.h"

#ifdef PRACTICE_ROM

#include "practice_save_config.h"

/* Expansion Pak slot pool only - entire .bss goes in .practice_pool_pak (VMA 0x80400000).
 * practice_save.c keeps gPracticeSaveDisabled and other globals in .main_bss so stock
 * 4 MB hardware never touches Expansion Pak addresses for control state. */
static u8 sSlotPoolPak[MAX_RAM_SLOTS_WITH_PAK * MAX_STATE_SIZE]
    __attribute__((aligned(8)));

uintptr_t Practice_Save_SlotPoolBase(void) {
    return (uintptr_t)sSlotPoolPak;
}

#endif /* PRACTICE_ROM */
