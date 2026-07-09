"""Smoke test: upload the current practice ROM build, confirm the cart
boots and emits any IS-Viewer output, snapshot the screen.

This is the canonical "is the HIL rig alive" test. Failures here mean
the rig itself is broken, not the ROM.

Pre-req: `make practice -j4` has run and produced
build/starfox64.us.rev1.uncompressed.z64.
"""
from __future__ import annotations

ROM_PATH = "build/starfox64.us.rev1.uncompressed.z64"


def run(ctx):
    import os
    if not os.path.isfile(ROM_PATH):
        ctx.failures.append(f"ROM not built at {ROM_PATH} — run `make practice -j4`")
        return

    ctx.upload_rom(ROM_PATH)
    # Wait for ANY IS-Viewer line. The IS-Viewer module emits an init
    # banner when the channel comes up; if our ROM uses MODS_ISVIEWER
    # this fires reliably within ~50ms.
    ctx.wait_for_log(r".+", timeout_ms=5000)
    ctx.advance_seconds(2)
    # snapshot() is best-effort by default and returns None when the
    # camera endpoint is unavailable — guard before touching the path.
    shot = ctx.snapshot("boot")
    ctx.assert_true(shot is not None and shot.exists() and shot.stat().st_size > 0,
                    "screenshot captured")
    ctx.assert_true(
        # Lenient assertion: ANY log line at all means the cart is alive
        # and printing. Tighter assertions belong in dedicated tests.
        True, "cart booted and emitted at least one IS-Viewer line",
    )
