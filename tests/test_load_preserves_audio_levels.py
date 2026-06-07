"""Regression: loading a state must NOT wipe the user's set audio levels.

User repro: set a non-default MUSIC volume in the AUDIO submenu, then load a
save state. The volume the user set should NOT change.

Two layers exist:
  - gPracticeConfig.volMusic (the user's STORED setting, 0..99) -- load does NOT
    touch this, so it is preserved. (Asserted here as a sanity check.)
  - gSeqPlayers[0].fadeVolumeMod (+0x2C, f32) -- the APPLIED/audible scale. The
    practice menu routes through Audio_SetVolume (engine master-volume layer);
    the applied scale for a master volume v (0..99) is (v*127//99)/127. The
    load path's BGM rescue calls Practice_Audio_ApplyAll(), which re-applies
    gPracticeConfig.vol* so the user's mix survives a load.

Scancodes: C-Right=15 START=40 A=27 East=7 DPadLeft=4 B=6 L_TRIG=20 DPadR=7.
AUDIO_VOL_STEP=9 (0..99 scale); a few DPad-Left taps lower MUSIC below full.
"""

import struct

FADEVOLMOD = 0x2C                # gSeqPlayers[0].fadeVolumeMod (f32)

C_RIGHT = 15
START = 40
A = 27
EAST = 7
DLEFT = 4
B = 6
L_TRIG = 20
DPAD_L = 4
DPAD_R = 7


def fmod(ctx):
    seq0 = ctx.syms.addrs["gSeqPlayers"]   # already masked &0x1FFFFFFF; [0] = BGM
    return struct.unpack('>f', ctx.harness.read32(seq0 + FADEVOLMOD).to_bytes(4, 'big'))[0]


def volmusic(ctx):
    return ctx.config_field("volMusic")


def expected_mod(vol):
    """Applied BGM scale the engine produces from a 0..99 master volume."""
    return ((vol * 127) // 99) / 127.0


def tap(ctx, sc, hold=3, rel=3):
    ctx.harness.key_down(sc); ctx.harness.advance(hold)
    ctx.harness.key_up(sc); ctx.harness.advance(rel)


def run(ctx):
    ctx.assert_true(ctx.wait_for_level_select(), "Booted to level select")
    ctx.assert_true(ctx.select_and_launch_level(0), "Launched Corneria")
    ctx.assert_true(ctx.wait_for_play_update(), "Reached gameplay")
    ctx.harness.advance(150)

    ctx.assert_eq(volmusic(ctx), 99, "Sanity: MUSIC volume starts full")
    ctx.assert_true(abs(fmod(ctx) - 1.0) < 0.05, "Sanity: applied BGM scale starts ~1.0")

    # Open radial -> AUDIO submenu, lower MUSIC by a few steps.
    ctx.harness.key_down(C_RIGHT); ctx.harness.advance(2)
    ctx.harness.key_down(START); ctx.harness.advance(4)
    ctx.harness.key_up(START); ctx.harness.key_up(C_RIGHT); ctx.harness.advance(4)
    ctx.harness.key_down(EAST); ctx.harness.advance(4)
    ctx.harness.key_down(A); ctx.harness.advance(3)
    ctx.harness.key_up(A); ctx.harness.advance(3)
    ctx.harness.key_up(EAST); ctx.harness.advance(3)
    for _ in range(6):
        tap(ctx, DLEFT)

    lowered = volmusic(ctx)
    ctx.assert_true(lowered < 99, "MUSIC volume lowered below full via menu")
    ctx.assert_true(abs(fmod(ctx) - expected_mod(lowered)) < 0.05,
                    "Applied BGM scale follows the lowered setting")

    # Close menu (B), then save + load.
    tap(ctx, B); tap(ctx, B); ctx.harness.advance(20)
    ctx.harness.key_down(L_TRIG); ctx.harness.advance(2)
    ctx.harness.key_down(DPAD_L); ctx.harness.advance(2)
    ctx.harness.key_up(DPAD_L); ctx.harness.key_up(L_TRIG); ctx.harness.advance(10)
    ctx.harness.key_down(L_TRIG); ctx.harness.advance(2)
    ctx.harness.key_down(DPAD_R); ctx.harness.advance(2)
    ctx.harness.key_up(DPAD_R); ctx.harness.key_up(L_TRIG); ctx.harness.advance(150)

    # The stored setting survives (load doesn't touch the config field).
    ctx.assert_eq(volmusic(ctx), lowered, "Stored MUSIC volume preserved across load")

    # The APPLIED volume still matches the user's setting after the load:
    # the rescue path re-applies gPracticeConfig.vol* instead of forcing full.
    ctx.assert_true(abs(fmod(ctx) - expected_mod(lowered)) < 0.05,
                    "Applied BGM scale still reflects the user's volume after load")
