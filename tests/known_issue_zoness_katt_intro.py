"""RESOLVED 2026-07-11 -- kept out of the suite (known_issue_ prefix).

The crash this file hunted was root-caused WITHOUT it ever reproducing:
the scene asset window (floating after `main`) had grown across the fixed
buffers segment at 0x80281000, so Zoness's ast_allies segment -- including
aKattShipDL and her portrait -- was DMA'd into gZBuffer and shredded by RDP
depth writes every frame; Katt's first appearance executed Z-buffer bytes
as her ship's display list and wedged the RDP. See
docs/superpowers/specs/2026-07-11-scene-window-zbuffer-overlap.md.

This mechanism is STRUCTURALLY invisible to mupen64plus (HLE plugins never
write the Z-buffer back to RDRAM), so no emulator test can regression-guard
it. The guards are the static invariants check_scene_stack_fits_buffers /
check_late_pak_segment plus the hardware checklist
docs/superpowers/plans/HW_VERIFY_late_pak_migration.md.

The file is retained (out of the suite) for its launch-bypass lessons:
the direct gNextLevel memory-write launch does NOT tick the ActorAllRange
actor, so event-timer-driven cutscenes (Katt's included) never fire that
way -- details in the original status note below.

--- original header follows ---

Repro attempt: crash right after Katt's first Zoness dialogue line.

User report (real hardware): crash happens right after Katt speaks for the
first time in Zoness, and her portrait renders corrupted right before it --
a "scrambled graphics near a crash" symptom this project has hit before
(Aquas crash, HIT64 logo corruption), suggesting a RAM overlap rather than
a generic timing/soak issue. See tests/test_zoness_extended_soak.py for the
earlier, broader (and inconclusive) 2-minute soak attempt -- this test
targets the specific event instead of grinding for minutes of gameplay.

STATUS (2026-07-09): does not currently trigger the Katt cutscene at all.
The direct-memory-write level launch this file uses (same bypass pattern
as the existing tests/test_zoness_crash.py) does NOT spawn/tick the
ActorAllRange actor that drives gAllRangeEventTimer -- confirmed via a
10-probe diagnostic (300 frames, timer stayed at 0 the whole time; visually
confirmed too, no Katt appeared). Practice_LaunchLevel() sets several
fields this bypass doesn't (gNextLevelPhase, gClearPlayerInfo); poking
those directly did NOT help and instead broke reaching GSTATE_PLAY at all
(gNextLevelPhase is suspected to be packed with another field, similar to
gNextLevel/gNextGameState -- writing a bare 0 likely zeroed something
else out). Reverted those pokes; this file is back to the last known
"reaches gameplay but never ticks the AllRange actor" state.

Next step if resumed: launch Zoness through real UI navigation (A-button
press through level select, like ctx.select_and_launch_level() does for
Corneria) instead of the memory-write bypass, so Practice_LaunchLevel's
full real setup runs. That path can't directly force the missile-count
trigger the way this file's write-gAllRangeSpawnEvent-directly approach
intended to -- would need either simulated missile kills or a different
way to reach the trigger once actually in normal gameplay.

Root cause: unknown. Written before any code changes.

Trigger mechanism (src/overlays/ovl_i4/fox_sz.c):
  SectorZ_KattCutscene() (state machine case 2 -> case 3) fires when
  `gAllRangeEventTimer == gAllRangeSpawnEvent`. Normally gAllRangeSpawnEvent
  only gets set to a real (non-"never") value after destroying 3 missiles
  AND gLeveLClearStatus[LEVEL_ZONESS] != 0 (fox_sz.c:74-76). Rather than
  play through that condition, this test sets both directly:
    - gLeveLClearStatus[LEVEL_ZONESS] = 1
    - gAllRangeSpawnEvent = gAllRangeEventTimer + 5
  gAllRangeEventTimer free-runs +1/frame during normal gameplay
  (src/engine/fox_360.c:728), so this reaches the real trigger condition
  in ~5 frames instead of ~1-3 minutes of missile-shooting. Once the
  cutscene state (case 3) is active, gCsFrameCount increments every frame
  (fox_sz.c:552) -- used here as the "cutscene actually started" signal.
  Her first line fires at cutscene-actor timer_0BC == 370 (not directly
  readable without ActorCutscene struct offsets), so this test just runs
  long enough (600 frames / 10s) to comfortably cover the whole sequence
  and watches for the same anomaly signature test_zoness_extended_soak.py
  found (gGameFrameCount going backward, gGameState/gPlayState leaving
  their expected values).

Test approach: launch Zoness, let the all-range state machine reach its
normal state 2, force the Katt trigger, then watch closely (small
checkpoints) through the whole cutscene window. Asserts HEALTHY progression
(same polarity rationale as test_zoness_extended_soak.py -- the exact
bad-state signal isn't known yet).
"""

LEVEL_ZONESS = 8

GSTATE_MAP = 4
GSTATE_PLAY = 7
PLAY_UPDATE = 2
PSCREEN_GAMEPLAY = 1

