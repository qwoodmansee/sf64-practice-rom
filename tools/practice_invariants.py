#!/usr/bin/env python3
"""Static invariant checks for the practice ROM.

Catches regressions that compile fine but break features at runtime.
Run as part of pre-commit hook.
"""
import os
import re
import sys

SRC_PRACTICE = "src/practice"
INCLUDE_PRACTICE = "include/practice.h"
PATCH_SCRIPT = "tools/patch_linker_script.py"
FOX_GAME = "src/engine/fox_game.c"
FOX_PLAY = "src/engine/fox_play.c"
FOX_DISPLAY = "src/engine/fox_display.c"
MAKEFILE = "Makefile"
PATCHER_PACKAGE = "tools/patcher/package.json"
PATCHER_CREATE_RELEASE = "tools/patcher/src/create-release.ts"

errors = []

def error(msg):
    errors.append(msg)

def read(path):
    with open(path) as f:
        return f.read()

def check_config_inits():
    """Every PracticeConfig field must have a default in Practice_Init."""
    header = read(INCLUDE_PRACTICE)
    main = read(os.path.join(SRC_PRACTICE, "practice_main.c"))

    struct_match = re.search(
        r"typedef struct PracticeConfig\s*\{(.*?)\}\s*PracticeConfig;",
        header, re.DOTALL
    )
    if not struct_match:
        error("Could not find PracticeConfig struct in practice.h")
        return

    fields = re.findall(r"\b(\w+)\s*;", struct_match.group(1))
    for field in fields:
        pattern = rf"gPracticeConfig\.{field}\s*="
        if not re.search(pattern, main):
            error(f"PracticeConfig.{field} declared but not initialized in Practice_Init")

def check_function_definitions():
    """Every function declared in practice.h must be defined in src/practice/."""
    header = read(INCLUDE_PRACTICE)
    decls = re.findall(r"^\w[\w\s\*]*\s+(Practice_\w+)\s*\(", header, re.MULTILINE)

    all_practice_src = ""
    for fname in os.listdir(SRC_PRACTICE):
        if fname.endswith(".c"):
            all_practice_src += read(os.path.join(SRC_PRACTICE, fname))

    for func in decls:
        pattern = rf"^[\w\s\*]+\b{func}\s*\("
        if not re.search(pattern, all_practice_src, re.MULTILINE):
            error(f"{func}() declared in practice.h but not defined in src/practice/")

def check_source_in_build():
    """Every .c file in src/practice/ must be in PRACTICE_OBJS."""
    patch_script = read(PATCH_SCRIPT)

    for fname in os.listdir(SRC_PRACTICE):
        if not fname.endswith(".c"):
            continue
        obj_name = fname.replace(".c", "")
        if f'"{obj_name}"' not in patch_script:
            error(f"{fname} exists in src/practice/ but '{obj_name}' not in PRACTICE_OBJS (patch_linker_script.py)")

def check_engine_hooks():
    """Critical engine hooks must exist inside PRACTICE_ROM guards."""
    hooks = [
        (FOX_GAME, "Practice_Init", "Practice_Init() must be called from fox_game.c"),
        (FOX_GAME, "Practice_Update", "Practice_Update() must be called from fox_game.c"),
        (FOX_GAME, "Practice_Draw", "Practice_Draw() must be called from fox_game.c"),
        (FOX_PLAY, "Practice_ApplyStartConditions", "Practice_ApplyStartConditions() must be called from fox_play.c"),
        (FOX_PLAY, "gPracticeCheckpointProgress", "gPracticeCheckpointProgress checkpoint hook must exist in fox_play.c Player_Setup"),
        (FOX_DISPLAY, "Practice_Hitbox_Draw", "Practice_Hitbox_Draw() must be called from fox_display.c"),
        (FOX_DISPLAY, "Practice_FreeCam_IsActive", "Practice_FreeCam_IsActive() hook must exist in fox_display.c"),
        (FOX_DISPLAY, "Practice_FreeCam_GetView", "Practice_FreeCam_GetView() hook must exist in fox_display.c"),
    ]
    for filepath, symbol, msg in hooks:
        src = read(filepath)
        if symbol not in src:
            error(msg)

