# Audio wedge after Practice_LaunchLevel restart

**Filed:** 2026-04-29
**Severity:** High — affects every practice session that uses radial RESTART or
cross-scene load, makes BGM unreliable for the rest of the session
**Phase 5 status:** Cross-scene load is functionally complete and reliable;
audio reapply is intermittent because of the bug described here. Phase 5 ships
with this as a known issue; this doc owns the follow-up.

## Symptoms (hardware-verified 2026-04-29 on SC64 + Expansion Pak)

1. Boot, launch any saveable level (e.g. Corneria) → BGM plays normally.
2. Open the practice radial menu, push stick toward `RESTART`, press A.
3. The level reloads. **BGM does not resume.** SFX (engine, lasers, explosions)
   continues to work — confirms the audio thread is alive but BGM specifically
   is muted.
4. From this point on, navigating to the practice level select or launching
   any other level via the menu **also leaves BGM silent** until the console
   is reset. SFX continues to function the entire time.

The same wedge surfaces during Phase 5 cross-scene load: BGM "works most of
the time but not always". When it fails, audio enters the same SFX-only state
that the restart path produces.

## What we know about the audio path

- `AUDIO_SET_SPEC(layout, spec)` expands to `Audio_SetAudioSpec(0, (layout << 8) | spec)`
  (`include/sf64audio_external.h:22`).
- `Audio_SetAudioSpec` queues a `SEQCMD_OP_RESET_AUDIO_HEAP` (audio_general.c).
- `Audio_ProcessSeqCmd` `SEQCMD_OP_RESET_AUDIO_HEAP` branch
  (`src/audio/audio_general.c:875-890`):

  ```c
  oldSpecId = sAudioSpecId;
  sAudioSpecId = specId;
  if (oldSpecId != specId) {
      AudioThread_ResetAudioHeap(specId);
      Audio_StartReset(oldSpecId);
      AUDIOCMD_GLOBAL_STOP_AUDIOCMDS();
  } else {
      Audio_StopSequence(SEQ_PLAYER_BGM, 1);
      Audio_StopSequence(SEQ_PLAYER_FANFARE, 1);
  }
  ```

  So calling `Audio_SetAudioSpec` with the **same** spec **stops BGM** without
  resetting the heap. Calling with a **different** spec triggers a real heap
  reset and stops audiocmds.

- `Audio_HandleReset()` (`audio_general.c:1182-1200`) ticks the reset state
  machine and returns `sAudioResetStatus`. While non-zero, `Audio_Update`
  skips `Audio_ProcessSeqCmds`, so any seq cmd queued in that window is
  buffered; whether it's processed cleanly afterward depends on the queue
  state when the reset finishes.

- `Practice_ServiceLevelSelectBgm` (`practice_level.c:107-136`) explicitly
  polls `Audio_HandleReset() != 0 → return`, then sets spec on one tick and
  plays BGM the next. **This pattern works** — the level-select BGM cycler
  is reliable.

- `Practice_LaunchLevel` (`practice_level.c:300+`) calls
  `Audio_SetAudioSpec(0, Practice_AudioSpecForLevel(levelId))` synchronously,
  with **no `Audio_HandleReset` gate** and no `AUDIO_PLAY_BGM`. The engine
  is supposed to start BGM via `Play_Init` once the transition completes.

- `Play_Init` (`fox_play.c:2695`) contains an `AUDIO_PLAY_BGM` block
  (`fox_play.c:4717-4757`) gated on `player->state == PLAYERSTATE_LEVEL_INTRO`
  for the start-demo BGM, plus an `else` that fires
  `AUDIO_PLAY_BGM(gBgmSeqId)` for the non-versus / non-AllRange-checkpoint
  case. Whether this path is reached on a same-level restart is one of the
  open questions below.

## Failed fix attempts (already reverted, recorded for reference)

### `eeb6d5c` (reverted in `18eef31`) — skip `Audio_SetAudioSpec` when bank already active

```c
void Practice_AudioApplyForLevel(LevelId levelId) {
    u16 packed = Practice_AudioSpecForLevel(levelId);
    if ((u8)(packed & 0xFFu) != gAudioSpecId) {
        Audio_SetAudioSpec(0, packed);
    }
}
```

Reasoning at the time: stacking redundant heap resets when the spec was already
right would wedge audio.

Why it broke things: the same-spec branch of `Audio_ProcessSeqCmd` is **not**
a no-op — it `Audio_StopSequence(SEQ_PLAYER_BGM, 1)`. Skipping the call meant
the BGM was never stopped, the engine's subsequent `AUDIO_PLAY_BGM` did not
cleanly take over, and the resulting state corrupted the BGM seq player for
the rest of the session.

Lesson: the same-spec call is **load-bearing** — it's how the engine signals
"stop the current BGM, please". Don't skip it.

### `cab02c8` (reverted in `95f7ab4`) — skip BGM reapply on cross-scene load

Snapshot apply skipped its own `AUDIO_PLAY_BGM` for cross-scene loads on the
theory that `Play_Init`'s LEVEL_INTRO path would fire BGM during the
transition. Hardware test result: cross-scene load was completely silent.
Either the LEVEL_INTRO path doesn't fire on our gNextGameState=GSTATE_PLAY
direct transition, or it fires too early to take.

Lesson: we **do** need to fire `AUDIO_PLAY_BGM` from snapshot apply for
cross-scene loads (deferred until audio reset settles). The intermittent
failure is somewhere else.

## Hypotheses to investigate (in priority order)

### H1 (most likely): Practice_LaunchLevel queues a reset that interferes with subsequent seq cmds

