#!/usr/bin/env python3
"""Generate include/practice_build_info.h with the current git commit hash.

Runs automatically as part of `make practice`. Uses a conditional write so
recompiles are only triggered when the hash actually changes (e.g. new commit
or dirty state changes).

Output:
    #define PRACTICE_BUILD_HASH "B3E7564"   (clean)
    #define PRACTICE_BUILD_HASH "B3E7564-"  (dirty working tree)
"""
import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(REPO_ROOT, "include", "practice_build_info.h")


def get_build_hash():
    try:
        short = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=REPO_ROOT, stderr=subprocess.DEVNULL, text=True
        ).strip().upper()
    except subprocess.CalledProcessError:
        return "UNKNOWN"

    dirty = subprocess.call(
        ["git", "diff", "--quiet", "HEAD"],
        cwd=REPO_ROOT, stderr=subprocess.DEVNULL
    ) != 0

    return short + ("-" if dirty else "")


def main():
    build_hash = get_build_hash()
    content = f'#define PRACTICE_BUILD_HASH "{build_hash}"\n'

    existing = ""
    if os.path.exists(OUT_H):
        with open(OUT_H) as f:
            existing = f.read()

    if existing != content:
        with open(OUT_H, "w") as f:
            f.write(content)
        print(f"Generated {OUT_H}: {build_hash}")
    else:
        print(f"No change: {build_hash}")


if __name__ == "__main__":
    main()
