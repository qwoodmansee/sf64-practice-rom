"""Regression: a snapshot carrying an insane ObjectInfo pointer must be
sanitized on load, not handed to the engine.

History: 2026-07-09, Zoness on real hardware. Loading a savestate crashed
within seconds -- the new Fault_IsvDump captured a TLB exception on load at
Actor_PracticeShouldCountCullEscape doing (s32)actor->info.hitbox[0] with
info.hitbox == 0x2484bb18 (not an RDRAM address). ObjectInfo's draw/dList,
action, and hitbox are absolute pointers baked at spawn time via
SEGMENTED_TO_VIRTUAL, so a snapshot restored into a launch whose asset/segment
layout differs (or one that captured garbage) plants wild pointers the engine
dereferences within frames. The sanitizer's behavior IS emulator-testable:
poison a live actor's info.hitbox, save, load, and assert the apply path
dropped that actor (gPracticeSnapSanitized > 0) instead of restoring it live.

Also asserts gPracticeSnapDirtyAtSave counted the poisoned actor at save
time (the read-only capture-side probe).

This is a REGRESSION test: it PASSES when the sanitizer works.

Poison-exposure note (2026-07-11, first two runs of this test hung mupen):
the engine dereferences a live actor's info.hitbox EVERY frame, and mupen
does not survive it -- KUSEG pointers hit real TLB-miss machinery and even
misaligned KSEG0 float reads wedge the emulated game inside a frame. The
poison must therefore never be live during an engine frame. Frame-advance
pause (D-Down) gates Play_Main while practice hotkeys keep working, so:
pause -> poison -> save (snapshot captures it, engine never runs) -> heal
-> unpause -> load. If the sanitizer ever regresses, the restored poison
will hang the harness rather than fail an assert -- loud either way.
"""

HW_FAULT_PTR = 0x2484BB18   # historical: the wild pointer from the HW dump
GARBAGE_PTR = 0x80300001    # misaligned KSEG0: sanitizer-invalid (align check)
# Aligned KSEG0 garbage INSIDE the fixed buffers segment (gZBuffer /
# framebuffer land, >= 0x80281000). Alignment- and osMemSize-range-checks
# both pass this; only the SNAPSHOT_PTR_CEILING bound rejects it. No
# legitimate entity pointer targets the buffers segment (all engine
# code/data and scene assets live below 0x80281000).
ALIGNED_GARBAGE_PTR = 0x802F0010

# SDL scancodes (match the m64p harness keymap / existing tests)
SC_A_BUTTON = 27    # 'x'
SC_DPAD_LEFT = 4    # 'a'  -> savestate shortcut (L_JPAD)
SC_DPAD_RIGHT = 7   # 'd'  -> loadstate shortcut (R_JPAD)
SC_DPAD_DOWN = 22   # 's'  -> frame-advance pause toggle (D_JPAD down)

ACTOR_SIZE = 0x2F4       # include/sf64object.h: Actor size
ACTOR_COUNT = 60
SCENERY_SIZE = 0x80      # include/sf64object.h: Scenery size
SCENERY_COUNT = 50
OFF_OBJ_STATUS = 0x00    # u8
OFF_INFO_DRAW = 0x1C     # obj (0x1C) + ObjectInfo.draw/dList (0x00)
OFF_INFO_HITBOX = 0x28   # obj (0x1C) + ObjectInfo.hitbox (0x0C)

OBJ_FREE = 0


def _press(h, scancode, hold=4, release=2):
    h.key_down(scancode)
    h.advance(hold)
    h.key_up(scancode)
    h.advance(release)


def _read_u8(h, addr):
    word = h.read32(addr & ~3)
    shift = 8 * (3 - (addr & 3))
    return (word >> shift) & 0xFF


