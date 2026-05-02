# Phase 5 — cross-scene load + slot picker UI

> **Phase target:** make `Practice_LoadStateSlot` work when `gCurrentLevel`
> differs from the slot's saved level. Implement
> `practice_overlay_request_load`, the IDLE → AWAIT → APPLY → FAIL state
> machine, the audio bank reapply on cross-scene, and a slot picker UI
> sufficient to browse/save/load all 4 RAM slots. SD persistence is still
> Phase 7.

## 0. Status & input artefacts

### 0.0 What Phase 4 left us

| Topic | Today |
|-------|-------|
| `practice_overlay_request_load` | Logging stub — `[overlay] request_load` only |
| `Practice_LoadStateSlot` | Same-scene only. The load callback's overlay-restore branch refuses if `tlv_ov_build_id != cur_build` (logged as `partial overlay skipped`). A cross-scene load currently lands the snapshot but skips the overlay bytes. |
| `practice_overlay_build_id` | Returns 0 unless `id`'s overlay is the one currently resident in the shared VRAM slot. So a save in Corneria sees `ovl_i1`'s build id; a load in Sector Z couldn't compute Corneria's build id even if asked. |
| Audio | `TAG_AUDIO_SPEC_PACKED` is emit-only (`/* Phase 4 emit-only - Phase 5: Audio_SetAudioSpec. */`). Cross-scene loads silently keep the destination scene's audio bank. |
| `gNextLevel` / `gNextLevelPhase` / `gNextGameState` | `u16` engine globals at `src/engine/fox_game.c:15-17`. The `GSTATE_PLAY` transition sets `gNextGameStateTimer = 3` — same 3-frame fuse `Play_Init` rides today. |
| Slot UI | Root radial shows active slot # + `SAVED`/`EMPTY`. L/R triggers cycle slots while practice menu is open. No multi-slot picker, no per-slot metadata browse. |

### 0.1 Spec entry points (re-read once)

- `docs/superpowers/specs/2026-04-27-gz-style-features-design.md` — § "Save/load flow" (cross-scene state machine), § "Audio spec re-apply", § "Edge cases"
- `docs/superpowers/plans/2026-04-27-phase4-practice-save-tlv-and-overlay.md` § 0.0 — current shipped layout
- `src/practice/practice_overlay.c` — current Wave 2.1 stub for `request_load`; build-id "active overlay only" guard at lines 210-219
- `src/practice/practice_save.c` — load callback overlay-restore at lines 1181-1198

### 0.2 What is explicitly **not** in Phase 5

- SD slots, OSK, file browser → Phase 6/7
- Watches, config persistence → Phase 8/9
- ED64 SDIO backend → Phase 1b
- A user-visible "save name" field — Phase 5 keeps slot metadata to `(level_id, level_phase, frame_stamp)`. The `name` header field stays empty until Phase 7.
- Cross-scene **save** semantics — there is no such thing; you save in scene A, you load in any scene. The state machine only runs at load.

## 1. Decisions locked in

