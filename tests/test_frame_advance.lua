-- Test: frame advance controls.
--
-- Verifies that:
--   1. D-Down press toggles pause: gGameFrameCount freezes.
--   2. D-Up press while running pauses (new: D-Up also enters pause).
--   3. Pressing D-Up while paused advances exactly one frame.
--   4. A short D-Up hold does not accidentally advance again.
--   5. A long D-Up hold starts repeat-advancing frames.
--   6. A second D-Down press resumes normal play.
--   7. D-Left (no L) triggers save; D-Right (no L/Z) triggers load (smoke).

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "frame_advance"

-- Boot to level select
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted to level select")

-- Launch Corneria (index 0 — no down presses needed)
ok = H.select_and_launch_level(0)
H.assert_true(ok, "Level launched")

-- Wait for gameplay to be fully active
ok = H.wait_for_gameplay(300)
H.assert_true(ok, "Gameplay active")

-- Let the game settle for a few frames
H.advance(10)

-- --- D-UP PAUSES TEST ---
-- Pressing D-Up while running should pause (not just step).
local before_up_pause = H.read_s32(S.gGameFrameCount)
H.press({Up = true})
H.advance(3)
local after_up_pause = H.read_s32(S.gGameFrameCount)
H.assert_eq(after_up_pause, before_up_pause, "D-Up paused when running (gGameFrameCount frozen)")

-- D-Down to unpause before continuing the rest of the tests
H.press({Down = true})
H.advance(2)

-- --- PAUSE TEST (D-Down) ---
-- Record gGameFrameCount before pausing
local before_pause = H.read_s32(S.gGameFrameCount)

-- Press D-Down to enter paused state
H.press({Down = true})

-- Advance 3 emulator ticks — Play_Main should be frozen, gGameFrameCount must not change
H.advance(3)

local after_pause = H.read_s32(S.gGameFrameCount)
H.assert_eq(after_pause, before_pause, "gGameFrameCount did not advance while paused (D-Down)")

-- --- SHORT HOLD STEP TEST ---
-- Hold D-Up for 5 ticks while paused. Only the press edge should run one frame.
local before_step = H.read_s32(S.gGameFrameCount)
H.hold({Up = true}, 5)

local after_step_hold = H.read_s32(S.gGameFrameCount)
local step_delta = after_step_hold - before_step
H.assert_eq(step_delta, 1,
    "gGameFrameCount advanced exactly 1 while D-Up held (delta=" .. tostring(step_delta) .. ")")

-- --- RE-FREEZE / LONG HOLD REPEAT TEST ---
-- Release D-Up; wait 3 ticks — game must stay frozen.
H.advance(3)
local after_release_settle = H.read_s32(S.gGameFrameCount)
H.assert_eq(after_release_settle, after_step_hold,
    "gGameFrameCount stayed frozen after the queued step")

-- Hold D-Up long enough to pass the repeat delay; it should advance multiple frames.
H.hold({Up = true}, 18)
local after_long_hold = H.read_s32(S.gGameFrameCount)
local long_hold_delta = after_long_hold - after_release_settle
H.assert_true(long_hold_delta > 1,
    "gGameFrameCount repeat-advanced on a long D-Up hold (delta=" .. tostring(long_hold_delta) .. ")")

-- --- RESUME TEST ---
-- Press D-Down again to resume normal play.
H.press({Down = true})

-- Advance 5 emulator ticks and check that frame count grew by at least 3.
H.advance(5)
local after_resume = H.read_s32(S.gGameFrameCount)
local resumed_delta = after_resume - after_long_hold
H.assert_true(resumed_delta >= 3,
    "gGameFrameCount advanced normally after resume (delta=" .. tostring(resumed_delta) .. ")")

H.assert_eq(H.game_state(), S.const.GSTATE_PLAY, "Game still in GSTATE_PLAY after frame advance cycle")

-- --- D-LEFT SAVE / D-RIGHT LOAD SMOKE TEST ---
-- Pause, press D-Left to save, then D-Right to load. Verify the game stays
-- in GSTATE_PLAY and doesn't crash (functional save/load correctness is
-- covered in test_save_state.lua).
H.press({Down = true})
H.advance(2)
H.press({Left = true})   -- D-Left: save to active slot
H.advance(2)
H.press({Right = true})  -- D-Right: load from active slot
-- After load the game should still be in play state
local ok_after_load = H.wait_for_gameplay(300)
H.assert_true(ok_after_load, "Game returned to GSTATE_PLAY+PLAY_UPDATE after D-Right load")

-- Load restored the paused state (saved while paused); unpause to leave
-- game running for the next test which controls pause state independently.
H.press({Down = true})
H.advance(2)

-- --- MENU-OPEN CLEARS PAUSE TEST ---
-- Pause again, then open the practice menu — this should clear frame advance.
H.press({Down = true})
H.advance(2)
local before_menu = H.read_s32(S.gGameFrameCount)
H.advance(3)
H.assert_eq(H.read_s32(S.gGameFrameCount), before_menu, "gGameFrameCount frozen before menu open")

-- Hold Start long enough to open the practice menu (START_HOLD_FRAMES = 45)
H.hold({Start = true}, 50)
H.assert_eq(H.read_s32(S.gPracticeMenuState), S.const.PMENU_OPEN,
    "Practice menu opened after Start hold")

-- Close the menu with B
H.press({B = true})
H.advance(2)
H.assert_eq(H.read_s32(S.gPracticeMenuState), S.const.PMENU_CLOSED,
    "Practice menu closed after B press")

-- Frame advance should be cleared — game must tick normally
local after_menu_close = H.read_s32(S.gGameFrameCount)
H.advance(5)
local after_settle = H.read_s32(S.gGameFrameCount)
H.assert_true(after_settle > after_menu_close,
    "gGameFrameCount advances after menu closes (was frozen by frame advance before menu)")

H.finish()
