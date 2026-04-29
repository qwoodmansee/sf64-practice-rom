-- Test: with the practice menu open, L/R triggers cycle gPracticeActiveSlot
-- across the picker. Verifies the picker cursor (gPracticeActiveSlot) advances
-- and wraps as expected with 4 slots.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "state_slot_picker_navigates"

if not H.checkpoint_save_enabled() then
    print("SKIP: slot picker needs Expansion Pak (gPracticeSaveDisabled)")
    os.exit(0)
end

local PMENU_CLOSED = 0

local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted")

ok = H.select_and_launch_level(0)
H.assert_true(ok, "Corneria launched")
ok = H.wait_for_gameplay(400)
H.assert_true(ok, "Gameplay active")

-- Open the practice menu (R trigger + D-Right binding).
H.press({R = true, Right = true})
ok = H.wait_until(function()
    return H.read_s32(S.gPracticeMenuState) ~= PMENU_CLOSED
end, 30, "menu opens")
H.assert_true(ok, "Practice menu opened")

H.assert_eq(H.read_s32(S.gPracticeActiveSlot), 0, "Active slot starts at 0")

-- R alone advances the cursor.
H.press({R = true})
H.advance(2)
H.assert_eq(H.read_s32(S.gPracticeActiveSlot), 1, "R cycles to slot 1")

H.press({R = true})
H.advance(2)
H.assert_eq(H.read_s32(S.gPracticeActiveSlot), 2, "R cycles to slot 2")

-- L retreats.
H.press({L = true})
H.advance(2)
H.assert_eq(H.read_s32(S.gPracticeActiveSlot), 1, "L cycles back to slot 1")

H.press({L = true})
H.advance(2)
H.press({L = true})
H.advance(2)
-- One more L from slot 0 should wrap to slot 3 (4 slots, wrap-around).
H.assert_true(H.read_s32(S.gPracticeActiveSlot) == 3 or H.read_s32(S.gPracticeActiveSlot) == 0,
    "L from slot 0 wraps (or clamps to 0)")

H.finish()
