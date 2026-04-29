---
name: debug-ram-layout
description: Use when an SF64 practice ROM feature boots but renders wrong - invisible text, garbage textures, missing menu sprites, corrupt level-select, scrambled overlays, or any symptom suggesting memory corruption near overlays or assets. Especially after adding large BSS allocations, custom NOLOAD linker sections, or bzero/memset on practice buffers. The linker map proves overlap statically without re-running the ROM.
---

## Overview

SF64 streams overlays and scene assets into a fixed RAM slot at runtime. Any practice-owned BSS in that slot gets clobbered, or clobbers a live overlay. The map tells you whether this has happened - no need to debug the runtime symptom.

## When to Use

- Text or HUD renders blank or garbage after a feature lands.
- Level-select looks wrong on first boot but recovers after playing a level.
- Overlay loads silently corrupt static data, or vice versa.
- A new large BSS array, `__attribute__((section(...)))`, or NOLOAD section in `tools/patch_linker_script.py`.
- A `bzero()` / `memset()` on a practice region precedes the breakage.

## Quick Workflow

1. Run `python3 tools/audit_ram_layout.py` from the repo root.
2. Scan for `*** ADDRESS RANGE OVERLAPS ***`. Each line names the practice section and the overlay slot or load window it intersects.
3. Locate the source: search `tools/patch_linker_script.py` for the section name, or grep `src/practice/` for `__attribute__((section`.
4. Fix: relocate below `.buffers`, shrink, or move onto a safe region. Re-run until exit 0.

## Reading the Report

- `STATIC RAM LAYOUT` - every region sorted by start address with size and kind tag.
- `DYNAMIC LOAD WINDOW` is `[ovl_i1_VRAM, buffers_VRAM)`; every overlay and DMA'd asset shares it.
- `*** ADDRESS RANGE OVERLAPS ***` - failure section; worst-case size is what you must reclaim.
- `FREE RAM` - gaps between static regions; candidate homes for displaced BSS.

## Common Causes

- Large BSS arrays in `src/practice/` (save-state pools).
- Large "temporary" scratch buffers made file-static in normal practice BSS.
  Hardware-confirmed example: `PracticeSnapshot gPracticeSaveScratch` in
  `practice_save.c` caused Aquas to crash even without save/load. Keep save
  scratch in `practice_save_slotpool.o(.bss)` so `.practice_pool_pak` parks it
  at `0x80400000`.
- Custom NOLOAD sections after `.main_bss` but before `.buffers`.
- `bzero()` of a region overlapping a loaded overlay.
- Forgetting `SEGMENT_VRAM_START(ovl_i1) = 0x8019ae40` is the load-window floor.

## Why This Works

The linker emits every section's RAM start/end and every `ovl_*_VRAM` into `build/starfox64.us.rev1.map`. Overlap is a static, provable property: if a practice range intersects any `ovl_*` range or the load window, runtime corruption is guaranteed when that overlay loads. The map is faster and more reliable than reproducing the symptom.

## Example

The `.practice_pool` overlap that motivated this tool:

```
.practice_pool   0x8018c930 - 0x8020c940  (size 512.0 KB)  [practice]
ovl_menu (slot)  0x8019ae40 - 0x801e2a00  (size 286.9 KB)  [overlay, dynamic]
.buffers         0x80281000 -             (size   1.5 MB)  [framebuffer/audio wall]

*** ADDRESS RANGE OVERLAPS ***
  .practice_pool OVERLAPS ovl_menu by 286.9 KB
  .practice_pool OVERLAPS dynamic-load window by 454.8 KB
```

Symptom: invisible text on level-select until a level overlay forces `ovl_menu` to reload.

## Pre-Commit Integration

`tools/practice_invariants.py` calls `check_practice_pool_no_overlay_overlap()` and emits a non-fatal warning per overlapping practice section. The hook still passes; the warning persists until the layout is fixed. New regressions surface the same way.
