"""RED repro: the AUDIO menu RESET does NOT fix audio frozen by pause->load.

Builds on test_audio_frozen_after_pause_load: pause + load leaves the BGM
sequence player wedged (gSeqPlayers[0].state stuck at 1 instead of 0). This
test then drives the practice radial -> AUDIO submenu -> RESET ALL and asserts
the BGM player recovers (state == 0).

It is EXPECTED TO FAIL (red): today RESET ALL only re-issues per-lane volume
commands (SEQCMD_SET_SEQPLAYER_VOLUME via Practice_Audio_ResetAll); it does
not un-wedge the sequence player, so the audio stays frozen. A real fix would
make the reset (or the load) restore the player.

Menu drive verified empirically:
  open menu  = hold C-Right (sc15) + START (sc40) together
  AUDIO wedge= hold East (sc7, shared stick/DPad-R) + tap A (sc27)
  RESET ALL  = from MUSIC row, 3x DPad-Down (sc22) to reach AOPT_RESET_ALL
               (rows: MUSIC SFX VOICE RESET_ALL BACK), then tap A
gPracticeMenuState: 0 closed, 1 open, 2 open_frozen.
"""

SEQ_BGM = 0x8016d038 & 0x1FFFFFFF   # gSeqPlayers[0]; state byte at +0x001

A = 27
START = 40
L_TRIG = 20
DPAD_L = 4
DPAD_R = 7
C_RIGHT = 15
EAST = 7       # stick East == AUDIO wedge (also DPad R)
DDOWN = 22     # 's' -> DPad Down


def bgm_state(ctx):
    return (ctx.harness.read32(SEQ_BGM + 0x000) >> 16) & 0xFF


def menu_state(ctx):
    return ctx.read_s32(ctx.syms.addrs["gPracticeMenuState"] & 0x1FFFFFFF)


def _combo(ctx, modifier, button):
    ctx.harness.key_down(modifier); ctx.harness.advance(2)
    ctx.harness.key_down(button); ctx.harness.advance(2)
    ctx.harness.key_up(button); ctx.harness.key_up(modifier); ctx.harness.advance(2)


def _tap(ctx, sc, hold=3, rel=3):
    ctx.harness.key_down(sc); ctx.harness.advance(hold)
    ctx.harness.key_up(sc); ctx.harness.advance(rel)


def run(ctx):
    ctx.assert_true(ctx.wait_for_level_select(), "Booted to level select")
    ctx.assert_true(ctx.select_and_launch_level(0), "Launched Corneria")
    ctx.assert_true(ctx.wait_for_play_update(), "Reached gameplay")
    ctx.harness.advance(150)
    ctx.assert_eq(bgm_state(ctx), 0, "Sanity: BGM player healthy while running")

    # Save, pause, load-while-paused -> wedge the BGM player.
    _combo(ctx, L_TRIG, DPAD_L)
    ctx.harness.advance(10)
    ctx.harness.key_down(START); ctx.harness.advance(4); ctx.harness.key_up(START)
    ctx.harness.advance(5)
    _combo(ctx, L_TRIG, DPAD_R)
    ctx.harness.advance(120)
    ctx.assert_eq(bgm_state(ctx), 1, "Sanity: BGM player wedged after pause+load")

    # Open the practice radial (C-Right + START), hover East to AUDIO, press A.
    ctx.harness.key_down(C_RIGHT); ctx.harness.advance(2)
    ctx.harness.key_down(START); ctx.harness.advance(4)
    ctx.harness.key_up(START); ctx.harness.key_up(C_RIGHT); ctx.harness.advance(4)
    ctx.assert_neq(menu_state(ctx), 0, "Sanity: practice menu opened")

    ctx.harness.key_down(EAST); ctx.harness.advance(4)
    ctx.harness.key_down(A); ctx.harness.advance(3)
    ctx.harness.key_up(A); ctx.harness.advance(3)
    ctx.harness.key_up(EAST); ctx.harness.advance(3)

    # Navigate MUSIC -> RESET_ALL (3 down: MUSIC SFX VOICE RESET_ALL) and activate.
    for _ in range(3):
        _tap(ctx, DDOWN)
    _tap(ctx, A)
    ctx.harness.advance(60)

    # EXPECTED-RED: RESET ALL does not recover the wedged BGM player.
    ctx.assert_eq(bgm_state(ctx), 0,
                  "AUDIO RESET ALL recovers BGM player (state 0) "
                  "(RED: reset only re-issues volume, audio stays frozen)")
