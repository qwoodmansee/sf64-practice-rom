"""Build the SD self-test ROM fixture by invoking `make hil-sd-fixture`.

The actual build is a Makefile target — see Makefile §hil-sd-fixture. This
wrapper exists so CI and the HIL runner can build the fixture consistently
with how the wedge fixture is built (build_wedge_rom.py).
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[3]
    proc = subprocess.run(["make", "hil-sd-fixture"], cwd=repo_root)
    if proc.returncode != 0:
        return proc.returncode
    fixture = repo_root / "tests/hil/_fixtures/sd_selftest_rom.z64"
    if not fixture.exists():
        print(f"Make target succeeded but {fixture} missing", file=sys.stderr)
        return 1
    print(f"OK: {fixture} ({fixture.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
