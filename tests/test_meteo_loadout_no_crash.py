"""Regression: opening the Loadout submenu while in-level on Meteo must not crash.

Steps the test reproduces (matches the reported bug):
  1. Boot, navigate to Meteo on the level select, press A to launch.
  2. Once in-level and in PLAY_UPDATE, advance 180 frames (~3 s at 60 fps).
  3. Open the practice radial menu (Z + D-pad Right chord).
  4. Tilt stick to the SW position (down-left) to hover LOADOUT.
  5. Press A to open the LOADOUT state submenu.
  6. Advance 60 more frames.
  7. Assert gGameFrameCount increased by at least 30 — proving the game
     loop is still executing (no crash or freeze).

Bug history:
  StateMenu_UpdateLoadout calls StateMenu_ApplyLoadoutLive every frame while
  PSUBMENU_LOADOUT is active.  On Meteo after a few seconds of gameplay,
  something in that path triggers a MIPS exception; the N64 CPU enters the
  exception handler (infinite loop) and gGameFrameCount stops incrementing.

  A near-zero frame-count delta after the crash trigger means the N64 froze.
"""

# GSTATE/PLAYSTATE constants from sf64thread.h.
# Intentionally NOT from parse_symbols() — that table has wrong values.
GSTATE_MAP   = 4
GSTATE_PLAY  = 7
PLAY_UPDATE  = 2
PMENU_CLOSED = 0

# mupen64plus SDL scancode mapping.
# The harness writes a pinned mupen64plus.cfg (write_input_config in
# m64p_harness.c) that maps SDL keysyms → N64 buttons using SDL_SCANCODE
# values at runtime.
#
#   DPad D   = key(115)='s'   -> SDL_SCANCODE_S = 22
#   DPad R   = key(100)='d'   -> SDL_SCANCODE_D = 7
#   A Button = key(120)='x'   -> SDL_SCANCODE_X = 27
#   C Button R = key(108)='l' -> SDL_SCANCODE_L = 15
#   Start    = key(13)=Return -> SDL_SCANCODE_RETURN = 40
#
# Stick axes use key(neg_keysym, pos_keysym).  The Y axis is inverted for N64:
#   X Axis = key(97,100)   -> 'a'=LEFT(-x),  'd'=RIGHT(+x)   [direct]
#   Y Axis = key(115,119)  -> 's'=N64_UP(+y), 'w'=N64_DOWN(-y) [inverted!]
#
# LOADOUT is the SW radial slice: N64 stick_x < 0 (left) AND stick_y < 0 (down).
#   Stick LEFT  (x<0): 'a' -> SDL_SCANCODE_A = 4
#   Stick DOWN  (y<0): 'w' -> SDL_SCANCODE_W = 26  [counter-intuitive; see above]
SC_DPAD_D    = 22   # D-pad Down ('s') — also moves N64 stick UP (no-op on level select)
SC_DPAD_R    = 7    # D-pad Right ('d')
SC_C_RIGHT   = 15   # C Button Right ('l')
SC_START     = 40   # Start (Return)
SC_A_BUTTON  = 27   # A button ('x')
SC_STICK_L   = 4    # 'a' -> N64 stick LEFT  (x<0)
SC_STICK_DOWN = 26  # 'w' -> N64 stick DOWN  (y<0) — counter-intuitive, see Y Axis note


