# Phase 4 — heap audit (hardware verification)

## BLOCKED — read first

The static slot pool (`.practice_pool`, 512 KB) **overlaps the dynamic overlay/
asset load window** in stock 4 MB RAM (see Phase 4 plan §0.3). Until Wave 6 picks
a real layout, **do not exercise SAVE / LOAD on hardware** — saving will write
256 KB of TLV stream into whatever overlay is currently resident and crash the
ROM. Boot, browse levels, and play levels are all safe.

The audit run below is **still useful**: its outputs feed Wave 6's layout choice.
Specifically, we need the dynamic-load high-water-mark per scene, not just the
"free RAM" approximation we used to want for `MAX_STATE_SIZE`.

Static layout proof (no ROM run needed):

```bash
python3 tools/audit_ram_layout.py
```

That report names every overlapping practice section. The current build's overlap
should appear as `.practice_pool OVERLAPS ovl_menu` (~287 KB) and
`.practice_pool OVERLAPS dynamic-load window` (~466 KB).

## Purpose

Collect IS-Viewer telemetry while visiting every saveable scene to:

1. Bound the dynamic-load high-water-mark — needed by Wave 6 to decide whether
   a smaller pool can sit between the highest scene-load extent and `.buffers`
   (`0x80281000`).
2. Pin `MAX_STATE_SIZE` and the slot count once the layout is chosen.
3. Confirm `osMemSize` reporting (Expansion Pak detection on this setup).

## Prerequisites

- SummerCart64 (or equivalent) with IS-Viewer path working.
- Patched `sc64deployer` that flushes stdout after each line (see `CLAUDE.md`
  IS-Viewer / deployer notes).

## Terminal A — IS-Viewer debug

```bash
sc64deployer debug --isv 0x03FF0000
```

Wait until it reports listening.

## Build and flash

```bash
make practice -j4
```

Upload `build/starfox64.us.rev1.uncompressed.z64`. Hard-reset the N64 after
upload so the IS-Viewer buffer resets cleanly.

## What you should see at boot

The current heap-audit print format (Wave 2.3, split across lines for IS-Viewer
buffering):

```text
[heap] boot memSz=<u32> bss~<u32> free~<u32>
[heap] boot bump=<u32> pool=<hex8> poolsz=<u32>
[heap] ovl i1=<bytes> i2=<bytes> i3=<bytes>
[heap] ovl i4=<bytes> i5=<bytes> i6=<bytes>
```

`memSz` is `osMemSize` (`0x400000` stock, `0x800000` with Pak).
`bss~` is `BSS_END - 0x80000000`.
`pool=` is the runtime address of `sSlotPool` — should match
`practice_pool_BSS_START` from the linker map.

On level changes or every 60 frames during gameplay:

```text
[heap] enter level=<id> bump=<u32> gfx_peak=<u32> audio=<u32> audio_peak=<u32> free~<u32> bump_hwm=<s32> free_low=<s32>
[heap] tick60 level=<id> bump=<u32> gfx_peak=<u32> audio=<u32> audio_peak=<u32> free~<u32> bump_hwm=<s32> free_low=<s32>
```

`free_low` is the lowest `free~` seen since boot (conservative diagnostic).

## Per-scene pass (17 saveable levels)

For each saveable level:

1. Select the level from practice level select.
2. Play ~10 seconds with heavy action (charge shots, dense spawns, bombs).
3. Note the lowest `free~` (and `free_low` after leaving the scene).
4. **DO NOT press SAVE / LOAD** — see BLOCKED banner above.

| LevelId | Lowest `free~` | `osMemSize` (Pak?) | Notes |
|---------|----------------|---------------------|-------|
| CORNERIA | | | |
| METEO | | | |
| SECTOR_X | | | |
| AREA_6 | | | |
| SECTOR_Y | | | |
| VENOM_1 | | | |
| SOLAR | | | |
| ZONESS | | | |
| VENOM_ANDROSS | | | |
| MACBETH | | | |
| TITANIA | | | |
| AQUAS | | | |
| FORTUNA | | | |
| KATINA | | | |
| BOLSE | | | |
| SECTOR_Z | | | |
| VENOM_2 | | | |

## Optional: Expansion Pak

Repeat with the Expansion Pak if available. Document whether `memSz=` jumps to
`0x800000` automatically, or whether the user's setup requires a different
trigger (libultra default behaviour varies by emulator/flashcart).

## After the run

- Worst-case `free~` across all levels feeds Wave 6's layout decision:
  - If `worst_free >= 256 KB` and the user has Expansion Pak available, a
    1-slot pool past `.buffers` works.
  - If `worst_free < 256 KB` and we're stock-only, `MAX_STATE_SIZE` must drop
    (likely to ~192 KB) and the pool moves to a measured-safe gap.
- Re-run `python3 tools/audit_ram_layout.py` after any layout change — it must
  exit 0 (no overlaps) before Wave 4 can resume.
- Re-run `python3 tools/practice_invariants.py` and `make practice -j4` after
  any constant changes.

## Compile-out audit (release / tournament)

```bash
make practice -j4 PRACTICE_HEAP_AUDIT=0
```

This keeps `Practice_HeapAudit_Boot` / `PerFrame` as empty stubs and drops
`PRACTICE_HEAP_AUDIT=1` telemetry from the translation unit.
