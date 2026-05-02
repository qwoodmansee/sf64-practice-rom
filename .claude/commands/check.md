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
set -o pipefail; make -C lib test 2>&1 | tail -30
```

Expected: all pass and the pipeline exit code is 0. `pipefail` is mandatory
— without it the exit status is just `tail`'s, which always succeeds, so
test failures would be silently reported as PASS. On failure: print which
test binary failed, then stop.

## Step 3 — Build smoke

```bash
set -o pipefail
make practice -j4 > /tmp/sf64-build.log 2>&1
build_status=$?
grep -E 'error:' /tmp/sf64-build.log | head -10
exit $build_status
```

Expected: `build_status == 0` and no error lines. `make ... | grep | head`
hides failures whose output doesn't match the regex (linker errors,
recipe-killed jobs) and returns success even on broken builds — capture
the log first, then both surface errors and propagate the real exit code.
On failure: print the first error line (`file:line: error:`) and the
non-zero `build_status`, then stop.

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