def check_cutscene_skip_hook():
    """gCsWasNotSkipped must be set to false in Game_SetGameState's GSTATE_PLAY case.

    This hook runs after Play_Setup() resets gCsWasNotSkipped=true but before
    Play_Init() reads it 3 frames later. Without it, intro cutscenes play even
    when skipCutscenes is enabled.
    """
    src = read(FOX_GAME)
    set_game_state = re.search(
        r"void\s+Game_SetGameState\b.*?^}",
        src, re.DOTALL | re.MULTILINE
    )
    if not set_game_state:
        error("Could not find Game_SetGameState in fox_game.c")
        return

    body = set_game_state.group(0)
    if "gCsWasNotSkipped" not in body or "skipCutscenes" not in body:
        error(
            "Game_SetGameState must set gCsWasNotSkipped=false when skipCutscenes is on. "
            "Without this, Play_Setup() resets it to true and intro cutscenes play."
        )

def check_release_patch_workflow():
    """Release patch workflow must build a compressed practice ROM and never ship ROM bytes."""
    makefile = read(MAKEFILE)
    if "practice-compressed:" not in makefile:
        error("Makefile must define practice-compressed target for release builds")
    if "practice-patch:" not in makefile:
        error("Makefile must define practice-patch target for BPS release generation")
    if "npm --prefix tools/patcher run create-release" not in makefile:
        error("practice-patch target must call tools/patcher create-release script")

    package = read(PATCHER_PACKAGE)
    if '"sf64-practice-patcher"' not in package or '"bin"' not in package:
        error("tools/patcher/package.json must expose the sf64-practice-patcher npx binary")
    if '"*.z64"' in package or '"*.n64"' in package or '"*.v64"' in package:
        error("tools/patcher/package.json must not include ROM file globs")

    create_release = read(PATCHER_CREATE_RELEASE)
    if "baserom.us.rev1.z64" not in create_release or "build/starfox64.us.rev1.z64" not in create_release:
        error("create-release defaults must patch the US rev1 base ROM to the compressed practice ROM")

def check_isviewer_sc64():
    """IS-Viewer 64 must be enabled and use the SC64 protocol, not libdragon's emulator protocol.

    SC64 firmware ignores buffers without the magic token 0x49533634 ("IS64") at offset 0,
    so libdragon's 0x12345678 magic-check silently no-ops on real hardware. Each cart-bus
    write must be followed by a dummy IO_READ to drain the SC64 write FIFO; without it
    back-to-back writes get dropped after the first few.
    """
    mods = read("include/mods.h")
    if not re.search(r"#define\s+MODS_ISVIEWER\s+1\b", mods):
        error("MODS_ISVIEWER must be 1 to enable osSyncPrintf over IS-Viewer 64")

    isv = read("src/mods/isviewer.c")
    if "0x49533634" not in isv:
        error("isviewer.c must use SC64 token 0x49533634 ('IS64') at base+0; libdragon's 0x12345678 is silently dropped on hardware")
    if "PI_WRITE" not in isv:
        error("isviewer.c must use a PI_WRITE macro that flushes via IO_READ; back-to-back writes to SC64 cart space get dropped")

def check_iodev_sc64():
    """SC64 iodev backend must preserve hard-won SC64 protocol invariants.

    These are exactly the gotchas documented in CLAUDE.md's
    'Hard-won SC64 protocol gotchas' section, applied to iodev rather
    than IS-Viewer. Without these, the channel silently fails on hardware.
    """
    path = "lib/iodev/iodev_sc64.c"
    if not os.path.isfile(path):
        return
    src = read(path)

    # Every cart-bus write needs a follow-up IO_READ to drain the PI bus.
    # Match the macro *definition* (and require it pair IO_WRITE with IO_READ),
    # not just any reference - bare callers would also satisfy a substring check.
    if not re.search(r"#define\s+PI_WRITE_FLUSH\b[^\n]*\\\s*\n[^\n]*IO_WRITE[^\n]*\\\s*\n[^\n]*IO_READ", src):
        error(f"{path}: PI_WRITE_FLUSH macro must be defined and pair IO_WRITE with a draining IO_READ (see CLAUDE.md SC64 gotchas)")

    # The four SD command bytes must remain literal - they're the SC64 wire protocol.
    for cmd_byte, name in [("'i'", "SD_CARD_OP"), ("'I'", "SD_SECTOR_SET"), ("'s'", "SD_READ"), ("'S'", "SD_WRITE")]:
        if cmd_byte not in src:
            error(f"{path}: SC64 command byte {cmd_byte} ({name}) missing - wire protocol broken")

    # The 128-sector cap must remain in both read and write - it's the 64 KiB DMA scratch limit.
    # A future "simplify" that drops it would silently corrupt memory past the scratch buffer.
    if src.count("count > 128") < 2:
        error(f"{path}: both sd_read_sectors and sd_write_sectors must enforce count > 128 -> ERR_PARAM (DMA scratch is 64 KiB / 128 sectors)")

