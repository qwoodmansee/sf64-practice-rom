# Phase 5 — cross-scene load (hardware verification)

> **Agent handoff:** Phase 5 adds a state machine that lets a save in
> scene A be loaded from scene B. The slot bytes already lived in the
> Pak slot pool (`practice_save_slotpool.c`, VMA `0x80400000`) and stay
> pinned; the new state machine fires `practice_overlay_request_load`
> to drive the engine transition, then runs `slot_manager_load_ram`
> once the destination scene reaches `PLAY_UPDATE`. **Stock 4 MB stays
> save-disabled** unchanged. Build / flash via `./tools/sc64dev`.

## Current state (read first)

| Topic | Behaviour |
|-------|-----------|
| Same-scene load | Single-frame slot_manager_load_ram, unchanged from Phase 4. |
| Cross-scene load | `Practice_LoadStateSlot` checks `gPracticeSlotMeta[slot]`; if `level/phase` differ from `gCurrentLevel/gLevelPhase`, the state machine takes over. HUD shows `XSCENE WAIT`, then `XSCENE OK` / `XSCENE FAIL` / `LOAD T/O`. |
| State machine timeout | 360 frames (6 s @ 60 fps). On expiry: `gPracticeLastLoadResult = SLOT_MANAGER_ERR_TIMEOUT (-10)`, state machine resets to `XLOAD_IDLE`, no game-state mutation. |
| Audio bank reapply | `Audio_SetAudioSpec(0, gPracticeSlotMeta[slot] -> packed)` is called from `practice_overlay_request_load` (matches `Practice_LaunchLevel`). `TAG_AUDIO_SPEC_PACKED` decode also calls `Audio_SetAudioSpec` from `Snapshot_ApplyToGame` so same-scene loads also restore the saved bank. |
| Overlay byte restore | Now happens regardless of which scene we saved from. Build-id check still gates the byte copy: matching build → bytes restored, mismatched build → bytes skipped with `WARN partial overlay skipped`. |
| Slot picker UI | Radial center now shows all slots: `SLOT N LEVEL Pphase SAVED/EMPTY`. Active slot has a small chevron box. L/R triggers cycle. |
| Build-id cache | Eagerly populated for all six `ovl_iN` from `Practice_Init` (`practice_overlay_prime_build_ids`). Hash is over linker / DMA-table addresses on the stack; never reads `vRomAddress` bytes. |

## Purpose

1. Confirm cross-scene transitions from save → load work end-to-end on real hardware (engine transition timing differs slightly from BizHawk).
2. Confirm the audio bank actually reapplies (BizHawk's audio engine isn't authoritative for instrument-bank state).
3. Confirm the timeout fires cleanly when a load can't complete (and the game keeps running).
4. Sanity-check the slot picker UI in the radial menu.

## Prerequisites

- SC64 + IS-Viewer path working (see Phase 4 doc).
- Patched `sc64deployer` (see `CLAUDE.md`).
- Expansion Pak installed (`osMemSize == 0x00800000`). Without it, save/load is disabled and Phase 5 has nothing to verify.

## Terminal A — IS-Viewer

```bash
sc64deployer debug --isv 0x03FF0000
```

## Build & flash

```bash
./tools/sc64dev
```

For triaging cross-scene hangs, build with `PRACTICE_SAVE_TRACE=1`:

```bash
PRACTICE_SAVE_TRACE=1 ./tools/sc64dev build
./tools/sc64dev upload
```

ISV will then bracket the cross-scene path with `[save_tr] xscene apply slot=N level=...`.

## Scenario 1 — same-scene save / load (regression check)

1. Boot, choose **CORNERIA** in practice level select, A to launch.
2. Wait for `PLAY_UPDATE` (gameplay scrolls, music plays).
3. Press **L + D-Left** (save) — HUD shows `SAVE OK`. ISV log: nothing extra without trace.
4. Move the Arwing, take a hit, fire some shots.
5. Press **L + D-Right** (load) — HUD shows `LOAD OK`. State snaps back.

**PASS:** game keeps running, position / hit count / charge state restored. **FAIL:** any crash, freeze, or `LOAD FAIL`.

## Scenario 2 — cross-scene save / load (the new path)

