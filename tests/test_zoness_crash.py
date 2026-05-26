"""Bug repro: Zoness crashes/freezes after ~5-10 seconds of gameplay.

Steps to reproduce manually:
  1. Boot practice ROM to level select
  2. Launch Zoness
  3. Wait ~5-10 seconds — game freezes

Test approach:
  Launch Zoness via direct memory write. Let the level run for 15 seconds (900
  frames) past the PLAY_UPDATE point. Compare gGameFrameCount before and after —
  if the game froze, the counter stops advancing far short of 900.

  Bug confirmed = test PASSES. Bug fixed = test FAILS.

This test ASSERTS THE BUG IS PRESENT. It will flip to FAIL once fixed.
"""

_GAME_STATE       = 0x00195fe4   # gGameState (s32)
_PLAY_STATE       = 0x00196004   # gPlayState (s32)
_GAME_FRAME_COUNT = 0x00196568   # gGameFrameCount (s32)
_NEXT_LEVEL_WORD  = 0x001801e0   # gNextLevel (u16 hi) | gNextGameState (u16 lo)
_PRACTICE_SCREEN  = 0x00197220   # gPracticeScreen (s32)

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

    # Bug confirmed: game froze — far fewer than 900 game frames actually ran
    ctx.assert_true(
        frames_advanced < 750,
        f"Bug: Zoness froze after {frames_advanced}/900 frames "
        f"(gGameState=0x{game_state:08x})",
    )