| Decision | Choice |
|---|---|
| Overlay build-id for non-resident overlays | Compute eagerly at boot for **all six** `ovl_iN`s by hashing their already-DMA'd-in-RAM image. SF64 loads every overlay during boot logo / title sequencing, so by the time `Practice_Save_Init` runs the cache can be populated. Drop the "current overlay only" guard. **Verify** during Wave 1: hash all six right after `Practice_Save_Init` and assert all are non-zero. If any overlay isn't resident at boot, fall back to "compute on first save in that overlay" and persist across scenes via a forced first-touch hash. |
| Slot pinning | The slot bytes already live in the Pak slot pool (VMA `0x80400000`) and survive across scene transitions — they were never on the game thread stack. Cross-scene load just defers the call to `slot_manager_load_ram(slot)` until we're in the destination scene. **No buffer copy.** |
| State machine ownership | Module-level statics in `practice_save.c`: `sCrossLoadState`, `sCrossLoadSlot`, `sCrossLoadStartFrame`. Polled from `Practice_Update` (already runs every frame). Survives menu close, scene transitions, etc. — it's not menu-scoped. |
| Apply timing | Wait for `gGameState == GSTATE_PLAY && gPlayState == PLAY_UPDATE && gCurrentLevel == target && gPlayer != NULL`. Then run `slot_manager_load_ram(slot)`. The current load callback already does the right thing post-`Play_Init`, including `Audio_SetAudioSpec` once we wire it (Wave 2). |
| Slot metadata for browse | Cache `(LevelId level, s32 phase, s32 frameStamp)` per slot in a `practice_save.c` static, populated on save, cleared on `clear_ram`. Not on disk. Disk metadata is a Phase 7 concern. |
| Timeout | `MAX_LOAD_FRAMES = 360` (6 s @ 60 fps). On timeout, log + reset. |
| Slot picker entry point | **Replaces the existing radial slot indicator** in `practice_menu.c`. The radial center previously showed only `(slot N + SAVED/EMPTY)`; it now renders a compact 4-row picker with cursor + per-slot metadata. L/R cycle still moves the cursor; A saves hovered; the existing radial SAVE/LOAD buttons act on the hovered slot instead of `gPracticeActiveSlot`. `gPracticeActiveSlot` itself remains and is what the cursor writes to. |
| Audio reapply | `TAG_AUDIO_SPEC_PACKED` decode finally lands. Apply via `Audio_SetAudioSpec(0, packed)` exactly like `Practice_LaunchLevel` already does at level launch. Same call site for both same-scene and cross-scene loads — same-scene loads also benefit (sets the right bank if you save during a transitional moment). |
| Refuse / error UX | Reuse existing `Practice_Hud_ShowStatus` toasts. New strings: `XSCENE WAIT`, `XSCENE OK`, `XSCENE FAIL`, `LOAD T/O`. |

## 2. Goal & exit criteria

**Goal:** save in Corneria, navigate to Sector Z (or back to map and pick a different stage), load Corneria's slot from gameplay or the slot picker, end up in Corneria with all snapshot state restored — pos, hit count, charge timers, audio bank, the works.

**Exit criteria** (each independently checkable):

- [ ] `practice_overlay_request_load(level, phase)` no longer logs-and-returns; it primes `gNextLevel` / `gNextLevelPhase` / `gNextGameState` / `gDrawMode` and sets the correct `Audio_SetAudioSpec` for the destination, replicating `Practice_LaunchLevel`'s audio dispatch.
- [ ] Cross-scene state machine: `Practice_LoadStateSlot` on a slot whose `level/phase` differs from `gCurrentLevel/gLevelPhase` does **not** call `slot_manager_load_ram` immediately; instead it kicks the transition and a per-frame poll completes the load when the destination scene reaches `PLAY_UPDATE`.
- [ ] Same-scene loads still complete in one frame (no behaviour regression, same code path as Phase 4).
- [ ] The load callback's overlay-restore branch unconditionally allows the byte copy when `tlv_ov_build_id == practice_overlay_build_id(gCurrentLevel)` regardless of which scene we saved from. Cross-scene loads on a same-build ROM bring overlay bytes back; mismatched-build saves still warn-and-skip.
- [ ] Audio bank gets reapplied on load via `Audio_SetAudioSpec`; same scene saved with one BGM and reloaded plays that BGM's instrument bank.
- [ ] Slot picker page in the practice menu lists all `gPracticeRamSlotCount` slots with `(slot N, LEVEL, phase, frame=…)` metadata, supports save/load/clear via D-pad + A/START/Y.
- [ ] Timeout path: forcing the destination scene to never reach `PLAY_UPDATE` (e.g. invalid `LevelId` injected via Lua) trips the timeout in ~10 s and clears state machine; no hang, no game-state mutation.
- [ ] Static invariants pass, including new `check_phase5_state_machine_lifecycle` and `check_overlay_build_id_eager_init`.
- [ ] Host unit tests pass (no new ones, but touched lib code re-runs).
- [ ] BizHawk functional tests pass:
  - `test_state_save_load_cross_scene.lua` — Corneria save → Sector Z navigate → load → assert back in Corneria with state.
  - `test_state_overlay_byte_restore_post_transition.lua` — same as above plus an overlay-byte sentinel check (write known byte into overlay before save, mutate after, assert restored).
  - `test_state_cross_scene_timeout.lua` — inject impossible target, assert timeout fires and gPracticeLastLoadResult reflects failure.
  - `test_state_slot_picker_navigates.lua` — picker D-pad + A/START exercise.
  - `test_state_audio_bank_restored.lua` — save under one bank, navigate (different bank), load, read `gAudioSpecId` (or whichever extern reflects the active spec) and assert match.
