---
description: Run the full SF64 verification pipeline — static invariants, lib tests, build smoke. Use before committing or after any code changes.
---

Run the SF64 practice ROM verification pipeline in sequence. Stop and report on the first failure.

## Step 1 — Static invariants

```bash
python3 tools/practice_invariants.py
```

Expected: exit 0. On failure: print the failing check name and the grep pattern that missed, then stop.

## Step 2 — Lib unit tests

```bash
make -C lib test 2>&1 | tail -30
```

Expected: all pass. On failure: print which test binary failed, then stop.

## Step 3 — Build smoke

```bash
make practice -j4 2>&1 | grep -E 'error:' | head -10
```

Expected: no output. On failure: print the first error line (`file:line: error:`), then stop.

## Step 4 — BizHawk tests (optional)

```bash
if [ -n "$BIZHAWK_PATH" ]; then
  python3 tools/run_tests.py
else
  echo "BIZHAWK: SKIP (BIZHAWK_PATH not set — run manually when emulator is available)"
fi
```

## Report format

Print a summary table at the end:

```
INVARIANTS : PASS
LIB TESTS  : PASS
BUILD      : PASS
BIZHAWK    : SKIP
```

If arguments were passed (e.g., `/check test_state_save_load_same_scene`), pass them to `run_tests.py` as the test name filter.
