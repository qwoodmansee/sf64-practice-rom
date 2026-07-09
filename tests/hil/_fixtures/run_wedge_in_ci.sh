#!/usr/bin/env bash
# Verifies the runner exits with EX_TEMPFAIL (75) when a wedged cart
# is detected in non-interactive (CI) mode.

set -e
cd "$(dirname "$0")/../../.."

# Disable stdin TTY to simulate CI
echo "" | PYTHONPATH=. python3 tools/hil_test_runner.py run \
    tests/hil/test_cart_wedge_detection.py \
    --skip-preflight < /dev/null \
    || rc=$?

rc=${rc:-0}
if [[ "$rc" == "75" ]]; then
    echo "OK: runner exited EX_TEMPFAIL (75) on cart-wedge"
    exit 0
else
    echo "FAIL: expected exit 75, got $rc"
    exit 1
fi
