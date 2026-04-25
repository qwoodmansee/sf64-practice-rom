#!/usr/bin/env python3
"""Inject practice ROM object files into the generated linker script.

Run after `make extract` which regenerates starfox64.ld from splat.
This is called automatically by the Makefile.
"""
import sys

LINKER_SCRIPT = "linker_scripts/us/rev1/starfox64.ld"

PRACTICE_OBJS = [
    "practice_main",
    "practice_draw",
    "practice_input",
    "practice_level",
    "practice_state",
    "practice_menu",
    "practice_save",
    "practice_input_display",
    "practice_hud",
    "practice_hitbox",
    "practice_freecam",
]

ANCHOR = "build/src/engine/fox_save.o"

def patch():
    with open(LINKER_SCRIPT, "r") as f:
        content = f.read()

    if "practice_main" in content:
        print("Linker script already patched, skipping.")
        return

    for section in [".text", ".data", ".rodata", ".bss"]:
        anchor_line = f"{ANCHOR}({section});"
        injection = "\n".join(
            f"        build/src/practice/{obj}.o({section});"
            for obj in PRACTICE_OBJS
        )
        content = content.replace(
            f"        {anchor_line}",
            f"        {anchor_line}\n{injection}",
        )

    with open(LINKER_SCRIPT, "w") as f:
        f.write(content)

    print(f"Patched {LINKER_SCRIPT} with practice ROM entries.")

if __name__ == "__main__":
    patch()
