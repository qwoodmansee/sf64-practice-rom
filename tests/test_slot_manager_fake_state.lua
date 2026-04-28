-- Verifies the Phase 3 slot_manager runs inside the practice ROM by checking
-- the boot-time fake-state save/load smoke test.
--
-- Run via: python3 tools/run_tests.py test_slot_manager_fake_state

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "slot_manager_fake_state"

local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 600, "boot to level select")
H.assert_true(ok, "Booted to level select")

H.assert_eq(H.read_s32(S.gPracticeSlotTestStatus), 1, "slot_manager fake-state smoke test passed")
H.assert_eq(H.read_s32(S.gPracticeSlotTestFirstLoadedValue), 0x13572468, "slot 0 fake state loaded")
H.assert_eq(H.read_s32(S.gPracticeSlotTestSecondLoadedValue), 0x24681357, "slot 1 fake state loaded")
H.assert_eq(H.read_s32(S.gPracticeSlotTestLoadCalls), 3, "fake-state load callback called three times")
H.assert_eq(H.read_s32(S.gPracticeSlotTestSlotCount), 2, "fake-state smoke used two RAM slots")

H.finish()
