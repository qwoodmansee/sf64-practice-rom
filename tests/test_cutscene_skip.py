"""Test: cutscene skip works when skipCutscenes is enabled (default).

The practice ROM defaults to skipCutscenes=true. When launching a level,
the player should go directly to PLAYERSTATE_ACTIVE, never
PLAYERSTATE_LEVEL_INTRO.

Root cause of the original bug: gCsWasNotSkipped must be set to false
AFTER Play_Setup() (which resets it to true) but BEFORE Player_Setup()
checks it. If the hook is in the wrong place, the intro cutscene plays
despite skipCutscenes being on.
"""

def run(ctx):
    S = ctx.syms

    ok = ctx.wait_for_level_select()
    ctx.assert_true(ok, "Practice ROM booted to level select")

    ctx.assert_eq(ctx.config_field("skipCutscenes"), 1, "skipCutscenes defaults ON")

    ok = ctx.select_and_launch_level(0)  # Corneria
    ctx.assert_true(ok, "Level launch triggered")

    ok = ctx.wait_for_gameplay(600)
    ctx.assert_true(ok, "Gameplay reached PLAY_UPDATE")

    state = ctx.player_state()
    ctx.assert_neq(state, S.const["PLAYERSTATE_LEVEL_INTRO"],
                   "Player NOT in LEVEL_INTRO (cutscene skipped)")
    ctx.assert_eq(state, S.const["PLAYERSTATE_ACTIVE"],
                  "Player in PLAYERSTATE_ACTIVE")

    ctx.assert_eq(ctx.cs_was_not_skipped(), 0,
                  "gCsWasNotSkipped is false after skip")

    ctx.assert_eq(ctx.practice_screen(), S.const["PSCREEN_GAMEPLAY"],
                  "Practice screen is GAMEPLAY")
