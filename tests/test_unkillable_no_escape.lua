-- Test: Actors the player physically cannot damage (EVID_EVENT_HANDLER and
-- anything pinned in long-form invulnerability via timer_0C2) must NOT
-- count as escapes when Actor_Move's cull catches them.
--
-- Origin bug: aCoEventScript_script_16 spawns the Slippy-chase Granga as
-- EVID_GRANGA_FIGHTER_2 (killable), then re-inits the same actor as
-- EVID_EVENT_HANDLER mid-chase. EVENT_HANDLER actors set timer_0C2 = 10000
-- so PlayerShot_CollisionCheck skips them entirely (fox_beam.c:791). When
-- the script finishes and the actor falls out of the cull bbox, the
-- escapes counter would tick - blaming the player for damage that was
-- physically impossible.
--
-- Strategy: synthesize an actor in a free slot, force-cull it, and
-- assert escapes stays zero. Then synthesize a control actor (normal
-- event actor, timer_0C2 = 0) and confirm escapes DOES tick - so the
-- test proves both branches of the new conditional, not just that the
-- counter sometimes stays still.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "unkillable_no_escape"

-- Skip cutscenes and enable hit tracking so the score-stats hook fires.
H.write_s32(S.gPracticeConfig + S.config.skipCutscenes,    1)
H.write_s32(S.gPracticeConfig + S.config.showHitTracking,  1)

-- Boot to level select, launch Corneria (index 0), wait for gameplay.
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
        and H.game_state() == S.const.GSTATE_MAP
end, 600, "level select")
H.assert_true(ok, "reached level select")

H.press({A = true})  -- launch Corneria (already on index 0)
ok = H.wait_for_gameplay(900)
H.assert_true(ok, "gameplay active in Corneria")

-- Wait for gPlayer to be allocated (Play_Init runs ~3 frames after GSTATE_PLAY).
ok = H.wait_until(function()
    local p = H.read_u32(S.gPlayer)
    return p ~= 0 and H.player_state() == S.const.PLAYERSTATE_ACTIVE
end, 600, "player active")
H.assert_true(ok, "player allocated and active")

-- Helpers for actor slot manipulation
local function actor_addr(slot)
    return S.gActors + slot * S.actor.sizeof
end

-- info.action is at ObjectInfo offset 0x08, info itself starts at Actor 0x1C
local INFO_ACTION_OFF      = 0x1C + 0x08
local INFO_CULLDISTANCE_OFF= 0x1C + 0x10  -- f32

