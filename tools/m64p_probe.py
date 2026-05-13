#!/usr/bin/env python3
"""ROM health probe — boots the practice ROM and checks key game state.

Usage:
    python3 tools/m64p_probe.py              # boot, confirm level select reached
    python3 tools/m64p_probe.py 0            # boot then navigate to Corneria
    python3 tools/m64p_probe.py 13           # boot then navigate to Aquas

Level IDs: 0=Corneria 1=Meteo 2=SectorX 3=Area6 5=SectorY 6=Venom1 7=Solar
           8=Zoness 9=VenomAndross 11=Macbeth 12=Titania 13=Aquas 14=Fortuna
           16=Katina 17=Bolse 18=SectorZ 19=Venom2

Exit 0 = all checks green, 1 = crash / silent music / check failure
"""
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

# Game state constants (from include/sf64thread.h)
GSTATE_MAP = 4    # practice level select lives here
GSTATE_PLAY = 7   # in-level gameplay
PLAY_INIT = 1
PLAY_UPDATE = 2

# Practice screen constants (from include/practice.h PracticeScreen enum)
PSCREEN_LEVEL_SELECT = 0
PSCREEN_GAMEPLAY = 1

# Player struct byte offsets (from include/sf64player.h)
PLAYER_POS_X = 0x074
PLAYER_POS_Y = 0x078
PLAYER_POS_Z = 0x07C
PLAYER_TRUE_ZPOS = 0x138

LEVEL_NAMES = {
    -1: "INVALID",
    0: "CORNERIA",
    1: "METEO",
    2: "SECTOR_X",
    3: "AREA_6",
    4: "UNK_4",
    5: "SECTOR_Y",
    6: "VENOM_1",
    7: "SOLAR",
    8: "ZONESS",
    9: "VENOM_ANDROSS",
    10: "TRAINING",
    11: "MACBETH",
    12: "TITANIA",
    13: "AQUAS",
    14: "FORTUNA",
    16: "KATINA",
    17: "BOLSE",
    18: "SECTOR_Z",
    19: "VENOM_2",
    20: "VERSUS",
    77: "WARP_ZONE",
}

# gBgmSeqId stores the sequence ID OR'd with SEQ_FLAG (0x8000) for looping BGMs.
# Strip the flag to get the base sequence ID for name lookup.
SEQ_FLAG = 0x8000
BGM_NAMES = {
    0x0000: "SEQ_ID_SFX",
    0x0001: "SEQ_ID_VOICE",
    0x0002: "SEQ_ID_CORNERIA",
    0x0003: "SEQ_ID_METEO",
    0x0004: "SEQ_ID_TITANIA",
    0x0005: "SEQ_ID_SECTOR_X",
    0x0006: "SEQ_ID_ZONESS",
    0x0007: "SEQ_ID_AREA_6",
    0x0008: "SEQ_ID_VENOM_1",
    0x0009: "SEQ_ID_SECTOR_Y",
    0x000A: "SEQ_ID_FORTUNA",
    0x000B: "SEQ_ID_SOLAR",
    0x000C: "SEQ_ID_BOLSE",
    0x000D: "SEQ_ID_KATINA",
    0x000E: "SEQ_ID_AQUAS",
    0x000F: "SEQ_ID_SECTOR_Z",
    0x0010: "SEQ_ID_MACBETH",
    0x0011: "SEQ_ID_ANDROSS",
    0x0012: "SEQ_ID_BOSS_CO_1",
    0x0022: "SEQ_ID_BOSS_CO_2",
    0x0030: "SEQ_ID_BOSS_AQ",
    0x0034: "SEQ_ID_TITLE",
    0x0036: "SEQ_ID_MENU",
    0x0039: "SEQ_ID_DEATH",
    0x003A: "SEQ_ID_GAME_OVER",
    0x003E: "SEQ_ID_TRAINING",
    0xFFFF: "SEQ_ID_NONE",
}


def compile_harness():
    if os.path.isfile(HARNESS_BIN):
        src_mtime = os.path.getmtime(HARNESS_SRC)
        bin_mtime = os.path.getmtime(HARNESS_BIN)
        if bin_mtime >= src_mtime:
            return True
    print("  Compiling harness...", end=" ", flush=True)
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


def parse_map():
    """Return {symbol_name: rdram_address} for symbols of interest."""
    if not os.path.isfile(MAP_FILE):
        return None
    syms = {}
    with open(MAP_FILE) as f:
        for line in f:
            m = re.match(r"\s+0x([0-9a-fA-F]{16})\s+(\w+)", line)
            if m:
                addr = int(m.group(1), 16) & 0x1FFFFFFF
                syms[m.group(2)] = addr
    return syms


