-- Verifies iodev_detect() returns IODEV_NONE on the emulator (no cart sim).
-- Pattern follows tests/test_config_defaults.lua.
--
-- Run via: python3 tools/run_tests.py test_iodev_detect

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "iodev_detect_emulator"

-- Boot to a state where Practice_Init has run (level select screen).
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 600, "boot to level select")
H.assert_true(ok, "Booted to level select")

-- sIodevActive is a pointer (u32). After Practice_Init's iodev_detect call,
-- it should point at the stub backend (no SC64 in BizHawk).
local backend_ptr = H.read_u32(S.sIodevActive)
H.assert_true(backend_ptr ~= 0, "sIodevActive is non-NULL after Practice_Init")

-- The backend descriptor's first field is `id` (iodev_id_t). On emulator
-- without flashcart sim, this should be IODEV_NONE = 0. Pointer is in
-- KSEG0 / KSEG1 (0x80xxxxxx / 0xA0xxxxxx); strip to RDRAM offset.
local rdram_offset = backend_ptr % 0x20000000
local backend_id = H.read_u32(rdram_offset)
H.assert_eq(backend_id, 0, "stub backend's id is IODEV_NONE (0)")

H.finish()