- [ ] `HW_VERIFY_phase5.md` exists with: cross-scene round trip, audio bank check, timeout smoke, picker UX walkthrough.

## 3. State machine

```
sCrossLoadState ∈ { IDLE, AWAIT_SCENE_LOAD, FAIL_PENDING }

Practice_LoadStateSlot(slot):
  if gPracticeSaveDisabled || bad slot || empty slot: refuse, IDLE
  meta := sSlotMeta[slot]
  if meta.level == gCurrentLevel && meta.phase == gLevelPhase:
      slot_manager_load_ram(slot)        # same-scene path, single frame
      stay IDLE
  else:
      sCrossLoadSlot      = slot
      sCrossLoadStartFrame= gGameFrameCount
      sCrossLoadState     = AWAIT_SCENE_LOAD
      practice_overlay_request_load(meta.level, meta.phase)
      Practice_Hud_ShowStatus("XSCENE WAIT", …)

Practice_Update tick (only when sCrossLoadState == AWAIT_SCENE_LOAD):
  if gGameState == GSTATE_PLAY
     && gPlayState == PLAY_UPDATE
     && gPlayer != NULL
     && gCurrentLevel == sSlotMeta[sCrossLoadSlot].level:
        rr = slot_manager_load_ram(sCrossLoadSlot)
        if rr == SLOT_MANAGER_OK: HUD "XSCENE OK"
        else:                     HUD "XSCENE FAIL"
        sCrossLoadState = IDLE
  elif gGameFrameCount - sCrossLoadStartFrame > MAX_LOAD_FRAMES: # 360 frames = 6s
        HUD "LOAD T/O"
        sCrossLoadState = IDLE
        gPracticeLastLoadResult = SLOT_MANAGER_ERR_TIMEOUT  # new code
```

`SLOT_MANAGER_ERR_TIMEOUT` is added to `lib/slot_manager.h` as a value the
**caller** uses; the lib itself doesn't drive the timeout.

## 4. File-by-file work

### 4.1 New files

- `tests/test_state_save_load_cross_scene.lua`
- `tests/test_state_overlay_byte_restore_post_transition.lua`
- `tests/test_state_cross_scene_timeout.lua`
- `tests/test_state_slot_picker_navigates.lua`
- `tests/test_state_audio_bank_restored.lua`
- `docs/superpowers/plans/HW_VERIFY_phase5.md`

### 4.2 Modified files

- `src/practice/practice_overlay.c`
  - Drop the "current overlay only" guard in `practice_overlay_build_id` after Wave 1 confirms eager-hash works.
  - New `practice_overlay_prime_build_ids()` called from `Practice_Save_Init`.
  - Implement `practice_overlay_request_load(level, phase)`: the same `gNextLevel = …; gNextLevelPhase = …; gNextGameState = GSTATE_PLAY; gDrawMode = DRAW_NONE` block `Practice_LaunchLevel` runs, plus the `AUDIO_SET_SPEC` switch table copied from `practice_level.c:311-331`. **Refactor**: extract the audio dispatch to a shared helper `Practice_AudioSpecForLevel(LevelId)` exposed in `practice.h`, so both `Practice_LaunchLevel` and `practice_overlay_request_load` share one source of truth.
- `src/practice/practice_overlay.h` — declare `practice_overlay_prime_build_ids`.
- `src/practice/practice_save.c`
  - Module statics: `sCrossLoadState`, `sCrossLoadSlot`, `sCrossLoadStartFrame`, `sSlotMeta[MAX_RAM_SLOTS_WITH_PAK]`.
  - New `Practice_Save_Tick` called from `Practice_Update`.
  - `Practice_SaveStateSlot`: stamp `sSlotMeta[slot]` after a successful save (level / phase / `gGameFrameCount`).
  - `Practice_LoadStateSlot`: branch on `meta.level == gCurrentLevel && meta.phase == gLevelPhase`. Cross-scene branch sets state machine.
  - `Practice_ClearCheckpoint`: also zero `sSlotMeta[]`.
  - `Snapshot_ApplyToGame`/`Practice_Save_LoadCb`: enable `TAG_AUDIO_SPEC_PACKED` apply via `Audio_SetAudioSpec(0, packed)`. Comment update.
  - Header peek helper `static bool ReadSlotMetaFromHeader(s32 slot, LevelId *out_level, s32 *out_phase)` for **recovering** metadata after a Practice_Save_Init in case slots persisted across reboot — Phase 5 doesn't survive reboot (RAM-only) so this is not strictly needed, but cheap to add now and it lets `Practice_HasCheckpoint`/picker show metadata correctly even if the in-memory `sSlotMeta` got blown away. Defer if it complicates Wave 1.
