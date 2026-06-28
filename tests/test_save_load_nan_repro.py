"""Repro: same-scene save->load must not leave gPlayer floats NaN (Bug B).

History: 2026-05-07 onward, loading a save state on the same scene (Corneria)
leaves gPlayer[0].baseSpeed / boostSpeed as NaN. The HUD surfaces this as "---"
(Practice_DrawFloat dashes when value!=value or |value|>99999). On hardware the
first load also caused a hard FP-exception reboot from (s32)NaN in the physics
update.

The SD transport was long suspected, but TAG_PLAYER_ARRAY round-trips cleanly
through serialize/deserialize, so the corruption lives in the shared apply path
(Snapshot_ApplyToGame) or in gameplay code reading a bad restored value the
next frame. That path is identical for the RAM-slot shortcut (D-Left save /
D-Right load), which needs no SD hardware -- so we can reproduce it in mupen.

This is a REGRESSION test: it PASSES when the floats are valid after load.
It is expected to FAIL (reproduce the bug) until Bug B is fixed.

Flow:
  1. Boot -> level select -> launch Corneria -> gameplay (PLAY_UPDATE)
  2. Settle, sanity-check save is enabled (8MB / Expansion Pak in mupen)
  3. Record baseSpeed/boostSpeed (must be valid before we do anything)
  4. D-Left: save to active slot; confirm gPracticeLastSaveResult == 0
  5. Advance frames so live state diverges from the snapshot
  6. D-Right: load from active slot (same scene -> immediate apply)
  7. Read baseSpeed/boostSpeed 1 frame and ~6 frames after load
  8. Assert the HUD-visible sum is finite and in range (no "---")
"""

import math
import struct

# SDL scancodes (match the m64p harness keymap / existing tests)
SC_A_BUTTON = 27   # 'x'
SC_DPAD_LEFT = 4   # 'a'  -> savestate shortcut (L_JPAD)
SC_DPAD_RIGHT = 7  # 'd'  -> loadstate shortcut (R_JPAD)

# Player struct field offsets (include/sf64player.h)
OFF_BASE_SPEED = 0xD0
OFF_BOOST_SPEED = 0x110
OFF_STATE = 0x1C8

PLAYERSTATE_ACTIVE = 3


def _press(h, scancode, hold=4, release=2):
    h.key_down(scancode)
    h.advance(hold)
    h.key_up(scancode)
    h.advance(release)


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs
    GSTATE_PLAY = ctx.syms.const["GSTATE_PLAY"]
    PLAY_UPDATE = ctx.syms.const["PLAY_UPDATE"]

    def player_ptr():
        p = h.read32(S["gPlayer"])
        return p & 0x1FFFFFFF

    def pf(off):
        return ctx.read_float(player_ptr() + off)

    def dash(value):
        """Mirror Practice_DrawFloat's exact dash condition."""
        return (value != value) or (value > 99999.0) or (value < -99999.0)

    # --- Boot to level select ---
    ok = h.wait_for(S["gGameState"], ctx.syms.const["GSTATE_MAP"], 8000)
    ctx.assert_true(ok, "ROM booted to level select (GSTATE_MAP)")
    if not ok:
        return
    h.advance(30)

    # --- Launch Corneria (default cursor) ---
    _press(h, SC_A_BUTTON, hold=4, release=2)
    ok = h.wait_for(S["gGameState"], GSTATE_PLAY, 5000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY")
    if not ok:
        return
    ok = h.wait_for(S["gPlayState"], PLAY_UPDATE, 5000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE (gPlayer allocated)")
    if not ok:
        return

    # Let the level settle so speeds reach steady-state values.
    h.advance(120)

    # --- Sanity: save must be enabled (mupen should emulate 8MB) ---
    if "gPracticeSaveDisabled" in S:
        save_disabled = ctx.read_s32(S["gPracticeSaveDisabled"])
        ctx.assert_eq(save_disabled, 0,
                      "save/load enabled (Expansion Pak detected in mupen)")
        if save_disabled != 0:
            print("  NOTE: mupen reports stock 4MB; enable extra mem to repro")
            return

    ptr = player_ptr()
    ctx.assert_true(ptr != 0, "gPlayer pointer is non-NULL")
    if ptr == 0:
        return
    pstate = ctx.read_s32(player_ptr() + OFF_STATE)
    ctx.assert_eq(pstate, PLAYERSTATE_ACTIVE, "player state ACTIVE before save")

    base0 = pf(OFF_BASE_SPEED)
    boost0 = pf(OFF_BOOST_SPEED)
    print(f"  before save: baseSpeed={base0!r} boostSpeed={boost0!r} sum={base0+boost0!r}")
    ctx.assert_true(not dash(base0 + boost0),
                    "baseSpeed+boostSpeed valid BEFORE save (sanity)")

    # --- Save: D-Left ---
    _press(h, SC_DPAD_LEFT, hold=4, release=2)
    h.advance(4)
    last_save = ctx.read_s32(S["gPracticeLastSaveResult"])
    print(f"  gPracticeLastSaveResult={last_save}")
    ctx.assert_eq(last_save, 0, "save succeeded (gPracticeLastSaveResult==0)")
    if last_save != 0:
        print("  NOTE: save did not succeed; cannot exercise load apply")
        return

    # --- Let live state diverge from the snapshot ---
    h.advance(40)

    # --- Load: D-Right (same scene -> immediate Snapshot_ApplyToGame) ---
    _press(h, SC_DPAD_RIGHT, hold=4, release=2)

    h.advance(1)
    base1 = pf(OFF_BASE_SPEED)
    boost1 = pf(OFF_BOOST_SPEED)
    print(f"  +1 frame after load: baseSpeed={base1!r} boostSpeed={boost1!r}")

    h.advance(6)
    base2 = pf(OFF_BASE_SPEED)
    boost2 = pf(OFF_BOOST_SPEED)
    print(f"  +7 frames after load: baseSpeed={base2!r} boostSpeed={boost2!r}")

    last_load = ctx.read_s32(S["gPracticeLastLoadResult"])
    print(f"  gPracticeLastLoadResult={last_load}")
    ctx.assert_eq(last_load, 0, "load succeeded (gPracticeLastLoadResult==0)")

    # --- The actual bug assertions: HUD must not show "---" ---
    ctx.assert_true(not dash(base1 + boost1),
                    "baseSpeed+boostSpeed valid +1 frame after load")
    ctx.assert_true(not dash(base2 + boost2),
                    "baseSpeed+boostSpeed valid +7 frames after load")

    # Game must still be alive in gameplay (NaN->(s32) FPE would wedge it).
    gs = ctx.read_s32(S["gGameState"])
    ps = ctx.read_s32(S["gPlayState"])
    ctx.assert_eq(gs, GSTATE_PLAY, "still in GSTATE_PLAY after load")
    ctx.assert_eq(ps, PLAY_UPDATE, "still in PLAY_UPDATE after load")