`Audio_SetAudioSpec(0, samePacked)` on restart fires the same-spec branch:
stops BGM via `Audio_StopSequence`. Audio thread processes that. Then the
engine's `Play_Init` runs and queues `AUDIO_PLAY_BGM(intro)`. If the audio
thread is still mid-stop or in some intermediate state, the play cmd lands
in a queue that doesn't drain.

**Verify by**: instrument `Practice_LaunchLevel` with `osSyncPrintf` around
the `Audio_SetAudioSpec` call, and the engine BGM start (`fox_play.c:4717`)
with a print of `gBgmSeqId` and `Audio_HandleReset()` value. Boot, restart,
look at the trace.

**Possible fix**: gate `Audio_SetAudioSpec` in `Practice_LaunchLevel` on
`Audio_HandleReset() == 0` like `Practice_ServiceLevelSelectBgm` does. If
audio is mid-reset, the launch should defer or busy-wait. Practice_LaunchLevel
runs synchronously today (no per-frame poll), so deferring means converting it
to a state-machine-driven launch.

### H2: Engine's BGM start path doesn't fire on same-level restart

The block at `fox_play.c:4717` is gated on `player->state == PLAYERSTATE_LEVEL_INTRO`.
If our direct-to-GSTATE_PLAY transition skips that state on restart (player
might land in `PLAYERSTATE_ACTIVE` directly because it was active when we
launched), BGM is never queued.

**Verify by**: add a print at the top of the `if (player->state == PLAYERSTATE_LEVEL_INTRO)`
block. Restart and observe whether the print fires.

**Possible fix**: explicitly set `gPlayer[0].state = PLAYERSTATE_LEVEL_INTRO`
in `Practice_LaunchLevel` after the gNext* writes — or call `AUDIO_PLAY_BGM`
ourselves from `Practice_LaunchLevel` after the `Audio_SetAudioSpec`.

### H3: Audio reset stacks specifically with the engine's intro BGM cmd

If `Practice_LaunchLevel`'s same-spec call is in flight (audio thread mid-
stopping BGM), and `Play_Init` fires an `AUDIO_PLAY_BGM` immediately, those
queue and the play cmd is lost.

**Verify by**: H1's instrumentation tells us whether this race window exists.

**Possible fix**: have `Practice_LaunchLevel` mark a pending BGM and let
Practice_Update poll `Audio_HandleReset` and fire the play, the same way the
load path does.

### H4: SEQ_PLAYER_BGM specifically gets disabled

`Audio_StopSequence(SEQ_PLAYER_BGM, 1)` may set `gSeqPlayers[BGM].enabled = false`.
A subsequent `AUDIO_PLAY_BGM` won't take if the player is disabled.
`Audio_PlaySequence`'s `SEQCMD_PLAY_SEQUENCE` should re-enable it, but only
if processed when `Audio_ProcessSeqCmds` runs.

**Verify by**: add a print of `gSeqPlayers[SEQ_PLAYER_BGM].enabled` after
launch.

## Fix applied 2026-04-30 (H3 — deferred rescue play)

`Practice_QueueBgmRescue(seqId, delayFrames)` defined in `practice_save.c`.
Called from `Practice_LaunchLevel` when `levelPacked == sBgmLastSpecPacked`
(same-spec launch detected). Sets `gPracticeBgmPending = true`,
`gPracticeBgmPendingSeqId`, `gPracticeBgmPendingDelay = 3`.

`Practice_Save_Tick` counts down `gPracticeBgmPendingDelay` each `PLAY_UPDATE`
frame (while `Audio_HandleReset() == 0`), then fires `AUDIO_PLAY_BGM`. The
3-frame countdown ensures the rescue fires after `Play_Init`'s own attempt and
any in-flight `isWaitingForFonts` font load has cleared.

`sBgmLastSpecPacked` is now also updated at the end of `Practice_LaunchLevel`
so subsequent same-level restarts are also detected.

**Hardware verification needed**: boot, launch a level whose level-select BGM
was already playing, verify BGM starts. Also test RESTART from radial menu.

## Suggested next session plan (if fix does not hold on hardware)

1. Build a diagnostic ROM with `PRACTICE_SAVE_TRACE=1` + extra prints in
   `Practice_LaunchLevel` (before and after `Audio_SetAudioSpec`, with values
   of `gAudioSpecId`, `Audio_HandleReset()`, `gSeqPlayers[BGM].enabled`).
2. Boot, launch Corneria, restart 3-5 times. Capture ISV log.
3. Match the trace against H1-H4. Whichever hypothesis matches the hardware
   behavior is the one to fix.
4. Apply the targeted fix. Re-test.

## Out of scope (do not touch in this fix)

- The level-select BGM cycler (`Practice_ServiceLevelSelectBgm`) — works fine.
- `Practice_AudioSpecForLevel` — single source of truth, not the issue.
- Phase 5 state machine, slot picker — solid; revisit only if the audio fix
  changes the load_cb / Snapshot_ApplyToGame signatures.
- The pre-existing same-spec `Audio_StopSequence` behavior in
  `audio_general.c` — that's the engine contract; fix our caller, not the
  contract.

## Related artifacts

- Phase 5 plan: `docs/superpowers/plans/2026-04-29-phase5-cross-scene-load.md`
- Phase 5 HW verify: `docs/superpowers/plans/HW_VERIFY_phase5.md`
- ISV trace skill: `.claude/skills/practice-hw-isv-trace/SKILL.md`
- Reverted attempts: `cab02c8`, `eeb6d5c` (commits in branch
  `worktree-user-requests`)
