-- Test: all PracticeConfig fields have expected defaults after boot.
--
-- Catches the case where a new field is added to PracticeConfig but
-- not initialized in Practice_Init(). On N64, uninitialized BSS memory
-- is zeroed, so bools would appear false — but that's accidental, not
-- intentional. This test documents the intended defaults.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "config_defaults"

-- Boot
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted to level select")

-- Loadout defaults
H.assert_eq(H.config_field("laserStrength"), 2, "laserStrength = LASERS_HYPER (2)")
H.assert_eq(H.config_field("bombCount"), 3, "bombCount = 3")
H.assert_eq(H.config_field("lifeCount"), 2, "lifeCount = 2")
H.assert_eq(H.config_field("goldRingCount"), 0, "goldRingCount = 0")

-- Team defaults
H.assert_eq(H.config_field("falcoAlive"), 1, "falcoAlive = true")
H.assert_eq(H.config_field("slippyAlive"), 1, "slippyAlive = true")
H.assert_eq(H.config_field("peppyAlive"), 1, "peppyAlive = true")

-- Display defaults
H.assert_eq(H.config_field("showInputDisplay"), 1, "showInputDisplay = true")
H.assert_eq(H.config_field("skipCutscenes"), 1, "skipCutscenes = true")
H.assert_eq(H.config_field("showHudOverlay"), 1, "showHudOverlay = true")
H.assert_eq(H.config_field("showLagFrames"), 1, "showLagFrames = true")
H.assert_eq(H.config_field("showSpeed"), 1, "showSpeed = true")
H.assert_eq(H.config_field("showChargeTiming"), 0, "showChargeTiming = false")
H.assert_eq(H.config_field("showMissedInputs"), 0, "showMissedInputs = false")
H.assert_eq(H.config_field("showHitTracking"), 1, "showHitTracking = true")

-- Hitbox defaults (all off)
H.assert_eq(H.config_field("showHitboxes"), 0, "showHitboxes = false")
H.assert_eq(H.config_field("showHitboxActors"), 0, "showHitboxActors = false")
H.assert_eq(H.config_field("showHitboxScenery"), 0, "showHitboxScenery = false")
H.assert_eq(H.config_field("showHitboxItems"), 0, "showHitboxItems = false")
H.assert_eq(H.config_field("showHitboxPlayer"), 0, "showHitboxPlayer = false")
H.assert_eq(H.config_field("showHitboxFlash"), 0, "showHitboxFlash = false")
H.assert_eq(H.config_field("showSpawnZones"), 0, "showSpawnZones = false")
H.assert_eq(H.config_field("expertMode"), 0, "expertMode = false")
H.assert_eq(H.config_field("longHealth"), 0, "longHealth = false")
H.assert_eq(H.config_field("showPauseMinimap"), 1, "showPauseMinimap = true")
H.assert_eq(H.config_field("showChargeShotMeter"), 0, "showChargeShotMeter = false")
H.assert_eq(H.config_field("autoFireChargeShot"), 0, "autoFireChargeShot = false")
H.assert_eq(H.config_field("infHealth"), 0, "infHealth = false")
H.assert_eq(H.config_field("infBombs"), 0, "infBombs = false")
H.assert_eq(H.config_field("infLives"), 0, "infLives = false")
H.assert_eq(H.config_field("infBoost"), 0, "infBoost = false")
H.assert_eq(H.config_field("prevPlanetsMask"), 0, "prevPlanetsMask = 0")
H.assert_eq(H.config_field("macroBindState"), 0, "macroBindState = false")
H.assert_eq(H.config_field("macroLoop"), 0, "macroLoop = false")
H.assert_eq(H.config_field("showEnemyHealth"), 0, "showEnemyHealth = false")
H.assert_eq(H.config_field("enemyHealthSort"), 0, "enemyHealthSort = 0 (nearest)")
H.assert_eq(H.config_field("enemyHealthMinHp"), 0, "enemyHealthMinHp = 0")
H.assert_eq(H.config_field("enemyHealthBossOnly"), 0, "enemyHealthBossOnly = false")
H.assert_eq(H.config_field("enemyHealthHideModels"), 0, "enemyHealthHideModels = false")
H.assert_eq(H.config_field("hitCount"), 0, "hitCount = 0")
H.assert_eq(H.config_field("showLevelTimers"), 1, "showLevelTimers = true")

H.finish()
