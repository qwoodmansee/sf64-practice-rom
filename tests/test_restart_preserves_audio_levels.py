"""Regression: restarting a level must NOT wipe the user's set audio levels.

Was a known gap (tests/known_issue_restart_audio_levels.py): restarting/switching
a level reset the MUSIC slider back to full even though loading a savestate did
not. Root cause: the level-start audio reset runs Audio_RestartSeqPlayers ->
Audio_RestoreVolumeSettings(AUDIO_TYPE_MUSIC), re-asserting the engine master
music volume onto BGM late in the async reset, which clobbered any practice-side
SEQCMD_SET_SEQPLAYER_VOLUME re-apply.

Fix: the AUDIO menu now routes through Audio_SetVolume, writing the engine's
sVolumeSettings[] master-volume layer. A practice restart goes through
GSTATE_PLAY (not GSTATE_INIT), so sVolumeSettings survives, and the engine's own
reset restore re-asserts OUR value -- no race. The applied scale for a master
volume v (0..99) is (v*127//99)/127.

Scancodes: C-Right=15 START=40 A=27 East=7 North/StickUp=26 DPadLeft=4 B=6.
Radial: AUDIO wedge = East; RESTART wedge = North.
"""

import struct

FADEVOLMOD = 0x2C                # gSeqPlayers[0].fadeVolumeMod (f32)

C_RIGHT = 15
START = 40
A = 27
EAST = 7
NORTH = 26
DLEFT = 4
B = 6


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

    # Back out of the AUDIO submenu to the radial (B), then RESTART (North + A).
    tap(ctx, B); ctx.harness.advance(5)
    ctx.harness.key_down(NORTH); ctx.harness.advance(4)
    ctx.harness.key_down(A); ctx.harness.advance(3)
    ctx.harness.key_up(A); ctx.harness.advance(3)
    ctx.harness.key_up(NORTH); ctx.harness.advance(3)
    ctx.assert_true(ctx.wait_for_play_update(8000), "Level restarted into gameplay")
    ctx.harness.advance(150)

    # Stored setting survives the restart (load/restart never touch the config).
    ctx.assert_eq(volmusic(ctx), lowered, "Stored MUSIC volume preserved across restart")

    # The fix: APPLIED volume still reflects the user's setting after a restart.
    # Routing through sVolumeSettings means the engine's own reset restore
    # re-asserts the practice value instead of snapping BGM back to full.
    ctx.assert_true(abs(fmod(ctx) - expected_mod(lowered)) < 0.05,
                    "Applied BGM scale still reflects the user's volume after restart")
