-- Test: save checkpoint corrupts gameplay state then load restores snapshot (same level).

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "state_save_load_same_scene"

if not H.checkpoint_save_enabled() then
    print("SKIP: checkpoint save disabled (needs Expansion Pak / 8MB - gPracticeSaveDisabled)")
    os.exit(0)
end


local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Practice ROM booted to level select")

ok = H.select_and_launch_level(0)
H.assert_true(ok, "Level launch")

ok = H.wait_for_gameplay(400)
H.assert_true(ok, "Gameplay active")

local hit_a = H.read_s32(S.gHitCount)
local px1, py1, pz1 = H.player_pos_xyz()
local path_a = H.read_float(S.gPathProgress)
H.assert_true(px1 ~= nil, "player pos readable")

-- Baseline checkpoint
H.press_save_checkpoint()
H.assert_eq(H.last_save_result(), S.const.SAVE_OK, "save slot produced OK")

-- Perturb (integer only; guaranteed visible)
H.write_s32(S.gHitCount, hit_a + 4242)
H.assert_neq(H.read_s32(S.gHitCount), hit_a, "hit count perturbed")

-- Restore
H.press_load_checkpoint()
H.assert_eq(H.last_load_result(), S.const.SAVE_OK, "load OK")

H.assert_eq(H.read_s32(S.gHitCount), hit_a, "hit count restored")
H.assert_true(H.float_near(H.read_float(S.gPathProgress), path_a, 0.08), "path progress restored")

local px2, py2, pz2 = H.player_pos_xyz()
H.assert_true(H.float_near(px2, px1, 0.15) and H.float_near(py2, py1, 0.15) and H.float_near(pz2, pz1, 0.15),
    "player position restored")

H.finish()
