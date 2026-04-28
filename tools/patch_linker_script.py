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

# lib/* objects (not under lib/iodev/). Anchored on the last lib/iodev
# entry; each subsequent entry anchors on the previous LIB_TOP entry.
LIB_TOP_OBJS = [
    "sd_crc",       # Phase 1b: SD-spec CRC layer (host-portable)
]

# lib/fatfs/* objects. Anchored on the last LIB_TOP entry; each subsequent
# entry anchors on the previous LIB_FATFS entry. ff_libc supplies memset/
# memcmp shims for FatFs (the project's libultra doesn't expose them).
LIB_FATFS_OBJS = [
    "ff",            # Phase 2: FatFs core
    "ffunicode",     # Phase 2: FatFs Unicode tables (mostly empty for cp437)
    "ff_libc",       # Phase 2: memset/memcmp shims for FatFs
    "diskio",        # Phase 2: FatFs<->iodev glue
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
        # Fully unpatched: inject practice + lib/iodev + lib-top + lib/fatfs after fox_save.o.
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{ANCHOR}({section});"
            practice_block = "\n".join(
                f"        build/src/practice/{obj}.o({section});"
                for obj in PRACTICE_OBJS
            )
            iodev_block = "\n".join(
                f"        build/lib/iodev/{obj}.o({section});"
                for obj in LIB_IODEV_OBJS
            )
            top_block = "\n".join(
                f"        build/lib/{obj}.o({section});"
                for obj in LIB_TOP_OBJS
            )
            fatfs_block = "\n".join(
                f"        build/lib/fatfs/{obj}.o({section});"
                for obj in LIB_FATFS_OBJS
            )
            blocks = [practice_block, iodev_block]
            if top_block:
                blocks.append(top_block)
            if fatfs_block:
                blocks.append(fatfs_block)
            injection = "\n".join(blocks)
            content = _replace_after_anchor(content, anchor_line, injection)
        with open(LINKER_SCRIPT, "w") as f:
            f.write(content)
        print(f"Patched {LINKER_SCRIPT}: practice + lib/iodev + lib-top + lib/fatfs (full).")
        return

    # Practice already patched. Walk LIB_IODEV_OBJS first, then LIB_TOP_OBJS,
    # and inject any missing entries. Each entry anchors on its predecessor
    # in injection order: lib/iodev follows the last practice obj, lib-top
    # follows the last lib/iodev obj.
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

    last_iodev_obj = LIB_IODEV_OBJS[-1]
    for i, obj in enumerate(LIB_TOP_OBJS):
        if f"build/lib/{obj}.o" in content:
            continue
        if i == 0:
            predecessor = f"build/lib/iodev/{last_iodev_obj}"
        else:
            predecessor = f"build/lib/{LIB_TOP_OBJS[i - 1]}"
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{predecessor}.o({section});"
            injection = f"        build/lib/{obj}.o({section});"
            content = _replace_after_anchor(content, anchor_line, injection)
        inject_count += 1

    # lib/fatfs/* — anchored on the last LIB_TOP entry (or the last lib/iodev
    # entry if LIB_TOP_OBJS is empty). Each subsequent entry anchors on the
    # previous LIB_FATFS entry.
    if LIB_TOP_OBJS:
        last_top_predecessor = f"build/lib/{LIB_TOP_OBJS[-1]}"
    else:
        last_top_predecessor = f"build/lib/iodev/{last_iodev_obj}"
    for i, obj in enumerate(LIB_FATFS_OBJS):
        if f"build/lib/fatfs/{obj}.o" in content:
            continue
        if i == 0:
            predecessor = last_top_predecessor
        else:
            predecessor = f"build/lib/fatfs/{LIB_FATFS_OBJS[i - 1]}"
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{predecessor}.o({section});"
            injection = f"        build/lib/fatfs/{obj}.o({section});"
            content = _replace_after_anchor(content, anchor_line, injection)
        inject_count += 1

    if inject_count == 0:
        print("Linker script already fully patched, skipping.")
        return

    with open(LINKER_SCRIPT, "w") as f:
        f.write(content)
    print(f"Patched {LINKER_SCRIPT}: injected {inject_count} lib entries.")


if __name__ == "__main__":
    patch()
