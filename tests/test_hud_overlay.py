"""Test: HUD overlay runs without crashing during gameplay.

Enables all HUD options and verifies stable gameplay for several seconds.
"""

def run(ctx):
    S = ctx.syms

    ok = ctx.wait_for_level_select()
    ctx.assert_true(ok, "Booted to level select")

    ctx.assert_eq(ctx.config_field("showHudOverlay"), 1, "showHudOverlay defaults ON")
    ctx.assert_eq(ctx.config_field("showSpeed"), 1, "showSpeed defaults ON")
    ctx.assert_eq(ctx.config_field("showLagFrames"), 1, "showLagFrames defaults ON")
    ctx.assert_eq(ctx.config_field("showHitTracking"), 1, "showHitTracking defaults ON")

    # Enable the optional HUD fields too
    ctx.set_config_field("showChargeTiming", 1)
    ctx.set_config_field("showMissedInputs", 1)

    ok = ctx.select_and_launch_level(0)
    ctx.assert_true(ok, "Level launched")

    ok = ctx.wait_for_gameplay(600)
    ctx.assert_true(ok, "Gameplay active")

    # Run 3 seconds with full HUD
    ctx.advance(180)

    ctx.assert_eq(ctx.game_state(), S.const["GSTATE_PLAY"],
                  "Game stable after 3s with full HUD")
