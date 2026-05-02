#ifndef PRACTICE_OVERLAY_H
#define PRACTICE_OVERLAY_H

#ifdef PRACTICE_ROM

#include "global.h"

/* Phase 4 — overlay region map.
 *
 * Resolves the per-scene `ovl_iN` segment that must be captured alongside
 * a save state, and exposes a stable build ID so a load can detect a
 * mismatched ROM. Wave 1 ships stubs only; Wave 2.1 fills in the table. */

/* Look up the saveable VRAM region for `id`.
 *
 * On success returns 0, writes the segment's RDRAM start to *vram and its
 * size in bytes to *size. Returns -1 (and writes NULL/0) when the level is
 * not saveable or when the table has no entry for it. Wave 1 always fails. */
s32  practice_overlay_get_region(LevelId id, void** vram, u32* size);

/* Cheap predicate: true iff `id` is in the saveable LevelId table. */
bool practice_overlay_is_saveable(LevelId id);

/* CRC32-IEEE of metadata identifying the `ovl_iN` segment that owns `id`.
 * Stable across the whole run regardless of which overlay is currently
 * resident in the shared VRAM slot — the hash is over linker / DMA-table
 * addresses, not segment contents, so it never reads `vRomAddress` bytes
 * (which are unsafe on hardware). Lazily computed, cached per overlay,
 * returns 0 for non-saveable levels. */
u32  practice_overlay_build_id(LevelId id);

/* Eagerly populate the build-id cache for all six gameplay overlays so
 * cross-scene loads can validate a saved overlay's id without first
 * having to switch to that overlay. Safe to call repeatedly. */
void practice_overlay_prime_build_ids(void);

/* Phase 5 hook — request a cross-scene transition that lands at `phase`
 * inside `id`. Wave 1 logs and returns. Phase 5 fills in the state machine. */
void practice_overlay_request_load(LevelId id, s32 phase);

#endif /* PRACTICE_ROM */
#endif /* PRACTICE_OVERLAY_H */
