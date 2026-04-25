-- Test: hitbox viewer can be enabled without crashing.
--
-- Enables all hitbox toggles, launches Corneria, and runs for several
-- seconds to verify no crash occurs. Also verifies the config fields
-- are writable and persist.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "hitbox_viewer"

-- Boot to level select
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "practice ROM boot")
H.assert_true(ok, "Practice ROM booted to level select")

-- Verify hitbox defaults are all OFF
H.assert_eq(H.config_field("showHitboxes"), 0, "showHitboxes defaults to OFF")
H.assert_eq(H.config_field("showHitboxActors"), 0, "showHitboxActors defaults to OFF")
H.assert_eq(H.config_field("showHitboxScenery"), 0, "showHitboxScenery defaults to OFF")
H.assert_eq(H.config_field("showHitboxItems"), 0, "showHitboxItems defaults to OFF")
H.assert_eq(H.config_field("showHitboxPlayer"), 0, "showHitboxPlayer defaults to OFF")
H.assert_eq(H.config_field("showHitboxFlash"), 0, "showHitboxFlash defaults to OFF")

-- Enable all hitbox options by writing to memory
H.write_s32(S.gPracticeConfig + S.config.showHitboxes, 1)
H.write_s32(S.gPracticeConfig + S.config.showHitboxActors, 1)
H.write_s32(S.gPracticeConfig + S.config.showHitboxScenery, 1)
H.write_s32(S.gPracticeConfig + S.config.showHitboxItems, 1)
H.write_s32(S.gPracticeConfig + S.config.showHitboxPlayer, 1)
H.write_s32(S.gPracticeConfig + S.config.showHitboxFlash, 1)

-- Verify writes stuck
H.assert_eq(H.config_field("showHitboxes"), 1, "showHitboxes written to ON")
H.assert_eq(H.config_field("showHitboxActors"), 1, "showHitboxActors written to ON")

-- Launch Corneria
ok = H.select_and_launch_level(0)
H.assert_true(ok, "Level launched")

-- Wait for gameplay
ok = H.wait_for_gameplay(300)
H.assert_true(ok, "Gameplay active")

-- Hitboxes should still be enabled
H.assert_eq(H.config_field("showHitboxes"), 1, "showHitboxes still ON in gameplay")

-- Run for 5 seconds (300 frames at 60fps) with hitboxes rendering.
-- If this completes without a crash, the hitbox viewer is stable.
H.advance(300)

-- Still alive — verify game state hasn't broken
H.assert_eq(H.game_state(), S.const.GSTATE_PLAY, "Game still in GSTATE_PLAY after 5s")
H.assert_eq(H.play_state(), S.const.PLAY_UPDATE, "Play state still PLAY_UPDATE after 5s")

H.finish()
