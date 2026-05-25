"""Regression test: pressing D-Pad Left (savestate shortcut) does not crash.

When the user presses D-Pad Left during gameplay (without L Trigger), the
frame-advance system should save the current state to the active slot. This
test verifies that the shortcut works without freezing or crashing.

Steps:
  1. Boot to level select
  2. Launch Corneria (default level)
  3. Enter gameplay
  4. Press D-Pad Left (savestate shortcut, no L Trigger needed)
  5. Verify the game remains responsive and stays in gameplay

A crash or hang on the savestate shortcut would show as gGameState changing
or the harness becoming unresponsive. The bug was caused by `.practice_late_core`
being placed at 0x801F4000 — inside the ramPtr window that fox_load.c's
Load_SceneFiles uses for level overlay+assets. Corneria's ast_bg_planet DMA
clobbered slot_manager_save_ram's code; the fix relocated the segment to the
Pak region at 0x80720000.
"""

GSTATE_MAP = 4
GSTATE_PLAY = 7
PLAY_UPDATE = 2

# SDL scancodes for controller buttons (matches the m64p harness keymap).
SC_A_BUTTON = 27      # 'x' key
SC_DPAD_LEFT = 4      # 'a' key
SC_DPAD_RIGHT = 7     # 'd' key


def _press_release(h, scancode, hold_frames=3, release_frames=2):
    """Press a button for N frames, then release."""
    h.key_down(scancode)
    h.advance(hold_frames)
    h.key_up(scancode)
    h.advance(release_frames)


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    game_state = S["gGameState"]
    play_state = S["gPlayState"]

    # --- Step 1: Boot to level select ---
    ok = h.wait_for(game_state, GSTATE_MAP, 8000)
    ctx.assert_true(ok, "ROM booted to level select (GSTATE_MAP=4)")
    if not ok:
        return

    # Let level-select OnEnter finish picking BGM etc.
    h.advance(30)

    # --- Step 2: Press A on level select to launch Corneria (default) ---
    _press_release(h, SC_A_BUTTON, hold_frames=4, release_frames=2)

    ok = h.wait_for(game_state, GSTATE_PLAY, 5000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY after A-press on level select")
    if not ok:
        return

    ok = h.wait_for(play_state, PLAY_UPDATE, 5000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE (gameplay active)")
    if not ok:
        return

    # Let the level settle in (BGM kick, asset finalize, etc.)
    h.advance(100)

    # --- Step 3: Press D-Pad Left (savestate shortcut, no L Trigger) ---
    _press_release(h, SC_DPAD_LEFT, hold_frames=4, release_frames=2)

    # --- Step 4: Verify game is still responsive ---
    # Advance several frames and check that gGameState and gPlayState haven't
    # regressed. A crash would prevent frames from advancing (the bug wedged
    # the game thread; the test runner would time out before assertions ran).
    for i in range(5):
        h.advance(30)
        gs = h.read32(game_state)
        ps = h.read32(play_state)

        ctx.assert_eq(gs, GSTATE_PLAY,
                      f"gGameState stays GSTATE_PLAY at frame ~{100 + 4 + 2 + 30 * (i + 1)}")
        ctx.assert_eq(ps, PLAY_UPDATE,
                      f"gPlayState stays PLAY_UPDATE at frame ~{100 + 4 + 2 + 30 * (i + 1)}")

        if gs != GSTATE_PLAY or ps != PLAY_UPDATE:
            # If we regressed, no point checking more frames
            return
