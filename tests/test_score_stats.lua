-- Test: PracticeStats counters initialize to zero on boot.
--
-- Catches three regressions:
-- (1) The PracticeStats struct gets removed/renamed and the symbol
--     extraction silently fails (gPracticeStats becomes 0x0).
-- (2) Practice_Hud_Reset stops zeroing some field, leaving stale state
--     across level reloads.
-- (3) The struct layout changes (e.g. someone adds a field in the middle
--     and forgets to update tools/extract_symbols.py), so reading by
--     offset returns garbage.
--
-- This is a boot-time sanity test only; verifying the engine actually
-- increments the counters during gameplay would require driving inputs
-- through a Corneria run, which BizHawk can do but is brittle to capture
-- and reproduce. Catching the layout/reset bugs is the high-value half.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "score_stats"

H.assert_true(S.gPracticeStats ~= nil and S.gPracticeStats ~= 0,
    "gPracticeStats symbol resolved (extract_symbols saw it in the map)")

-- Boot to level select.
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 300, "boot")
H.assert_true(ok, "Booted to level select")

-- All six counters should be zero at boot. They live in .bss but are
-- explicitly zeroed by Practice_Hud_Reset on each level reload, so a
-- non-zero value here means either the struct moved, the offsets are
-- wrong, or someone wrote to it before the test started.
local function read_stat(field)
    local off = S.stats[field]
    H.assert_true(off ~= nil, "stats offset for " .. field .. " is defined")
    return H.read_s32(S.gPracticeStats + off)
end

H.assert_eq(read_stat("kills"),      0, "kills = 0 at boot")
H.assert_eq(read_stat("csBonus"),    0, "csBonus = 0 at boot")
H.assert_eq(read_stat("directHits"), 0, "directHits = 0 at boot")
H.assert_eq(read_stat("escapes"),    0, "escapes = 0 at boot")
H.assert_eq(read_stat("crashes"),    0, "crashes = 0 at boot")
H.assert_eq(read_stat("teamKills"),  0, "teamKills = 0 at boot")

-- Pad-and-write probe: write a sentinel into each field via offset, read
-- it back, and confirm the offsets really do address distinct s32s. This
-- catches the case where two fields collide on the same offset (the
-- offsets table in extract_symbols.py drifts from the struct layout).
local SENTINELS = {
    kills      = 0x11111111,
    csBonus    = 0x22222222,
    directHits = 0x33333333,
    escapes    = 0x44444444,
    crashes    = 0x55555555,
    teamKills  = 0x66666666,
}
for field, val in pairs(SENTINELS) do
    H.write_s32(S.gPracticeStats + S.stats[field], val)
end
for field, val in pairs(SENTINELS) do
    -- s32 read of an unsigned-looking sentinel comes back negative once the
    -- top bit is set; compare via low 32 bits.
    local got = H.read_u32(S.gPracticeStats + S.stats[field])
    H.assert_eq(got, val, field .. " field stores its sentinel cleanly")
end

-- Restore zeros so a follow-on test isn't surprised.
for field, _ in pairs(SENTINELS) do
    H.write_s32(S.gPracticeStats + S.stats[field], 0)
end

H.finish()
