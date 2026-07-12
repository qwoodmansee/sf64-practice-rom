#!/usr/bin/env python3
"""Regression suite for patch_linker_script.py's inject/self-heal logic.

Pure Python, no emulator, <1s. Run standalone:

    python3 tools/test_patch_linker_script.py

or via practice_invariants.py (check_linker_patcher_selfheal), which the
pre-commit hook runs on every commit.

Why this exists: the static invariants validate the ARTIFACT (the current
generated .ld / .map) and say nothing about how the patcher behaves on
inputs that do not exist in a green checkout -- a splat-fresh .ld, a stale
block copied from another branch's worktree, a truncated block. Those heal
paths only execute on degraded inputs, so a patcher edit can break them
while every artifact check stays green. (2026-07-11: the stale-excise
path's re-injection gate was broken by a '.practice_late_core' substring
false-match against the .main placement comments; nothing could notice
until a heal actually ran.) Every scenario here reconstructs a degraded
input from the current canonical .ld and asserts the patcher either
converges on the canonical layout or stops loudly WITHOUT touching the
file. The late_core comparisons are byte-exact on purpose: that block's
internal layout is SD-timing sensitive (see CLAUDE.md).

NOT in tests/: the m64p test runner globs tests/test_*.py and would boot
an emulator instance around a suite that needs none.
"""
import contextlib
import io
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import patch_linker_script as p

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIVE_LD = os.path.join(REPO_ROOT, "linker_scripts", "us", "rev1", "starfox64.ld")

SAVE_BSS = "        build/src/practice/practice_save.o(.bss);\n"


def _block(text, marker, endm):
    a = text.find(marker)
    if a < 0:
        return None
    b = text.find(endm, a)
    if b < 0:
        return None
    return text[a:b + len(endm)]


def _late_core(text):
    return _block(text, "/* practice_late_core:",
                  "practice_late_core_VRAM_END = .;")


def _late_pak(text):
    return _block(text, "/* practice_late_pak:",
                  "practice_late_pak_VRAM_END = .;")


def _main_bss(text):
    a = text.find(".main_bss")
    return text[a:text.find("main_VRAM_END", a)]


class _Patcher:
    """Run p.patch() against throwaway files in a temp dir."""

    def __init__(self, tmpdir):
        self.tmpdir = tmpdir
        self.n = 0

    def _write(self, content):
        self.n += 1
        path = os.path.join(self.tmpdir, f"case{self.n}.ld")
        with open(path, "w") as f:
            f.write(content)
        p.LINKER_SCRIPT = path
        return path

    def run(self, content):
        path = self._write(content)
        with contextlib.redirect_stdout(io.StringIO()):
            p.patch()
        with open(path) as f:
            return f.read()

    def expect_untouched_error(self, content):
        """None if patch() raised RuntimeError and left the file untouched;
        a failure description otherwise."""
        path = self._write(content)
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                p.patch()
        except RuntimeError:
            with open(path) as f:
                if f.read() != content:
                    return "raised RuntimeError but modified the file"
            return None
        return "did not raise RuntimeError"


def _derive_fresh(canonical):
    """Approximate a splat-fresh (unpatched) .ld by removing every practice
    injection from the canonical one: the five injected section blocks,
    every practice/lib object line, and the placement comments."""
    txt = canonical
    for pat in [
        r"[ \t]*/\* practice_late_core:.*?practice_late_core_VRAM_END = \.;\n",
        r"[ \t]*/\* practice_late_pak:.*?practice_late_pak_VRAM_END = \.;\n",
        r"[ \t]*/\* Phase 3: Expansion Pak slot pool.*?\n    \}\n",
        r"[ \t]*/\* Macro frame buffer:.*?\n    \}\n",
        r"[ \t]*/\* Macro snapshot buffer:.*?\n    \}\n",
    ]:
        txt, n = re.subn(pat, "", txt, count=1, flags=re.DOTALL)
        if n != 1:
            raise RuntimeError(f"fresh-derivation pattern did not match: {pat}")
    keep = []
    for line in txt.splitlines(keepends=True):
        s = line.strip()
        if s.startswith(("build/src/practice/", "build/lib/")):
            continue
        if "moved to .practice_late_core" in s or "BSS is in .practice" in s:
            continue
        keep.append(line)
    fresh = "".join(keep)
    if "practice" in fresh:
        leftovers = [l.strip() for l in fresh.splitlines() if "practice" in l]
        raise RuntimeError(f"fresh-derivation left practice content: {leftovers[:3]}")
    return fresh


