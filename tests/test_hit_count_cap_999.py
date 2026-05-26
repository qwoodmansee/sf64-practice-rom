"""Test: per-level hit-count cap is 999, not 511 (1.0 cart behavior).

Vanilla 1.1 Display_Update clamps gHitCount to 511 every frame, which is
below the 1.0 cart limit and surprises score-runners. The practice ROM
raises this cap to 999 so scoring matches 1.0 carts.
"""

def run(ctx):
    S = ctx.syms

    ok = ctx.wait_for_level_select()
    ctx.assert_true(ok, "Booted to level select")

    # Launch Corneria so Display_Update runs each frame.
    ok = ctx.select_and_launch_level(0)
    ctx.assert_true(ok, "Launched Corneria")

    ok = ctx.wait_for_gameplay(10000)
    ctx.assert_true(ok, "Gameplay active")

    hc_addr = S.addrs["gHitCount"]

    # Just-below-cap value should survive the clamp.
    ctx.write_s32(hc_addr, 998)
    ctx.advance(2)
    ctx.assert_eq(ctx.read_s32(hc_addr), 998, "gHitCount=998 not clamped")

    # Cap value (999) should also survive.
    ctx.write_s32(hc_addr, 999)
    ctx.advance(2)
    ctx.assert_eq(ctx.read_s32(hc_addr), 999, "gHitCount=999 not clamped")

    # Over-cap value must clamp to 999 (NOT 511).
    ctx.write_s32(hc_addr, 1500)
    ctx.advance(2)
    clamped = ctx.read_s32(hc_addr)
    ctx.assert_eq(clamped, 999, "gHitCount=1500 clamped to 999, not 511")
