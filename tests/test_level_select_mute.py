"""Test: Z mutes/unmutes level-select BGM; mute clears automatically on level entry.

Pressing Z on the level select screen silences the BGM jukebox. Pressing Z
again restores it. Launching a level while muted must clear the mute so the
in-level BGM plays at full volume.

sActiveSequences[0].mainVolume.mod (IEEE-754 f32, offset 0 of the struct) is
the ground truth for BGM player volume:
  1.0f = 0x3F800000  — full volume
  0.0f = 0x00000000  — muted
"""

GSTATE_MAP  = 4
GSTATE_PLAY = 7
PLAY_UPDATE = 2

# SDL scancode for Z trigger ('z' -> SDL_SCANCODE_Z = 29)
SC_Z_TRIG   = 29
SC_A_BUTTON = 27


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    game_state = S["gGameState"]
    play_state = S["gPlayState"]
    seq_bgm    = S["sActiveSequences"]  # mainVolume.mod is first f32 (offset 0)

    # --- Boot to level select ---
    ok = h.wait_for(game_state, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "ROM booted to level select")
    if not ok:
        return

    # The BGM jukebox starts with a fade-in; wait until it reaches full volume.
    ok = h.wait_for(seq_bgm, 0x3F800000, 15000)
    ctx.assert_true(ok, "BGM reached full volume on level select (mainVolume.mod == 1.0f)")
    if not ok:
        return

    # --- Press Z once: mute ---
    h.key_down(SC_Z_TRIG)
    h.advance(4)
    h.key_up(SC_Z_TRIG)
    h.advance(15)  # give audio command queue time to process

    bits = h.read32(seq_bgm)
    ctx.assert_eq(bits, 0x00000000,
                  "BGM muted after first Z press (mainVolume.mod == 0.0f)")

    # --- Press Z again: unmute ---
    h.key_down(SC_Z_TRIG)
    h.advance(4)
    h.key_up(SC_Z_TRIG)
    h.advance(15)

    bits = h.read32(seq_bgm)
    ctx.assert_eq(bits, 0x3F800000,
                  "BGM restored after second Z press (mainVolume.mod == 1.0f)")

    # --- Mute again, then launch a level ---
    h.key_down(SC_Z_TRIG)
    h.advance(4)
    h.key_up(SC_Z_TRIG)
    h.advance(15)

    bits = h.read32(seq_bgm)
    ctx.assert_eq(bits, 0x00000000,
                  "BGM muted before level launch")

    # Press A to launch Corneria (default cursor).
    h.key_down(SC_A_BUTTON)
    h.advance(4)
    h.key_up(SC_A_BUTTON)

    ok = h.wait_for(game_state, GSTATE_PLAY, 30000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY after launch")
    if not ok:
        return

    ok = h.wait_for(play_state, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE")
    if not ok:
        return

    # Advance well past the BGM rescue (fires ~3 frames into PLAY_UPDATE).
    h.advance(200)

    bits = h.read32(seq_bgm)
    ctx.assert_eq(bits, 0x3F800000,
                  "BGM at full volume after level entry (mute auto-cleared on launch)")
