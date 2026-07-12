#ifdef PRACTICE_ROM

#include "sys.h"
#include "sf64dma.h"
#include "PR/os.h"
#include "practice_late.h"

#ifdef PRACTICE_LATE_PROBE
#include "iodev/iodev.h"
#endif

/* Set only after BOTH Pak segments are resident. Lives in this main-resident
 * object so Practice_PakReady() is callable from anywhere, Pak or no Pak. */
static bool sPakSegmentsLoaded = false;

bool Practice_PakReady(void) {
    return sPakSegmentsLoaded;
}

static void LoadCoreSegment(void) {
    Lib_DmaRead(SEGMENT_ROM_START(practice_late_core),
                SEGMENT_VRAM_START(practice_late_core),
                SEGMENT_ROM_SIZE(practice_late_core));
    bzero(SEGMENT_BSS_START(practice_late_core),
          SEGMENT_BSS_SIZE(practice_late_core));
}

static void LoadPakSegment(void) {
    Lib_DmaRead(SEGMENT_ROM_START(practice_late_pak),
                SEGMENT_VRAM_START(practice_late_pak),
                SEGMENT_ROM_SIZE(practice_late_pak));
    bzero(SEGMENT_BSS_START(practice_late_pak),
          SEGMENT_BSS_SIZE(practice_late_pak));
}

void Practice_Late_Init(void) {
    /* practice_late_core / practice_late_pak live at 0x80720000+ (Pak
     * region). On stock 4MB carts that VRAM is unmapped; Lib_DmaRead would
     * fault. Skip the loads entirely; Practice_PakReady() stays false and
     * every consumer of either segment is gated on it (or osMemSize). */
    if (osMemSize < 0x800000U) {
        return;
    }
    LoadCoreSegment();
    LoadPakSegment();
    sPakSegmentsLoaded = true;

#ifdef PRACTICE_LATE_PROBE
    /* Phase 1 architectural probe. Validates that data-resident function
     * pointers into .practice_late_core resolve correctly after DMA. If
     * this faults, Phase 2's gLateOps dispatch model is unsound. The
     * #ifdef stays in tree (default-undefined) so the probe is rerunnable
     * against future toolchain changes. Spec rationale lives in the
     * Phase 1 prerequisite section of the architecture spec. */
    {
        typedef iodev_id_t (*ProbeFn)(void);
        static const ProbeFn sLateProbe[] = { iodev_detect };
        (void)sLateProbe[0]();
    }
#endif
}

#endif /* PRACTICE_ROM */