- `src/practice/practice_main.c`
  - Add `Practice_Save_Tick();` to `Practice_Update`.
  - Add `practice_overlay_prime_build_ids();` after `Practice_Save_Init` in `Practice_Init`.
- `src/practice/practice_level.c`
  - Replace the inlined `AUDIO_SET_SPEC` switch with `Practice_AudioSpecForLevel(levelId)`. Same exact mapping; refactor to keep parity with overlay's request_load.
- `src/practice/practice_state.c`
  - New menu page `Saves` with the slot list. Add to `OOPT_*` enum / dispatch, with `STATE_MENU_SAVES_DRAW` and `_UPDATE` functions.
- `include/practice.h`
  - `Practice_AudioSpecForLevel(LevelId)` declaration.
  - `Practice_Save_Tick(void)` declaration.
- `lib/slot_manager.h` — add `SLOT_MANAGER_ERR_TIMEOUT` to the error enum (value: next free negative). Lib-side no-op: only callers reference it.
- `tools/extract_symbols.py` — add `gPracticeAudioSpecId` (or whichever audio extern the BizHawk audio test reads; identify in Wave 2), the cross-load state machine fields if the picker test wants them.
- `tools/practice_invariants.py` — new checks (§5).

### 4.3 Tag registry

No new tags. `TAG_AUDIO_SPEC_PACKED` was emitted in Phase 4 and now gets a real decode, so `check_serializer_parity` already passes (one save site, one load case). The test that the apply actually fires is BizHawk-side.

### 4.4 New menu page (slot picker)

Layout sketch (40 char wide, monospace via `Practice_DrawText`):

```
SAVES
> 0  CORNERIA P1   F=12345 SAVED
  1  ----                   EMPTY
  2  SECTOR_Z  P2  F=20100 SAVED
  3  ----                   EMPTY

A SAVE  START LOAD  Y CLEAR
DPAD: SELECT     B: BACK
```

- Hovered row indicator: `>` and slight color shift.
- Disabled state when `gPracticeSaveDisabled`: page shows `SAVE DISABLED — NEEDS EXPANSION PAK` and inputs do nothing.
- `Practice_Hud_ShowStatus` toasts cover the result of each action; the page redraws on next frame (slot meta updates).

## 5. Static invariants

```python
def check_phase5_state_machine_lifecycle():
    """practice_save.c declares sCrossLoadState/sCrossLoadSlot/sCrossLoadStartFrame
       and Practice_Save_Tick. practice_main.c calls Practice_Save_Tick from
       Practice_Update."""

def check_overlay_build_id_eager_init():
    """practice_overlay.c exposes practice_overlay_prime_build_ids and
       practice_main.c calls it from Practice_Init AFTER Practice_Save_Init."""

def check_audio_spec_for_level_single_source():
    """Practice_AudioSpecForLevel exists in practice_level.c and is called from
       practice_overlay.c's request_load. The literal AUDIO_SET_SPEC table
       does NOT appear in practice_overlay.c."""
```

`check_overlay_table_complete` and `check_serializer_parity` from Phase 4
keep doing their jobs unchanged.

## 6. Tests

### 6.1 Static + host

- All existing checks must still pass.
- The three new invariants above each get a synthetic-fixture failure case.

### 6.2 BizHawk

- `test_state_save_load_cross_scene.lua`
  1. Boot, launch Corneria via practice level select.
  2. Wait `PLAY_UPDATE`.
  3. Stash `pos`, `hitCount`, `gGameFrameCount`.
  4. SAVE (`D_JPAD`).
  5. Open practice menu, navigate back to map / level select, launch SECTOR_Z.
  6. Wait for SECTOR_Z `PLAY_UPDATE`. Stash a value that proves we transitioned.
  7. LOAD (`U_JPAD`).
  8. Wait up to 600 frames for `gCurrentLevel == LEVEL_CORNERIA && gPlayState == PLAY_UPDATE && gPracticeLastLoadResult == 0`.
  9. Assert all stashed values restored.

