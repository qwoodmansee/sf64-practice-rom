-- Test: Selecting BOSSES -> SARUMAR from the level select warps into Zoness
-- with the Zoness Sarumarine boss spawning.

local H = dofile("tests/harness.lua")
H.test_name = "boss_test_sarumar"

local OBJ_BOSS_ZO_SARUMARINE = 307  -- include/sf64object.h
local BOSS_INDEX              = 5   -- sBossList[5]
local BOSSES_INDEX            = 16

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

-- Scroll right to SARUMAR (boss index 5).
for i = 1, BOSS_INDEX do
    H.press({Right = true})
    H.advance(2)
end

-- Launch.
H.press({A = true})
H.advance(2)

-- gPracticeForceCarrier must remain false for Sarumarine.
H.assert_eq(H.read_u8(H.S.gPracticeForceCarrier), 0,
    "gPracticeForceCarrier is false")

-- Wait for gameplay to be active.
ok = H.wait_for_gameplay(900)
H.assert_true(ok, "gameplay became active")

-- Verify we landed in Zoness.
H.assert_eq(H.read_s32(H.S.gCurrentLevel), H.S.const.LEVEL_ZONESS,
    "current level is ZONESS")

-- Wait for the Sarumarine boss to spawn (timeout 600 frames ~10s).
local boss_addr = H.S.gBosses + 0 * H.S.boss.sizeof
local boss_ok = H.wait_until(function()
    return H.read_u16(boss_addr + H.S.boss.obj_id) ~= 0
end, 600, "boss spawn")
H.assert_true(boss_ok, "boss spawned within 600 frames")

local boss_id = H.read_u16(boss_addr + H.S.boss.obj_id)
H.assert_eq(boss_id, OBJ_BOSS_ZO_SARUMARINE, "boss id is correct")

H.finish()
