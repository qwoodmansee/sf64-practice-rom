-- Regression: PracticeConfig charge-shot fields are at expected offsets (writable).
local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "charge_shot_config"

local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted to level select")

H.assert_eq(H.config_field("showChargeShotMeter"), 0, "default showChargeShotMeter off")
H.assert_eq(H.config_field("autoFireChargeShot"), 0, "default autoFireChargeShot off")

H.write_s32(S.gPracticeConfig + S.config.showChargeShotMeter, 1)
H.assert_eq(H.config_field("showChargeShotMeter"), 1, "showChargeShotMeter round-trip")
H.write_s32(S.gPracticeConfig + S.config.autoFireChargeShot, 1)
H.assert_eq(H.config_field("autoFireChargeShot"), 1, "autoFireChargeShot round-trip")

H.write_s32(S.gPracticeConfig + S.config.showChargeShotMeter, 0)
H.write_s32(S.gPracticeConfig + S.config.autoFireChargeShot, 0)

H.finish()