- `test_state_overlay_byte_restore_post_transition.lua`
  1. Boot, launch Corneria.
  2. Read 16 bytes from a known offset inside `ovl_i1` VRAM.
  3. SAVE.
  4. Write a sentinel pattern over those bytes.
  5. Cross-scene navigate (forces overlay reload of a different `ovl_iN`).
  6. LOAD; wait for completion.
  7. Read those 16 bytes again — must equal the original.

- `test_state_cross_scene_timeout.lua`
  1. Save in Corneria.
  2. Inject a write to `sCrossLoadSlot`'s metadata `level` to a level that will never enter `PLAY_UPDATE` in the test (LEVEL_INVALID via Lua write into RDRAM).
  3. Trigger LOAD.
  4. Step ~700 frames.
  5. Assert `gPracticeLastLoadResult == SLOT_MANAGER_ERR_TIMEOUT` and `sCrossLoadState == 0`.

- `test_state_slot_picker_navigates.lua`
  1. Open practice menu → SAVES page.
  2. D-pad down twice — verify `sStateMenuSavesCursor == 2`.
  3. Press A — verify slot 2 saved.
  4. D-pad up — verify cursor moves.
  5. Press START on a saved slot — verify load fires.

- `test_state_audio_bank_restored.lua`
  1. Save in Corneria (audio spec CO).
  2. Navigate to Solar (audio spec SO uses `SFX_LAYOUT_SO`).
  3. Load slot.
  4. After completion, read whichever audio extern most directly reflects the active spec — confirm it's CO again. (Exact extern picked in Wave 2 once we audit `Audio_SetAudioSpec`.)

### 6.3 HW verification

`HW_VERIFY_phase5.md` mirrors the Phase 4 doc structure: prerequisites, deployer commands, the four scenarios above, and a save-trace bracketing reference for cross-scene saves.

## 7. Wave decomposition

Each wave = independent commit, full test stack passes (`make practice -j4`,
`python3 tools/practice_invariants.py`, `make lib-test`, `python3 tools/run_tests.py`),
plus the wave's own new tests pass.

1. **Wave 1 — overlay build-id eager init.**
   - Add `practice_overlay_prime_build_ids` and call it from `Practice_Init`.
   - Drop the active-overlay guard once Wave 1 verification on hardware (or BizHawk) shows all six hashes are non-zero post-boot.
   - Static invariant `check_overlay_build_id_eager_init`.
   - No behaviour change yet — saves and loads still same-scene only.

2. **Wave 2 — audio spec helper + decode.**
   - Extract `Practice_AudioSpecForLevel` from `practice_level.c`.
   - Wire `TAG_AUDIO_SPEC_PACKED` decode → `Audio_SetAudioSpec`.
   - `test_state_audio_bank_restored.lua` (same-scene variant, just to prove the wiring before cross-scene rides on top of it).
   - Static invariant `check_audio_spec_for_level_single_source`.

3. **Wave 3 — `practice_overlay_request_load` real implementation.**
   - Replicate `Practice_LaunchLevel`'s scene-launch sequence inside `practice_overlay_request_load` (no menu / HUD / checkpoint clear; just the engine transition + audio).
   - Manual smoke test: call from a one-shot debug button, confirm transition runs.
   - No state machine yet — load callback still refuses cross-scene.

4. **Wave 4 — cross-scene state machine.**
   - Add module statics + `Practice_Save_Tick`.
   - `Practice_LoadStateSlot` branches.
   - `Practice_SaveStateSlot` stamps `sSlotMeta`.
   - `Practice_ClearCheckpoint` clears `sSlotMeta`.
   - `Practice_Save_Tick` polls and runs `slot_manager_load_ram` when destination is reached.
   - Drop the same-build-ID-only "active overlay" pre-condition in load_cb's overlay-restore branch (already weakened by Wave 1).
   - `test_state_save_load_cross_scene.lua`.
   - `test_state_overlay_byte_restore_post_transition.lua`.
   - `test_state_cross_scene_timeout.lua`.
   - Static invariant `check_phase5_state_machine_lifecycle`.

5. **Wave 5 — slot picker replaces radial indicator.**
   - In `practice_menu.c`: replace the single-slot indicator (current line ~327) with a 4-row list showing each slot's level/phase/saved-frame.
   - Existing L/R bindings (`Practice_CycleSlot`) move the cursor; existing radial `SAVE`/`LOAD`/`EXP` buttons act on `gPracticeActiveSlot` (which the cursor sets) — no new button bindings needed.
   - Add a `CLR` (clear) entry to the radial buttons list, calling `slot_manager_clear_ram(gPracticeActiveSlot)` + `sSlotMeta` zero.
   - `test_state_slot_picker_navigates.lua`.

