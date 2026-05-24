"""Smoke test: confirm KEYDOWN/KEYUP plumbing reaches the ROM controller.

Boots to level select, presses 'x' (mapped to A button in our pinned keymap),
reads gControllerHold[0] across a few frames, and asserts the A bit goes high
while we hold the key and clears after we release. If this fails, every other
real-menu test will fail too -- it isolates the harness/plugin layer.
"""

GSTATE_MAP   = 4
# mupen64plus-input-sdl quirk: SDL_KeyDown stores myKeyState[keysym] but
# doSdlKeys reads keystate[scancode] (config parse converts keysym->scancode
# at load time but the runtime path doesn't). Workaround: pass the SCANCODE
# value via KEYDOWN so it lands in the same slot doSdlKeys reads.
SDL_SCANCODE_X = 27   # A button (mapped to 'x' via key(120) in config)

A_BUTTON_MASK = 0x8000  # OSContPad.button A bit (matches include/PR/os_cont.h)

# OSContPad layout (4 controllers, 6 bytes each, 4-byte aligned -> 8 bytes each
# in arrays). gControllerHold[0].button is the first u16 (hi16 of the word at
# gControllerHold).
_CTRL0_BUTTON_OFF = 0  # u16 hi16 of word


def _read_u16_hi(h, addr):
    return (h.read32(addr & ~3) >> 16) & 0xFFFF


def run(ctx):
    h = ctx.harness
    S = ctx.syms.addrs
    game_state = S["gGameState"]
    ctrl_hold  = S["gControllerHold"]

    ok = h.wait_for(game_state, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "Booted to level select")
    if not ok:
        return

    h.advance(30)

    # Baseline: no buttons pressed.
    baseline = _read_u16_hi(h, ctrl_hold + _CTRL0_BUTTON_OFF)
    ctx.assert_eq(baseline & A_BUTTON_MASK, 0,
                  f"A button bit is 0 before KEYDOWN (button=0x{baseline:04X})")

    # Press X (= A button per pinned keymap), hold for several frames.
    h.key_down(SDL_SCANCODE_X)
    h.advance(6)
    held = _read_u16_hi(h, ctrl_hold + _CTRL0_BUTTON_OFF)
    ctx.assert_neq(held & A_BUTTON_MASK, 0,
                   f"A button bit is set while X held (button=0x{held:04X})")

    h.key_up(SDL_SCANCODE_X)
    h.advance(6)
    released = _read_u16_hi(h, ctrl_hold + _CTRL0_BUTTON_OFF)
    ctx.assert_eq(released & A_BUTTON_MASK, 0,
                  f"A button bit clears after KEYUP (button=0x{released:04X})")