-- Find a free actor slot. Slots 0..3 are reserved for teammates/team-boss,
-- so start at 4 (matches the engine's own scan in Object_Load).
local function find_free_slot()
    for i = 4, 31 do
        local addr = actor_addr(i)
        if H.read_u8(addr + S.actor.obj_status) == S.const.OBJ_FREE then
            return i
        end
    end
    return nil
end

-- Synthesize an OBJ_ACTOR_EVENT actor in `slot` that will be culled by
-- Actor_Move's bbox check on the next frame. xPath +/- 4000 is the cull
-- bound; placing pos.x at +999999 is well outside it regardless of where
-- the player happens to be on the rails.
local function spawn_synth(slot, eventType, timer_val, bonus)
    local addr = actor_addr(slot)
    -- Zero out info.action so Object_Update doesn't dispatch into junk.
    H.write_s32(addr + INFO_ACTION_OFF, 0)
    -- info.cullDistance: small positive so the camera-z check also fires
    -- (forward cull). Stored as f32 - write the bit pattern for 50.0.
    H.write_s32(addr + INFO_CULLDISTANCE_OFF, 0x42480000)
    -- info.bonus must be > 0 for the score-stats hook to even consider it.
    H.write_u8(addr + S.actor.info_bonus, bonus)
    H.write_s32(addr + S.actor.index, slot)
    -- Identity
    H.write_u16(addr + S.actor.obj_id, S.const.OBJ_ACTOR_EVENT)
    H.write_s32(addr + S.actor.eventType, eventType)
    H.write_u16(addr + S.actor.timer_0C2, timer_val)
    -- Force out-of-bounds: pos.x way to the right of any plausible xPath.
    H.write_s32(addr + S.actor.obj_pos_x, 0x49742400)  -- ~1e6 as f32
    H.write_s32(addr + S.actor.obj_pos_y, 0x00000000)  -- 0.0
    H.write_s32(addr + S.actor.obj_pos_z, 0x00000000)  -- 0.0
    -- Status last so the engine doesn't see a half-built actor.
    H.write_u8(addr + S.actor.obj_status, S.const.OBJ_ACTIVE)
end

local function read_escapes()
    return H.read_s32(S.gPracticeStats + S.stats.escapes)
end

-- ===== Case 1: EVENT_HANDLER actor culled - should NOT count =====
local slot = find_free_slot()
H.assert_true(slot ~= nil, "found free actor slot for synth EVENT_HANDLER")

local before = read_escapes()
spawn_synth(slot, S.const.EVID_EVENT_HANDLER, 10000, 1)

-- One frame is enough: Actor_Move runs in the per-frame actor loop and
-- the cull bbox check trips immediately on a 1e6 X position.
H.advance(2)

-- The actor should have been killed by Object_Kill (status -> OBJ_FREE).
local status_after = H.read_u8(actor_addr(slot) + S.actor.obj_status)
H.assert_eq(status_after, S.const.OBJ_FREE,
    "synth EVENT_HANDLER was culled by Actor_Move")

local after = read_escapes()
H.assert_eq(after, before,
    "escapes unchanged when EVENT_HANDLER actor culls (filter works)")

-- ===== Case 2: Normal event actor culled - SHOULD count =====
-- Confirms the test would actually fail if the filter were wrong (i.e.
-- guards against a regression that just disables the entire counter).
slot = find_free_slot()
H.assert_true(slot ~= nil, "found free actor slot for control")

before = read_escapes()
spawn_synth(slot, 0, 0, 1)  -- eventType=EVID_VENOM_FIGHTER_1, timer_0C2=0
H.advance(2)

status_after = H.read_u8(actor_addr(slot) + S.actor.obj_status)
H.assert_eq(status_after, S.const.OBJ_FREE,
    "control synth actor was culled by Actor_Move")

after = read_escapes()
H.assert_eq(after, before + 1,
    "escapes ticked by 1 when a damageable actor culled (counter still works)")

-- ===== Case 3: Long timer_0C2 on normal eventType - should NOT count =====
-- Covers the timer_0C2 < 1000 half of the conditional independently
-- from the EVENT_HANDLER half (e.g. EVID_SX_LASER post-hit state).
slot = find_free_slot()
H.assert_true(slot ~= nil, "found free actor slot for timer-pinned synth")

before = read_escapes()
spawn_synth(slot, 0, 5000, 1)  -- normal eventType but pinned invuln
H.advance(2)

status_after = H.read_u8(actor_addr(slot) + S.actor.obj_status)
H.assert_eq(status_after, S.const.OBJ_FREE,
    "timer-pinned synth actor was culled by Actor_Move")

after = read_escapes()
H.assert_eq(after, before,
    "escapes unchanged when timer_0C2-pinned actor culls (filter works)")

-- ===== Case 4: Chase captain (group_flag set, teammate sibling) =====
-- The Corneria Slippy-chase Granga is the canonical instance: script_16
-- adds it as captain (EVA_GROUP_FLAG = 256) of group 12, script_17 adds
-- Slippy to the same group with EVA_TEAM_ID = TEAM_ID_SLIPPY. The
-- captain stays as EVID_GRANGA_FIGHTER_2 throughout the script unless
-- Z-trigger #2 hits (rare in practice), so the EVENT_HANDLER filter
-- doesn't catch this path. We synthesize the relationship and confirm
-- the cull-time group lookup excludes the captain.
local chosen_group = 99  -- arbitrary, won't collide with live actors
local teammate_slot = find_free_slot()
H.assert_true(teammate_slot ~= nil, "found free actor slot for synth teammate")
do
    local addr = actor_addr(teammate_slot)
    -- Active OBJ_ACTOR_EVENT teammate sibling: EVA_TEAM_ID set, EVA_GROUP_ID
    -- matches the captain we'll spawn next. Position it at origin so the
    -- cull bbox check leaves it alone (we only want to cull the captain).
    H.write_s32(addr + INFO_ACTION_OFF, 0)
    H.write_s32(addr + INFO_CULLDISTANCE_OFF, 0x42480000)  -- 50.0
    H.write_u8 (addr + S.actor.info_bonus, 0)
    H.write_u16(addr + S.actor.obj_id, S.const.OBJ_ACTOR_EVENT)
    H.write_s32(addr + S.actor.eventType, 0)
    H.write_u16(addr + S.actor.timer_0C2, 0)
    H.write_s32(addr + S.actor.iwork_team_id, 2)             -- TEAM_ID_SLIPPY
    H.write_s32(addr + S.actor.iwork_group_id, chosen_group)
    H.write_s32(addr + S.actor.iwork_group_flag, 0)          -- not captain
    H.write_s32(addr + S.actor.obj_pos_x, 0)
    H.write_s32(addr + S.actor.obj_pos_y, 0)
    H.write_s32(addr + S.actor.obj_pos_z, 0)
    H.write_u8 (addr + S.actor.obj_status, S.const.OBJ_ACTIVE)
end

slot = find_free_slot()
H.assert_true(slot ~= nil, "found free actor slot for synth chase captain")
before = read_escapes()
do
    local addr = actor_addr(slot)
    H.write_s32(addr + INFO_ACTION_OFF, 0)
    H.write_s32(addr + INFO_CULLDISTANCE_OFF, 0x42480000)
    H.write_u8 (addr + S.actor.info_bonus, 1)                -- countable
    H.write_u16(addr + S.actor.obj_id, S.const.OBJ_ACTOR_EVENT)
    H.write_s32(addr + S.actor.eventType, 4)                 -- EVID_GRANGA_FIGHTER_1 (any non-EVENT_HANDLER)
    H.write_u16(addr + S.actor.timer_0C2, 0)                 -- not pinned-invuln
    H.write_s32(addr + S.actor.iwork_team_id, 0)             -- not a teammate itself
    H.write_s32(addr + S.actor.iwork_group_id, chosen_group) -- same group as synth Slippy
    H.write_s32(addr + S.actor.iwork_group_flag, 256)        -- captain
    H.write_s32(addr + S.actor.obj_pos_x, 0x49742400)        -- ~1e6, force cull
    H.write_s32(addr + S.actor.obj_pos_y, 0)
    H.write_s32(addr + S.actor.obj_pos_z, 0)
    H.write_u8 (addr + S.actor.obj_status, S.const.OBJ_ACTIVE)
end
H.advance(2)

status_after = H.read_u8(actor_addr(slot) + S.actor.obj_status)
H.assert_eq(status_after, S.const.OBJ_FREE,
    "synth chase captain was culled by Actor_Move")

after = read_escapes()
H.assert_eq(after, before,
    "escapes unchanged when chase captain (with teammate sibling) culls")

-- Clean up the synth teammate so it doesn't interfere with later tests.
H.write_u8(actor_addr(teammate_slot) + S.actor.obj_status, S.const.OBJ_FREE)
H.write_s32(actor_addr(teammate_slot) + S.actor.iwork_team_id, 0)
H.write_s32(actor_addr(teammate_slot) + S.actor.iwork_group_id, 0)

-- ===== Case 5: Corneria Granga chase captain after sibling is gone =====
-- This is the live miss: by cull time the teammate relationship may no
-- longer be discoverable, but the chaser still carries the durable script
-- identity (Corneria + Granga eventType + nonzero group flag). It must not
-- count as an escape.
slot = find_free_slot()
H.assert_true(slot ~= nil, "found free actor slot for synth no-sibling chase captain")
before = read_escapes()
do
    local addr = actor_addr(slot)
    H.write_s32(addr + INFO_ACTION_OFF, 0)
    H.write_s32(addr + INFO_CULLDISTANCE_OFF, 0x42480000)
    H.write_u8 (addr + S.actor.info_bonus, 1)
    H.write_u16(addr + S.actor.obj_id, S.const.OBJ_ACTOR_EVENT)
    H.write_s32(addr + S.actor.eventType, S.const.EVID_GRANGA_FIGHTER_2)
    H.write_u16(addr + S.actor.timer_0C2, 0)
    H.write_s32(addr + S.actor.iwork_team_id, 0)
    H.write_s32(addr + S.actor.iwork_group_id, 12)
    H.write_s32(addr + S.actor.iwork_group_flag, 256)
    H.write_s32(addr + S.actor.obj_pos_x, 0x49742400)
    H.write_s32(addr + S.actor.obj_pos_y, 0)
    H.write_s32(addr + S.actor.obj_pos_z, 0)
    H.write_u8 (addr + S.actor.obj_status, S.const.OBJ_ACTIVE)
end
H.advance(2)

status_after = H.read_u8(actor_addr(slot) + S.actor.obj_status)
H.assert_eq(status_after, S.const.OBJ_FREE,
    "synth no-sibling Corneria Granga chase captain was culled by Actor_Move")

after = read_escapes()
H.assert_eq(after, before,
    "escapes unchanged when no-sibling Corneria Granga chase captain culls")

-- ===== Case 6: Live Slippy-chase cull signature =====
-- Hardware readout after the bad +1 showed:
--   reserved slot 1/3, OBJ_ACTOR_EVENT, EVID_GRANGA_FIGHTER_2
--   group 0, flag 0, team TEAM_ID_MAX, timer_0C2 0, state 200
-- By this point the group/captain metadata is gone, so the filter needs
-- to recognize the remaining reserved-slot chase signature.
slot = find_free_slot()
H.assert_true(slot ~= nil, "found free actor slot for live-signature chase synth")
before = read_escapes()
do
    local addr = actor_addr(slot)
    H.write_s32(addr + INFO_ACTION_OFF, 0)
    H.write_s32(addr + INFO_CULLDISTANCE_OFF, 0x42480000)
    H.write_u8 (addr + S.actor.info_bonus, 1)
    H.write_s32(addr + S.actor.index, 1)
    H.write_u16(addr + S.actor.obj_id, S.const.OBJ_ACTOR_EVENT)
    H.write_s32(addr + S.actor.eventType, S.const.EVID_GRANGA_FIGHTER_2)
    H.write_u16(addr + S.actor.timer_0C2, 0)
    H.write_u16(addr + S.actor.state, 200)
    H.write_s32(addr + S.actor.iwork_team_id, S.const.TEAM_ID_MAX)
    H.write_s32(addr + S.actor.iwork_group_id, 0)
    H.write_s32(addr + S.actor.iwork_group_flag, 0)
    H.write_s32(addr + S.actor.obj_pos_x, 0x49742400)
    H.write_s32(addr + S.actor.obj_pos_y, 0)
    H.write_s32(addr + S.actor.obj_pos_z, 0)
    H.write_u8 (addr + S.actor.obj_status, S.const.OBJ_ACTIVE)
end
H.advance(2)

status_after = H.read_u8(actor_addr(slot) + S.actor.obj_status)
H.assert_eq(status_after, S.const.OBJ_FREE,
    "live-signature Corneria Granga chase actor was culled by Actor_Move")

after = read_escapes()
H.assert_eq(after, before,
    "escapes unchanged for measured live Slippy-chase cull signature")

H.finish()
