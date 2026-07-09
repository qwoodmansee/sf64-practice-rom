"""SD save/load durability regression test (real hardware).

The rig has no controller-input injection, so an interactive menu-driven save
can't be automated. Instead we boot a fixture ROM built with IODEV_DIAG_FATFS=1
(`make hil-sd-fixture`) whose boot self-test reproduces the exact temp-file +
rename pattern slot_manager uses for real saves — the path where the SC64
metadata-durability bug surfaced as f_rename -> FR_NO_FILE ("444").

We then assert on the IS-Viewer output:
  - T11 rename=0 (FR_OK) — the rename that used to fail
  - T12 read-back match=1 — the renamed file's contents survived
  - "RENAME-ROUNDTRIP PASS" — overall self-test verdict
  - no "RENAME-ROUNDTRIP FAIL" line

This is the hardware proof for the diskio.c metadata write-verify-retry fix.

Pre-req: `python3 tests/hil/_fixtures/build_sd_rom.py` (or `make hil-sd-fixture`)
and an SD card present in the cart (the self-test writes/removes SF64SELF.*).
"""
from __future__ import annotations

import os

SD_ROM = "tests/hil/_fixtures/sd_selftest_rom.z64"


def run(ctx):
    if not os.path.isfile(SD_ROM):
        ctx.failures.append(
            f"SD fixture not built — run "
            f"`python3 tests/hil/_fixtures/build_sd_rom.py`"
        )
        return

    ctx.upload_rom(SD_ROM)

    # The self-test runs once at boot. Give it room: SD mount + write + rename +
    # read on real hardware, plus the deliberate single-printf-per-line pacing.
    ctx.wait_for_log(r"\[diag-fatfs\] === DONE ===", timeout_ms=30_000)
    ctx.advance_seconds(1)
    ctx.snapshot("sd_selftest")

    # Mount must have come up — otherwise every later step is meaningless.
    ctx.assert_log_contains(r"\[diag-fatfs\] T7 fatfs_mount=0",
                            "SD card mounted (T7 FR_OK)")
    # The rename that used to fail FR_NO_FILE must now be FR_OK.
    ctx.assert_log_contains(r"\[diag-fatfs\] T11 rename=0",
                            "f_rename succeeded (T11 FR_OK) — durability fix holds")
    # The renamed file's contents must survive the round-trip.
    ctx.assert_log_contains(r"\[diag-fatfs\] T12 read_back=0 bytes=\d+ match=1",
                            "renamed file read back intact (T12 match=1)")
    # Overall verdict + no failure line anywhere.
    ctx.assert_log_contains(r"\[diag-fatfs\] RENAME-ROUNDTRIP PASS",
                            "SD save/load round-trip PASS")
    ctx.assert_log_not_contains(r"\[diag-fatfs\] RENAME-ROUNDTRIP FAIL",
                                "no round-trip failure reported")
