#!/usr/bin/env python3
"""Extract symbol addresses from the linker map file and write a Lua table.

Usage: python3 tools/extract_symbols.py > tests/symbols.lua

Also supported (empty worktrees with a bogus build/ symlink):
  PRACTICE_LINK_MAP=/path/to/full/starfox64.us.rev1.map python3 tools/extract_symbols.py ...

Practice-only BSS symbols listed in SYMBOLS sometimes need hand-merging after
changing practice_save bss layout — keep tests/symbols.lua in sync once the
practice ROM links and re-run this script.

The output is consumed by BizHawk Lua test scripts so they can reference
game variables by name instead of hardcoded addresses.
"""
import os
import re
import sys

# Default layout; overridden by PRACTICE_LINK_MAP env (worktrees may symlink empty build/).
MAP_FILE = os.environ.get("PRACTICE_LINK_MAP", "build/starfox64.us.rev1.map")

SYMBOLS = [
    "gGameState",
    "gPlayState",
    "gCurrentLevel",
    "gPlayer",
    "gCsWasNotSkipped",
    "gPracticeScreen",
    "gPracticeConfig",
    "gPracticeMenuState",
    "gHitCount",
    "gPracticeDirectHits",
    "gPracticeDespawns",
    "gActors",
    "gBosses",
    "gScenery",
    "gItems",
    "gChargeTimers",
    "gLevelPhase",
    "gPathProgress",
    "gSavedPathProgress",
    "gPracticeCheckpointProgress",
    "gIodevActive",
    # Phase 4 — slot-manager-backed save state diagnostics. Defined in
    # the Wave 1 skeleton block in practice_save.c (Wave 2.2 takes ownership).
    "gPracticeActiveSlot",
    "gPracticeSlotValidBits",
    "gPracticeLastSaveResult",
    "gPracticeLastLoadResult",
    "gPracticeSaveDisabled",
    # Boss-test feature symbols.
    "sFightCarrier",
    "gPracticeForceCarrier",
    # Phase 5 — cross-scene state machine + frame counter for the timeout test.
    "gPracticeCrossLoadState",
    "gPracticeCrossLoadSlot",
    "gPracticeCrossLoadStartFrame",
    # Deferred BGM rescue (same-spec level launch fix).
    "gPracticeBgmPending",
    "gPracticeBgmPendingSeqId",
    "gPracticeBgmPendingDelay",
    "gPracticeSlotMeta",
    "gControllerHold",
    "gControllerPress",
    "gGameFrameCount",
    "gNextLevel",
    "gNextLevelPhase",
    "gNextGameState",
    # Phase 4 — overlay segment ranges. These are linker-defined symbols
    # in starfox64.ld; the existing regex matches them when present and the
    # WARNING branch handles absence gracefully (some builds may not export
    # all six). Tests only consume them when they appear.
    "ovl_i1_VRAM",
    "ovl_i1_VRAM_END",
    "ovl_i2_VRAM",
    "ovl_i2_VRAM_END",
    "ovl_i3_VRAM",
    "ovl_i3_VRAM_END",
    "ovl_i4_VRAM",
    "ovl_i4_VRAM_END",
    "ovl_i5_VRAM",
    "ovl_i5_VRAM_END",
    "ovl_i6_VRAM",
    "ovl_i6_VRAM_END",
]

# PracticeConfig field offsets (bool = int = 4 bytes, s32 = 4, u8 = 1)
# Struct layout with IDO alignment rules:
#   0x00: LaserStrength laserStrength (s32, 4)
#   0x04: s32 bombCount (4)
#   0x08: s32 lifeCount (4)
#   0x0C: u8 rightWingState (1)
#   0x0D: u8 leftWingState (1)
#   0x0E: 2 bytes padding (align to 4 for bool/int)
#   0x10: bool falcoAlive (4)
#   0x14: bool slippyAlive (4)
#   0x18: bool peppyAlive (4)
#   0x1C: bool showInputDisplay (4)
#   0x20: bool skipCutscenes (4)
#   0x24: bool showHudOverlay (4)
#   0x28: bool showLagFrames (4)
#   0x2C: bool showSpeed (4)
#   0x30: bool showChargeTiming (4)
#   0x34: bool showMissedInputs (4)
#   0x38: bool showHitTracking (4)
#   0x3C: bool showHitboxes (4)
#   0x40: bool showHitboxActors (4)
#   0x44: bool showHitboxScenery (4)
#   0x48: bool showHitboxItems (4)
#   0x4C: bool showHitboxPlayer (4)
#   0x50: bool showHitboxFlash (4)
CONFIG_OFFSETS = {
    "laserStrength":    0x00,
    "bombCount":        0x04,
    "lifeCount":        0x08,
    "goldRingCount":    0x0C,
    "rightWingState":   0x10,
    "leftWingState":    0x11,
    "falcoAlive":       0x14,
    "slippyAlive":      0x18,
    "peppyAlive":       0x1C,
    "showInputDisplay": 0x20,
    "skipCutscenes":    0x24,
    "showHudOverlay":   0x28,
    "showLagFrames":    0x2C,
    "showSpeed":        0x30,
    "showChargeTiming": 0x34,
    "showMissedInputs": 0x38,
    "showHitTracking":  0x3C,
    "showHitboxes":     0x40,
    "showHitboxActors": 0x44,
    "showHitboxScenery":0x48,
    "showHitboxItems":  0x4C,
    "showHitboxPlayer": 0x50,
    "showHitboxFlash":  0x54,
    "showSpawnZones":   0x58,
    "showSpawnActors":  0x5C,
    "showSpawnItems":   0x60,
    "showSpawnScenery": 0x64,
    "expertMode":       0x68,
    "longHealth":       0x6C,
    "showPauseMinimap": 0x70,
    "showChargeShotMeter": 0x74,
    "autoFireChargeShot": 0x78,
    "infHealth": 0x7C,
    "infBombs": 0x80,
    "infLives": 0x84,
    "infBoost": 0x88,
    "prevPlanetsMask": 0x8C,
    "macroBindState":        0x90,
    "macroLoop":             0x94,
    "showEnemyHealth":       0x98,
    "enemyHealthSort":       0x9C,
    "enemyHealthMinHp":      0xA0,
    "enemyHealthBossOnly":   0xA4,
    "enemyHealthHideModels": 0xA8,
    "hitCount":              0xAC,
}

