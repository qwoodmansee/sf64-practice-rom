#!/usr/bin/env python3
"""Run BizHawk Lua test scripts against the practice ROM.

Usage:
    python3 tools/run_tests.py                    # run all tests
    python3 tools/run_tests.py test_cutscene_skip # run one test

Requires:
    - BIZHAWK_PATH env var pointing to the BizHawk executable
    - A built practice ROM at build/starfox64.us.rev1.uncompressed.z64
    - Generated symbols at tests/symbols.lua (run tools/extract_symbols.py)

Exit code 0 = all tests passed, 1 = failures or setup error.
"""
import os
import glob
import subprocess
import sys
import shutil

ROM_PATH = "build/starfox64.us.rev1.uncompressed.z64"
TESTS_DIR = "tests"
SYMBOLS_FILE = os.path.join(TESTS_DIR, "symbols.lua")
EXTRACT_SCRIPT = "tools/extract_symbols.py"

def find_bizhawk():
    path = os.environ.get("BIZHAWK_PATH")
    if path and os.path.isfile(path):
        return path
    for candidate in [
        shutil.which("bizhawk"),
        shutil.which("EmuHawk"),
        shutil.which("EmuHawk.exe"),
    ]:
        if candidate:
            return candidate
    return None

def generate_symbols():
    result = subprocess.run(
        [sys.executable, EXTRACT_SCRIPT],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"Symbol extraction failed:\n{result.stderr}", file=sys.stderr)
        return False
    with open(SYMBOLS_FILE, "w") as f:
        f.write(result.stdout)
    return True

def run_test(bizhawk_path, test_file):
    name = os.path.basename(test_file).replace(".lua", "")
    print(f"  Running {name}...", end=" ", flush=True)

    cmd = [
        bizhawk_path,
        ROM_PATH,
        "--lua=" + os.path.abspath(test_file),
    ]

    try:
        result = subprocess.run(
            cmd,
            timeout=120,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            print("PASSED")
            return True
        else:
            print("FAILED")
            if result.stdout.strip():
                print(f"    stdout: {result.stdout.strip()}")
            if result.stderr.strip():
                print(f"    stderr: {result.stderr.strip()}")
            return False
    except subprocess.TimeoutExpired:
        print("TIMEOUT (120s)")
        return False

def main():
    if not os.path.isfile(ROM_PATH):
        print(f"ROM not found at {ROM_PATH}. Build first: make practice -j4")
        return 1

    bizhawk = find_bizhawk()
    if not bizhawk:
        print("BizHawk not found. Set BIZHAWK_PATH or add to PATH.")
        print("Skipping functional tests.")
        return 0

    if not generate_symbols():
        return 1

    # Collect test files
    if len(sys.argv) > 1:
        test_files = [os.path.join(TESTS_DIR, f"test_{sys.argv[1]}.lua")]
        if not os.path.isfile(test_files[0]):
            test_files = [os.path.join(TESTS_DIR, f"{sys.argv[1]}.lua")]
    else:
        test_files = sorted(glob.glob(os.path.join(TESTS_DIR, "test_*.lua")))

    if not test_files:
        print("No test files found.")
        return 1

    print(f"Running {len(test_files)} test(s) with BizHawk at {bizhawk}")
    print(f"ROM: {ROM_PATH}")
    print()

    passed = 0
    failed = 0
    for tf in test_files:
        if not os.path.isfile(tf):
            print(f"  {tf}: NOT FOUND")
            failed += 1
            continue
        if run_test(bizhawk, tf):
            passed += 1
        else:
            failed += 1

    print()
    print(f"Results: {passed} passed, {failed} failed, {passed + failed} total")
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
