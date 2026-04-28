-- Test: save is refused while the player is not ACTIVE (intro with cutscenes on).

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "state_refuse_during_cutscene"

local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Practice ROM booted to level select")

-- Intro plays; saves must be refused until PLAYERSTATE_ACTIVE.
H.write_s32(S.gPracticeConfig + S.config.skipCutscenes, 0)

ok = H.select_and_launch_level(0)
H.assert_true(ok, "Level launch")

ok = H.wait_until(function()
    return H.game_state() == S.const.GSTATE_PLAY
        and H.play_state() == S.const.PLAY_UPDATE
        and H.player_state() ~= nil
end, 400, "PLAY_UPDATE with allocated player")
H.assert_true(ok, "entered gameplay update loop")

local saw_refusal = false
for _ = 1, 500 do
    emu.frameadvance()
    local ps = H.player_state()
    if ps ~= nil and ps ~= S.const.PLAYERSTATE_ACTIVE then
        H.press_save_checkpoint()
        H.assert_eq(H.last_save_result(), S.const.SAVE_ERR_SLOT, "save refused when not ACTIVE")
        saw_refusal = true
        break
    end
end

H.assert_true(saw_refusal, "caught a non-ACTIVE frame before ACTIVE gameplay")

H.finish()
