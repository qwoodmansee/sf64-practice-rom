"""Regression: Zoness must not freeze during the first ~15s of gameplay.

History: Zoness used to crash/freeze ~5-10 seconds in. Originally an inverted
bug-repro test (passed while the bug was present); flipped to regression format
2026-06-12 after the fixed ROM ran the full 900-frame window in emulator.

Test approach:
  Launch Zoness via tests/_zoness_common.py (direct memory write). Let the
  level run for 15 seconds (900 frames) past the PLAY_UPDATE point. Compare
  gGameFrameCount before and after -- if the game froze, the counter stops
  advancing far short of 900.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _zoness_common import launch_zoness, read_s32


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    if not launch_zoness(ctx):
        return

    # Let the level settle for 1 second before the crash window
    h.advance(60)

    frame_before = read_s32(h, S["gGameFrameCount"])

    # Run through the full crash window: 15 seconds = 900 VI frames
    h.advance(900)

    frame_after = read_s32(h, S["gGameFrameCount"])
    frames_advanced = frame_after - frame_before

    game_state = h.read32(S["gGameState"])

    # Healthy: the game-frame counter kept pace with the 900 VI frames we ran
    ctx.assert_true(
        frames_advanced >= 750,
        f"Zoness froze after {frames_advanced}/900 frames "
        f"(gGameState=0x{game_state:08x})",
    )
