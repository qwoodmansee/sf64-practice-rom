#!/usr/bin/env python3
"""Validate the /sageraces/sf64 folder structure on an SD card.

Two backends — pick one:

  Mount mode (SD card physically inserted in Mac):
    python3 tools/sd_validate.py /Volumes/MyCard

  Deployer mode (card stays in SummerCart64, accessed via USB):
    python3 tools/sd_validate.py --deployer
    python3 tools/sd_validate.py --deployer --remote localhost:9064

Options:
  --fix     Create any missing directories
  --remote  Route through sc64deployer server at the given address

Exit codes:
    0  structure valid
    1  directories missing (flash is safe, but run --fix first)
    2  usage/connection error
"""

import os
import subprocess
import sys

SC64 = os.environ.get("SC64_DEPLOYER", "sc64deployer")

# Canonical schema for the SF64 subtree.
# Add entries for new subdirs; never remove old ones (upgrade safety).
REQUIRED_DIRS = [
    ("sageraces",             "shared namespace root"),
    ("sageraces/sf64",        "SF64 practice ROM data"),
    ("sageraces/sf64/states", "save state files (.SF64ST)"),
]

STATE_EXT = ".SF64ST"


# ---------------------------------------------------------------------------
# Mount backend
# ---------------------------------------------------------------------------

class MountBackend:
    def __init__(self, mount: str):
        self.mount = mount

    def isdir(self, rel: str) -> bool:
        return os.path.isdir(os.path.join(self.mount, rel))

    def mkdir(self, rel: str) -> None:
        os.makedirs(os.path.join(self.mount, rel), exist_ok=True)

    def listdir(self, rel: str):
        path = os.path.join(self.mount, rel)
        return os.listdir(path) if os.path.isdir(path) else []

    def filesize(self, rel: str) -> int:
        return os.path.getsize(os.path.join(self.mount, rel))


# ---------------------------------------------------------------------------
# Deployer backend
# ---------------------------------------------------------------------------

class DeployerBackend:
    def __init__(self, remote: str = ""):
        self.base_cmd = [SC64]
        if remote:
            self.base_cmd += ["-r", remote]

    def _run(self, *args, capture=True):
        cmd = self.base_cmd + ["sd"] + list(args)
        result = subprocess.run(cmd, capture_output=capture, text=True)
        return result

    def isdir(self, rel: str) -> bool:
        r = self._run("stat", "/" + rel)
        return r.returncode == 0

    def mkdir(self, rel: str) -> None:
        self._run("mkdir", "/" + rel, capture=False)

    def listdir(self, rel: str):
        r = self._run("ls", "/" + rel)
        if r.returncode != 0:
            return []
        # sc64deployer sd ls outputs one name per line
        return [line.strip() for line in r.stdout.splitlines() if line.strip()]

    def filesize(self, rel: str) -> int:
        r = self._run("stat", "/" + rel)
        # stat output format: look for a size field
        # e.g. "Size: 12345  Type: File"
        for token in r.stdout.split():
            try:
                return int(token)
            except ValueError:
                continue
        return -1


# ---------------------------------------------------------------------------
# Validation logic (backend-agnostic)
# ---------------------------------------------------------------------------

def validate(backend, fix: bool) -> int:
    ok = True

    for rel, desc in REQUIRED_DIRS:
        if backend.isdir(rel):
            print(f"  ok  {rel}/  ({desc})")
        elif fix:
            backend.mkdir(rel)
            print(f" fix  {rel}/  ({desc}) — created")
        else:
            print(f" MISSING  {rel}/  ({desc})")
            ok = False

    states_rel = "sageraces/sf64/states"
    if backend.isdir(states_rel):
        entries = backend.listdir(states_rel)
        saves = sorted(e for e in entries if e.upper().endswith(STATE_EXT))
        if saves:
            print(f"\n  {len(saves)} save state(s) in {states_rel}/:")
            for s in saves:
                size = backend.filesize(f"{states_rel}/{s}")
                size_str = f"{size:,} bytes" if size >= 0 else "unknown size"
                print(f"    {s}  ({size_str})")
        else:
            print(f"\n  no save states found in {states_rel}/")

    return 0 if ok else 1


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    args = sys.argv[1:]
    fix = "--fix" in args
    use_deployer = "--deployer" in args
    args = [a for a in args if a not in ("--fix", "--deployer")]

    remote = ""
    if "--remote" in args:
        idx = args.index("--remote")
        if idx + 1 >= len(args):
            print("error: --remote requires an address", file=sys.stderr)
            return 2
        remote = args[idx + 1]
        args = args[:idx] + args[idx + 2:]

    if use_deployer:
        if args:
            print("error: --deployer mode takes no positional argument", file=sys.stderr)
            return 2
        backend = DeployerBackend(remote=remote)
        label = f"sc64deployer sd {('(-r ' + remote + ')') if remote else '(direct)'}"
    else:
        if len(args) != 1:
            print(__doc__, file=sys.stderr)
            return 2
        mount = args[0]
        if not os.path.isdir(mount):
            print(f"error: '{mount}' is not a directory", file=sys.stderr)
            return 2
        backend = MountBackend(mount)
        label = mount

    print(f"sageraces SD layout check: {label}")
    return validate(backend, fix=fix)


if __name__ == "__main__":
    sys.exit(main())
