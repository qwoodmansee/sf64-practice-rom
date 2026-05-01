-- PracticeConfig cheat toggles: offsets and round-trip writes.
local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "cheats_config"

local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted to level select")

H.assert_eq(H.config_field("infHealth"), 0, "default infHealth off")
H.assert_eq(H.config_field("infBombs"), 0, "default infBombs off")
H.assert_eq(H.config_field("infLives"), 0, "default infLives off")
H.assert_eq(H.config_field("infBoost"), 0, "default infBoost off")

H.write_s32(S.gPracticeConfig + S.config.infHealth, 1)
H.assert_eq(H.config_field("infHealth"), 1, "infHealth round-trip")
H.write_s32(S.gPracticeConfig + S.config.infBombs, 1)
H.assert_eq(H.config_field("infBombs"), 1, "infBombs round-trip")
H.write_s32(S.gPracticeConfig + S.config.infLives, 1)
H.assert_eq(H.config_field("infLives"), 1, "infLives round-trip")
H.write_s32(S.gPracticeConfig + S.config.infBoost, 1)
H.assert_eq(H.config_field("infBoost"), 1, "infBoost round-trip")

H.write_s32(S.gPracticeConfig + S.config.infHealth, 0)
H.write_s32(S.gPracticeConfig + S.config.infBombs, 0)
H.write_s32(S.gPracticeConfig + S.config.infLives, 0)
H.write_s32(S.gPracticeConfig + S.config.infBoost, 0)

H.finish()
