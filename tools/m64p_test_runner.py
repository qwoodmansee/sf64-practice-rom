#!/usr/bin/env python3
"""Functional test runner using mupen64plus via C harness subprocess.

Boots the practice ROM, advances frames, reads/writes memory, and asserts
game state. Tests are defined as Python functions in tests/test_*.py.

Usage:
    python3 tools/m64p_test_runner.py                    # run all tests
    python3 tools/m64p_test_runner.py test_cutscene_skip # run one test

Requires:
    - mupen64plus installed (brew install mupen64plus)
    - C harness compiled (auto-compiled if missing)
"""
import glob
import importlib.util
import os
import re
import struct
import subprocess
import sys
import time

ROM_PATH = "build/starfox64.us.rev1.uncompressed.z64"
MAP_FILE = "build/starfox64.us.rev1.map"
HARNESS_SRC = "tools/m64p_harness.c"
HARNESS_BIN = "tools/m64p_harness"
M64P_LIB = "/opt/homebrew/lib/libmupen64plus.dylib"


def compile_harness():
    """Compile the C harness if needed."""
    if os.path.isfile(HARNESS_BIN):
        src_mtime = os.path.getmtime(HARNESS_SRC)
        bin_mtime = os.path.getmtime(HARNESS_BIN)
        if bin_mtime >= src_mtime:
            return True

    print("  Compiling test harness...", end=" ", flush=True)
    ret = subprocess.run([
        "cc", "-O2", "-o", HARNESS_BIN, HARNESS_SRC,
        "-I/opt/homebrew/include", "-I/opt/homebrew/include/SDL2",
        "-D_THREAD_SAFE",
        "-L/opt/homebrew/lib", "-lmupen64plus", "-lSDL2",
    ], capture_output=True)
    if ret.returncode != 0:
        print("FAILED")
        print(ret.stderr.decode())
        return False
    print("OK")
    return True


def parse_symbols():
    """Parse key symbols from the map file."""
    if not os.path.isfile(MAP_FILE):
        return None

    syms = {}
    config_fields = []

    with open(MAP_FILE) as f:
        for line in f:
            m = re.match(r"\s+0x([0-9a-fA-F]+)\s+(\w+)", line)
            if m:
                addr = int(m.group(1), 16) & 0x1FFFFFFF
                name = m.group(2)
                syms[name] = addr

    # Parse PracticeConfig field offsets from practice.h with correct sizes.
    # bool=s32 (4B), enums=4B, u8/s8=1B, u16/s16=2B, everything else=4B.
    # MIPS struct alignment: each field aligns to min(sizeof, 4).
    _TYPE_SZ = {'u8': 1, 's8': 1, 'u16': 2, 's16': 2}
    def _fsz(t): return _TYPE_SZ.get(t, 4)
    def _align(off, a): return (off + a - 1) & ~(a - 1)

    practice_h = "include/practice.h"
    if os.path.isfile(practice_h):
        with open(practice_h) as f:
            in_config = False
            offset = 0
            for line in f:
                if "PracticeConfig" in line and "{" in line:
                    in_config = True
                    offset = 0
                    continue
                if in_config:
                    # Struct ends at the closing brace line ("} PracticeConfig;"),
                    # NOT at a "}" that appears inside a field's trailing comment
                    # (e.g. "s32 x; /* one of {0,1,5} */"). Strip comments first.
                    code = re.sub(r"/\*.*?\*/", "", line)
                    code = re.sub(r"//.*", "", code)
                    if "}" in code:
                        break
                    m = re.match(r"\s+(\w+)\s+(\w+);", code)
                    if m:
                        sz = _fsz(m.group(1))
                        al = min(sz, 4)
                        offset = _align(offset, al)
                        config_fields.append((m.group(2), offset, sz))
                        offset += sz

    config_offsets = {name: off for name, off, _ in config_fields}
    config_sizes = {name: sz for name, _, sz in config_fields}

    # Constants — must match enums in sf64thread.h and practice.h
    constants = {}
    for name, val in [
        ("GSTATE_BOOT", 0), ("GSTATE_LOGO", 1), ("GSTATE_TITLE", 2),
        ("GSTATE_MAP", 4), ("GSTATE_PLAY", 7),
        ("PLAY_INIT", 0), ("PLAY_UPDATE", 2),
        ("PSCREEN_LEVEL_SELECT", 0), ("PSCREEN_GAMEPLAY", 1),
        ("PLAYERSTATE_LEVEL_INTRO", 2), ("PLAYERSTATE_ACTIVE", 3),
    ]:
        constants[name] = val

    return {"addrs": syms, "config": config_offsets, "config_size": config_sizes,
            "const": constants}


