#ifndef PRACTICE_SAVE_CONFIG_H
#define PRACTICE_SAVE_CONFIG_H

/* Phase 4 — single source of truth for save-state sizes and versions.
 *
 * Decision (Phase 2 commit):
 * - Stock 4 MB (osMemSize == 0x400000): no save-state support (no room above
 *   dynamic load window; Titania setup 5 worst-case reaches 0x8028a210, which is
 *   37 KB above buffers_VRAM at 0x80281000).
 * - Expansion Pak (osMemSize == 0x800000): 4-slot pool above 0x80400000, each
 *   slot 256 KB with overlay snapshots enabled.
 *
 * The static invariant check_max_state_size_budget() enforces:
 *     MAX_STATE_SIZE * MAX_RAM_SLOTS_NO_PAK   <= 1 048 576   (1 MB)
 *     MAX_STATE_SIZE * MAX_RAM_SLOTS_WITH_PAK <= 2 621 440   (2.5 MB)
 */

/* Header version embedded in every slot. Bump = breaking change. */
#define STATE_VERSION         1

/* Companion library version (slot_manager codec). */
#define LIB_VERSION           1

/* Worst-case bytes per slot (TLV stream, including header).
 * 256 KB leaves ~46 KB of slop above ~90 KB scalars/arrays + ~120 KB
 * worst-case overlay segment. Fits Pak pool; unreachable on stock. */
#define MAX_STATE_SIZE        0x40000

/* Number of RAM slots exposed in this build. Runtime value set at boot
 * based on osMemSize. Stock gets 0, Pak gets 4. */
#define RAM_SLOT_COUNT        4

/* Hard ceilings used by check_max_state_size_budget(). Stock cannot fit
 * any same-scene save/load in Phase 4 due to overlay load-window conflict.
 * Pak gets 4 slots above 0x80400000. */
#define MAX_RAM_SLOTS_NO_PAK  0
#define MAX_RAM_SLOTS_WITH_PAK 4

#endif /* PRACTICE_SAVE_CONFIG_H */
