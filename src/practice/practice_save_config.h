#ifndef PRACTICE_SAVE_CONFIG_H
#define PRACTICE_SAVE_CONFIG_H

/* Phase 4 — single source of truth for save-state sizes and versions.
 *
 * These constants are *provisional*. Wave 6 (heap audit) will tighten them
 * to values measured on hardware. The static invariant
 * check_max_state_size_budget() enforces:
 *
 *     MAX_STATE_SIZE * MAX_RAM_SLOTS_NO_PAK   <= 1 048 576   (1 MB)
 *     MAX_STATE_SIZE * MAX_RAM_SLOTS_WITH_PAK <= 2 621 440   (2.5 MB)
 */

/* Header version embedded in every slot. Bump = breaking change. */
#define STATE_VERSION         1

/* Companion library version (slot_manager codec). */
#define LIB_VERSION           1

/* Worst-case bytes per slot (TLV stream, including header).
 * 256 KB leaves ~46 KB of slop above ~90 KB scalars/arrays + ~120 KB
 * worst-case overlay segment. Audited downward in Wave 6 if possible. */
#define MAX_STATE_SIZE        0x40000

/* Number of RAM slots exposed in this build. Provisional: 2 slots
 * exercises the slot-cycle path end-to-end; the audit may shrink to 1
 * on stock 4 MB hardware. */
#define RAM_SLOT_COUNT        2

/* Hard ceilings used by check_max_state_size_budget(). The actual
 * RAM_SLOT_COUNT chosen at build time must be <= the appropriate one
 * of these depending on whether the Expansion Pak is present. */
#define MAX_RAM_SLOTS_NO_PAK  2
#define MAX_RAM_SLOTS_WITH_PAK 8

#endif /* PRACTICE_SAVE_CONFIG_H */
