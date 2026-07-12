"""Shared bootstrap for the Zoness functional tests.

test_zoness_crash.py (15s regression window) and
test_zoness_extended_soak.py (long-run soak) launch the level identically:
boot to level select, then write gNextLevel/gPracticeScreen directly
(bypassing Practice_LaunchLevel navigation -- button injection isn't
reliable for sustained gameplay since the OS controller read overwrites
gControllerPress every frame). Keeping the constants and launch sequence
here prevents the two tests from drifting apart.

The leading underscore keeps this file out of the runner's test_*.py
discovery glob.
"""

LEVEL_ZONESS = 8

GSTATE_MAP = 4
GSTATE_PLAY = 7
PLAY_UPDATE = 2
PSCREEN_GAMEPLAY = 1


def read_s32(h, addr):
    val = h.read32(addr)
    return val - 0x100000000 if val >= 0x80000000 else val


def launch_zoness(ctx):
    """Boot to level select and warp into Zoness.

    Returns True once gPlayState == PLAY_UPDATE; False (with failed asserts
    already recorded on ctx) if any stage times out.
    """
    h = ctx.harness
    S = ctx.syms.addrs

    ok = h.wait_for(S["gGameState"], GSTATE_MAP, 60000)
    ctx.assert_true(ok, "Booted to level select")
    if not ok:
        return False

    h.advance(10)

    # Launch Zoness directly, bypassing Practice_LaunchLevel navigation.
    h.write32(S["gNextLevel"], (LEVEL_ZONESS << 16) | GSTATE_PLAY)
    h.write32(S["gPracticeScreen"], PSCREEN_GAMEPLAY)

    ok = h.wait_for(S["gGameState"], GSTATE_PLAY, 15000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY")
    if not ok:
        return False

    ok = h.wait_for(S["gPlayState"], PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE")
    return ok