def check_spawn_zone_typing():
    """Spawn zone draw loop must classify entries by type and respect per-type toggles."""
    hitbox = read("src/practice/practice_hitbox.c")
    for sym in ("OBJ_ACTOR_START", "OBJ_ACTOR_MAX", "OBJ_BOSS_START", "OBJ_BOSS_MAX",
                "OBJ_ITEM_START", "OBJ_ITEM_MAX", "OBJ_SCENERY_START", "OBJ_SCENERY_MAX"):
        if sym not in hitbox:
            error(f"Hitbox_DrawSpawnZones must use {sym} for type classification")
    for field in ("showSpawnActors", "showSpawnItems", "showSpawnScenery"):
        if field not in hitbox:
            error(f"Hitbox_DrawSpawnZones must respect gPracticeConfig.{field}")

LIB_DIR = "lib"

# Headers/paths that lib/ code must NOT include (it must stay portable).
FORBIDDEN_LIB_INCLUDES = [
    "global.h",
    "practice.h",
    "variables.h",
    # Family patterns checked separately below.
]
FORBIDDEN_LIB_INCLUDE_PATTERNS = [
    r"sf64\w*\.h",   # sf64audio.h, sf64level.h, sf64thread.h, etc.
    r"fox_\w*\.h",   # fox_game.h, fox_play.h, etc.
    r"include/[\w./_-]+",  # any include with explicit include/ path prefix
]

def check_lib_isolation():
    """lib/ code must not include game/decomp headers."""
    if not os.path.isdir(LIB_DIR):
        return  # lib/ doesn't exist yet — nothing to check
    for root, _dirs, files in os.walk(LIB_DIR):
        for fname in files:
            if not fname.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fname)
            src = read(path)
            for inc in FORBIDDEN_LIB_INCLUDES:
                if re.search(rf'#include\s*[<"]{re.escape(inc)}[>"]', src):
                    error(f"{path}: lib/ may not include game header '{inc}'")
            for pat in FORBIDDEN_LIB_INCLUDE_PATTERNS:
                if re.search(rf'#include\s*[<"]{pat}[>"]', src):
                    error(f"{path}: lib/ may not include game header matching /{pat}/")

# Files allowed to include libultra headers (PI/cart-bus access).
LIBULTRA_ALLOWED = [
    "lib/iodev/iodev_sc64.c",
    "lib/iodev/iodev_ed64.c",  # Phase 1b
    "lib/lib_types.h",         # toolchain shim — bridges <stdint.h> ↔ PR/ultratypes.h
]
LIBULTRA_INCLUDE_PATTERNS = [
    r"PR/[\w/]+\.h",
    r"ultra64\.h",
    r"libultra/[\w/]+\.h",
]

def check_lib_libultra_scope():
    """lib/ files outside the iodev backends must build host-portable.

    Forbid libultra includes everywhere except the explicit allowlist,
    so unit tests can build with native gcc.
    """
    if not os.path.isdir(LIB_DIR):
        return
    for root, _dirs, files in os.walk(LIB_DIR):
        for fname in files:
            if not fname.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fname)
            if path in LIBULTRA_ALLOWED:
                continue
            src = read(path)
            for pat in LIBULTRA_INCLUDE_PATTERNS:
                if re.search(rf'#include\s*[<"]{pat}[>"]', src):
                    error(f"{path}: only iodev backends and lib_types.h may include libultra (matched /{pat}/)")

def main():
    check_config_inits()
    check_function_definitions()
    check_source_in_build()
    check_engine_hooks()
    check_cutscene_skip_hook()
    check_isviewer_sc64()
    check_iodev_sc64()
    check_spawn_zone_typing()
    check_release_patch_workflow()
    check_lib_isolation()
    check_lib_libultra_scope()

    if errors:
        print("Practice ROM invariant check FAILED:")
        for e in errors:
            print(f"  - {e}")
        return 1

    print("Practice ROM invariant checks passed.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