class Harness:
    def __init__(self):
        self.proc = subprocess.Popen(
            [HARNESS_BIN, "--headed", ROM_PATH],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True,
        )
        line = self.proc.stdout.readline().strip()
        if line != "READY":
            raise RuntimeError(f"harness did not send READY, got: {line!r}")

    def _send(self, cmd):
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()
        return self.proc.stdout.readline().strip()

    def advance(self, n):
        self._send(f"ADVANCE {n}")

    def read32(self, addr):
        resp = self._send(f"READ32 0x{addr:X}")
        if resp.startswith("VAL "):
            return int(resp.split()[1], 16)
        return 0

    def write32(self, addr, val):
        self._send(f"WRITE32 {addr:x} {val:x}")

    def wait(self, addr, expected, timeout_ms=60000):
        resp = self._send(f"WAIT {addr:x} {expected:x} {timeout_ms}")
        return resp == "OK"

    def read_s32(self, addr):
        val = self.read32(addr)
        return val - 0x100000000 if val >= 0x80000000 else val

    def read_u16_at(self, addr):
        """Read a u16 from a 4-byte-aligned word (big-endian N64)."""
        aligned = addr & ~3
        byte_off = addr & 3
        val32 = self.read32(aligned)
        if byte_off == 0:
            return (val32 >> 16) & 0xFFFF
        elif byte_off == 2:
            return val32 & 0xFFFF
        else:
            raise ValueError(f"u16 at non-even address 0x{addr:x}")

    def read_f32(self, addr):
        """Read a 32-bit float (N64 big-endian bit pattern)."""
        val = self.read32(addr)
        return struct.unpack('>f', struct.pack('>I', val))[0]

    def deref_player_pos(self, gplayer_addr):
        """Dereference gPlayer pointer and return (x, y, z, trueZ) or None."""
        ptr = self.read32(gplayer_addr)
        if ptr == 0:
            return None
        rdram = ptr & 0x1FFFFFFF
        x = self.read_f32(rdram + PLAYER_POS_X)
        y = self.read_f32(rdram + PLAYER_POS_Y)
        z = self.read_f32(rdram + PLAYER_POS_Z)
        true_z = self.read_f32(rdram + PLAYER_TRUE_ZPOS)
        return (x, y, z, true_z)

    def quit(self):
        try:
            self.proc.stdin.write("QUIT\n")
            self.proc.stdin.flush()
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def _ok(msg):
    print(f"  [OK]   {msg}")


def _fail(msg):
    print(f"  [FAIL] {msg}")


def _info(msg):
    print(f"         {msg}")


