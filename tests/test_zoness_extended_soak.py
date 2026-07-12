"""Repro attempt: Zoness crash reported on real hardware after extended play.

User report: witnessed a crash on real hardware after playing Zoness for a
while (not immediately on load). This is DIFFERENT from the already-fixed,
already-regression-tested crash in tests/test_zoness_crash.py, which only
covers the first ~15 seconds (900 frames) of gameplay.

Root cause: unknown. Written before any code changes, to establish a repro
baseline per the project's "write the test first" bug workflow.

Test approach: launch Zoness via tests/_zoness_common.py (direct memory
write, per the m64p-repro skill). Run the soak window in 10-second
checkpoints, asserting after each chunk that gGameFrameCount kept advancing
and gGameState is still GSTATE_PLAY. Checkpoint prints are flushed
immediately, so a genuine in-frame hang (which blocks the harness's ADVANCE
command from returning at all) still leaves every prior checkpoint's output
visible even when the runner's stdout is piped/block-buffered.

Soak length: the default (ZONESS_SOAK_FRAMES unset) is 1800 frames (~30s of
gameplay), sized so the whole test -- boot + launch + soak -- stays inside
the project's 90-second functional-test budget when the full suite runs
(measured 2026-07-11: 2400 frames ran 88.5s wall, too tight for variance).
For a long manual soak (the original ~2-minute hardware-repro window), run:

    ZONESS_SOAK_FRAMES=7200 python3 tools/m64p_test_runner.py test_zoness_extended_soak

NOTE: this test asserts HEALTHY progression (opposite of the usual
"assert the bug is present" repro convention) because the exact bad-state
signal for this crash isn't known yet -- there's nothing specific to
assert against until we see whether/how it manifests here. If mupen64plus
never fails this test, that's consistent with this session's broader
finding that most of today's hardware bugs are invisible to emulation --
useful information, not a dead end.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _zoness_common import (
    GSTATE_PLAY, PLAY_UPDATE, launch_zoness, read_s32,
)

TOTAL_FRAMES = int(os.environ.get("ZONESS_SOAK_FRAMES", "1800"))
CHUNK_FRAMES = 600           # ~10s per checkpoint
MIN_FRAMES_PER_CHUNK = 500   # allow some slack for lag frames, still catch a hang


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    _GAME_STATE = S["gGameState"]
    _PLAY_STATE = S["gPlayState"]
    _GAME_FRAME_COUNT = S["gGameFrameCount"]

    if not launch_zoness(ctx):
        return

    # Let the level settle before starting the timed soak.
    h.advance(60)

    total_chunks = TOTAL_FRAMES // CHUNK_FRAMES
    frame_prev = read_s32(h, _GAME_FRAME_COUNT)
    all_healthy = True

    for chunk in range(1, total_chunks + 1):
        h.advance(CHUNK_FRAMES)

        game_state = h.read32(_GAME_STATE)
        play_state = h.read32(_PLAY_STATE)
        frame_now = read_s32(h, _GAME_FRAME_COUNT)
        advanced = frame_now - frame_prev
        frame_prev = frame_now

        elapsed_s = chunk * CHUNK_FRAMES // 60
        print(f"  [{elapsed_s:3d}s] gGameFrameCount={frame_now} "
              f"(+{advanced}/{CHUNK_FRAMES}) gGameState={game_state} "
              f"gPlayState={play_state}", flush=True)

        # gGameFrameCount legitimately RESETS mid-soak: an unattended run
        # eventually flies into terrain, dies, and the level restarts
        # (observed 2026-07-11: 1862 -> 74 at the 40s mark, states still
        # PLAY/UPDATE). A reset means the game is alive and ticking from 0,
        # not hung -- a genuine in-frame wedge would block advance() from
        # returning at all. Only a *stalled* counter with sane states, or
        # bad states, count as unhealthy.
        counter_reset = advanced < 0
        if counter_reset:
            print("         (frame counter reset -- death/restart; still alive)",
                  flush=True)
        healthy = (
            game_state == GSTATE_PLAY
            and play_state == PLAY_UPDATE
            and (advanced >= MIN_FRAMES_PER_CHUNK or counter_reset)
        )
        ctx.assert_true(
            healthy,
            f"Zoness alive at {elapsed_s}s "
            f"(gGameState={game_state}, gPlayState={play_state}, "
            f"advanced={advanced}/{CHUNK_FRAMES})",
        )
        if not healthy:
            all_healthy = False
            break

    if all_healthy:
        print(f"  Completed {TOTAL_FRAMES // 60}s of Zoness gameplay "
              "without a hang/crash signal", flush=True)
