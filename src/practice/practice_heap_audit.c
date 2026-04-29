#include "practice.h"

#ifdef PRACTICE_ROM

#include "practice_heap_audit.h"
#include "practice_save_config.h"
#include "sf64audio.h"
#include "sf64dma.h"
#include "sf64thread.h"

#include "PR/os_system.h"

#ifndef PRACTICE_HEAP_AUDIT
#define PRACTICE_HEAP_AUDIT 1
#endif

s32 gPracticeMaxMemAllocHWM;
s32 gPracticeFreeRamLow;
s32 gPracticeOverlaySizes[6];

#if !PRACTICE_HEAP_AUDIT

void Practice_HeapAudit_Boot(void) {
}

void Practice_HeapAudit_PerFrame(void) {
}

#else /* PRACTICE_HEAP_AUDIT */

static LevelId sHeapAuditLastLevel = (LevelId)-1;
static u32     sHeapAuditPeakGfx;
static u32     sHeapAuditPeakAudio;

static u32 heap_audit_audio_used(void) {
    u32 a;
    u32 b;

    if ((gInitPool.startRamAddr == NULL) || (gSessionPool.startRamAddr == NULL)) {
        return 0;
    }
    a = (u32)((uintptr_t)gInitPool.curRamAddr - (uintptr_t)gInitPool.startRamAddr);
    b = (u32)((uintptr_t)gSessionPool.curRamAddr - (uintptr_t)gSessionPool.startRamAddr);
    return a + b;
}

static u32 heap_audit_gfx_used(void) {
    if ((gGfxPool == NULL) || (gMasterDisp == NULL)) {
        return 0;
    }
    return (u32)((uintptr_t)gMasterDisp - (uintptr_t)gGfxPool->masterDL) * sizeof(Gfx);
}

static u32 heap_audit_static_span(void) {
    return (u32)((uintptr_t)SEGMENT_BSS_END(main) - (uintptr_t)0x80000000U);
}

static u32 heap_audit_free_approx(u32 bump, u32 gfx, u32 audio) {
    u32 span;
    u32 dyn;
    u32 mem;
    u32 sum;

    mem = osMemSize;
    span = heap_audit_static_span();
    dyn = bump + gfx + audio;
    if (span > mem) {
        return 0;
    }
    sum = span + dyn;
    if (sum > mem) {
        return 0;
    }
    return mem - sum;
}

static void heap_audit_fill_overlay_rom_sizes(void) {
    void* keys[7];
    s32   ov;
    s32   i;
    s32   idx;

    keys[0] = NULL;
    keys[1] = SEGMENT_ROM_START(ovl_i1);
    keys[2] = SEGMENT_ROM_START(ovl_i2);
    keys[3] = SEGMENT_ROM_START(ovl_i3);
    keys[4] = SEGMENT_ROM_START(ovl_i4);
    keys[5] = SEGMENT_ROM_START(ovl_i5);
    keys[6] = SEGMENT_ROM_START(ovl_i6);

    for (ov = 1; ov <= 6; ov++) {
        gPracticeOverlaySizes[ov - 1] = 0;
        idx = -1;
        for (i = 0; gDmaTable[i].pRom.end != NULL; i++) {
            if (gDmaTable[i].pRom.start == keys[ov]) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) {
            gPracticeOverlaySizes[ov - 1] = (s32)SEGMENT_SIZE(gDmaTable[idx].pRom);
        }
    }
}

static void heap_audit_log(const char* reason) {
    u32 bump;
    u32 gfx;
    u32 aud_now;
    u32 aud_peak;
    u32 free_now;

    bump = (u32)Practice_MemoryGetBumpUsed();
    gfx = sHeapAuditPeakGfx;
    aud_now = heap_audit_audio_used();
    aud_peak = sHeapAuditPeakAudio;
    free_now = heap_audit_free_approx(bump, gfx, aud_peak);

    osSyncPrintf(
        "[heap] %s level=%d bump=%u gfx_peak=%u audio=%u audio_peak=%u free~%u "
        "bump_hwm=%d free_low=%d\n",
        reason, (int)gCurrentLevel, bump, gfx, aud_now, aud_peak, free_now,
        gPracticeMaxMemAllocHWM, gPracticeFreeRamLow);
}

void Practice_HeapAudit_Boot(void) {
    u32 span;
    u32 i;

    gPracticeMaxMemAllocHWM = 0;
    gPracticeFreeRamLow = 0x7FFFFFFF;
    sHeapAuditPeakGfx = 0;
    sHeapAuditPeakAudio = 0;
    sHeapAuditLastLevel = (LevelId)-1;

    for (i = 0; i < 6; i++) {
        gPracticeOverlaySizes[i] = 0;
    }
    heap_audit_fill_overlay_rom_sizes();

    span = heap_audit_static_span();

    osSyncPrintf("[heap] boot memSz=%u bss~%u free~%u\n",
        osMemSize, span, (osMemSize > span) ? (osMemSize - span) : 0U);
    osSyncPrintf("[heap] boot bump=%u pool=%08x poolsz=%u slotcnt=%d disabled=%d\n",
        (u32)Practice_MemoryGetBumpUsed(), (u32)Practice_Save_SlotPoolBase(),
        (u32)(MAX_RAM_SLOTS_WITH_PAK * MAX_STATE_SIZE),
        (int)gPracticeRamSlotCount, (int)gPracticeSaveDisabled);
    osSyncPrintf("[heap] ovl i1=%d i2=%d i3=%d\n",
        gPracticeOverlaySizes[0], gPracticeOverlaySizes[1], gPracticeOverlaySizes[2]);
    osSyncPrintf("[heap] ovl i4=%d i5=%d i6=%d\n",
        gPracticeOverlaySizes[3], gPracticeOverlaySizes[4], gPracticeOverlaySizes[5]);
}

void Practice_HeapAudit_PerFrame(void) {
    u32 bump;
    u32 gfx;
    u32 aud;
    u32 free_now;
    s32 bump_s;
    bool scene_change;
    bool slow_tick;

    bump = (u32)Practice_MemoryGetBumpUsed();
    bump_s = (s32)bump;
    if (bump_s > gPracticeMaxMemAllocHWM) {
        gPracticeMaxMemAllocHWM = bump_s;
    }

    gfx = heap_audit_gfx_used();
    if (gfx > sHeapAuditPeakGfx) {
        sHeapAuditPeakGfx = gfx;
    }

    aud = heap_audit_audio_used();
    if (aud > sHeapAuditPeakAudio) {
        sHeapAuditPeakAudio = aud;
    }

    free_now = heap_audit_free_approx(bump, sHeapAuditPeakGfx, sHeapAuditPeakAudio);
    if ((s32)free_now < gPracticeFreeRamLow) {
        gPracticeFreeRamLow = (s32)free_now;
    }

    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }

    scene_change = (gCurrentLevel != sHeapAuditLastLevel);
    slow_tick = ((gSysFrameCount % 60U) == 0U);

    if (scene_change) {
        sHeapAuditLastLevel = gCurrentLevel;
        heap_audit_log("enter");
    } else if (slow_tick) {
        heap_audit_log("tick60");
    }
}

#endif /* PRACTICE_HEAP_AUDIT */

#endif /* PRACTICE_ROM */
