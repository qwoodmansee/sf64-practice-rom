#!/usr/bin/env python3
"""Validate the /sageraces/sf64 folder structure on a mounted SD card.

Usage:
    python3 tools/sd_validate.py <sd-mount-path>

Exit codes:
    0  structure is valid (all required dirs exist)
    1  one or more required dirs are missing (safe to flash, but run --fix first)
    2  usage error
"""

import os
import sys

# The canonical schema for the SF64 subtree.
# Each entry is (relative-path, description).
# Add new entries here when new subdirs are introduced — never remove old ones.
REQUIRED_DIRS = [
    ("sageraces",              "shared namespace root"),
    ("sageraces/sf64",         "SF64 practice ROM data"),
    ("sageraces/sf64/states",  "save state files (.SF64ST)"),
]

STATE_EXT = ".SF64ST"


def validate(mount: str, fix: bool = False) -> int:
    ok = True

    for rel, desc in REQUIRED_DIRS:
        path = os.path.join(mount, rel)
        if os.path.isdir(path):
            print(f"  ok  {rel}/  ({desc})")
        else:
            if fix:
                os.makedirs(path, exist_ok=True)
                print(f" fix  {rel}/  ({desc}) — created")
            else:
                print(f" MISSING  {rel}/  ({desc})")
                ok = False

    states_path = os.path.join(mount, "sageraces", "sf64", "states")
    if os.path.isdir(states_path):
        saves = sorted(
            f for f in os.listdir(states_path)
            if f.upper().endswith(STATE_EXT)
        )
        if saves:
            print(f"\n  {len(saves)} save state(s) in sageraces/sf64/states/:")
            for s in saves:
                size = os.path.getsize(os.path.join(states_path, s))
                print(f"    {s}  ({size:,} bytes)")
        else:
            print("\n  no save states found")

    return 0 if ok else 1


def main() -> int:
    fix = False
    args = [a for a in sys.argv[1:] if a != "--fix"]
    if "--fix" in sys.argv:
        fix = True

    if len(args) != 1:
        print("usage: sd_validate.py [--fix] <sd-mount-path>", file=sys.stderr)
        print("  --fix  create any missing directories", file=sys.stderr)
        return 2

    mount = args[0]
    if not os.path.isdir(mount):
        print(f"error: '{mount}' is not a directory", file=sys.stderr)
        return 2

    print(f"sageraces SD layout check: {mount}")
    return validate(mount, fix=fix)


if __name__ == "__main__":
    sys.exit(main())
