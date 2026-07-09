"""Confirm the runner produces the cart-wedge banner and EX_TEMPFAIL.

This test uploads a deliberately-broken ROM (MODS_ISVIEWER=0) that
boots but emits no IS-Viewer output. The expected behavior is that
ctx.upload_rom raises CartWedgedError — and we want that exception
to propagate to the runner so the runner can drive its banner +
EX_TEMPFAIL exit path. The wrapper script
tests/hil/_fixtures/run_wedge_in_ci.sh runs this test
non-interactively and asserts the runner exits 75.

CRITICAL: this test must NOT catch CartWedgedError. The runner's
catch in cmd_run is the contract. If this body catches and converts
to a pass, the runner sees ctx.failures == [] → exit 0, and the
wrapper script's exit-code assertion (expect 75) fails.

The pre-flight check (fixture exists?) is the only failure mode this
test asserts on directly.
"""
from __future__ import annotations

import os

WEDGE_ROM = "tests/hil/_fixtures/wedge_rom.z64"


def run(ctx):
    if not os.path.isfile(WEDGE_ROM):
        ctx.failures.append(
            f"wedge fixture not built — run "
            f"`python3 tests/hil/_fixtures/build_wedge_rom.py`"
        )
        return

    # Expected to raise CartWedgedError. Do not catch — the runner
    # catches it, prints the banner, and exits EX_TEMPFAIL.
    ctx.upload_rom(WEDGE_ROM)

    # If we reach here, the fixture failed to actually wedge the cart
    # (it printed something). Record a failure so the test surfaces.
    ctx.failures.append(
        "Expected CartWedgedError but upload succeeded silently — "
        "is the wedge fixture actually wedging the cart?"
    )
