# Phase 4 — heap audit (hardware verification)

## Purpose

Collect **IS-Viewer** telemetry for RAM pressure while visiting every **saveable**
scene, so Wave 4 can pin `MAX_STATE_SIZE` / slot counts in
`src/practice/practice_save_config.h`. This doc is the checklist; paste measured
numbers into the table after you run the procedure.

**Static slot pool size (current build):** from `practice_save_config.h`:

- Bytes reserved in `.bss`: `RAM_SLOT_COUNT * MAX_STATE_SIZE` (e.g. `2 * 0x40000` = **524288** bytes with provisional constants).

## Prerequisites

- **SummerCart64** (or equivalent) with IS-Viewer path working.
- Patched `sc64deployer` that flushes stdout after each line (see `CLAUDE.md`
  IS-Viewer / deployer notes).

## Terminal A — IS-Viewer debug

```bash
sc64deployer debug --isv 0x03FF0000
```

Wait until the tool reports it is listening.

## Build and flash

```bash
make practice -j4
```

Upload the ROM (`build/starfox64.us.rev1.uncompressed.z64` or your usual
artifact). **Hard-reset** the N64 after upload so the IS-Viewer buffer resets
cleanly (see `CLAUDE.md`).

## What you should see at boot

One-shot lines similar to:

```text
[heap] boot osMemSize=... bss_span~... headroom~... bump=... slot_pool=... slot_pool_sz=... (RAM_SLOT_COUNT*MAX_STATE_SIZE=...)
[heap] ovl_i1=... ovl_i2=... ovl_i3=... ovl_i4=... ovl_i5=... ovl_i6=...
```

Then, on **level changes** or about every **60 frames**:

```text
[heap] enter level=... bump=... gfx_peak=... audio=... audio_peak=... free~... bump_hwm=... free_low=...
```

Use the `free~` and `free_low` columns while stressing each scene (enemies,
charge shots, bombs, dense spawns). **`free_low`** is the smallest approximate
“slack” seen so far since boot (conservative diagnostic; see plan §7.3).

## Per-scene pass (17 saveable levels)

For **each** saveable level:

1. Select the level from practice level select.
2. Play **~10 seconds** with heavy action (match plan §7.2 guidance).
3. Note the **lowest** `free~` (and/or `free_low` after leaving the scene) from
   the deployer log.

Fill in:

| LevelId / scene | Lowest `free~` (notes) | Notes |
|-------------------|------------------------|-------|
| CORNERIA | | |
| METEO | | |
| SECTOR_X | | |
| AREA_6 | | |
| SECTOR_Y | | |
| VENOM_1 | | |
| SOLAR | | |
| ZONESS | | |
| VENOM_ANDROSS | | |
| MACBETH | | |
| TITANIA | | |
| AQUAS | | |
| FORTUNA | | |
| KATINA | | |
| BOLSE | | |
| SECTOR_Z | | |
| VENOM_2 | | |

## Optional: Expansion Pak

Repeat on hardware with the **Expansion Pak** if available. If `osMemSize` does
not increase on your setup, document **4 MB only** and treat Pak sizing as a
follow-up (plan §7.2 step 6).

## After the run

- Worst-case `free~` / overlay footprint feeds **Wave 4** tightening of
  `MAX_STATE_SIZE` and `RAM_SLOT_COUNT` (not changed in Wave 2.3).
- Re-run `python3 tools/practice_invariants.py` and `make practice -j4` after any
  constant changes.

## Compile-out audit (release / tournament)

```bash
make practice -j4 PRACTICE_HEAP_AUDIT=0
```

This keeps `Practice_HeapAudit_Boot` / `PerFrame` as empty stubs and drops
`PRACTICE_HEAP_AUDIT=1` telemetry from the translation unit.
