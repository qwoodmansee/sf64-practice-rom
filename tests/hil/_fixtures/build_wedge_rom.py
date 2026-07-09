"""Build the wedge ROM fixture by invoking `make hil-wedge-fixture`.

The actual build is a Makefile target — see Makefile §hil-wedge-fixture.
This wrapper exists so the Chunk 6 acceptance test and CI can invoke
`python3 tests/hil/_fixtures/build_wedge_rom.py` consistently with
how the other fixtures are built.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[3]
    proc = subprocess.run(["make", "hil-wedge-fixture"], cwd=repo_root)
    if proc.returncode != 0:
        return proc.returncode
    fixture = repo_root / "tests/hil/_fixtures/wedge_rom.z64"
    if not fixture.exists():
        print(f"Make target succeeded but {fixture} missing", file=sys.stderr)
        return 1
    print(f"OK: {fixture} ({fixture.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