def _press_release(h, keycode, hold_frames=3, release_frames=2):
    h.key_down(keycode)
    h.advance(hold_frames)
    h.key_up(keycode)
    h.advance(release_frames)


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    game_state  = S["gGameState"]
    play_state  = S["gPlayState"]
    menu_state  = S["gPracticeMenuState"]
    frame_count = S["gSysFrameCount"]   # always increments every VI frame, even frozen
    ctrl_hold   = S["gControllerHold"]  # gControllerHold[0] (gMainController=0)

    # --- Step 1: Boot to level select ---
    ok = h.wait_for(game_state, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "ROM booted to level select (GSTATE_MAP=4)")
    if not ok:
        return

    h.advance(30)

    # --- Step 2: Navigate level cursor to Meteo ---
    # The level cursor starts at index 0 (CORNERIA).  D_JPAD increments
    # sSelectedLevel: 0 -> 1 (METEO).  SC_DPAD_D (22) also moves the N64
    # stick UP, but the level select ignores stick axes for navigation.
    _press_release(h, SC_DPAD_D, hold_frames=3, release_frames=2)

    # --- Step 3: Launch Meteo ---
    _press_release(h, SC_A_BUTTON, hold_frames=4, release_frames=2)

    ok = h.wait_for(game_state, GSTATE_PLAY, 30000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY after launching Meteo")
    if not ok:
        return

    ok = h.wait_for(play_state, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE on Meteo")
    if not ok:
        return

    # --- Step 4: Wait 3 seconds of in-level gameplay ---
    # The bug requires a few seconds of play before opening Loadout.
    h.advance(180)

    # --- Step 5: Open the practice radial menu (C-Right + Start chord) ---
    h.key_down(SC_C_RIGHT)
    h.key_down(SC_START)
    h.advance(4)
    h.key_up(SC_C_RIGHT)
    h.key_up(SC_START)
    h.advance(3)

    menu = h.read32(menu_state)
    ctx.assert_neq(menu, PMENU_CLOSED,
                   f"Practice menu opened after C-Right+Start (state={menu})")
    if menu == PMENU_CLOSED:
        return

    # --- Step 6: Hover the LOADOUT slice (SW: stick down-left) ---
    # RSLICE_LOADOUT requires N64 stick_x < 0 (left) and stick_y < 0 (down).
    # SC_STICK_L ('a', scancode 4) drives x negative.
    # SC_STICK_DOWN ('w', scancode 26) drives y negative (counter-intuitive
    # inversion — see Y Axis config comment above).
    # Both keys also set DPad buttons (Left and Up), which the radial menu
    # ignores for slice selection.
    h.key_down(SC_STICK_L)
    h.key_down(SC_STICK_DOWN)
    h.advance(4)  # let the slice detector register RSLICE_LOADOUT

    # Verify the stick is actually in the SW position before pressing A.
    # OSContPad layout (from os_cont.h): u16 button | s8 stick_x | s8 stick_y.
    # In mupen64plus host-LE RDRAM, read32(ctrl_hold) gives:
    #   bits 31-16: button, bits 15-8: stick_x, bits 7-0: stick_y.
    word0 = h.read32(ctrl_hold)
    raw_stick_x = (word0 >> 8) & 0xFF
    raw_stick_y = word0 & 0xFF
    stick_x = raw_stick_x - 0x100 if raw_stick_x >= 0x80 else raw_stick_x
    stick_y = raw_stick_y - 0x100 if raw_stick_y >= 0x80 else raw_stick_y

    ctx.assert_true(stick_x < -20,
                    f"Stick is LEFT before A press (stick_x={stick_x}, expected <-20) "
                    f"[SW slice requires x<0 past dead zone 20]")
    ctx.assert_true(stick_y < -20,
                    f"Stick is DOWN before A press (stick_y={stick_y}, expected <-20) "
                    f"[SW slice requires y<0 past dead zone 20; "
                    f"if this fails, 'w' did not drive stick_y negative as expected]")

    # --- Step 7: Press A to open the LOADOUT submenu (crash trigger) ---
    # advance(1): frame N — A button is processed, PSUBMENU_LOADOUT opens.
    # StateMenu_UpdateLoadout does NOT run on this frame because Practice_Menu_Update
    # checks Practice_StateMenuIsOpen() before the A_BUTTON handler fires, so the
    # state-menu update first runs on frame N+1.  Frame N is therefore safe.
    h.key_down(SC_A_BUTTON)
    h.advance(1)   # frame N completes safely — LOADOUT opens, no crash yet
    h.key_up(SC_A_BUTTON)
    h.key_up(SC_STICK_L)
    h.key_up(SC_STICK_DOWN)

    # Record frame count right after the safe frame (emulator is now paused).
    frames_before = h.read32(frame_count)

    # --- Step 8: Sleep 2 s (wall-clock) and check gSysFrameCount ---
    # IMPORTANT: use h.sleep() NOT h.advance() here.
    #
    # h.advance() waits for VI frame callbacks.  When the N64 crashes (RSP hang
    # from a circular display list), VI interrupts are masked and mupen64plus
    # stops generating frame callbacks — h.advance() would block forever.
    #
    # h.sleep() uses usleep() (wall-clock), so it always completes in ~2 s.
    # We check gSysFrameCount (not gGameFrameCount) because gGameFrameCount only
    # increments inside Play_Main, which is frozen while PMENU_OPEN_FROZEN — it
    # would be 0 for a healthy frozen game too.  gSysFrameCount increments every
    # VI frame unconditionally, so a frozen N64 (RSP hang) stops it while a
    # frozen-but-alive game keeps it moving.
    #
    #   Bug present — RSP hangs, VI interrupts masked; gSysFrameCount stops.
    #     delta will be near 0-2 after 2 s.
    #   Bug absent — VI frames continue normally; delta will be ~60 per second.
    h.sleep(2000)
    frames_after = h.read32(frame_count)
    frame_delta = frames_after - frames_before

    ctx.assert_true(
        frame_delta >= 30,
        f"Game stayed alive after opening Loadout on Meteo "
        f"(gSysFrameCount: {frames_before} -> {frames_after}, "
        f"delta={frame_delta}; expected >=30 for ~2 s at any normal emulation speed). "
        f"A near-zero delta means the N64 froze on frame N+1 -- "
        f"masterDL overflow caused a circular RSP display list that hung the game thread."
    )
