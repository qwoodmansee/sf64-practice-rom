#ifndef PRACTICE_LATE_H
#define PRACTICE_LATE_H

#ifdef PRACTICE_ROM

#include "sf64dma.h"

DECLARE_SEGMENT(practice_late_core);
DECLARE_SEGMENT(practice_late_pak);

void Practice_Late_Init(void);

/* True once osMemSize >= 8 MB AND Practice_Late_Init has DMA'd both Pak
 * segments (.practice_late_core + .practice_late_pak). Every call edge from
 * always-resident code into .practice_late_pak must be dominated by this. */
bool Practice_PakReady(void);

#endif /* PRACTICE_ROM */

#endif /* PRACTICE_LATE_H */