class HarnessConnection:
    """Communicates with the C harness subprocess."""

    def __init__(self, headed=True):
        args = [HARNESS_BIN]
        if headed:
            args.append("--headed")
        args.append(ROM_PATH)

        self.proc = subprocess.Popen(
            args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=None,
            text=True,
        )
        # Wait for READY
        line = self.proc.stdout.readline().strip()
        if line != "READY":
            raise RuntimeError(f"Harness did not send READY, got: {line}")

    def send(self, cmd):
        """Send a command and return the response line."""
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()
        return self.proc.stdout.readline().strip()

    def advance(self, n):
        resp = self.send(f"ADVANCE {n}")
        return resp == "OK"

    def sleep(self, ms):
        resp = self.send(f"SLEEP {ms}")
        return resp == "OK"

    def read32(self, addr):
        resp = self.send(f"READ32 0x{addr:X}")
        if resp.startswith("VAL "):
            return int(resp.split()[1], 16)
        return 0

    def write32(self, addr, val):
        self.send(f"WRITE32 {addr:x} {val:x}")

    def wait_for(self, addr, value, timeout_ms=10000):
        resp = self.send(f"WAIT {addr:x} {value:x} {timeout_ms}")
        return resp == "OK"

    def key_down(self, sdl_keycode):
        return self.send(f"KEYDOWN {sdl_keycode}") == "OK"

    def key_up(self, sdl_keycode):
        return self.send(f"KEYUP {sdl_keycode}") == "OK"

    def quit(self):
        try:
            self.proc.stdin.write("QUIT\n")
            self.proc.stdin.flush()
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


class TestContext:
    """Passed to each test function — wraps harness connection + symbols."""

    def __init__(self, harness, syms):
        self.harness = harness
        self.syms = type("Syms", (), syms)()
        self.passes = []
        self.failures = []

    def advance(self, n=1):
        self.harness.advance(n)

    def sleep(self, ms):
        self.harness.sleep(ms)

    def read_s32(self, addr):
        val = self.harness.read32(addr)
        if val >= 0x80000000:
            val -= 0x100000000
        return val

    def write_s32(self, addr, val):
        if val < 0:
            val += 0x100000000
        self.harness.write32(addr, val)

    def read_float(self, addr):
        raw = self.harness.read32(addr)
        return struct.unpack('>f', raw.to_bytes(4, 'big'))[0]

    def game_state(self):
        return self.read_s32(self.syms.addrs["gGameState"])

    def play_state(self):
        return self.read_s32(self.syms.addrs["gPlayState"])

    def practice_screen(self):
        return self.read_s32(self.syms.addrs["gPracticeScreen"])

    def cs_was_not_skipped(self):
        return self.read_s32(self.syms.addrs["gCsWasNotSkipped"])

    def player_state(self):
        ptr = self.harness.read32(self.syms.addrs["gPlayer"])
        if ptr == 0:
            return 0
        return self.read_s32((ptr & 0x1FFFFFFF) + 0x1C8)

    def press_right(self, hold_frames=3):
        self.harness.key_down(7)   # SC_DPAD_R
        self.advance(hold_frames)
        self.harness.key_up(7)

    def config_field(self, name):
        """Read a PracticeConfig field, returning just that field's bytes.

        u8/u16 fields are packed into a 32-bit word; the N64 is big-endian, so
        the lowest-addressed byte is the most significant byte of the word.
        Extract only the field's own bytes so e.g. a u8 volMusic returns 99, not
        a packed neighbour word.
        """
        base = self.syms.addrs["gPracticeConfig"]
        offset = self.syms.config[name]
        size = getattr(self.syms, "config_size", {}).get(name, 4)
        if size >= 4:
            return self.read_s32(base + offset)
        word = self.harness.read32((base + offset) & ~3)
        shift = 8 * (4 - size - (offset & 3))   # big-endian byte position
        return (word >> shift) & ((1 << (8 * size)) - 1)

    def set_config_field(self, name, val):
        base = self.syms.addrs["gPracticeConfig"]
        offset = self.syms.config[name]
        size = getattr(self.syms, "config_size", {}).get(name, 4)
        if size >= 4:
            self.write_s32(base + offset, val)
            return
        word_addr = (base + offset) & ~3
        word = self.harness.read32(word_addr)
        shift = 8 * (4 - size - (offset & 3))   # big-endian byte position
        mask = ((1 << (8 * size)) - 1) << shift
        word = (word & ~mask) | ((val << shift) & mask)
        self.harness.write32(word_addr, word & 0xFFFFFFFF)

    def wait_for_level_select(self, timeout_ms=10000):
        # Wait for GSTATE_MAP (4) — non-zero so BSS false-fire can't happen
        addr = self.syms.addrs.get("gGameState", 0)
        if addr:
            return self.harness.wait_for(
                addr, self.syms.const["GSTATE_MAP"], timeout_ms
            )
        self.sleep(10000)
        return True

    def wait_for_gameplay(self, timeout_ms=10000):
        addr = self.syms.addrs.get("gGameState", 0)
        if addr:
            return self.harness.wait_for(
                addr, self.syms.const["GSTATE_PLAY"], timeout_ms
            )
        return False

    def wait_for_play_update(self, timeout_ms=10000):
        """Wait until gPlayState==PLAY_UPDATE (gPlayer is allocated)."""
        addr = self.syms.addrs.get("gPlayState", 0)
        if addr:
            return self.harness.wait_for(
                addr, self.syms.const["PLAY_UPDATE"], timeout_ms
            )
        return False

    def select_and_launch_level(self, level_index, press_only=False):
        if not self.wait_for_level_select():
            return False
        # Use keyboard A press — avoids write_s32 to u16 gNextGameState (big-endian issue)
        self.harness.key_down(27)  # SC_A_BUTTON
        self.advance(4)
        self.harness.key_up(27)
        self.sleep(2000)
        return True

    def assert_eq(self, actual, expected, msg=""):
        if actual == expected:
            self.passes.append(msg)
        else:
            fail = f"{msg}: expected {expected}, got {actual}"
            self.failures.append(fail)
            print(f"  FAIL: {fail}")

    def assert_neq(self, actual, expected, msg=""):
        if actual != expected:
            self.passes.append(msg)
        else:
            fail = f"{msg}: expected NOT {expected}"
            self.failures.append(fail)
            print(f"  FAIL: {fail}")

    def assert_true(self, val, msg=""):
        if val:
            self.passes.append(msg)
        else:
            self.failures.append(msg)
            print(f"  FAIL: {msg}")


