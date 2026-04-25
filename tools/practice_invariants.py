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
        (FOX_DISPLAY, "Practice_Hitbox_Draw", "Practice_Hitbox_Draw() must be called from fox_display.c"),
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

def main():
    check_config_inits()
    check_function_definitions()
    check_source_in_build()
    check_engine_hooks()
    check_cutscene_skip_hook()

    if errors:
        print("Practice ROM invariant check FAILED:")
        for e in errors:
            print(f"  - {e}")
        return 1

    print("Practice ROM invariant checks passed.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
