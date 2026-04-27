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

LIB_IODEV_OBJS = [
    "iodev",
    "iodev_sc64",
    "iodev_stub",
]

ANCHOR = "build/src/engine/fox_save.o"

def patch():
    with open(LINKER_SCRIPT, "r") as f:
        content = f.read()

    has_practice = "practice_main" in content
    has_iodev = "iodev.o" in content

    if has_practice and has_iodev:
        print("Linker script already patched (practice + lib/iodev), skipping.")
        return

    for section in [".text", ".data", ".rodata", ".bss"]:
        if not has_practice:
            # Fresh inject: add both practice and iodev after the anchor.
            anchor_line = f"{ANCHOR}({section});"
            injection_practice = "\n".join(
                f"        build/src/practice/{obj}.o({section});"
                for obj in PRACTICE_OBJS
            )
            injection_iodev = "\n".join(
                f"        build/lib/iodev/{obj}.o({section});"
                for obj in LIB_IODEV_OBJS
            )
            injection = injection_practice + "\n" + injection_iodev
            content = content.replace(
                f"        {anchor_line}",
                f"        {anchor_line}\n{injection}",
            )
        else:
            # Incremental: practice already injected; anchor on the last
            # practice_*.o line for this section and append iodev.
            last_practice_obj = PRACTICE_OBJS[-1]
            anchor_line = f"build/src/practice/{last_practice_obj}.o({section});"
            injection_iodev = "\n".join(
                f"        build/lib/iodev/{obj}.o({section});"
                for obj in LIB_IODEV_OBJS
            )
            content = content.replace(
                f"        {anchor_line}",
                f"        {anchor_line}\n{injection_iodev}",
            )

    with open(LINKER_SCRIPT, "w") as f:
        f.write(content)

    print(f"Patched {LINKER_SCRIPT} with lib/iodev entries.")

if __name__ == "__main__":
    patch()