# Player struct field offsets (include/sf64player.h Player)
PLAYER_OFFSETS = {
    "pos_x":   0x074,
    "pos_y":   0x078,
    "pos_z":   0x07C,
    "state":   0x1C8,
}

# Actor fields (first gActors[], include/sf64object.h Actor)
ACTOR_OFFSETS = {
    "state": 0x0B8,
}

# Boss struct offsets (include/sf64object.h Boss).
# Boss.obj is at offset 0; Object.id is u16 at offset 0x02.
BOSS_OFFSETS = {
    "obj_id": 0x02,  # u16
}
BOSS_SIZEOF = 0x408  # explicit comment in include/sf64object.h

# Enum constants needed by tests
CONSTANTS = {
    "SAVE_OK":                  0,
    "SAVE_ERR_SLOT":           -2,
    "GSTATE_PLAY":              7,
    "GSTATE_MAP":               4,
    "PLAY_STANDBY":             0,
    "PLAY_INIT":                1,
    "PLAY_UPDATE":              2,
    "PLAYERSTATE_STANDBY":      0,
    "PLAYERSTATE_INIT":         1,
    "PLAYERSTATE_LEVEL_INTRO":  2,
    "PLAYERSTATE_ACTIVE":       3,
    "PSCREEN_LEVEL_SELECT":     0,
    "PSCREEN_GAMEPLAY":         1,
    "PMENU_CLOSED":             0,
    "PMENU_OPEN":               1,
    "PMENU_OPEN_FROZEN":        2,
    "LEVEL_CORNERIA":           0,
    "LEVEL_METEO":              1,
    "LEVEL_SECTOR_Y":           2,
    "LEVEL_FORTUNA":            3,
    "LEVEL_KATINA":             4,
    "LEVEL_AQUAS":              13,
    "LEVEL_SECTOR_X":           2,
    "LEVEL_SOLAR":              7,
    "LEVEL_ZONESS":             8,
    "LEVEL_TITANIA":            12,
    "LEVEL_AREA_6":             3,
    "LEVEL_VENOM_1":            6,
    "LEVEL_VENOM_ANDROSS":      9,
}

def kseg0_to_rdram(addr):
    """Convert MIPS KSEG0 address (0x80XXXXXX) to RDRAM offset (0x00XXXXXX)."""
    return addr & 0x1FFFFFFF

def main():
    with open(MAP_FILE) as f:
        map_text = f.read()

    found = {}
    for sym in SYMBOLS:
        match = re.search(rf"0x([0-9a-fA-F]+)\s+{sym}\b", map_text)
        if match:
            found[sym] = int(match.group(1), 16)
        else:
            print(f"-- WARNING: symbol '{sym}' not found in map file", file=sys.stderr)

    print("-- Auto-generated by tools/extract_symbols.py")
    print("-- Do not edit manually. Regenerate after each build.")
    print("")
    print("local S = {}")
    print("")

    print("-- Global variable RDRAM addresses")
    for sym in SYMBOLS:
        if sym in found:
            rdram = kseg0_to_rdram(found[sym])
            print(f"S.{sym} = 0x{rdram:08X}")
    print("")

    print("-- PracticeConfig field offsets")
    print("S.config = {}")
    for field, off in sorted(CONFIG_OFFSETS.items(), key=lambda x: x[1]):
        print(f"S.config.{field} = 0x{off:02X}")
    print("")

    print("-- Player struct field offsets")
    print("S.player = {}")
    for field, off in sorted(PLAYER_OFFSETS.items(), key=lambda x: x[1]):
        print(f"S.player.{field} = 0x{off:03X}")
    print("")

    print("-- Actor (gActors[0]) field offsets")
    print("S.actor = {}")
    for field, off in sorted(ACTOR_OFFSETS.items(), key=lambda x: x[1]):
        print(f"S.actor.{field} = 0x{off:03X}")
    print("")

    print("-- Boss struct field offsets (include/sf64object.h Boss)")
    print("S.boss = {}")
    for field, off in sorted(BOSS_OFFSETS.items(), key=lambda x: x[1]):
        print(f"S.boss.{field} = 0x{off:03X}")
    print(f"S.boss.sizeof = 0x{BOSS_SIZEOF:03X}")
    print("")

    print("-- Enum constants")
    print("S.const = {}")
    for name, val in sorted(CONSTANTS.items()):
        print(f"S.const.{name} = {val}")
    print("")

    print("return S")

if __name__ == "__main__":
    main()
