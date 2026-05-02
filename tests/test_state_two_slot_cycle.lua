-- Test: two RAM slots hold independent checkpoints (slot 0 vs slot 1).

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "state_two_slot_cycle"

if not H.checkpoint_save_enabled() then
    print("SKIP: checkpoint save disabled (needs Expansion Pak / 8MB - gPracticeSaveDisabled)")
    os.exit(0)
end

local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Practice ROM booted to level select")

ok = H.select_and_launch_level(0)
H.assert_true(ok, "Level launch")

ok = H.wait_for_gameplay(400)
H.assert_true(ok, "Gameplay active")

local hit_base = H.read_s32(S.gHitCount)

-- Slot 0: baseline snapshot
H.write_s32(S.gPracticeActiveSlot, 0)
H.press_save_checkpoint()
H.assert_eq(H.last_save_result(), S.const.SAVE_OK, "slot0 save OK")

-- Slot 1: perturbed snapshot
H.write_s32(S.gPracticeActiveSlot, 1)
H.write_s32(S.gHitCount, hit_base + 9001)
H.press_save_checkpoint()
H.assert_eq(H.last_save_result(), S.const.SAVE_OK, "slot1 save OK")

H.assert_eq(H.read_s32(S.gPracticeSlotValidBits), 3, "slots 0 and 1 marked valid")

-- Reload slot 0
H.write_s32(S.gPracticeActiveSlot, 0)
H.write_s32(S.gHitCount, hit_base + 1111)
H.press_load_checkpoint()
H.assert_eq(H.last_load_result(), S.const.SAVE_OK, "reload slot0")
H.assert_eq(H.read_s32(S.gHitCount), hit_base, "slot0 restores baseline hits")

-- Reload slot 1
H.write_s32(S.gPracticeActiveSlot, 1)
H.press_load_checkpoint()
H.assert_eq(H.last_load_result(), S.const.SAVE_OK, "reload slot1")
H.assert_eq(H.read_s32(S.gHitCount), hit_base + 9001, "slot1 restores perturbed hits")

H.finish()
