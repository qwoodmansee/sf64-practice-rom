"""Regression: Zoness must not freeze during the first ~15s of gameplay.

History: Zoness used to crash/freeze ~5-10 seconds in. Originally an inverted
bug-repro test (passed while the bug was present); flipped to regression format
2026-06-12 after the fixed ROM ran the full 900-frame window in emulator.

Test approach:
  Launch Zoness via direct memory write. Let the level run for 15 seconds (900
  frames) past the PLAY_UPDATE point. Compare gGameFrameCount before and after —
  if the game froze, the counter stops advancing far short of 900.
"""

GSTATE_MAP       = 4
GSTATE_PLAY      = 7
PLAY_UPDATE      = 2
LEVEL_ZONESS     = 8
PSCREEN_GAMEPLAY = 1


def _read_s32(h, addr):
    val = h.read32(addr)
    return val - 0x100000000 if val >= 0x80000000 else val


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    # Resolve symbol addresses from the map file so the test survives BSS shifts.
    _GAME_STATE       = S["gGameState"]
    _PLAY_STATE       = S["gPlayState"]
    _GAME_FRAME_COUNT = S["gGameFrameCount"]
    _NEXT_LEVEL_WORD  = S["gNextLevel"]     # u16 (gNextGameState is u16 right after)
    _PRACTICE_SCREEN  = S["gPracticeScreen"]

    ok = h.wait_for(_GAME_STATE, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "Booted to level select")
    if not ok:
        return

    h.advance(10)

    # Launch Zoness via direct memory write (bypasses Practice_LaunchLevel nav)
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

    # Let the level settle for 1 second before the crash window
    h.advance(60)

    frame_before = _read_s32(h, _GAME_FRAME_COUNT)

    # Run through the full crash window: 15 seconds = 900 VI frames
    h.advance(900)

    frame_after = _read_s32(h, _GAME_FRAME_COUNT)
    frames_advanced = frame_after - frame_before

    game_state = h.read32(_GAME_STATE)

    # Healthy: the game-frame counter kept pace with the 900 VI frames we ran
    ctx.assert_true(
        frames_advanced >= 750,
        f"Zoness froze after {frames_advanced}/900 frames "
        f"(gGameState=0x{game_state:08x})",
    )
