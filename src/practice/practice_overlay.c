#include "practice.h"

#ifdef PRACTICE_ROM

#include "practice_overlay.h"

/* Wave 1 skeleton: every entry point is a no-op stub. The real
 * LevelId -> ovl_iN segment table, the saveable / exclusion sets, and the
 * cached CRC32 build IDs land in Wave 2.1. */

s32 practice_overlay_get_region(LevelId id, void** vram, u32* size) {
    /* TODO Wave 2.1: real implementation. */
    (void)id;
    if (vram != NULL) {
        *vram = NULL;
    }
    if (size != NULL) {
        *size = 0;
    }
    return -1;
}

bool practice_overlay_is_saveable(LevelId id) {
    /* TODO Wave 2.1: real implementation. */
    (void)id;
    return false;
}

u32 practice_overlay_build_id(LevelId id) {
    /* TODO Wave 2.1: real implementation. */
    (void)id;
    return 0;
}

void practice_overlay_request_load(LevelId id, s32 phase) {
    /* TODO Wave 2.1: real implementation. */
    osSyncPrintf("[overlay] request_load stub level=%d phase=%d\n",
                 (s32)id, phase);
}

#endif /* PRACTICE_ROM */
