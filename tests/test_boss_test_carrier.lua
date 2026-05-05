-- Test: Selecting BOSSES -> CARRIER from the level select warps into Corneria
-- with the Attack Carrier spawning instead of Granga.

local H = dofile("tests/harness.lua")
H.test_name = "boss_test_carrier"

local OBJ_BOSS_CO_CARRIER = 293  -- include/sf64object.h:620
local CARRIER_INDEX       = 0    -- gBosses[CARRIER]: sBossList[0] in practice_boss_test.c
-- BOSSES is the last entry in sLevelList (index 16, 0-based).
-- sLevelList: CORNERIA(0) METEO(1) SECTOR Y(2) FORTUNA(3) KATINA(4) AQUAS(5)
--             SECTOR X(6) SOLAR(7) ZONESS(8) TITANIA(9) MACBETH(10) SECTOR Z(11)
--             BOLSE(12) AREA 6(13) VENOM 1(14) VENOM 2(15) BOSSES(16)
local BOSSES_INDEX = 16

-- Wait for level select.
local ok = H.wait_until(function()
    return H.practice_screen() == H.S.const.PSCREEN_LEVEL_SELECT
        and H.game_state() == H.S.const.GSTATE_MAP
end, 600, "level select")
H.assert_true(ok, "reached level select")

-- Navigate to BOSSES and press A (launches CARRIER, the only boss entry).
ok = H.select_and_launch_level(BOSSES_INDEX)
H.assert_true(ok, "navigated to BOSSES and pressed A")

-- gPracticeForceCarrier is set by Practice_BossTest_Launch immediately after
-- Practice_LaunchLevel. Verify it while still in map state.
H.assert_eq(H.read_u8(H.S.gPracticeForceCarrier), 1,
    "gPracticeForceCarrier set during boss-test launch")

-- Wait for gameplay to be active.
ok = H.wait_for_gameplay(900)
H.assert_true(ok, "gameplay became active")

-- Verify we landed in Corneria.
H.assert_eq(H.read_s32(H.S.gCurrentLevel), H.S.const.LEVEL_CORNERIA,
    "current level is Corneria")

-- Wait for the Carrier boss to spawn (timeout 600 frames ~10s).
local boss_addr = H.S.gBosses + CARRIER_INDEX * H.S.boss.sizeof
local boss_ok = H.wait_until(function()
    return H.read_u16(boss_addr + H.S.boss.obj_id) ~= 0
end, 600, "boss spawn")
H.assert_true(boss_ok, "a boss spawned within 600 frames")

-- The boss must be the Carrier, not Granga.
local boss_id = H.read_u16(boss_addr + H.S.boss.obj_id)
H.assert_eq(boss_id, OBJ_BOSS_CO_CARRIER,
    "boss id is OBJ_BOSS_CO_CARRIER (293), not Granga (292)")

-- sFightCarrier must be 1 (set by Corneria_CoCarrier_Init when gPracticeForceCarrier was true).
H.assert_eq(H.read_u8(H.S.sFightCarrier), 1, "sFightCarrier == 1")

H.finish()
