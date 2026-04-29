-- Test harness for BizHawk Lua test scripts.
-- Provides helpers for reading game state, waiting for conditions,
-- injecting input, and reporting pass/fail.

local S = dofile("tests/symbols.lua")

local H = {}
H.S = S
H.failures = {}
H.passes = {}
H.test_name = "unknown"

-- Memory helpers (N64 is big-endian)
function H.read_s32(addr)
    return mainmemory.read_s32_be(addr)
end

function H.read_u32(addr)
    return mainmemory.read_u32_be(addr)
end

function H.read_u8(addr)
    return mainmemory.read_u8(addr)
end

function H.read_u16(addr)
    return mainmemory.read_u16_be(addr)
end

function H.read_float(addr)
    return mainmemory.readfloat(addr, true) -- big-endian
end

function H.write_s32(addr, val)
    mainmemory.write_s32_be(addr, val)
end

function H.write_u16(addr, val)
    mainmemory.write_u16_be(addr, val)
end

function H.write_u8(addr, val)
    mainmemory.write_u8(addr, val)
end

-- Game state readers
function H.game_state()
    return H.read_s32(S.gGameState)
end

function H.play_state()
    return H.read_s32(S.gPlayState)
end

function H.current_level()
    return H.read_s32(S.gCurrentLevel)
end

function H.practice_screen()
    return H.read_s32(S.gPracticeScreen)
end

function H.player_state()
    local player_ptr = H.read_u32(S.gPlayer)
    if player_ptr == 0 then return nil end
    local rdram = player_ptr % 0x20000000
    return H.read_s32(rdram + S.player.state)
end

function H.config_field(field_name)
    local offset = S.config[field_name]
    if offset == nil then
        error("Unknown config field: " .. field_name)
    end
    if field_name == "rightWingState" or field_name == "leftWingState" then
        return H.read_u8(S.gPracticeConfig + offset)
    end
    return H.read_s32(S.gPracticeConfig + offset)
end

function H.cs_was_not_skipped()
    return H.read_s32(S.gCsWasNotSkipped)
end

-- Frame control
function H.advance(n)
    n = n or 1
    for i = 1, n do
        emu.frameadvance()
    end
end

-- Wait until a condition function returns true, or timeout.
-- Returns true if condition met, false if timed out.
function H.wait_until(condition_fn, max_frames, description)
    max_frames = max_frames or 600
    description = description or "condition"
    for i = 1, max_frames do
        if condition_fn() then
            return true
        end
        emu.frameadvance()
    end
    return false
end

-- Input injection: press buttons for one frame
function H.press(buttons)
    joypad.set(buttons, 1)
    emu.frameadvance()
end

-- Hold buttons for N frames
function H.hold(buttons, frames)
    for i = 1, frames do
        joypad.set(buttons, 1)
        emu.frameadvance()
    end
end

-- Navigate level select: press D-pad to reach target level index,
-- then press A to launch.
function H.select_and_launch_level(target_index)
    -- Wait for level select screen
    local ok = H.wait_until(function()
        return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
            and H.game_state() == S.const.GSTATE_MAP
    end, 300, "level select screen")

    if not ok then
        return false, "Timed out waiting for level select"
    end

    -- Current selection starts at 0 (Corneria).
    -- Press down to reach target.
    for i = 1, target_index do
        H.press({Down = true})
        H.advance(2) -- debounce
    end

    -- Press A to launch
    H.press({A = true})
    return true
end

-- Wait for gameplay to be fully active (GSTATE_PLAY + PLAY_UPDATE)
function H.wait_for_gameplay(max_frames)
    max_frames = max_frames or 600
    return H.wait_until(function()
        return H.game_state() == S.const.GSTATE_PLAY
            and H.play_state() == S.const.PLAY_UPDATE
    end, max_frames, "gameplay active")
end

--- KSEG0/KSEG1 pointer → physical RDRAM offset (0 .. 8 MiB Expansion Pak maps here).
local function rdram_phys(ptr)
    if ptr == 0 then return 0 end
    return ptr % 0x20000000
end

--- RDRAM offset of Player[0]; nil while unallocated / NULL pointer.
function H.player_rdram()
    local p = H.read_u32(S.gPlayer)
    if p == 0 then return nil end
    return rdram_phys(p)
end

function H.player_pos_xyz()
    local r = H.player_rdram()
    if r == nil then return nil end
    local px = H.read_float(r + S.player.pos_x)
    local py = H.read_float(r + S.player.pos_y)
    local pz = H.read_float(r + S.player.pos_z)
    return px, py, pz
end

--- Practice checkpoint bindings: L + D-Left save, L + D-Right load.
function H.press_save_checkpoint()
    H.hold({L = true}, 2)
    H.press({L = true, Left = true})
end

function H.press_load_checkpoint()
    H.hold({L = true}, 2)
    H.press({L = true, Right = true})
end

function H.last_save_result()
    return H.read_s32(S.gPracticeLastSaveResult)
end

function H.last_load_result()
    return H.read_s32(S.gPracticeLastLoadResult)
end

--- Checkpoint save/load is enabled only when Expansion Pak RAM is available (Practice_Save_Init).
function H.checkpoint_save_enabled()
    return H.read_s32(S.gPracticeSaveDisabled) == 0
end

function H.float_near(a, b, eps)
    eps = eps or 0.05
    if a == nil or b == nil then return false end
    return math.abs(a - b) < eps
end

-- Assertions
function H.assert_eq(actual, expected, message)
    if actual == expected then
        table.insert(H.passes, message or "assert_eq")
    else
        local msg = (message or "assert_eq") ..
            ": expected " .. tostring(expected) ..
            ", got " .. tostring(actual)
        table.insert(H.failures, msg)
        print("FAIL: " .. msg)
    end
end

function H.assert_neq(actual, expected, message)
    if actual ~= expected then
        table.insert(H.passes, message or "assert_neq")
    else
        local msg = (message or "assert_neq") ..
            ": expected NOT " .. tostring(expected) ..
            ", got " .. tostring(actual)
        table.insert(H.failures, msg)
        print("FAIL: " .. msg)
    end
end

function H.assert_true(val, message)
    H.assert_neq(val, false, message)
    H.assert_neq(val, nil, message)
end

-- Finish test and exit
function H.finish()
    local total = #H.passes + #H.failures
    print("")
    print("=== " .. H.test_name .. " ===")
    print(string.format("%d/%d passed", #H.passes, total))
    if #H.failures > 0 then
        print("FAILURES:")
        for _, msg in ipairs(H.failures) do
            print("  - " .. msg)
        end
        client.exitCode(1)
    else
        print("ALL PASSED")
        client.exitCode(0)
    end
end

return H
