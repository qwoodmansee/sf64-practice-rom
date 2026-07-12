# Scene-Window / Z-Buffer Overlap: the "Katt Freeze" Root Cause

**Date:** 2026-07-11
**Status:** Diagnosed; fix = migrate practice code out of `main` into a Pak
segment (see "Fix architecture" below).
**Symptom family explained:** Zoness freeze at Katt's first radio line
(hardware-only, savestate or plain gameplay), historic Aquas crashes,
Macbeth instability, Andross weirdness, options-menu anomalies.

## TL;DR

The scene asset window (`ovl_i1_VRAM`) **floats after `main`** via splat's
`follows_vram` chain. Practice code grew `main`, pushing the window from
vanilla `0x80187520` to `0x801a3430` (+0x1BF10, ~114 KB). The `buffers`
segment (`gDramStack`, `gOSYieldData`, `gZBuffer`, `gTaskOutputBuffer`,
`gAudioHeap`, `gFrameBuffers`) is **fixed at `0x80281000`** and ends exactly
at the 4 MB line. Vanilla's longest scene stack left only **~0xA710 (43 KB)**
of headroom. Result: 10 scene setups' asset stacks now DMA past `0x80281000`
into the buffer segment.

## Mechanism (Zoness / Katt)

1. `Load_SceneFiles` stacks Zoness segments contiguously from `ovl_i1_VRAM`.
   `ast_allies` (segment 0xD) lands at `0x802807b0`; everything past +0x850
   overlaps `gDramStack` (0x281000), `gOSYieldData` (0x281400), and the first
   0xB220 bytes of `gZBuffer` (0x282000).
2. Every rendered frame, RDP depth writes shred that region. The Z clear
   value is `0xfffc` (`GPACK_ZDZ(G_MAXFBZ,0)`), which is exactly the garbage
   pattern captured in the RDP FIFO dumps.
3. `aKattShipDL` sits at allies+0x9a40 = `0x8028a1f0`, inside `gZBuffer`.
   The first time Katt appears/speaks, the RSP executes Z-buffer contents as
   her ship's display list. Opcodes >= 0xE4 are forwarded verbatim to the
   RDP; a garbage `SETCIMG`/fill wedges the pipe (`DPC_CUR == DPC_END`), the
   RSP spins on FIFO space, never yields, all CPU threads starve, and the
   audio buffer loops (~0.3 s music loop).

## Why no emulator ever caught it

HLE graphics plugins keep the depth buffer internal and **never write it
back to RDRAM**. In emulator, `ast_allies` is never corrupted, Katt draws
fine, and the entire bug class is invisible. Only real hardware (real RDP
z-writeback) reproduces it. mupen64plus passing 213/213 means nothing here.

## Blast radius (audit of all scene setups, 2026-07-11 build)

Scene stack end vs `0x80281000`:

| Scene setup       | Overflow  |
|-------------------|-----------|
| Titania[5]        | +0x11800  |
| Macbeth[1]        | +0xe800   |
| Zoness[0]         | +0xcb10   |
| Andross[0]        | +0xb590   |
| Options menu[0]   | +0xaf20   |
| Macbeth[0]        | +0x93e0   |
| Area6[0], Unk4[0] | +0x5ce0   |
| Aquas[0]          | +0x4d30   |
| Solar[0]          | +0x3120   |

Everything else fits (Titania[4] margin +0x690 is the tightest survivor).

Audit method: scene end = `ovl_i1_VRAM` + overlay ROM size + overlay
`bss_size` (yaml) + sum of `ROM_SEGMENT` sizes from the scene's table in
`fox_load_inits.c`. All sizes from the linker map. This is enforced by
`check_scene_stack_fits_buffers` in `tools/practice_invariants.py`.

## Hard-won conclusions

- **There is no "free" stock-RAM region for practice code.** The
  2026-05-09 memory-architecture spec placed a "stock-RAM safe" segment at
  `0x801F4000`; that address is inside the floating scene window. Any fixed
  placement in `[ovl_i1_VRAM, 0x80281000)` gets DMA'd over by big levels.
  On a 4 MB console, practice code can only live in `main`, and `main`
  growth is capped by the ~43 KB vanilla scene-window headroom.
- **The full practice feature set can never fit in that headroom** (~102 KB
  of practice/lib code was in `main` at diagnosis time). Therefore practice
  features are Expansion Pak resident by architecture, not by choice. This
  was already half-true: the save/SD stack and level-select textures live in
  `.practice_late_core` at `0x80720000` (Pak).
- **RAM budgets need invariants exactly like ROM budgets.** This is the RAM
  twin of the Macbeth boot-staging lesson (`main_ROM_END <= 0xFC000`): a
  floating window silently creeping toward a fixed line, hardware-only
  failures, level-specific symptoms that look unrelated.
- **Diagnostic pattern that cracked it** (keep in the toolbox): PRENMI
  rerouted to the fault thread dumping all threads + RCP status (`DPC_*`) +
  the RDP FIFO window around `DPC_CURRENT`, plus a DL-branch ring buffer at
  `gSPDisplayList` call sites. Recognizing `0xfffc` as the Z clear value was
  the pivot. Note the ring only instrumented `info.dList` sites; the actual
  killer (Katt's ship) drew through an uninstrumented actor-draw path. Ring
  saturation by one object indicates draw frequency, not guilt.

## Fix architecture

Move essentially all practice/lib code out of `main` into a new Pak-resident
LOAD segment `.practice_pak_ui` at `0x80730000` (after `.practice_late_core`
which ends at `0x8072af00`; Pak ceiling `0x80800000`; slot pool occupies
`0x80400000`-`0x80680000`). Same shape and loader mechanism as
`.practice_late_core`, ROM-placed AFTER it so late_core's ROM offset and
internal layout are byte-identical (SD timing landmine untouched).

Stays in `main`:
- `practice_main.o` as the dispatch gate: `Practice_Init/Update/Draw` check
  `osMemSize >= 0x800000` and a segment-loaded flag before calling any Pak
  code. Without a Pak the game runs vanilla plus a small notice.
- Engine-hook globals (anything `src/engine/` or `src/sys/` objects
  reference must resolve into main-resident objects; enforced by invariant).
- `practice_icon_tex.o` (per existing CLAUDE.md rule), pinned-BSS objects
  (`practice_save_slotpool.o`, `practice_macro_buf.o`, `practice_macro_snap.o`),
  `loader.o`.

Expected result: `main` shrinks by ~0x18000+, worst scene margin goes from
-0x11800 to roughly +0x7000, and the invariant keeps it there.

## Hardware verification required (emulator cannot gate this)

SD save/load pass, Zoness played through Katt's first line, Titania,
Macbeth, Aquas, options menu, plus the standing rule: any layout change
near the SD stack gets a full hardware SD retest.
