-- Test: Selecting BOSSES -> CRUSHER from the level select warps into Meteo
-- with the Meteo Crusher boss spawning.

local H = dofile("tests/harness.lua")
H.test_name = "boss_test_crusher"

local OBJ_BOSS_ME_CRUSHER = 297  -- include/sf64object.h
local BOSS_CRUSHER_INDEX  = 0    -- gBosses[0]: Crusher is the sole boss spawn entry, gets slot 0
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

-- Navigate down to BOSSES.
for i = 1, BOSSES_INDEX do
    H.press({Down = true})
    H.advance(2)
end

-- Scroll right to CRUSHER (boss index 1).
H.press({Right = true})
H.advance(2)

-- Launch.
H.press({A = true})
H.advance(2)

-- gPracticeForceCarrier must remain false for Crusher.
H.assert_eq(H.read_u8(H.S.gPracticeForceCarrier), 0,
    "gPracticeForceCarrier is false for Crusher")

-- Wait for gameplay to be active.
ok = H.wait_for_gameplay(900)
H.assert_true(ok, "gameplay became active")

-- Verify we landed in Meteo.
H.assert_eq(H.read_s32(H.S.gCurrentLevel), H.S.const.LEVEL_METEO,
    "current level is Meteo")

-- Wait for the Crusher boss to spawn (timeout 600 frames ~10s).
local boss_addr = H.S.gBosses + BOSS_CRUSHER_INDEX * H.S.boss.sizeof
local boss_ok = H.wait_until(function()
    return H.read_u16(boss_addr + H.S.boss.obj_id) ~= 0
end, 600, "boss spawn")
H.assert_true(boss_ok, "a boss spawned within 600 frames")

-- The boss must be the Meteo Crusher.
local boss_id = H.read_u16(boss_addr + H.S.boss.obj_id)
H.assert_eq(boss_id, OBJ_BOSS_ME_CRUSHER,
    "boss id is OBJ_BOSS_ME_CRUSHER (297)")

H.finish()
