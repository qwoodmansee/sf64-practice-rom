"""Regression: enabling hitbox visualizers mid-fight at the Corneria Carrier
must not freeze the game.

Reported reproduction (faithfully reproduced here via real menu navigation,
not memory writes):
  1. Boss-test menu -> CARRIER (Corneria Carrier, hitboxes OFF at launch).
  2. Let the carrier fight begin.
  3. Open the practice pause menu (C-Right + Start).
  4. Walk the radial: DISPLAY (NE) -> VISUALS (E) -> opens VISUALIZERS state menu.
  5. Press A to toggle each of the 6 hitbox visualizers ON in order:
     HITBOXES, ACTORS, SCENERY, ITEMS, PLAYER, FLASH.
  6. Close the menus with B (state -> sub-radial -> root -> closed).
  7. Game must keep running.
"""

# sf64thread.h
GSTATE_MAP   = 4
GSTATE_PLAY  = 7
PLAY_UPDATE  = 2

# practice.h PracticeMenuState
PMENU_CLOSED      = 0
PMENU_OPEN_FROZEN = 2

# SDL scancodes -- match write_input_config() in tools/m64p_harness.c
#   x=120 -> SDL_SCANCODE_X = 27   A button
#   c=99  -> SDL_SCANCODE_C = 6    B button
#   l=108 -> SDL_SCANCODE_L = 15   C Right
#   13    -> SDL_SCANCODE_RETURN = 40 Start
#   d=100 -> SDL_SCANCODE_D = 7    DPad R + stick X+
#   a=97  -> SDL_SCANCODE_A = 4    DPad L + stick X-
#   w=119 -> SDL_SCANCODE_W = 26   DPad U + stick Y- (counter-intuitive, see Y Axis)
#   s=115 -> SDL_SCANCODE_S = 22   DPad D + stick Y+ (counter-intuitive)
#
# N64 stick Y convention in the radial code: y > 0 == UP, y < 0 == DOWN.
# mupen Y Axis = key(115,119) -> 's' is axis neg, 'w' is axis pos. On the N64
# side, axis pos -> stick_y < 0 (DOWN), axis neg -> stick_y > 0 (UP).
# Therefore: 's' raises N64 stick UP, 'w' lowers N64 stick DOWN.
SC_A         = 27   # A button
SC_B         = 6    # B button
SC_C_RIGHT   = 15
SC_START     = 40
SC_STICK_R   = 7    # 'd' -- also DPad R
SC_STICK_UP  = 22   # 's' -- also DPad D (used both ways: radial-up and stateMenu-down)
SC_DPAD_U    = 26   # 'w' -- also stick DOWN, used for level-select wrap
SC_DPAD_D    = 22   # 's' -- same code as SC_STICK_UP; in state menu this acts as DPad D

# Object/Boss layout
OBJ_BOSS_CO_CARRIER = 293
LEVEL_CORNERIA      = 0
BOSS_SIZEOF         = 0x408

HITBOX_FIELDS = [
    "showHitboxes",
    "showHitboxActors",
    "showHitboxScenery",
    "showHitboxItems",
    "showHitboxPlayer",
    "showHitboxFlash",
]


def _press(h, sc, hold=3, release=2):
    h.key_down(sc)
    h.advance(hold)
    h.key_up(sc)
    h.advance(release)


