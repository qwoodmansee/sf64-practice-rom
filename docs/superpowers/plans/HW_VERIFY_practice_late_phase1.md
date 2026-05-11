# Hardware Verification: Phase 1 — Practice Late Core

> Run after merging Phase 1 of the practice ROM memory architecture
> spec (`docs/superpowers/specs/2026-05-09-practice-rom-memory-architecture-design.md`).
> Confirms the .practice_late_core segment loads correctly on real
> cartridges and that the eight migrated objects function from their
> new RAM home:
>
>   `lib/iodev/{iodev, iodev_sc64, iodev_ed64, iodev_stub}`
>   `lib/{sd_crc, serial, slot_manager, crc32}`

## Pre-flight

- Cart: SC64 or EverDrive (any model that runs the practice ROM).
- ROM: latest `build/starfox64.us.rev1.z64` from a Phase-1-merged main.
- IS-Viewer or equivalent serial trace, if available.

## Checks

### 1. Cold boot — no blue screen

- Power-cycle the cart.
- Expected: ROM enters game, vanilla intro/title plays through to the
  practice ROM's added menu hooks. **No solid blue screen.**
- Failure mode if this fails: `main_ROM_END` is still over the IPL cap
  somehow, or `Practice_Late_Init` is referencing a not-yet-loaded
  late-segment symbol. Capture the IS-Viewer trace and revert.

### 2. Practice menu reachable

- Navigate from title to the practice menu via the standard input
  (whatever the project uses — typically L+R or a level-select entry).
- Expected: menu renders normally, all entries are present.
- Failure mode: a `_core`-resident draw helper (e.g., something pulled
  in transitively by one of the migrated `lib/` files) is being called
  before `Practice_Late_Init` ran. Verify the call order in
  `Practice_Init`.

### 3. Migrated `_core` features work

Exercise at least one feature whose code path hits a migrated `.o`:

- **iodev path**: insert a known SD card; confirm IS-Viewer prints
  `[iodev] cart=N sd_init=R` with N matching the cart variant and R
  indicating SD success or expected failure code. This validates
  `lib/iodev/iodev*.o` is running from `.practice_late_core`.
- **CRC path** (if a CRC-using feature is in the menu): exercise it.
  This validates `lib/crc32.o` and `lib/sd_crc.o`.
- **Slot manager / serial** (if a slot-list view is in the menu):
  navigate to it. This validates `lib/slot_manager.o` and `lib/serial.o`.

If any of these crash the ROM, the migrated code's runtime path is
broken. Capture the IS-Viewer trace, note which feature failed, and
revert Wave 4.

### 4. IS-Viewer trace shows the loader fired

If you have a serial trace running, confirm the boot output includes a
line from the practice ROM's existing `[iodev] cart=...` print. The
absence of that line on a cart that should produce it (SC64 or
EverDrive) means `Practice_Late_Init` didn't complete or
`iodev_detect` faulted.

(Phase 1 doesn't add a `[late]` print; the existing
`[iodev]` output is sufficient evidence that the late-loaded code ran.
A `[late] core loaded N bytes` print is suggested in the spec but
deferred to Phase 2 alongside the dispatch refactor's logging.)

### 5. Stock 4 MB cart (no Expansion Pak)

If a Pak-removable cart or emulator with 4 MB config is available,
repeat checks 1–4 with no Pak. The `_core` segment is stock-RAM safe
by design and must function identically.

If this fails but Pak runs work, an unexpected `_core` reference is
reaching into Pak-only memory. Audit recent changes.

## Sign-off

When all checks pass, file the verification record (with cart variant,
ROM hash, and date) wherever the project keeps QA notes. The architecture
spec's "Risks" section #1 and #2 are confirmed mitigated when checks
1 and 4 pass on at least one real cartridge.
