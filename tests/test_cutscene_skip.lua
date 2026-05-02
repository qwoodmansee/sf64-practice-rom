-- Test: cutscene skip works when skipCutscenes is enabled (default).
--
-- This is the regression that motivated the test suite. The practice ROM
-- defaults to skipCutscenes=true. When launching a level, the player should
-- go directly to PLAYERSTATE_ACTIVE (or PLAYERSTATE_INIT), never
-- PLAYERSTATE_LEVEL_INTRO.
--
-- Root cause of the original bug: gCsWasNotSkipped must be set to false
-- AFTER Play_Setup() (which resets it to true) but BEFORE Player_Setup()
-- checks it. If the hook is in the wrong place, the intro cutscene plays
-- despite skipCutscenes being on.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "cutscene_skip"

-- Boot: wait for practice ROM level select
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "practice ROM boot")
H.assert_true(ok, "Practice ROM booted to level select")

-- Verify skipCutscenes defaults to true
H.assert_eq(H.config_field("skipCutscenes"), 1, "skipCutscenes defaults to ON")

-- Launch Corneria (first level, index 0)
ok = H.select_and_launch_level(0)
H.assert_true(ok, "Level select accepted input")

-- Wait for GSTATE_PLAY
ok = H.wait_until(function()
    return H.game_state() == S.const.GSTATE_PLAY
end, 300, "game state reached PLAY")
H.assert_true(ok, "Game transitioned to GSTATE_PLAY")

-- Wait for PLAY_UPDATE (player is allocated and initialized)
ok = H.wait_for_gameplay(300)
H.assert_true(ok, "Play state reached PLAY_UPDATE")

-- THE KEY ASSERTION: player should NOT be in PLAYERSTATE_LEVEL_INTRO
local state = H.player_state()
H.assert_neq(state, S.const.PLAYERSTATE_LEVEL_INTRO,
    "Player is NOT in PLAYERSTATE_LEVEL_INTRO (cutscene skipped)")

-- Player should be in ACTIVE or at least past intro
H.assert_eq(state, S.const.PLAYERSTATE_ACTIVE,
    "Player is in PLAYERSTATE_ACTIVE")

-- gCsWasNotSkipped should be false (0) after skip
H.assert_eq(H.cs_was_not_skipped(), 0,
    "gCsWasNotSkipped is false after cutscene skip")

-- Verify practice screen switched to gameplay
H.assert_eq(H.practice_screen(), S.const.PSCREEN_GAMEPLAY,
    "Practice screen switched to GAMEPLAY")

H.finish()
