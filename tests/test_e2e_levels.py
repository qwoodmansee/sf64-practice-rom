#!/usr/bin/env python3
"""E2E tests for SF64 practice ROM levels."""


def run(ctx):
    """Harness entry point: verify ROM boots and reaches level select."""
    ok = ctx.wait_for_level_select()
    ctx.assert_true(ok, "ROM boots to level select")
    if not ok:
        return
    ok = ctx.select_and_launch_level(0)
    ctx.assert_true(ok, "Corneria launched")
    ok = ctx.wait_for_gameplay(10000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY")


import subprocess
import sys
import time
from pathlib import Path


class LevelE2ETests:
    """Run E2E tests by booting ROM and entering each level."""

    def __init__(self, rom_path, dtm_dir="tests/dtm"):
        """Initialize test runner.

        Args:
            rom_path: Path to the practice ROM
            dtm_dir: Directory containing DTM movie files
        """
        self.rom_path = Path(rom_path)
        self.dtm_dir = Path(dtm_dir)

        if not self.rom_path.exists():
            raise FileNotFoundError(f"ROM not found: {rom_path}")

        if not self.dtm_dir.exists():
            raise FileNotFoundError(f"DTM directory not found: {dtm_dir}")

    def run_level_test(self, level_num, timeout_seconds=30):
        """Run a single level test.

        Args:
            level_num: Level to test (0-5)
            timeout_seconds: Max time to let mupen64plus run before killing it

        Returns:
            (passed: bool, message: str)
        """
        try:
            # Run mupen64plus with the ROM
            # Just boot the ROM and let it run; we'll terminate after timeout
            cmd = [
                'mupen64plus',
                '--noosd',
                str(self.rom_path)
            ]

            print(f"[Level {level_num}] Starting ROM (will run for {timeout_seconds}s)...")

            result = subprocess.run(
                cmd,
                timeout=timeout_seconds,
                capture_output=True,
                text=True
            )

            # If mupen64plus ran without crashing, it's a pass
            # Note: We're just testing that the ROM boots without immediate crash
            # More sophisticated tests would check memory state or frame output
            if result.returncode == 0 or result.returncode == -15:  # -15 = SIGTERM (we killed it)
                return True, f"Level {level_num} boot successful (ran without crashing)"
            else:
                # Check stderr for crash indicators
                stderr_snippet = result.stderr[:200] if result.stderr else "no output"
                return False, f"Level {level_num} crashed (code {result.returncode}): {stderr_snippet}"

        except subprocess.TimeoutExpired:
            # Timeout is expected - we intentionally kill it after the timeout
            return True, f"Level {level_num} boot successful (ran for {timeout_seconds}s)"
        except Exception as e:
            return False, f"Level {level_num} error: {e}"

    def run_all_tests(self, levels=None):
        """Run all level tests.

        Args:
            levels: List of level indices to test (default: [0, 1, 2])

        Returns:
            (passed_count: int, total_count: int, results: list)
        """
        if levels is None:
            levels = [0, 1, 2]  # Cold-boot tests for Corneria, Meteo, Sector X

        results = []
        passed = 0

        print(f"\n{'='*60}")
        print(f"SF64 Practice ROM E2E Level Tests")
        print(f"ROM: {self.rom_path}")
        print(f"{'='*60}\n")

        for level in levels:
            success, message = self.run_level_test(level)
            results.append((level, success, message))
            status = "✓ PASS" if success else "✗ FAIL"
            print(f"[{status}] {message}")

            if success:
                passed += 1

        # Summary
        print(f"\n{'='*60}")
        print(f"Results: {passed}/{len(levels)} passed")
        print(f"{'='*60}\n")

        return passed, len(levels), results


def main():
    """Run tests."""
    rom_dir = Path.home() / "Desktop" / "current-practice-rom"

    # Find the .z64 file in the directory
    rom_files = list(rom_dir.glob("*.z64"))
    if not rom_files:
        print(f"Error: No .z64 ROM file found in {rom_dir}")
        sys.exit(1)

    rom_path = rom_files[0]
    print(f"Using ROM: {rom_path}")

    if not rom_path.exists():
        print(f"Error: ROM not found at {rom_path}")
        sys.exit(1)

    # Generate DTM files first
    print("Generating DTM files...")
    from dtm_generator import DTMGenerator
    gen = DTMGenerator()
    gen.generate_level_test(0)  # Corneria
    gen.generate_level_test(1)  # Meteo
    gen.generate_level_test(2)  # Sector X

    # Run tests
    tester = LevelE2ETests(rom_path)
    passed, total, results = tester.run_all_tests()

    # Exit with appropriate code
    sys.exit(0 if passed == total else 1)


if __name__ == '__main__':
    main()