6. **Wave 6 — HW verify.**
   - `HW_VERIFY_phase5.md` checklist + run on real hardware (Pak required). User runs through the four scenarios; we paste results into the doc.

## 8. Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| Eager build-id hashes some overlay that hasn't been DMA'd into RAM yet at boot | Medium | Wave 1 verifies all six hashes are non-zero post-`Practice_Save_Init`. If any are zero, fall back to lazy on-first-touch hashing keyed on `gCurrentLevel`'s overlay; cache only across scenes whose owning overlay we've actually touched. |
| `practice_overlay_request_load` runs from menu-frozen path and engine objects to mid-frame transition | Medium | Phase 4's radial save path proved engine state can be captured/poked while `PMENU_OPEN_FROZEN`. Cross-scene load only fires when menu closes (LOAD button clears menu) or from gameplay, and `gNextGameState` machinery is exactly what `Practice_LaunchLevel` already uses safely. |
| Audio glitch / pop on bank reapply | Low-Medium | Spec already accepts this for v1. Same machinery `Practice_LaunchLevel` uses, so behaviour is no worse than launching the saved level via menu. |
| Slot meta lost across `Practice_Save_Init` reinit | Low | Phase 5 RAM slots are session-only; `Practice_Save_Init` runs once at boot. Header-peek helper covers the case anyway. |
| Cross-scene timeout fires falsely on long boss intros | Medium | 360 frames = 6 s. Looser than every same-scene LOAD we've measured but tighter than worst-case boss-intro setup. If it bites we widen to 600 (10 s). User can re-trigger LOAD if it does fire spuriously. |
| Overlay byte restore mismatches new build's layout | Already covered | `tlv_ov_build_id != cur_build` already skips the byte copy with a warning; the non-overlay tags still apply. |
| State machine survives across `Practice_Init` re-runs (e.g. soft reset) | Low | `Practice_Init` zeroes `sCrossLoadState` explicitly. Add to existing init bzero. |
| Slot picker's `LEVEL` text exceeds the small-text font | Low | Names are existing constants (`CORNERIA`, `SECTOR_Z`, etc.) that already render today. |

## 9. Open questions (deferred to implementation)

- Whether the slot-picker page lives behind a new `STATE_MENU_SAVES` enum or replaces the existing root radial save indicator. Default: keep both — radial stays for one-press save/load on active slot, picker is the multi-slot browser.
- Whether `Practice_Save_Tick` polls every frame or only when `sCrossLoadState != IDLE`. Default: early-return on IDLE for zero-cost; the state machine is dormant 99.9% of the time.
- Whether `Audio_SetAudioSpec` needs an `Audio_ClearVoice` companion call as the spec mentions. We'll match `Practice_LaunchLevel`'s pattern (no explicit ClearVoice today). If audio glitches show up in Wave 2 testing, add the ClearVoice; otherwise omit.
- Naming of `gPracticeLastLoadResult == SLOT_MANAGER_ERR_TIMEOUT` vs. a separate `gPracticeCrossLoadResult`. Default: reuse the existing field; timeout is a load failure mode like any other.

## 10. Verification matrix

| Item | Command | Pass |
|---|---|---|
| Static invariants | `python3 tools/practice_invariants.py` | exit 0 |
| Host lib tests | `make lib-test` | pass |
| Build | `make practice -j4` | exit 0 |
| Symbols regenerate | `python3 tools/extract_symbols.py > tests/symbols.lua` | new symbols present |
| Cross-scene save/load | `python3 tools/run_tests.py test_state_save_load_cross_scene` | PASSED |
| Overlay byte restore post-transition | `python3 tools/run_tests.py test_state_overlay_byte_restore_post_transition` | PASSED |
| Cross-scene timeout | `python3 tools/run_tests.py test_state_cross_scene_timeout` | PASSED |
| Slot picker navigation | `python3 tools/run_tests.py test_state_slot_picker_navigates` | PASSED |
| Audio bank restored | `python3 tools/run_tests.py test_state_audio_bank_restored` | PASSED |
| HW verify | `HW_VERIFY_phase5.md` | non-empty results |

When all rows green and the maintainer signs off the HW verify row,
Phase 5 is complete and Phase 6 (file_browser + OSK) can start.