def run_all():
    """Run every scenario. Returns a list of failure strings (empty = all
    pass), or None when the generated .ld is missing (fresh clone before
    make extract -- nothing to test against yet)."""
    if not os.path.isfile(LIVE_LD):
        return None
    with open(LIVE_LD) as f:
        live = f.read()

    failures = []
    saved_path = p.LINKER_SCRIPT

    def check(name, cond):
        if not cond:
            failures.append(name)

    try:
        with tempfile.TemporaryDirectory() as tmpdir:
            pt = _Patcher(tmpdir)

            # Canonical = the patcher's fixed point for the CURRENT object
            # lists. (The live .ld can lag the config by one build when a
            # membership list was just edited; converge first so the suite
            # never false-fails mid-change.)
            canonical = pt.run(live)
            check("patcher converges (second run is a no-op)",
                  pt.run(canonical) == canonical)
            c_core = _late_core(canonical)
            c_pak = _late_pak(canonical)
            check("canonical has both late blocks", bool(c_core and c_pak))
            if failures:
                return failures  # everything below compares against canonical

            # A fresh extract must reconstruct the canonical layout exactly.
            out = pt.run(_derive_fresh(canonical))
            check("fresh: late_core byte-identical", _late_core(out) == c_core)
            check("fresh: late_pak byte-identical", _late_pak(out) == c_pak)
            check("fresh: split .bss in .main_bss", SAVE_BSS in _main_bss(out))
            check("fresh: result idempotent", pt.run(out) == out)

            # Stale block wrongly carrying the split object's .bss (engine
            # globals in unmapped Pak RAM on stock 4 MB), with the .main_bss
            # line also gone: heal must re-emit and re-home the .bss.
            bad = canonical.replace(
                "        build/lib/sd_host/sd_host.o(.bss);\n",
                "        build/lib/sd_host/sd_host.o(.bss);\n" + SAVE_BSS, 1)
            mb = _main_bss(bad)
            bad = bad.replace(mb, mb.replace(SAVE_BSS, ""), 1)
            out = pt.run(bad)
            check("stale-bss: late_core healed", _late_core(out) == c_core)
            check("stale-bss: .bss restored to .main_bss",
                  SAVE_BSS in _main_bss(out))
            check("stale-bss: .bss not in late_core",
                  SAVE_BSS not in _late_core(out))
            check("stale-bss: late_pak untouched", _late_pak(out) == c_pak)

            # Stale block missing one section line, each block.
            bad = canonical.replace(
                "        build/src/practice/practice_save.o(.rodata);\n", "", 1)
            out = pt.run(bad)
            check("stale-core-section: healed", _late_core(out) == c_core)
            check("stale-core-section: exactly one late_core block",
                  out.count(".practice_late_core 0x80720000 :") == 1)
            check("stale-core-section: result idempotent", pt.run(out) == out)
            bad = canonical.replace(
                "        build/lib/fatfs/ff.o(.rodata);\n", "", 1)
            out = pt.run(bad)
            check("stale-pak-section: healed", _late_pak(out) == c_pak)

            # Truncated / decapitated remnants: stop loudly, file untouched,
            # never a duplicate block next to the remnant.
            head = c_core[:c_core.find("practice_late_core_ROM_START")]
            err = pt.expect_untouched_error(canonical.replace(c_core, head, 1))
            check(f"truncated late_core stops loudly ({err})", err is None)

            head = c_pak[:c_pak.find("practice_late_pak_ROM_START")]
            err = pt.expect_untouched_error(canonical.replace(c_pak, head, 1))
            check(f"truncated late_pak stops loudly ({err})", err is None)

            decap = c_core.split("\n", 1)[1]  # drop the comment-marker line
            err = pt.expect_untouched_error(canonical.replace(c_core, decap, 1))
            check(f"decapitated late_core stops loudly ({err})", err is None)
    finally:
        p.LINKER_SCRIPT = saved_path

    return failures


def main():
    failures = run_all()
    if failures is None:
        print("linker script not generated yet; skipping patcher suite")
        return 0
    for f in failures:
        print(f"  FAIL: {f}")
    if failures:
        print(f"Linker-patcher self-heal suite FAILED ({len(failures)} failures)")
        return 1
    print("Linker-patcher self-heal suite passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