def discover_tests():
    test_dir = "tests"
    if not os.path.isdir(test_dir):
        return []
    return sorted(glob.glob(os.path.join(test_dir, "test_*.py")))


def main():
    if not os.path.isfile(ROM_PATH):
        print(f"ROM not found at {ROM_PATH}. Build first: make practice -j4")
        return 1

    if not os.path.isfile(M64P_LIB):
        print("mupen64plus not found. Install: brew install mupen64plus")
        print("Skipping functional tests.")
        return 0

    if not os.path.isfile(HARNESS_SRC):
        print(f"Harness source not found at {HARNESS_SRC}")
        print("Skipping functional tests.")
        return 0

    if not compile_harness():
        print("Could not compile test harness. Skipping functional tests.")
        return 0

    syms = parse_symbols()
    if not syms:
        print(f"Could not parse symbols from {MAP_FILE}")
        return 0

    # Collect tests
    if len(sys.argv) > 1:
        name = sys.argv[1]
        if not name.startswith("test_"):
            name = f"test_{name}"
        if not name.endswith(".py"):
            name = f"{name}.py"
        test_files = [os.path.join("tests", name)]
    else:
        test_files = discover_tests()

    py_tests = [f for f in test_files if os.path.isfile(f)]
    if not py_tests:
        print("No test files found in tests/")
        return 0

    total_passed = 0
    total_failed = 0

    for test_file in py_tests:
        name = os.path.basename(test_file).replace(".py", "")
        print(f"  {name}...", end=" ", flush=True)

        harness = None
        try:
            harness = HarnessConnection(headed=True)
            ctx = TestContext(harness, syms)

            spec = importlib.util.spec_from_file_location(name, test_file)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            mod.run(ctx)

            total = len(ctx.passes) + len(ctx.failures)
            if ctx.failures:
                print(f"FAILED ({len(ctx.failures)}/{total})")
                total_failed += len(ctx.failures)
            else:
                print(f"PASSED ({total}/{total})")
            total_passed += len(ctx.passes)
        except Exception as e:
            print(f"ERROR: {e}")
            total_failed += 1
        finally:
            if harness:
                harness.quit()

    print(f"\nResults: {total_passed} passed, {total_failed} failed")
    return 0 if total_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
