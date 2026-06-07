"""Test: non-default master audio volumes are stable across a level load.

The AUDIO radial panel writes the three PracticeConfig volume fields
(volMusic/volSfx/volVoice) and the post-load rescue in practice_save.c
re-applies them via Practice_Audio_ApplyAll(). This test sets every lane to a
distinct non-default value, launches a level, and verifies gameplay stays
stable for several seconds and the values persist — guarding the u8 config
fields and the per-frame Practice_Save_Tick rescue path against crashes.

Note on coverage: the mupen64plus harness injects keyboard buttons only (no
analog stick), so it cannot hover a radial wedge — the menu interaction itself
(L/R adjust, A reset) is covered by the static invariant check_audio_volume_menu
in tools/practice_invariants.py rather than here.
"""

VOL_FULL = 99  # engine master-volume scale is 0..99


def run(ctx):
    S = ctx.syms

    ok = ctx.wait_for_level_select()
    ctx.assert_true(ok, "Booted to level select")

    # Defaults are full on every lane.
    ctx.assert_eq(ctx.config_field("volMusic"), VOL_FULL, "volMusic default full")
    ctx.assert_eq(ctx.config_field("volSfx"), VOL_FULL, "volSfx default full")
    ctx.assert_eq(ctx.config_field("volVoice"), VOL_FULL, "volVoice default full")

    # Simulate a custom mix set via the AUDIO menu (distinct non-default,
    # in-range 0..99 values, including a fully-muted lane).
    ctx.set_config_field("volMusic", 40)
    ctx.set_config_field("volSfx", 80)
    ctx.set_config_field("volVoice", 0)

    ok = ctx.select_and_launch_level(0)
    ctx.assert_true(ok, "Level launched")

    ok = ctx.wait_for_gameplay(10000)
    ctx.assert_true(ok, "Gameplay active")

    # Run 3 seconds; the per-frame audio paths (and Practice_Save_Tick's rescue
    # apply) must not choke on the non-default lane volumes.
    ctx.advance(180)

    ctx.assert_eq(ctx.game_state(), S.const["GSTATE_PLAY"],
                  "Game stable after 3s with custom audio mix")

    # Config volumes persist through the level load.
    ctx.assert_eq(ctx.config_field("volMusic"), 40, "volMusic mix persisted")
    ctx.assert_eq(ctx.config_field("volSfx"), 80, "volSfx mix persisted")
    ctx.assert_eq(ctx.config_field("volVoice"), 0, "volVoice mute persisted")
