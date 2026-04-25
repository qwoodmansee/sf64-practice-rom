-- Test: Selecting a CHECKPOINT phase in the level select starts gameplay
-- at the checkpoint progress position, not at 0.

local H = dofile("tests/harness.lua")
H.test_name = "checkpoint_start"

-- Wait for level select (Corneria is already selected at index 0)
local ok = H.wait_until(function()
    return H.practice_screen() == H.S.const.PSCREEN_LEVEL_SELECT
        and H.game_state() == H.S.const.GSTATE_MAP
end, 600, "level select")

H.assert_true(ok, "reached level select")

-- Press Right to cycle from START to CHECKPOINT phase on Corneria
H.press({Right = true})
H.advance(2)

-- Press A to launch
H.press({A = true})

-- Wait for GSTATE_PLAY + PLAY_UPDATE
ok = H.wait_for_gameplay(600)
H.assert_true(ok, "gameplay became active")

-- gPathProgress should be at the checkpoint position (>50000 for Corneria)
local path_progress = H.read_float(H.S.gPathProgress)
H.assert_true(path_progress > 50000.0, "gPathProgress at checkpoint position (>50000)")
H.assert_true(path_progress < 200000.0, "gPathProgress reasonable (<200000)")

-- gPracticeCheckpointProgress should be cleared to 0 after the hook consumed it
local ckpt_prog = H.read_float(H.S.gPracticeCheckpointProgress)
H.assert_eq(ckpt_prog, 0.0, "gPracticeCheckpointProgress cleared after use")

H.finish()
