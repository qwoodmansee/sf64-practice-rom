"""Repro attempt: Zoness crash reported on real hardware after extended play.

User report: witnessed a crash on real hardware after playing Zoness for a
while (not immediately on load). This is DIFFERENT from the already-fixed,
already-regression-tested crash in tests/test_zoness_crash.py, which only
covers the first ~15 seconds (900 frames) of gameplay.

Root cause: unknown. Written before any code changes, to establish a repro
baseline per the project's "write the test first" bug workflow.

Test approach: launch Zoness (bypassing Practice_LaunchLevel via a direct
memory write, per the m64p-repro skill -- button injection isn't reliable
for sustained gameplay since the OS controller read overwrites
gControllerPress every frame). Run ~2 minutes (7200 VI frames) in 10-second
checkpoints, asserting after each chunk that gGameFrameCount kept advancing
and gGameState is still GSTATE_PLAY. Checkpoint prints give partial
visibility into how far it got even if a later chunk hangs -- Python's
stdout is flushed after each chunk, so a genuine in-frame hang (which
blocks the harness's ADVANCE command from returning at all) still leaves
every prior checkpoint's output visible.

NOTE: this test asserts HEALTHY progression (opposite of the usual
"assert the bug is present" repro convention) because the exact bad-state
signal for this crash isn't known yet -- there's nothing specific to
assert against until we see whether/how it manifests here. If mupen64plus
never fails this test, that's consistent with this session's broader
finding that most of today's hardware bugs are invisible to emulation --
useful information, not a dead end.

Runtime: ~2 minutes of simulated gameplay plus boot/launch overhead.
"""

LEVEL_ZONESS = 8

GSTATE_MAP = 4
GSTATE_PLAY = 7
PLAY_UPDATE = 2
PSCREEN_GAMEPLAY = 1

TOTAL_FRAMES = 7200          # ~2 minutes at 60 VI/s
CHUNK_FRAMES = 600           # ~10s per checkpoint
MIN_FRAMES_PER_CHUNK = 500   # allow some slack for lag frames, still catch a hang


def _read_s32(h, addr):
    val = h.read32(addr)
    return val - 0x100000000 if val >= 0x80000000 else val


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    _GAME_STATE = S["gGameState"]
    _PLAY_STATE = S["gPlayState"]
    _GAME_FRAME_COUNT = S["gGameFrameCount"]
    _NEXT_LEVEL_WORD = S["gNextLevel"]
    _PRACTICE_SCREEN = S["gPracticeScreen"]

    ok = h.wait_for(_GAME_STATE, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "Booted to level select")
    if not ok:
        return

    h.advance(10)

    # Launch Zoness directly, bypassing Practice_LaunchLevel navigation.
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

    # Let the level settle before starting the timed soak.
    h.advance(60)

    total_chunks = TOTAL_FRAMES // CHUNK_FRAMES
    frame_prev = _read_s32(h, _GAME_FRAME_COUNT)
    all_healthy = True

    for chunk in range(1, total_chunks + 1):
        h.advance(CHUNK_FRAMES)

        game_state = h.read32(_GAME_STATE)
        play_state = h.read32(_PLAY_STATE)
        frame_now = _read_s32(h, _GAME_FRAME_COUNT)
        advanced = frame_now - frame_prev
        frame_prev = frame_now

        elapsed_s = chunk * CHUNK_FRAMES // 60
        print(f"  [{elapsed_s:3d}s] gGameFrameCount={frame_now} "
              f"(+{advanced}/{CHUNK_FRAMES}) gGameState={game_state} "
              f"gPlayState={play_state}")

        # gGameFrameCount legitimately RESETS mid-soak: an unattended run
        # eventually flies into terrain, dies, and the level restarts
        # (observed 2026-07-11: 1862 -> 74 at the 40s mark, states still
        # PLAY/UPDATE). A reset means the game is alive and ticking from 0,
        # not hung -- a genuine in-frame wedge would block advance() from
        # returning at all. Only a *stalled* counter with sane states, or
        # bad states, count as unhealthy.
        counter_reset = advanced < 0
        if counter_reset:
            print(f"         (frame counter reset -- death/restart; still alive)")
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
        print(f"  Completed {TOTAL_FRAMES // 60}s of Zoness gameplay without a hang/crash signal")