1. Boot, launch **CORNERIA**, wait for `PLAY_UPDATE`.
2. Save (L + D-Left). HUD: `SAVE OK`.
3. Open practice menu (R + D-Right), select **LEVELS**, pick **SECTOR Z** or **METEO**, A to launch. Wait for the new scene's `PLAY_UPDATE`.
4. Confirm `gCurrentLevel != LEVEL_CORNERIA` from the radial slot picker (active slot should still show `CO P0 SAVED` since the slot didn't move).
5. Press **L + D-Right** (load).
6. HUD shows `XSCENE WAIT`. Engine transitions back to Corneria. After ~1–3 s, HUD shows `XSCENE OK`.

**PASS:** Arwing position, hit count, audio bank are all back to the save point. Music sounds like Corneria again (not Sector Z's leftover instruments).

**FAIL:**
- HUD stuck on `XSCENE WAIT` past 6 s → state machine should fall through to `LOAD T/O`. If it doesn't, that's a regression.
- `XSCENE FAIL` → check ISV log for slot_manager error code.
- Wrong audio bank after load → `Audio_SetAudioSpec` didn't take. Try same-scene load on the saved level too.
- Crash / freeze → bracket with `PRACTICE_SAVE_TRACE=1` and report the last `[save_tr]` line.

## Scenario 3 — cross-scene timeout

This is harder to trigger naturally on hardware. The BizHawk test (`test_state_cross_scene_timeout.lua`) corrupts `gPracticeSlotMeta[0].level` to `LEVEL_INVALID` from the host. On hardware, we can approximate by:

1. Save in Corneria.
2. From level select, launch **VENOM 2** (or any other large scene).
3. Wait for `PLAY_UPDATE`.
4. Trigger a load.
5. **If** `XSCENE OK` appears within 6 s — system works fine, timeout will not fire (skip this scenario; the path is exercised by BizHawk).
6. **If** the engine ever stalls before `PLAY_UPDATE` for >6 s on hardware specifically (rare), you should observe HUD `LOAD T/O` and game continues running. Report the scene + scenario.

**PASS by absence:** no hang ever observed past 6 s, so timeout never fires under real load. **FAIL:** HUD freezes or shows `XSCENE WAIT` indefinitely.

## Scenario 4 — slot picker walkthrough

1. Boot, launch CORNERIA, wait gameplay.
2. Open the practice menu (R + D-Right). Confirm the center area now lists **4 slots** (rows: `SLOT idx LEVEL Pphase SAVED/EMPTY`).
3. Press **R trigger** alone → cursor (chevron box) moves to slot 1.
4. Press **R** twice more → slot 3.
5. Press **R** once more → wraps to slot 0.
6. Press **L trigger** alone → wraps to slot 3.
7. From slot 3, save (stick → SAVE slice, A). HUD: `SAVE OK`. Picker row 3 should now read `CO P0 SAVED`.

**PASS:** picker reflects state immediately, cursor wraps, saves go into the cursor's slot. **FAIL:** any slot mis-rendered, cursor stuck, or save hits the wrong slot.

## Scenario 5 — overlay build-id cache prime

Build with `PRACTICE_SAVE_TRACE=1`. After `[save_tr]` boot lines, check that `practice_overlay_prime_build_ids` doesn't crash (boot completes; ISV stays alive). All six overlays should have non-zero build ids cached after `Practice_Save_Init`.

There is no on-ROM probe for the cache contents in Phase 5. If you want explicit logging, hardcode an `osSyncPrintf` to `practice_overlay_prime_build_ids` for a one-off diagnostic build and report the six u32 values.

## Reporting results

Paste IS-Viewer log excerpts plus a brief PASS/FAIL per scenario back into this file under a new `## Run YYYY-MM-DD` section. The maintainer signs off when scenarios 1, 2, and 4 pass on real hardware.

## Local status — 2026-04-29

- Phase 5 implementation complete: Wave 1 (eager build-id), Wave 2 (audio spec helper + decode), Wave 3 (request_load real impl), Wave 4 (state machine), Wave 5 (slot picker).
- Verified locally: static invariants, `make practice -j4`, `make lib-test`.
- BizHawk functional tests not run locally (`BIZHAWK_PATH` unset).
- Hardware verification pending for scenarios 1, 2, 4. Scenarios 3 and 5 are best-effort.
