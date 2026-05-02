-- Test: save in Corneria, force-transition to Meteo, load slot, verify
-- the state machine drove a cross-scene transition back to Corneria
-- and applied the snapshot.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "state_save_load_cross_scene"

if not H.checkpoint_save_enabled() then
    print("SKIP: cross-scene load needs Expansion Pak (gPracticeSaveDisabled)")
    os.exit(0)
end

-- Reach gameplay in Corneria.
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted to level select")

ok = H.select_and_launch_level(0)  -- 0 = CORNERIA in sLevelList
H.assert_true(ok, "Corneria launched")
ok = H.wait_for_gameplay(400)
H.assert_true(ok, "Gameplay active in Corneria")

-- Stash baseline.
local hit_a = H.read_s32(S.gHitCount)
local px1, py1, pz1 = H.player_pos_xyz()
local path_a = H.read_float(S.gPathProgress)
H.assert_true(px1 ~= nil, "Player allocated")

-- Save into slot 0.
H.press_save_checkpoint()
H.assert_eq(H.last_save_result(), S.const.SAVE_OK, "Slot 0 saved in Corneria")

-- Perturb so a successful load is observable.
H.write_s32(S.gHitCount, hit_a + 9999)
H.assert_neq(H.read_s32(S.gHitCount), hit_a, "Perturbed hit count")

-- Force a scene transition to Meteo by writing the engine's "next" globals.
-- Using Meteo (LEVEL_METEO == 1) keeps audio + overlay distinctly different
-- from Corneria, which is the cross-scene path the load must reverse.
local LEVEL_METEO = 1
local LEVEL_CORNERIA = 0
H.write_u16(S.gNextLevel, LEVEL_METEO)
H.write_u16(S.gNextLevelPhase, 0)
H.write_u16(S.gNextGameState, S.const.GSTATE_PLAY)

ok = H.wait_until(function()
    return H.read_u16(S.gCurrentLevel) == LEVEL_METEO
        and H.game_state() == S.const.GSTATE_PLAY
        and H.play_state() == S.const.PLAY_UPDATE
end, 600, "transition to Meteo")
H.assert_true(ok, "Reached Meteo gameplay")

-- Trigger the cross-scene load.
H.press_load_checkpoint()

-- Wait up to 6 s for the state machine to land in Corneria PLAY_UPDATE
-- and report SLOT_MANAGER_OK. (Timeout in the ROM is 360 frames; we give
-- a little extra slack for the test scheduler.)
ok = H.wait_until(function()
    return H.read_u16(S.gCurrentLevel) == LEVEL_CORNERIA
        and H.game_state() == S.const.GSTATE_PLAY
        and H.play_state() == S.const.PLAY_UPDATE
        and H.last_load_result() == S.const.SAVE_OK
end, 500, "cross-scene apply complete")
H.assert_true(ok, "State machine restored Corneria")

-- Snapshot fields restored?
H.assert_eq(H.read_s32(S.gHitCount), hit_a, "Hit count restored after cross-scene load")
H.assert_true(H.float_near(H.read_float(S.gPathProgress), path_a, 0.5),
    "Path progress restored after cross-scene load")

local px2, py2, pz2 = H.player_pos_xyz()
H.assert_true(H.float_near(px2, px1, 1.0) and H.float_near(py2, py1, 1.0) and H.float_near(pz2, pz1, 1.0),
    "Player position restored after cross-scene load")

H.finish()
