"""RED repro: BGM sequence player is left stuck after pause -> load.

User repro: start level, save state, pause (START), load state. The music/
audio stays frozen after the load even though gameplay resumes.

Empirically isolated signal (gSeqPlayers[0] == BGM sequence player):
  - During normal gameplay the player's `state` byte (struct offset 0x001)
    reads 0.
  - Loading a state WITHOUT pausing first leaves it at 0 (healthy) -- this is
    the control path, proving the load itself is fine.
  - Loading a state WHILE PAUSED leaves `state` stuck at 1 (and the
    fontDmaInProgress flag bit 0x20 stuck set) for 4+ seconds. This is the
    bug: the pause->load path corrupts the BGM player so it never resumes.

gPlayState is NOT a usable signal here -- it correctly returns to PLAY_UPDATE
after the load; only the audio engine stays wedged.

This test asserts the BGM player is healthy (state == 0) after a pause->load.
It is EXPECTED TO FAIL (red) against current source. The control assert (load
without pause leaves state == 0) must PASS for the repro to be valid.

Harness SDL scancodes (calibrated): A=27, START=40, L_TRIG=20,
DPad L=4, DPad R=7. Practice bindings: SAVE = L_TRIG + DPad L,
LOAD = L_TRIG + DPad R. PlayState: PLAY_UPDATE=2, PLAY_PAUSE=100.
"""

SEQ_BGM = 0x8016d038 & 0x1FFFFFFF   # gSeqPlayers[0]; state byte at +0x001
PLAY_UPDATE = 2
PLAY_PAUSE = 100

A = 27
START = 40
L_TRIG = 20
DPAD_L = 4
DPAD_R = 7


def bgm_state(ctx):
    """SequencePlayer.state -- byte at struct offset 0x001 (second byte of the
    first 32-bit word, big-endian)."""
    return (ctx.harness.read32(SEQ_BGM + 0x000) >> 16) & 0xFF


def _combo(ctx, modifier, button):
    ctx.harness.key_down(modifier)
    ctx.harness.advance(2)
    ctx.harness.key_down(button)
    ctx.harness.advance(2)
    ctx.harness.key_up(button)
    ctx.harness.key_up(modifier)
    ctx.harness.advance(2)


def _save(ctx):
    _combo(ctx, L_TRIG, DPAD_L)
    ctx.harness.advance(10)


def _load(ctx):
    _combo(ctx, L_TRIG, DPAD_R)
    ctx.harness.advance(120)


def _pause(ctx):
    ctx.harness.key_down(START)
    ctx.harness.advance(4)
    ctx.harness.key_up(START)
    ctx.harness.advance(5)


def run(ctx):
    ps = ctx.syms.addrs["gPlayState"]

    ctx.assert_true(ctx.wait_for_level_select(), "Booted to level select")
    ctx.assert_true(ctx.select_and_launch_level(0), "Launched Corneria")
    ctx.assert_true(ctx.wait_for_play_update(), "Reached gameplay")
    ctx.harness.advance(150)

    ctx.assert_eq(bgm_state(ctx), 0, "Sanity: BGM player healthy (state 0) while running")

    _save(ctx)

    # Control: load WITHOUT pausing -- proves the load path itself is fine.
    _load(ctx)
    ctx.assert_eq(bgm_state(ctx), 0,
                  "Control: BGM player healthy (state 0) after plain load")
    ctx.harness.advance(120)

    # Repro: pause, then load while paused.
    _pause(ctx)
    ctx.assert_eq(ctx.read_s32(ps), PLAY_PAUSE, "Sanity: PLAY_PAUSE while paused")
    _load(ctx)

    # Game state itself recovers...
    ctx.assert_eq(ctx.read_s32(ps), PLAY_UPDATE, "Sanity: gameplay resumed after load")

    # EXPECTED-RED: ...but the BGM sequence player is left wedged at state 1.
    ctx.assert_eq(bgm_state(ctx), 0,
                  "BGM player healthy (state 0) after pause+load "
                  "(RED: stuck at 1 today -- audio frozen)")
