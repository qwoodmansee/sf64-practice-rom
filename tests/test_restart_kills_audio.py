"""Regression: restarting a level via the practice menu must NOT silence
audio.

Steps the test reproduces (matches the original repro):
  1. Boot, default-select Corneria on level select, press A to launch.
  2. Once in-level, hold Z + D-pad Right to open the practice menu.
  3. Tilt stick UP to hover the N slice (RESTART) and press A.
  4. Level restarts. BGM must remain audible (mainVolume.mod ~= 1.0).

Bug history:
  Practice_LaunchLevel calls Audio_SetAudioSpec(0, samesPec) for a
  same-spec restart, which only invokes Audio_StopSequence(BGM). The
  stop fades sActiveSequences[BGM].mainVolume.mod toward 0. The BGM
  rescue in Practice_Save_Tick fires AUDIO_PLAY_BGM(NA_BGM_STAGE_CO)
  after a 3-frame delay -- that sets seqId back to SEQ_ID_CORNERIA but
  does NOT restore mainVolume.mod, so the player claimed to be playing
  while the main volume sat at 0.0f (totally silent).

  Fix: the rescue now also queues SEQCMD_SET_SEQPLAYER_VOLUME with
  duration=0, volume=0x7F (=1.0) right after AUDIO_PLAY_BGM.

Test approach:
  Drives the real menu via SDL key injection (KEYDOWN/KEYUP through
  the pinned mupen64plus-input-sdl config; see tests/test_input_smoke.py
  for the scancode-vs-keysym workaround). After the restart completes
  and the rescue has fired (400 frames post-PLAY_UPDATE), reads
  sActiveSequences[BGM].mainVolume.mod and asserts it's 1.0f.

  A regression that breaks the volume restore will flip
  mainVolume.mod back to 0.0f and this test fails.
"""

# sActiveSequences struct offsets (stable; computed from struct layout).
_SEQ_BGM_SEQ_ID_OFF    = 0x248
_SEQ_BGM_WAIT_FONT_OFF = 0x254

# mupen64plus-input-sdl quirk (see tests/test_input_smoke.py): the plugin
# stores myKeyState[keysym] in SDL_KeyDown but doSdlKeys reads
# keystate[scancode]. Config parse converts keysym->scancode at load time,
# so the runtime button bindings are in scancodes. We must send SCANCODE
# values via KEYDOWN, not keysym values.
#
# Pinned config maps keysym->key role; corresponding scancode in parens:
#   A Button: key(120)='x' -> SDL_SCANCODE_X = 27
#   Z Trig:   key(122)='z' -> SDL_SCANCODE_Z = 29
#   DPad R:   key(100)='d' -> SDL_SCANCODE_D = 7
#   Start:    key(13)=RETURN -> SDL_SCANCODE_RETURN = 40
# Stick UP: Y Axis = key(115,119) treats SDL screen-Y convention
#   (key_a=negative-direction='s', key_b=positive-direction='w'), then
#   inverts to N64 convention. Net result: pressing 's' pushes N64
#   stick UP (which is what the radial-menu N slice RSLICE_RESTART needs).
SC_A_BUTTON = 27
SC_Z_TRIG   = 29
SC_DPAD_R   = 7
SC_STICK_UP = 22   # 's' -- counter-intuitive but correct for mupen64plus
SC_START    = 40

GSTATE_MAP       = 4
GSTATE_PLAY      = 7
PLAY_UPDATE      = 2
PMENU_CLOSED     = 0

SEQ_FLAG         = 0x8000
SEQ_ID_NONE      = 0xFFFF
NA_BGM_STAGE_CO  = 0x8002
SEQ_ID_CORNERIA  = NA_BGM_STAGE_CO & ~SEQ_FLAG  # 0x0002


def _read_u16_hi(h, addr):
    return (h.read32(addr & ~3) >> 16) & 0xFFFF


def _read_u8_hi(h, addr):
    return (h.read32(addr & ~3) >> 24) & 0xFF


