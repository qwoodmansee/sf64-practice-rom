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

# SDL scancodes (match the m64p harness keymap / existing tests)
SC_A_BUTTON = 27    # 'x'
SC_DPAD_LEFT = 4    # 'a'  -> savestate shortcut (L_JPAD)
SC_DPAD_RIGHT = 7   # 'd'  -> loadstate shortcut (R_JPAD)
SC_DPAD_DOWN = 22   # 's'  -> frame-advance pause toggle (D_JPAD down)

ACTOR_SIZE = 0x2F4       # include/sf64object.h: Actor size
ACTOR_COUNT = 60
OFF_OBJ_STATUS = 0x00    # u8
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

    # Let the level settle so some actors are live.
    h.advance(180)

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

    # --- Find a live actor and poison its info.hitbox (engine is gated) ---
    actors = S["gActors"]
    victim = -1
    for i in range(ACTOR_COUNT):
        if _read_u8(h, actors + i * ACTOR_SIZE + OFF_OBJ_STATUS) != OBJ_FREE:
            victim = i
            break
    ctx.assert_true(victim >= 0, "found a live actor to poison")
    if victim < 0:
        return

    victim_addr = actors + victim * ACTOR_SIZE
    orig_hitbox = h.read32(victim_addr + OFF_INFO_HITBOX)
    h.write32(victim_addr + OFF_INFO_HITBOX, GARBAGE_PTR)
    print(f"  poisoned gActors[{victim}].info.hitbox = {GARBAGE_PTR:#010x} "
          f"(paused; orig {orig_hitbox:#010x})")

    # --- Save: D-Left (snapshot captures the garbage pointer) ---
    _press(h, SC_DPAD_LEFT, hold=4, release=2)
    h.advance(4)
    last_save = ctx.read_s32(S["gPracticeLastSaveResult"])
    ctx.assert_eq(last_save, 0, "save succeeded")
    if last_save != 0:
        return

    dirty = ctx.read_s32(S["gPracticeSnapDirtyAtSave"])
    print(f"  gPracticeSnapDirtyAtSave={dirty}")
    ctx.assert_true(dirty >= 1, "save-side probe counted the poisoned actor")

    # --- Heal the live actor with its ORIGINAL pointer, then unpause ---
    # (Healing with NULL is itself a wild deref for a live actor: the
    # engine reads info.hitbox[0] every frame and NULL is KUSEG -- that
    # wedged mupen on this test's first runs.)
    h.write32(victim_addr + OFF_INFO_HITBOX, orig_hitbox)
    _press(h, SC_DPAD_DOWN, hold=4, release=2)
    h.advance(30)

    # --- Load: D-Right (same scene -> immediate Snapshot_ApplyToGame) ---
    _press(h, SC_DPAD_RIGHT, hold=4, release=2)
    h.advance(2)

    last_load = ctx.read_s32(S["gPracticeLastLoadResult"])
    ctx.assert_eq(last_load, 0, "load succeeded")

    sanitized = ctx.read_s32(S["gPracticeSnapSanitized"])
    print(f"  gPracticeSnapSanitized={sanitized}")
    ctx.assert_true(sanitized >= 1, "apply-side sanitizer dropped the poisoned actor")

    # The poisoned slot must not be live with the garbage pointer.
    status = _read_u8(h, victim_addr + OFF_OBJ_STATUS)
    hitbox = h.read32(victim_addr + OFF_INFO_HITBOX)
    print(f"  post-load gActors[{victim}]: status={status} hitbox={hitbox:#010x}")
    ctx.assert_true(not (status != OBJ_FREE and hitbox == GARBAGE_PTR),
                    "poisoned actor not restored live with garbage hitbox")

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
