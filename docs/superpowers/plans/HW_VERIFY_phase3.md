# Phase 3 Manual Verification

## Time required: ~2 minutes

## What you'll need

- Any emulator or hardware setup that can boot the practice ROM.
- Optional: BizHawk configured through `BIZHAWK_PATH` for automated Lua testing.

## Background

Phase 3 does not perform real SD card I/O. It adds the portable TLV codec and
RAM-only `slot_manager`, then proves the same code links and runs in the N64 ROM
with a boot-time fake-state smoke test.

## Automated check

If BizHawk is available:

```bash
make practice -j4
python3 tools/run_tests.py test_slot_manager_fake_state
```

PASS criteria:

- The test reports `PASSED`.
- The Lua assertions confirm:
  - `gPracticeSlotTestStatus == 1`
  - slot 0 loaded `0x13572468`
  - slot 1 loaded `0x24681357`
  - the fake load callback ran three times
  - the smoke test used two RAM slots

## Manual boot check

If BizHawk is unavailable:

1. Build the normal practice ROM:
   ```bash
   make practice -j4
   ```
2. Boot `build/starfox64.us.rev1.uncompressed.z64`.
3. Confirm the ROM reaches the practice level select.
4. Launch Corneria and confirm normal gameplay starts.

The Phase 3 smoke test is silent in normal play. A visible boot failure, hang
before level select, or broken level launch is a regression to investigate.

## Optional SC64 sanity

On SC64, confirm the normal boot log still appears:

```text
=== PRACTICE ROM boot @ <date> <time> ===
[iodev] cart=1 sd_init=0
```

No Phase 3 SD files are written, and no SD card content should change.