def _press_release(h, keycode, hold_frames=3, release_frames=2):
    h.key_down(keycode)
    h.advance(hold_frames)
    h.key_up(keycode)
    h.advance(release_frames)


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    game_state   = S["gGameState"]
    play_state   = S["gPlayState"]
    menu_state   = S["gPracticeMenuState"]
    seq_bgm      = S["sActiveSequences"]
    seq_bgm_id   = seq_bgm + _SEQ_BGM_SEQ_ID_OFF
    seq_bgm_wait = seq_bgm + _SEQ_BGM_WAIT_FONT_OFF

    # --- Step 1: Boot to level select ---
    ok = h.wait_for(game_state, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "ROM booted to level select (GSTATE_MAP=4)")
    if not ok:
        return

    # Let level-select OnEnter finish picking a random BGM and settle.
    h.advance(30)

    # --- Step 2: Press A on level select to launch Corneria (default cursor) ---
    _press_release(h, SC_A_BUTTON, hold_frames=4, release_frames=2)

    ok = h.wait_for(game_state, GSTATE_PLAY, 30000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY after A-press on level select")
    if not ok:
        return
    ok = h.wait_for(play_state, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE (1st time)")
    if not ok:
        return

    # --- Step 3: Let BGM fully load + start ---
    h.advance(200)

    seq_id_before = _read_u16_hi(h, seq_bgm_id)
    ctx.assert_eq(seq_id_before, SEQ_ID_CORNERIA,
                  f"BGM is playing pre-restart "
                  f"(seqId=0x{seq_id_before:04X} expected 0x{SEQ_ID_CORNERIA:04X})")

    # --- Step 4: Open the practice menu (Z + D-pad Right chord) ---
    h.key_down(SC_Z_TRIG)
    h.key_down(SC_DPAD_R)
    h.advance(4)
    h.key_up(SC_Z_TRIG)
    h.key_up(SC_DPAD_R)
    h.advance(3)

    menu = h.read32(menu_state)
    ctx.assert_neq(menu, PMENU_CLOSED,
                   f"Practice menu opened (gPracticeMenuState={menu})")
    if menu == PMENU_CLOSED:
        return

    # --- Step 5: Hover RESTART (N slice) and press A ---
    # Diagnostic: read stick after holding UP key.
    ctrl_hold = S["gControllerHold"]
    # OSContPad layout: u16 button, u16 padding(?), s8 stick_x, s8 stick_y, ...
    # stick_y is the low byte of the second word at gControllerHold+4 (BE).
    h.key_down(SC_STICK_UP)
    h.advance(4)
    word4 = h.read32(ctrl_hold + 4)
    # OSContPad layout per include/PR/os_cont.h:
    #   u16 button; s8 stick_x; s8 stick_y; ...
    # So word at ctrl_hold+0 is (button_hi16 | (stick_x<<8) | stick_y).
    word0 = h.read32(ctrl_hold)
    stick_y = word0 & 0xFF
    if stick_y >= 0x80:
        stick_y -= 0x100
    ctx.assert_true(stick_y > 20,
                    f"Stick is pushed UP after holding S key (stick_y={stick_y}, expected >20)")

    # Capture frame counter before restart -- Play_Init resets it.
    frame_counter_addr = S["gGameFrameCount"]
    frames_before = h.read32(frame_counter_addr)

    h.key_down(SC_A_BUTTON)
    h.advance(2)
    h.key_up(SC_A_BUTTON)

    # Read Practice_LaunchLevel side-effect IMMEDIATELY (before Play_Init
    # consumes gClearPlayerInfo and resets it to false).
    clear_player_info = h.read32(S["gClearPlayerInfo"])

    h.key_up(SC_STICK_UP)
    h.advance(3)

    # Verify menu closed -- proves a slice was actually selected and its
    # handler ran. RESTART, LOAD, and LEVELS all close the menu, so we need
    # the side-effect check above to know it was RESTART specifically.
    menu_after = h.read32(menu_state)
    ctx.assert_eq(menu_after, PMENU_CLOSED,
                  f"Menu closed after A-press (state={menu_after})")
    ctx.assert_neq(clear_player_info, 0,
                   f"Practice_LaunchLevel ran (gClearPlayerInfo=true after A-press; got {clear_player_info})")

    # --- Step 6: Wait for the restart to complete ---
    # gNextGameStateTimer=3 + Play_Cleanup + Play_Init -> back to PLAY_UPDATE.
    ok = h.wait_for(play_state, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE post-restart")
    if not ok:
        return

    # --- Step 7: Advance well past the BGM rescue + any font-load latency ---
    h.advance(400)

    # --- Step 8: Assert audio is alive post-restart ---
    # sActiveSequences[BGM].mainVolume is at offset 0 (FadeModulation), and
    # FadeModulation.mod is the first f32 field. So sActiveSequences[BGM].mod
    # is at sActiveSequences + 0. A 32-bit read returns the IEEE-754 bits.
    main_volume_mod_bits = h.read32(seq_bgm)

    # The rescue should set seqId back to the Corneria BGM id.
    seq_id_after = _read_u16_hi(h, seq_bgm_id)
    ctx.assert_eq(seq_id_after, SEQ_ID_CORNERIA,
                  f"BGM seqId stays SEQ_ID_CORNERIA after restart "
                  f"(got 0x{seq_id_after:04X})")

    # IEEE-754 bits for 1.0f = 0x3F800000. Anything non-zero would also
    # indicate the player is producing audio, but the rescue uses
    # SEQCMD_SET_SEQPLAYER_VOLUME(BGM, 0, 0x7F) which maps to exactly 1.0f.
    ctx.assert_eq(main_volume_mod_bits, 0x3F800000,
                  f"BGM mainVolume.mod restored to 1.0f after restart "
                  f"(raw bits=0x{main_volume_mod_bits:08X}, expected 0x3F800000). "
                  f"If this is 0x00000000 the same-spec restart fade-out wasn't "
                  f"restored by the rescue path in Practice_Save_Tick.")
