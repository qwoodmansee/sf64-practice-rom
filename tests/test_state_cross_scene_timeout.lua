-- Test: corrupt a saved slot's metadata to point to a non-saveable level,
-- trigger load, and verify the cross-scene state machine times out cleanly
-- (no hang, gPracticeLastLoadResult == SLOT_MANAGER_ERR_TIMEOUT, state -> IDLE).

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "state_cross_scene_timeout"

if not H.checkpoint_save_enabled() then
    print("SKIP: timeout test needs Expansion Pak (gPracticeSaveDisabled)")
    os.exit(0)
end

local SLOT_MANAGER_ERR_TIMEOUT = -10
local XLOAD_IDLE = 0
local XLOAD_AWAIT_SCENE_LOAD = 1
local LEVEL_INVALID = 0xFFFF  -- s16 -1 reinterpreted as u16; the state machine
                              -- only checks `level != gCurrentLevel` so any
                              -- non-current value works. INVALID is also
                              -- rejected by practice_overlay_is_saveable so
                              -- request_load logs and returns without driving
                              -- gNextLevel, which is exactly the wedge state
                              -- the timeout protects against.

-- PracticeSlotMeta layout: bool valid(4) | LevelId level(4) | s32 phase(4) | s32 frame(4) = 16 bytes/slot.
local META_STRIDE = 16
local META_LEVEL_OFFSET = 4

local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted to level select")

ok = H.select_and_launch_level(0)
H.assert_true(ok, "Corneria launched")
ok = H.wait_for_gameplay(400)
H.assert_true(ok, "Gameplay active in Corneria")

-- Save into slot 0; meta gets stamped CORNERIA.
H.press_save_checkpoint()
H.assert_eq(H.last_save_result(), S.const.SAVE_OK, "Slot 0 saved in Corneria")

-- Corrupt slot 0 meta level to a non-saveable / impossible value.
H.write_s32(S.gPracticeSlotMeta + 0 * META_STRIDE + META_LEVEL_OFFSET, LEVEL_INVALID)

-- Trigger load. The state machine will see meta.level != gCurrentLevel,
-- kick request_load (which refuses LEVEL_INVALID and returns without
-- driving the engine), and then sit waiting for gCurrentLevel to flip.
-- The 360-frame timeout should fire and cleanly reset.
H.press_load_checkpoint()

-- Verify the state machine entered AWAIT_SCENE_LOAD initially.
H.assert_eq(H.read_s32(S.gPracticeCrossLoadState), XLOAD_AWAIT_SCENE_LOAD,
    "State machine entered AWAIT_SCENE_LOAD")

-- Step past the timeout (360 frames + slack).
for i = 1, 420 do
    emu.frameadvance()
end

H.assert_eq(H.read_s32(S.gPracticeCrossLoadState), XLOAD_IDLE,
    "State machine reset to IDLE after timeout")
H.assert_eq(H.last_load_result(), SLOT_MANAGER_ERR_TIMEOUT,
    "gPracticeLastLoadResult == SLOT_MANAGER_ERR_TIMEOUT")

-- The game must still be running — no hang.
H.assert_true(H.game_state() == S.const.GSTATE_PLAY,
    "Game still running after timeout (no hang)")

H.finish()
