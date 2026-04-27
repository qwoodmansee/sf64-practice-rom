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


def _replace_after_anchor(content, anchor_line, injection):
    """Append `injection` immediately after `anchor_line` in `content`.

    Both `anchor_line` and `injection` are already-formatted snippets without
    the 8-space linker-script indent; we add it here. Asserts the anchor
    matches exactly once -- otherwise a silent no-op leaves the script
    half-patched and produces a mysterious link error downstream.
    """
    needle = f"        {anchor_line}"
    repl = f"        {anchor_line}\n{injection}"
    new_content = content.replace(needle, repl)
    if new_content == content:
        raise RuntimeError(
            f"Linker patcher anchor not found: {needle!r}. "
            f"Has PRACTICE_OBJS or the linker-script structure changed?"
        )
    return new_content


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
            content = _replace_after_anchor(content, anchor_line, injection)
        else:
            # Incremental: practice already injected; anchor on the last
            # practice_*.o line for this section and append iodev.
            last_practice_obj = PRACTICE_OBJS[-1]
            anchor_line = f"build/src/practice/{last_practice_obj}.o({section});"
            injection_iodev = "\n".join(
                f"        build/lib/iodev/{obj}.o({section});"
                for obj in LIB_IODEV_OBJS
            )
            content = _replace_after_anchor(content, anchor_line, injection_iodev)

    with open(LINKER_SCRIPT, "w") as f:
        f.write(content)

    print(f"Patched {LINKER_SCRIPT} with lib/iodev entries.")

if __name__ == "__main__":
    patch()
