#!/usr/bin/env python3
"""Inject practice ROM object files into the generated linker script.

Run after `make extract` which regenerates starfox64.ld from splat.
This is called automatically by the Makefile.

Algorithm:
  - If the linker script has no practice block at all (fresh extract),
    inject the full practice + lib block in one shot, anchored on
    fox_save.o.
  - Otherwise, walk the expected LIB_IODEV_OBJS list in order. For each
    entry that's missing from the script, inject it anchored on its
    predecessor (the previous entry in the list, or the last practice
    obj if it's the first entry). This is N-state agnostic: appending
    new lib files to the list "just works" without code changes here.
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

# Order matters: each entry's predecessor must precede it in the list,
# because the dynamic patcher anchors each missing entry on the previous
# list element.
LIB_IODEV_OBJS = [
    "iodev",
    "iodev_sc64",
    "iodev_ed64",   # Phase 1b -- must precede iodev_stub
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

    if not has_practice:
        # Fully unpatched: inject the full practice block + lib block after fox_save.o.
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{ANCHOR}({section});"
            practice_block = "\n".join(
                f"        build/src/practice/{obj}.o({section});"
                for obj in PRACTICE_OBJS
            )
            lib_block = "\n".join(
                f"        build/lib/iodev/{obj}.o({section});"
                for obj in LIB_IODEV_OBJS
            )
            injection = practice_block + "\n" + lib_block
            content = _replace_after_anchor(content, anchor_line, injection)
        with open(LINKER_SCRIPT, "w") as f:
            f.write(content)
        print(f"Patched {LINKER_SCRIPT}: practice + lib/iodev (full).")
        return

    # Practice already patched. Walk LIB_IODEV_OBJS and inject any missing
    # entries. The first entry's predecessor is the last practice obj;
    # each subsequent entry anchors on the previous entry in LIB_IODEV_OBJS.
    last_practice_obj = PRACTICE_OBJS[-1]
    inject_count = 0
    for i, obj in enumerate(LIB_IODEV_OBJS):
        if f"build/lib/iodev/{obj}.o" in content:
            continue  # Already present
        if i == 0:
            predecessor = f"build/src/practice/{last_practice_obj}"
        else:
            predecessor = f"build/lib/iodev/{LIB_IODEV_OBJS[i - 1]}"
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{predecessor}.o({section});"
            injection = f"        build/lib/iodev/{obj}.o({section});"
            content = _replace_after_anchor(content, anchor_line, injection)
        inject_count += 1

    if inject_count == 0:
        print("Linker script already fully patched, skipping.")
        return

    with open(LINKER_SCRIPT, "w") as f:
        f.write(content)
    print(f"Patched {LINKER_SCRIPT}: injected {inject_count} lib/iodev entries.")


if __name__ == "__main__":
    patch()
