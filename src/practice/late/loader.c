#ifdef PRACTICE_ROM

#include "sys.h"
#include "sf64dma.h"
#include "practice_late.h"

#ifdef PRACTICE_LATE_PROBE
#include "iodev/iodev.h"
#endif

static void LoadCoreSegment(void) {
    Lib_DmaRead(SEGMENT_ROM_START(practice_late_core),
                SEGMENT_VRAM_START(practice_late_core),
                SEGMENT_ROM_SIZE(practice_late_core));
    bzero(SEGMENT_BSS_START(practice_late_core),
          SEGMENT_BSS_SIZE(practice_late_core));
}

void Practice_Late_Init(void) {
    LoadCoreSegment();

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
