-- Test: HUD overlay defaults are applied and don't crash during gameplay.
--
-- Verifies that the default config (showHudOverlay=true, showSpeed=true,
-- showLagFrames=true) is active and that gameplay runs stable with the
-- overlay drawing.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "hud_overlay"

-- Boot
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted to level select")

-- Check HUD defaults
H.assert_eq(H.config_field("showHudOverlay"), 1, "showHudOverlay defaults ON")
H.assert_eq(H.config_field("showSpeed"), 1, "showSpeed defaults ON")
H.assert_eq(H.config_field("showLagFrames"), 1, "showLagFrames defaults ON")
H.assert_eq(H.config_field("showHitTracking"), 1, "showHitTracking defaults ON")
H.assert_eq(H.config_field("showChargeTiming"), 0, "showChargeTiming defaults OFF")
H.assert_eq(H.config_field("showMissedInputs"), 0, "showMissedInputs defaults OFF")

-- Launch Corneria
ok = H.select_and_launch_level(0)
H.assert_true(ok, "Level launched")

ok = H.wait_for_gameplay(300)
H.assert_true(ok, "Gameplay active")

-- Enable all HUD options
H.write_s32(S.gPracticeConfig + S.config.showChargeTiming, 1)
H.write_s32(S.gPracticeConfig + S.config.showMissedInputs, 1)

-- Run 3 seconds with full HUD active
H.advance(180)

H.assert_eq(H.game_state(), S.const.GSTATE_PLAY, "Game stable after 3s with full HUD")

H.finish()
