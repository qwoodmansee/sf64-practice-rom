"""Regression test: the level-select BGM jukebox boots cleanly and stays at MAP.

Practice_LevelSelect_OnEnter randomizes the starting sBgmIndex via Rand_ZeroOne,
so each boot picks one of ~43 BGM entries (stage themes, boss themes, VS,
menu themes, character themes). Practice_ServiceLevelSelectBgm runs every
frame and triggers AUDIO_SET_SPEC + AUDIO_PLAY_BGM for the picked entry.

A malformed (sequence, audio-spec, sfx-layout) tuple would either:
  - hang the audio reset state machine, freezing the level-select Update loop, or
  - crash MIPS via a NaN-to-int cast in some audio path.

This test asserts the ROM reaches GSTATE_MAP and stays there for 300 frames
without dropping back. A regression that broke any BGM entry would show up
as gGameState flipping back to a pre-MAP state.
"""

GSTATE_MAP = 4


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs
    game_state = S["gGameState"]

    ok = h.wait_for(game_state, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "ROM booted to level select (GSTATE_MAP=4)")
    if not ok:
        return

    # Sample gGameState every 30 frames over 5 seconds. Practice_ServiceLevelSelectBgm
    # runs every frame from Practice_LevelSelect_Update; any wedge in AUDIO_SET_SPEC
    # or AUDIO_PLAY_BGM for the randomly-selected entry would surface as a state
    # regression (gGameState != GSTATE_MAP).
    for i in range(10):
        h.advance(30)
        gs = h.read32(game_state)
        ctx.assert_eq(gs, GSTATE_MAP,
                      f"gGameState stays GSTATE_MAP at frame ~{30 * (i + 1)}")