def _live_scenery(h, S):
    """(count, one live index with a segmented dList draw or -1)."""
    base = S["gScenery"]
    count = 0
    seg_idx = -1
    for i in range(SCENERY_COUNT):
        if _read_u8(h, base + i * SCENERY_SIZE + OFF_OBJ_STATUS) != OBJ_FREE:
            count += 1
            draw = h.read32(base + i * SCENERY_SIZE + OFF_INFO_DRAW)
            if seg_idx < 0 and 0x01000000 <= draw < 0x10000000:
                seg_idx = i
    return count, seg_idx


def _is_paused(ctx, h, S):
    """Frame-advance pause gates Play_Main, so gGameFrameCount freezes."""
    before = ctx.read_s32(S["gGameFrameCount"])
    h.advance(8)
    return ctx.read_s32(S["gGameFrameCount"]) == before


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs
    GSTATE_PLAY = ctx.syms.const["GSTATE_PLAY"]
    PLAY_UPDATE = ctx.syms.const["PLAY_UPDATE"]

    # --- Boot to level select, launch Corneria ---
    ok = h.wait_for(S["gGameState"], ctx.syms.const["GSTATE_MAP"], 60000)
    ctx.assert_true(ok, "ROM booted to level select (GSTATE_MAP)")
    if not ok:
        return
    h.advance(30)

    _press(h, SC_A_BUTTON, hold=4, release=2)
    ok = h.wait_for(S["gGameState"], GSTATE_PLAY, 5000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY")
    if not ok:
        return
    ok = h.wait_for(S["gPlayState"], PLAY_UPDATE, 5000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE")
    if not ok:
        return

    # Let the level settle so some actors are live, and keep flying until
    # dList-drawn scenery exists (Corneria's opening seconds have an empty
    # gScenery array; the city buildings spawn as the level scrolls in).
    h.advance(180)
    for _ in range(15):
        count, seg_idx = _live_scenery(h, S)
        if count > 0 and seg_idx >= 0:
            break
        h.advance(120)

    if "gPracticeSaveDisabled" in S:
        save_disabled = ctx.read_s32(S["gPracticeSaveDisabled"])
        ctx.assert_eq(save_disabled, 0, "save/load enabled (Expansion Pak in mupen)")
        if save_disabled != 0:
            return

    # --- Freeze gameplay: engine must never dereference the poison ---
    _press(h, SC_DPAD_DOWN, hold=4, release=2)
    paused = _is_paused(ctx, h, S)
    ctx.assert_true(paused, "frame-advance pause engaged (gGameFrameCount frozen)")
    if not paused:
        return

    # --- Find live actors and poison their info.hitbox (engine is gated) ---
    # Two poison flavors: misaligned KSEG0 (align check) and ALIGNED garbage
    # inside the buffers segment (only the SNAPSHOT_PTR_CEILING bound catches
    # it -- the 2026-07-11 review gap). Degrade to one victim if the level
    # only has a single live actor right now.
    actors = S["gActors"]
    live = [i for i in range(ACTOR_COUNT)
            if _read_u8(h, actors + i * ACTOR_SIZE + OFF_OBJ_STATUS) != OBJ_FREE]
    ctx.assert_true(len(live) >= 1, "found a live actor to poison")
    if not live:
        return

    poisons = [(live[0], GARBAGE_PTR)]
    if len(live) >= 2:
        poisons.append((live[1], ALIGNED_GARBAGE_PTR))
    else:
        print("  only one live actor; skipping aligned-garbage victim")

    victims = []   # (index, addr, orig_hitbox, poison)
    for idx, poison in poisons:
        addr = actors + idx * ACTOR_SIZE
        orig = h.read32(addr + OFF_INFO_HITBOX)
        h.write32(addr + OFF_INFO_HITBOX, poison)
        victims.append((idx, addr, orig, poison))
        print(f"  poisoned gActors[{idx}].info.hitbox = {poison:#010x} "
              f"(paused; orig {orig:#010x})")
    expected_bad = len(victims)

    # Baseline for the over-aggressive-sanitizer regression (2026-07-11:
    # segmented dList draws like 0x06024ac0 were being treated as garbage
    # and every dList-drawn entity got freed on load). Captured under
    # pause, so the snapshot must restore exactly this population.
    scenery_at_save, seg_scenery = _live_scenery(h, S)
    print(f"  live scenery at save: {scenery_at_save} "
          f"(segmented-draw example idx {seg_scenery})")
    ctx.assert_true(scenery_at_save > 0, "live scenery present at save time")

    # --- Save: D-Left (snapshot captures the garbage pointer) ---
    _press(h, SC_DPAD_LEFT, hold=4, release=2)
    h.advance(4)
    last_save = ctx.read_s32(S["gPracticeLastSaveResult"])
    ctx.assert_eq(last_save, 0, "save succeeded")
    if last_save != 0:
        return

    dirty = ctx.read_s32(S["gPracticeSnapDirtyAtSave"])
    print(f"  gPracticeSnapDirtyAtSave={dirty}")
    ctx.assert_true(dirty >= expected_bad,
                    f"save-side probe counted the poisoned actors ({dirty}/{expected_bad})")

    # --- Heal the live actors with their ORIGINAL pointers, then unpause ---
    # (Healing with NULL is itself a wild deref for a live actor: the
    # engine reads info.hitbox[0] every frame and NULL is KUSEG -- that
    # wedged mupen on this test's first runs.)
    for _, addr, orig, _poison in victims:
        h.write32(addr + OFF_INFO_HITBOX, orig)
    _press(h, SC_DPAD_DOWN, hold=4, release=2)
    h.advance(30)

    # --- Load: D-Right (same scene -> immediate Snapshot_ApplyToGame) ---
    _press(h, SC_DPAD_RIGHT, hold=4, release=2)
    h.advance(2)

    last_load = ctx.read_s32(S["gPracticeLastLoadResult"])
    ctx.assert_eq(last_load, 0, "load succeeded")

    sanitized = ctx.read_s32(S["gPracticeSnapSanitized"])
    print(f"  gPracticeSnapSanitized={sanitized}")
    # EXACTLY the poisoned actors and nothing else. The 2026-07-11
    # regression sanitized every dList-drawn entity (segmented draw
    # pointers), which shows up here as sanitized >> expected_bad. A
    # sanitizer missing the SNAPSHOT_PTR_CEILING bound shows up as
    # sanitized < expected_bad (the aligned-garbage actor restored live).
    ctx.assert_eq(sanitized, expected_bad,
                  "sanitizer dropped ONLY the poisoned actors")

    scenery_after_load, _ = _live_scenery(h, S)
    print(f"  live scenery after load: {scenery_after_load}")
    ctx.assert_eq(scenery_after_load, scenery_at_save,
                  "dList-drawn scenery survived the load (segmented draw pointers are sane)")

    # The poisoned slots must not be live with their garbage pointers.
    for idx, addr, _orig, poison in victims:
        status = _read_u8(h, addr + OFF_OBJ_STATUS)
        hitbox = h.read32(addr + OFF_INFO_HITBOX)
        print(f"  post-load gActors[{idx}]: status={status} hitbox={hitbox:#010x}")
        ctx.assert_true(not (status != OBJ_FREE and hitbox == poison),
                        f"poisoned actor {idx} not restored live with garbage hitbox")

    # The state was saved while paused, so the load restores the paused
    # flag; unpause before the survival check so the engine actually runs.
    _press(h, SC_DPAD_DOWN, hold=4, release=2)
    if _is_paused(ctx, h, S):
        # Pause restore behavior may differ; toggle once more if needed.
        _press(h, SC_DPAD_DOWN, hold=4, release=2)

    # Game survives the frames where the hardware crash fired.
    h.advance(120)
    gs = ctx.read_s32(S["gGameState"])
    ps = ctx.read_s32(S["gPlayState"])
    ctx.assert_eq(gs, GSTATE_PLAY, "still in GSTATE_PLAY 2s after load")
    ctx.assert_eq(ps, PLAY_UPDATE, "still in PLAY_UPDATE 2s after load")