def _chord(h, scs, hold=4, release=3):
    for sc in scs:
        h.key_down(sc)
    h.advance(hold)
    for sc in scs:
        h.key_up(sc)
    h.advance(release)


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    game_state   = S["gGameState"]
    play_state   = S["gPlayState"]
    menu_state   = S["gPracticeMenuState"]
    frame_count  = S["gSysFrameCount"]

    # --- Boot to level select ---
    ok = h.wait_for(game_state, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "Booted to level select")
    if not ok:
        return

    h.advance(30)

    # Sanity: all hitbox toggles OFF
    for f in HITBOX_FIELDS:
        ctx.assert_eq(ctx.config_field(f), 0, f"{f} default OFF")

    # --- Navigate BOSSES (DPad UP wraps 0 -> 16) and launch CARRIER ---
    _press(h, SC_DPAD_U, hold=4, release=2)
    _press(h, SC_A, hold=4, release=2)

    ctx.assert_eq(h.read32(S["gPracticeForceCarrier"]) & 0xFF, 1,
                  "gPracticeForceCarrier set by BOSSES->CARRIER launch")

    # --- Wait for gameplay + carrier spawn ---
    ok = h.wait_for(game_state, GSTATE_PLAY, 30000)
    ctx.assert_true(ok, "GSTATE_PLAY after boss-test launch")
    if not ok:
        return
    ok = h.wait_for(play_state, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "PLAY_UPDATE on Corneria")
    if not ok:
        return

    boss0_base = S["gBosses"]
    spawned = False
    for _ in range(60):
        h.advance(10)
        if (h.read32(boss0_base) & 0xFFFF) == OBJ_BOSS_CO_CARRIER:
            spawned = True
            break
    ctx.assert_true(spawned, "OBJ_BOSS_CO_CARRIER spawned in gBosses[0]")

    # Let the carrier fight settle so the runtime-mutated hitbox arrays
    # (+/-100000 entries in aCoCarrierUpperHitbox / aCoCarrierBottomHitbox)
    # are live.
    h.advance(180)

    # --- Open practice radial menu (C-Right + Start) ---
    _chord(h, [SC_C_RIGHT, SC_START], hold=4, release=3)
    menu = h.read32(menu_state) & 0xFFFFFFFF
    ctx.assert_neq(menu, PMENU_CLOSED, f"Practice menu opened (state={menu})")
    if menu == PMENU_CLOSED:
        return

    # --- Stick NE -> DISPLAY slice, press A -> sub-radial DISPLAY ---
    # NE: stick_x > 0 ('d') AND stick_y > 0 -- N64 stick_y > 0 is UP, which
    # mupen Y axis maps from key=115 ('s', axis-neg). So 'd' + 's' = NE.
    h.key_down(SC_STICK_R)
    h.key_down(SC_STICK_UP)
    h.advance(4)  # let Root_GetSlice hover DISPLAY
    h.key_down(SC_A)
    h.advance(3)
    h.key_up(SC_A)
    h.key_up(SC_STICK_R)
    h.key_up(SC_STICK_UP)
    h.advance(3)

    # --- Stick E -> VISUALS slice, press A -> opens PSUBMENU_VISUALIZERS ---
    h.key_down(SC_STICK_R)
    h.advance(4)
    h.key_down(SC_A)
    h.advance(3)
    h.key_up(SC_A)
    h.key_up(SC_STICK_R)
    h.advance(3)

    # --- Toggle each of the 6 hitbox visualizers ON, one at a time ---
    # PSUBMENU_VISUALIZERS opens with sSelectedOption = 0 (VOPT_HITBOXES).
    # VOPT enum order: HITBOXES, ACTORS, SCENERY, ITEMS, PLAYER, FLASH, ...
    # Sequence per option: press A (toggle), press DPad D (next option).
    for i, field in enumerate(HITBOX_FIELDS):
        if i > 0:
            _press(h, SC_DPAD_D, hold=3, release=3)
        before = ctx.config_field(field)
        _press(h, SC_A, hold=3, release=3)
        after = ctx.config_field(field)
        ctx.assert_true(after != before,
                        f"Menu A toggled {field}: {before} -> {after}")

    # Verify every hitbox field is now ON.
    for f in HITBOX_FIELDS:
        ctx.assert_eq(ctx.config_field(f), 1, f"{f} ON after menu toggle")

    # --- Close the menus: B (state) -> B (depth1) -> B (close radial) ---
    _press(h, SC_B, hold=3, release=3)  # closes PSUBMENU_VISUALIZERS state menu
    _press(h, SC_B, hold=3, release=3)  # depth 1 (sub-radial DISPLAY) -> depth 0
    _press(h, SC_B, hold=3, release=3)  # depth 0 -> Practice_Menu_Close

    closed = h.read32(menu_state) & 0xFFFFFFFF
    ctx.assert_eq(closed, PMENU_CLOSED, "Menu closed after 3x B")

    # --- Resume gameplay; bug (if present) hits within a handful of frames ---
    h.advance(60)

    frames_before = h.read32(frame_count)

    # Wall-clock sleep: a crashed N64 stops VI callbacks, freezing gSysFrameCount.
    # h.advance() would deadlock; h.sleep() always returns.
    h.sleep(2000)
    frames_after = h.read32(frame_count)
    delta = frames_after - frames_before

    ctx.assert_true(
        delta >= 30,
        f"Game stayed alive after enabling all hitboxes via menu mid-carrier-fight "
        f"(gSysFrameCount: {frames_before} -> {frames_after}, delta={delta}; "
        f"expected >=30 over 2 s)."
    )
