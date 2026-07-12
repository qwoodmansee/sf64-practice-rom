"""Regression: a slot whose payload bytes were corrupted after save must be
refused at load (payload CRC32), leaving the game untouched and alive.

History: 2026-07-10 hardware. The SD transport intermittently corrupted
payload bytes in flight; magic+version+size validated fine and the garbage
was applied straight into game state (garbage actor info pointers -> TLB
crash; earlier: NaN player floats). slot_manager now stores a payload CRC32
at header offset 0x0C on save and refuses mismatches on load
(SLOT_MANAGER_ERR_CORRUPT, with one reread retry on the SD path).

The RAM-slot path shares the same header/CRC logic and is emulator-testable:
save to slot 0, flip one payload byte in the Expansion Pak slot pool, load,
and assert the load is refused and gameplay survives. A fresh save then
loads cleanly.

Note: same-scene gSegments-mismatch refusal (the other stale-save guard)
cannot be exercised in-emulator because the engine rewrites gSegments every
frame; it is covered by the static invariant check_slot_payload_crc.

This is a REGRESSION test: it PASSES when the CRC refusal works.
"""

SC_A_BUTTON = 27   # 'x'
SC_DPAD_LEFT = 4   # 'a'  -> savestate shortcut (L_JPAD)
SC_DPAD_RIGHT = 7  # 'd'  -> loadstate shortcut (R_JPAD)

SLOT_MANAGER_OK = 0
SLOT_MANAGER_ERR_CORRUPT = -7

SLOT_POOL_BASE = 0x80400000        # practice_save_slotpool.c, Pak-only pool
SLOT_MANAGER_HEADER_SIZE = 0x3C
POKE_PAYLOAD_OFFSET = 0x100        # well inside any real snapshot payload


def _press(h, scancode, hold=4, release=2):
    h.key_down(scancode)
    h.advance(hold)
    h.key_up(scancode)
    h.advance(release)


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs
    GSTATE_PLAY = ctx.syms.const["GSTATE_PLAY"]
    PLAY_UPDATE = ctx.syms.const["PLAY_UPDATE"]

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
    h.advance(120)

    if "gPracticeSaveDisabled" in S:
        if ctx.read_s32(S["gPracticeSaveDisabled"]) != 0:
            ctx.assert_true(False, "save/load enabled (Expansion Pak in mupen)")
            return

    # --- Save to the active slot (slot 0 by default) ---
    _press(h, SC_DPAD_LEFT, hold=4, release=2)
    h.advance(4)
    ctx.assert_eq(ctx.read_s32(S["gPracticeLastSaveResult"]), SLOT_MANAGER_OK, "save succeeded")

    # --- Flip one payload byte in the stored slot image ---
    poke_addr = (SLOT_POOL_BASE & 0x1FFFFFFF) + SLOT_MANAGER_HEADER_SIZE + POKE_PAYLOAD_OFFSET
    word = h.read32(poke_addr)
    h.write32(poke_addr, word ^ 0xFF000000)
    h.advance(10)

    # --- Load must be REFUSED via CRC, game must stay alive ---
    _press(h, SC_DPAD_RIGHT, hold=4, release=2)
    h.advance(4)
    result = ctx.read_s32(S["gPracticeLastLoadResult"])
    print(f"  load result with corrupted payload: {result}")
    ctx.assert_eq(result, SLOT_MANAGER_ERR_CORRUPT, "corrupted payload refused with ERR_CORRUPT")

    gs = ctx.read_s32(S["gGameState"])
    ps = ctx.read_s32(S["gPlayState"])
    ctx.assert_eq(gs, GSTATE_PLAY, "still in GSTATE_PLAY after refused load")
    ctx.assert_eq(ps, PLAY_UPDATE, "still in PLAY_UPDATE after refused load")

    # --- A fresh save repairs the slot; load must succeed ---
    _press(h, SC_DPAD_LEFT, hold=4, release=2)
    h.advance(4)
    ctx.assert_eq(ctx.read_s32(S["gPracticeLastSaveResult"]), SLOT_MANAGER_OK, "re-save succeeded")
    _press(h, SC_DPAD_RIGHT, hold=4, release=2)
    h.advance(4)
    ctx.assert_eq(ctx.read_s32(S["gPracticeLastLoadResult"]), SLOT_MANAGER_OK,
                  "load succeeds after fresh save")

    h.advance(60)
    gs = ctx.read_s32(S["gGameState"])
    ps = ctx.read_s32(S["gPlayState"])
    ctx.assert_eq(gs, GSTATE_PLAY, "still in GSTATE_PLAY after good load")
    ctx.assert_eq(ps, PLAY_UPDATE, "still in PLAY_UPDATE after good load")
