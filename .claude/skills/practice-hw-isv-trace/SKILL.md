---
name: practice-hw-isv-trace
description: Use when debugging Star Fox 64 Practice ROM on real hardware (SummerCart64 + IS-Viewer) — save/load crashes, silent hangs, no deployer "crash frame", or confirming the save hotkey path. Covers PRACTICE_SAVE_TRACE bracketing, interpreting [save_tr] lines, PSCREEN_GAMEPLAY vs engine state, and the large PracticeSnapshot stack rule.
---

## When to use

- SC64 + `sc64deployer debug --isv 0x03FF0000`; ROM freezes or resets with **no** final error line.
- Checkpoint save (L + D-pad Left) or load suspected; need to know **how far** the ROM got.
- ISV shows heap lines but **nothing** on save — may be success (no prints on OK path) or hotkey never firing.
- After changing `PracticeSnapshot`, TLV paths, or anything in `Practice_Save_Cb` / `Snapshot_*`.

## Build with trace

```bash
make practice -j4 PRACTICE_SAVE_TRACE=1
```

Default is `PRACTICE_SAVE_TRACE=0` (quiet ROMs). Toggle requires recompiling `practice_save.o` (and friends) so the define is picked up.

Flash from any checkout path:

```bash
./tools/sc64dev
```

(`tools/sc64dev` finds the repo Makefile with `practice:` by walking up from cwd; optional `SF64_REPO_ROOT`.)

## Reading `[save_tr]` (save path)

Expected order on a successful save:

1. `hotkey SAVE` — includes `scr=` practice screen: **`scr=1` is `PSCREEN_GAMEPLAY`** (hotkey wired). `scr=0` means level-select UI mode; save hotkey in `practice_main.c` is **not** active — use practice level select **A** to launch so `Practice_LaunchLevel` sets gameplay screen.
2. `SaveStateSlot enter` → `gates …` (same predicates as `Practice_CanSaveHere`) → `call slot_manager_save_ram`.
3. `cb enter` (payload buf, cap, `gPlayer`, `gCamCount`).
4. `fill begin` → `fill after players` → `fill after world arrays` → … → `fill done`.
5. `cb after Snapshot_FillFromGame` → many `cb after …` TLV stages → `cb done wr_sz=…`.
6. `slot_manager_save_ram ret=0`.

**Last line before hang** brackets the fault (e.g. stuck after `fill after players` once indicated stack blow-up before static scratch).

## Silent success vs no input

`Practice_SaveStateSlot` only `osSyncPrintf`s on **refusal** paths. A clean save produces **no** `[save]` refuse lines — that is normal. Use `[save_tr]` or `gPracticeLastSaveResult` / HUD if you need confirmation without ISV spam.

## Stack / BSS rule (critical)

`PracticeSnapshot` is **hundreds of KB**. It must **not** live as a function-local `PracticeSnapshot snap` on the game thread — N64 stacks are tiny and you get a **silent** crash as soon as fill or `bcopy` touches the bulk of the struct. Use a **file-static** scratch (e.g. `gPracticeSaveScratch`) for both save and load callbacks; `slot_manager` does not nest save inside load.

## Load path

`Practice_SaveTrace_LoadHotkeyIsv` + load TLV lines (`load tlv #N tag=…`) and `apply …` stages mirror save for load regressions.

## Cross-checks

- `Practice_CanSaveHere`: `GSTATE_PLAY`, `PLAY_UPDATE`, menu closed, overlay saveable, `gPlayer` non-NULL, P0 active.
- Expansion Pak: `gPracticeSaveDisabled`, pool at `0x80400000` from boot `[practice_save]` / `[heap]` lines.

## Related

- `debug-ram-layout` skill — overlap / BSS vs overlays when corruption is layout-related.
- `CLAUDE.md` — ISViewer SC64 protocol gotchas, deployer stdout flush, `tools/sc64dev` workflow.
