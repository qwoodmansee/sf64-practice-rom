"""Regression test: IS-Viewer SC64 protocol gotchas (CLAUDE.md).

The practice ROM's isviewer.c was hardened against a list of SC64
protocol gotchas — IS64 token, atomic rp/wp, follow-up IO_READ for
write-FIFO drain, etc. Any rewrite that breaks one of these silently
breaks the printf channel.

This test asserts that the channel works end-to-end on real hardware
by:
  1. Upload the current practice ROM
  2. Wait for an ISViewer init line (any printf-emitted text)
  3. Confirm multiple lines arrive (proves rp/wp ring rotation works)
  4. Confirm no malformed/garbled lines (proves byte-ordering correct)
"""
from __future__ import annotations

import os

ROM_PATH = "build/starfox64.us.rev1.uncompressed.z64"


def run(ctx):
    if not os.path.isfile(ROM_PATH):
        ctx.failures.append(f"ROM not built at {ROM_PATH}")
        return

    ctx.upload_rom(ROM_PATH)

    # Boot prints: any line in first 3s
    first = ctx.wait_for_log(r".+", timeout_ms=3000)
    ctx.assert_true(first is not None, "first IS-Viewer line received")

    # Multi-line: at least 3 distinct printfs in 5s window
    ctx.advance_seconds(5)
    lines = ctx.client.get_logs(since_ms=ctx.upload_complete_ts)
    distinct = {l["line"] for l in lines if l["line"].strip()}
    ctx.assert_true(
        len(distinct) >= 3,
        f"received at least 3 distinct printf lines (got {len(distinct)})"
    )

    # Sanity: no obvious garbage. Lines should be 7-bit ASCII printable +
    # newline tab. If we see non-ASCII high bytes, byte-ordering is wrong.
    bad = [l["line"] for l in lines if any(ord(c) > 126 and ord(c) != 9 for c in l["line"])]
    ctx.assert_true(
        len(bad) == 0,
        f"all lines are 7-bit ASCII printable (found {len(bad)} suspicious)"
    )