CHUNK_FRAMES = 30            # 0.5s per checkpoint -- fine enough to localize a glitch
TOTAL_FRAMES = 600           # 10s, comfortably past the whole dialogue sequence
MIN_FRAMES_PER_CHUNK = 20    # allow some slack for lag frames


def _read_s32(h, addr):
    val = h.read32(addr)
    return val - 0x100000000 if val >= 0x80000000 else val


def _read_u8(h, addr):
    word = h.read32(addr & ~3)
    shift = 8 * (3 - (addr & 3))
    return (word >> shift) & 0xFF


def _write_u8(h, addr, value):
    word_addr = addr & ~3
    word = h.read32(word_addr)
    shift = 8 * (3 - (addr & 3))
    mask = 0xFF << shift
    word = (word & ~mask) | ((value & 0xFF) << shift)
    h.write32(word_addr, word & 0xFFFFFFFF)


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    _GAME_STATE = S["gGameState"]
    _PLAY_STATE = S["gPlayState"]
    _GAME_FRAME_COUNT = S["gGameFrameCount"]
    _NEXT_LEVEL_WORD = S["gNextLevel"]
    _PRACTICE_SCREEN = S["gPracticeScreen"]
    _LEVEL_CLEAR_STATUS = S["gLeveLClearStatus"]
    _ALL_RANGE_EVENT_TIMER = S["gAllRangeEventTimer"]
    _ALL_RANGE_SPAWN_EVENT = S["gAllRangeSpawnEvent"]
    _CS_FRAME_COUNT = S["gCsFrameCount"]

    ok = h.wait_for(_GAME_STATE, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "Booted to level select")
    if not ok:
        return

    h.advance(10)

    # Launch Zoness directly, bypassing Practice_LaunchLevel navigation.
    # Also set gNextLevelPhase/gClearPlayerInfo, which Practice_LaunchLevel
    # sets but the m64p-repro skill's documented bypass pattern doesn't --
    # suspected missing piece for why the AllRange actor wasn't ticking.
    h.write32(_NEXT_LEVEL_WORD, (LEVEL_ZONESS << 16) | GSTATE_PLAY)
    h.write32(_PRACTICE_SCREEN, PSCREEN_GAMEPLAY)

    ok = h.wait_for(_GAME_STATE, GSTATE_PLAY, 15000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY")
    if not ok:
        return
    ok = h.wait_for(_PLAY_STATE, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE")
    if not ok:
        return

    # DIAGNOSTIC: find out when (if ever) gAllRangeEventTimer starts
    # incrementing under the direct-launch bypass, before committing to a
    # fixed settle window.
    for probe in range(10):
        h.advance(30)
        t = ctx.read_s32(_ALL_RANGE_EVENT_TIMER)
        print(f"  probe {probe}: frame {(probe + 1) * 30}, gAllRangeEventTimer={t}")

    # --- Force the Katt trigger ---
    _write_u8(h, _LEVEL_CLEAR_STATUS + LEVEL_ZONESS, 1)
    timer_now = ctx.read_s32(_ALL_RANGE_EVENT_TIMER)
    ctx.write_s32(_ALL_RANGE_SPAWN_EVENT, timer_now + 5)
    print(f"  gAllRangeEventTimer={timer_now}, set gAllRangeSpawnEvent={timer_now + 5}")

    h.advance(30)
    cs_frame_count = ctx.read_s32(_CS_FRAME_COUNT)
    print(f"  gCsFrameCount after trigger window: {cs_frame_count}")
    ctx.assert_true(cs_frame_count > 0, "Katt cutscene state entered (gCsFrameCount advancing)")

    # --- Watch closely through the whole dialogue sequence ---
    total_chunks = TOTAL_FRAMES // CHUNK_FRAMES
    frame_prev = ctx.read_s32(_GAME_FRAME_COUNT)
    all_healthy = True

    for chunk in range(1, total_chunks + 1):
        h.advance(CHUNK_FRAMES)

        game_state = h.read32(_GAME_STATE)
        play_state = h.read32(_PLAY_STATE)
        frame_now = ctx.read_s32(_GAME_FRAME_COUNT)
        advanced = frame_now - frame_prev
        frame_prev = frame_now

        elapsed_ms = chunk * CHUNK_FRAMES * 1000 // 60
        print(f"  [{elapsed_ms:5d}ms] gGameFrameCount={frame_now} "
              f"(+{advanced}/{CHUNK_FRAMES}) gGameState={game_state} "
              f"gPlayState={play_state}")

        healthy = (
            game_state == GSTATE_PLAY
            and play_state == PLAY_UPDATE
            and advanced >= MIN_FRAMES_PER_CHUNK
        )
        ctx.assert_true(
            healthy,
            f"Zoness alive at {elapsed_ms}ms after Katt trigger "
            f"(gGameState={game_state}, gPlayState={play_state}, "
            f"advanced={advanced}/{CHUNK_FRAMES})",
        )
        if not healthy:
            all_healthy = False
            break

    if all_healthy:
        print(f"  Completed {TOTAL_FRAMES * 1000 // 60}ms after the Katt trigger without a hang/crash signal")
