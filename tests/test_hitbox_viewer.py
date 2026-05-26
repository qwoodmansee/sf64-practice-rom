"""Test: hitbox viewer can be enabled without crashing.

Enables all hitbox toggles, launches Corneria, and runs for several
seconds to verify no crash or freeze occurs.
"""

def run(ctx):
    S = ctx.syms

    ok = ctx.wait_for_level_select()
    ctx.assert_true(ok, "Booted to level select")

    # Verify defaults off
    ctx.assert_eq(ctx.config_field("showHitboxes"), 0, "showHitboxes defaults OFF")

    # Enable all hitbox options via memory write
    ctx.set_config_field("showHitboxes", 1)
    ctx.set_config_field("showHitboxActors", 1)
    ctx.set_config_field("showHitboxScenery", 1)
    ctx.set_config_field("showHitboxItems", 1)
    ctx.set_config_field("showHitboxPlayer", 1)
    ctx.set_config_field("showHitboxFlash", 1)

    ctx.assert_eq(ctx.config_field("showHitboxes"), 1, "showHitboxes written ON")

    # Launch Corneria
    ok = ctx.select_and_launch_level(0)
    ctx.assert_true(ok, "Level launched")

    ok = ctx.wait_for_gameplay(10000)
    ctx.assert_true(ok, "Gameplay active")

    ctx.assert_eq(ctx.config_field("showHitboxes"), 1, "showHitboxes still ON")

    # Run 5 seconds with hitboxes rendering — crash = test timeout
    ctx.advance(300)

    ctx.assert_eq(ctx.game_state(), S.const["GSTATE_PLAY"],
                  "Game still GSTATE_PLAY after 5s with hitboxes")
    ctx.assert_eq(ctx.play_state(), S.const["PLAY_UPDATE"],
                  "Still PLAY_UPDATE after 5s with hitboxes")