def main():
    level_arg = None
    if len(sys.argv) > 1:
        try:
            level_arg = int(sys.argv[1])
        except ValueError:
            print(f"Usage: m64p_probe.py [level_id]")
            return 1

    if not os.path.isfile(ROM_PATH):
        print(f"ROM not found: {ROM_PATH}  (run: make practice -j4)")
        return 1

    if not os.path.isfile("/opt/homebrew/lib/libmupen64plus.dylib"):
        print("mupen64plus not found  (run: brew install mupen64plus)")
        return 1

    if not compile_harness():
        return 1

    syms = parse_map()
    if not syms:
        print(f"Cannot parse {MAP_FILE}  (build first)")
        return 1

    required = ["gGameState", "gPlayState", "gBgmSeqId", "gGameFrameCount",
                "gCurrentLevel", "gPlayer", "gPracticeScreen",
                "gNextLevel", "gNextGameState"]
    missing = [s for s in required if s not in syms]
    if missing:
        print(f"Missing symbols in map: {missing}")
        return 1

    print(f"\nSF64 practice ROM probe")
    print(f"ROM: {ROM_PATH}")
    if level_arg is not None:
        print(f"Target level: {level_arg} ({LEVEL_NAMES.get(level_arg, 'UNKNOWN')})")
    print()

    problems = []
    harness = None
    try:
        harness = Harness()

        # --- Wait for level select (GSTATE_MAP=4; game starts at GSTATE_BOOT=100) ---
        print("Waiting for level select...")
        ok = harness.wait(syms["gGameState"], GSTATE_MAP, timeout_ms=60_000)
        if not ok:
            actual = harness.read_s32(syms["gGameState"])
            _fail(f"Timed out waiting for GSTATE_MAP — gGameState={actual} (ROM may have crashed)")
            problems.append("boot crash")
        else:
            _ok("Level select reached (GSTATE_MAP)")

        if level_arg is not None and not problems:
            # Replicate what Practice_LaunchLevel does:
            #   gNextLevel (u16, high16 of word at gNextLevel addr)
            #   gNextGameState (u16, low16 of same word)  <- both in one WRITE32
            #   gPracticeScreen = PSCREEN_GAMEPLAY
            # gNextLevel and gNextGameState are adjacent u16s at the same 4-byte-aligned word.
            word = ((level_arg & 0xFFFF) << 16) | (GSTATE_PLAY & 0xFFFF)
            harness.write32(syms["gNextLevel"] & ~3, word)
            harness.write32(syms["gPracticeScreen"], PSCREEN_GAMEPLAY)

            print(f"\nNavigating to level {level_arg} ({LEVEL_NAMES.get(level_arg, '?')})...")
            ok = harness.wait(syms["gGameState"], GSTATE_PLAY, timeout_ms=30_000)
            if not ok:
                actual = harness.read_s32(syms["gGameState"])
                _fail(f"Timed out waiting for GSTATE_PLAY — gGameState={actual}")
                problems.append("level launch timeout")
            else:
                ok2 = harness.wait(syms["gPlayState"], PLAY_UPDATE, timeout_ms=15_000)
                if not ok2:
                    actual = harness.read_s32(syms["gPlayState"])
                    _fail(f"Timed out waiting for PLAY_UPDATE — gPlayState={actual}")
                    problems.append("play_update timeout")
                else:
                    _ok("Gameplay active (GSTATE_PLAY + PLAY_UPDATE)")

        if level_arg is not None and not problems:
            # Let the level run for 5 seconds
            print("\nRunning for 300 frames...")
            frame_before = harness.read_s32(syms["gGameFrameCount"])
            harness.advance(300)
            frame_after = harness.read_s32(syms["gGameFrameCount"])

            print("\nProbe results:")
            print(f"  {'SYMBOL':<20} {'VALUE':<12} STATUS")
            print(f"  {'-'*20} {'-'*12} ------")

            # Frame delta (crash check)
            delta = frame_after - frame_before
            label = f"gGameFrameCount +{delta}"
            if delta >= 200:
                _ok(f"Frame delta: +{delta}  (game is running)")
            elif delta > 0:
                _fail(f"Frame delta: +{delta}  (low — possible slowdown)")
                problems.append(f"low frame delta {delta}")
            else:
                _fail(f"Frame delta: +{delta}  (FROZEN — game crashed)")
                problems.append("game frozen")

            # Level
            level_id = harness.read_s32(syms["gCurrentLevel"])
            level_name = LEVEL_NAMES.get(level_id, f"UNKNOWN_{level_id}")
            if level_arg is None or level_id == level_arg:
                _ok(f"gCurrentLevel: {level_id} ({level_name})")
            else:
                _fail(f"gCurrentLevel: {level_id} ({level_name})  — expected {level_arg}")
                problems.append(f"wrong level {level_id}")

            # BGM — gBgmSeqId stores seqId | SEQ_FLAG (0x8000) for looping BGMs
            bgm_raw = harness.read_u16_at(syms["gBgmSeqId"])
            bgm_base = bgm_raw & ~SEQ_FLAG
            bgm_loop = bool(bgm_raw & SEQ_FLAG)
            bgm_name = BGM_NAMES.get(bgm_base, f"SEQ_0x{bgm_base:04X}")
            bgm_display = f"0x{bgm_raw:04X} ({bgm_name}{'+loop' if bgm_loop else ''})"
            if bgm_base >= 2:
                _ok(f"gBgmSeqId: {bgm_display}")
            else:
                _fail(f"gBgmSeqId: {bgm_display}  — music not playing!")
                problems.append(f"silent music (bgm=0x{bgm_raw:04X})")

            # Player position
            pos = harness.deref_player_pos(syms["gPlayer"])
            if pos is None:
                _info("gPlayer: NULL (not in PLAY_UPDATE yet)")
            else:
                x, y, z, true_z = pos
                _ok(f"gPlayer pos: x={x:.1f}  y={y:.1f}  z={z:.1f}  trueZ={true_z:.1f}")

        elif level_arg is None and not problems:
            print("\nProbe results (level select only):")
            game_state = harness.read_s32(syms["gGameState"])
            frame = harness.read_s32(syms["gGameFrameCount"])
            _ok(f"gGameState: {game_state}")
            _ok(f"gGameFrameCount: {frame}")
            _info("Pass --level N to also check gameplay, music, and position")

    finally:
        if harness:
            harness.quit()

    print()
    if problems:
        print(f"RESULT: UNHEALTHY — {len(problems)} problem(s)")
        for p in problems:
            print(f"  - {p}")
        return 1
    else:
        print("RESULT: HEALTHY")
        return 0


if __name__ == "__main__":
    sys.exit(main())
