---
description: Print the hardware verification procedure for Phase N. Use when you need to run the IS-Viewer/SC64 heap audit or smoke tests on real hardware.
---

Print the hardware verification procedure for the current phase. No code changes — this is a human-in-the-loop step.

## Step 1 — Find the active phase plan

```bash
ls -t docs/superpowers/plans/*.md | head -5
```

Read the most recent plan doc (or the one passed as argument).

## Step 2 — Find the HW_VERIFY section

Look for sections named "Heap audit", "Hardware verification", "HW_VERIFY", or similar in the plan.

## Step 3 — Print the procedure

Format it clearly:

```
=== HARDWARE VERIFICATION REQUIRED ===

Phase: <N> — <title>
Output file: docs/superpowers/plans/HW_VERIFY_phase<N>.md

Setup:
  Terminal A: sc64deployer debug --isv 0x03FF0000
              (wait for "Listening on...")
  Terminal B: ./tools/sc64dev   (build + upload; works from worktrees — see tools/sc64dev help)
              Then press physical N64 reset button

Procedure:
  <numbered steps from the plan>

When done:
  1. Paste IS-Viewer output into HW_VERIFY_phase<N>.md
  2. Update practice_save_config.h constants
  3. Run /check to verify invariants still pass
  4. Run /phase-wave to execute the constants-pinning step
```

## SC64 reminder (from CLAUDE.md)

- Token must be `0x49533634` ("IS64") — not `0x12345678`
- Every cart-bus write needs a follow-up `IO_READ` drain
- Deployer needs explicit stdout flush — rebuilt at `~/code/SummerCart64/sw/deployer/target/release/sc64deployer`
- Serial device: `/dev/cu.usbserial-SC649T0HH2` (auto-detected)
- `--reboot` may show "no response" warning on Analogue 3D menu — just press the physical reset button
