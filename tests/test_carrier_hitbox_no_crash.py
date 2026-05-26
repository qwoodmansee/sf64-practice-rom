"""Regression test: entering the Corneria Carrier with hitboxes ON must not crash.

Reported bug: enabling the hitbox viewer and then warping into the Corneria
Carrier boss freezes the N64. Disabling hitboxes makes the bug disappear.

Steps:
  1. Boot to level select.
  2. Enable showHitboxes + every per-category toggle via PracticeConfig.
  3. Navigate level cursor UP once -- wraps 0 (CORNERIA) -> 16 (BOSSES).
  4. Press A. Practice_BossTest_Launch warps to Corneria with the Carrier
     forced (warpProgress=201000, ~2000 units before the carrier spawn).
  5. Wait for GSTATE_PLAY + PLAY_UPDATE; advance long enough for the carrier
     boss objects (CARRIER + UPPER + BOTTOM + LEFT) to spawn and for
     Practice_Hitbox_Draw to walk every boss hitbox for several seconds.
  6. Sleep wall-clock 2 s and assert gSysFrameCount still advances.
     A crash freezes RSP/VI; gSysFrameCount stops; delta near zero.
"""

# sf64thread.h
GSTATE_MAP   = 4
GSTATE_PLAY  = 7
PLAY_UPDATE  = 2

# SDL scancodes -- match write_input_config() in tools/m64p_harness.c
SC_A_BUTTON  = 27   # 'x'
SC_DPAD_U    = 26   # 'w' (DPad U)

# Object/Boss layout from sf64object.h and tests/symbols.lua
OBJ_BOSS_CO_CARRIER = 293  # include/sf64object.h:620
LEVEL_CORNERIA      = 0    # include/sf64level.h:86
BOSS_SIZEOF         = 0x408
BOSS_OBJ_ID         = 0x002


def _press_release(h, sc, hold=4, release=2):
    h.key_down(sc)
    h.advance(hold)
    h.key_up(sc)
    h.advance(release)


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs

    game_state  = S["gGameState"]
    play_state  = S["gPlayState"]
    frame_count = S["gSysFrameCount"]

    # --- Step 1: Boot to level select ---
    ok = h.wait_for(game_state, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "ROM booted to level select (GSTATE_MAP=4)")
    if not ok:
        return

    h.advance(30)

    # --- Step 2: Enable hitboxes ---
    ctx.set_config_field("showHitboxes",       1)
    ctx.set_config_field("showHitboxActors",   1)
    ctx.set_config_field("showHitboxScenery",  1)
    ctx.set_config_field("showHitboxItems",    1)
    ctx.set_config_field("showHitboxPlayer",   1)
    ctx.set_config_field("showHitboxFlash",    1)

    ctx.assert_eq(ctx.config_field("showHitboxes"), 1, "showHitboxes written ON")

    # --- Step 3: Navigate to BOSSES (D-pad UP wraps 0 -> 16) ---
    _press_release(h, SC_DPAD_U, hold=4, release=2)

    # --- Step 4: Press A -- launches CARRIER (sBossList[0]) ---
    _press_release(h, SC_A_BUTTON, hold=4, release=2)

    # Practice_BossTest_Launch sets gPracticeForceCarrier=1 after LaunchLevel.
    ctx.assert_eq(h.read32(S["gPracticeForceCarrier"]) & 0xFF, 1,
                  "gPracticeForceCarrier set after BOSSES->CARRIER launch")

    # --- Step 5: Wait for gameplay ---
    ok = h.wait_for(game_state, GSTATE_PLAY, 30000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY after BOSSES launch")
    if not ok:
        return

    ok = h.wait_for(play_state, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE (gameplay active)")
    if not ok:
        return

    # Confirm hitboxes are still on inside gameplay (Play_Init shouldn't reset them).
    ctx.assert_eq(ctx.config_field("showHitboxes"), 1,
                  "showHitboxes still ON after entering gameplay")

    # Confirm we actually landed in Corneria, not somewhere else.
    cur_level = ctx.read_s32(S["gCurrentLevel"])
    ctx.assert_eq(cur_level, LEVEL_CORNERIA,
                  f"Boss-test landed in LEVEL_CORNERIA (got {cur_level})")

    # --- Step 6: Wait for the carrier boss to spawn ---
    # Object layout: u8 status @ +0x00, u16 id @ +0x02. mupen RDRAM is
    # host-endian 32-bit words: read aligned word at boss base, low 16 bits = id.
    boss0_base = S["gBosses"] + 0 * BOSS_SIZEOF
    spawned = False
    for _ in range(60):  # up to ~600 frames (10 s)
        h.advance(10)
        boss_id = h.read32(boss0_base) & 0xFFFF
        if boss_id == OBJ_BOSS_CO_CARRIER:
            spawned = True
            break
    ctx.assert_true(spawned, f"OBJ_BOSS_CO_CARRIER (293) spawned in gBosses[0]")

    # Render hitboxes against the live carrier for several seconds before sleep check.
    # Carrier states transition (intro, salvos, ram, explode); hitbox mutation in
    # fox_co.c:2271-2286 changes y.offset to +/-100000 across states, so we want
    # to walk through several state transitions.
    h.advance(900)  # 15 s of carrier-fight hitbox rendering

    frames_before = h.read32(frame_count)

    # Wall-clock sleep -- if RSP hangs, VI interrupts mask out and h.advance()
    # would block forever waiting for frame callbacks. h.sleep() always returns.
    h.sleep(2000)
    frames_after = h.read32(frame_count)
    delta = frames_after - frames_before

    ctx.assert_true(
        delta >= 30,
        f"Game stayed alive after carrier spawn with hitboxes ON "
        f"(gSysFrameCount: {frames_before} -> {frames_after}, delta={delta}; "
        f"expected >=30 over 2 s). Near-zero delta means the N64 froze -- "
        f"Practice_Hitbox_Draw walked carrier boss hitbox data into a crash."
    )
