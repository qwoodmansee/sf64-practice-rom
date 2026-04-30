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
    "practice_save_slotpool",  # Pak-only BSS; must follow practice_save for patch anchors
    "practice_heap_audit",  # Phase 4 Wave 2.3: IS-Viewer heap audit
    "practice_overlay",     # Phase 4: LevelId -> ovl_iN region map (Wave 1: stubs)
    "practice_input_display",
    "practice_hud",
    "practice_hitbox",
    "practice_minimap",
    "practice_freecam",
    "practice_logo_tex",
    "practice_slot_test",  # Phase 3: in-ROM slot_manager fake-state smoke test
    "practice_test_fatfs",  # Phase 2: gated by IODEV_DIAG_FATFS, otherwise empty .o
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
    "serial",       # Phase 3: TLV codec (host-portable)
    "slot_manager", # Phase 3: RAM slot manager (host-portable)
    "crc32",        # Phase 4: CRC32-IEEE for overlay build IDs (host-portable)
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

# Phase 3: Expansion Pak slot pool at 0x80400000 (above 4 MB stock limit).
# Inserted after dma_table_VRAM_END so practice_save.o(.bss) does not
# clobber gDmaTable during BSS zero-init. VMA 0x80400000 is Expansion Pak DRAM;
# on stock 4 MB save/load stays disabled — practice_save globals remain in low RAM,
# slotpool BSS is never referenced past slot_manager_init.
PRACTICE_POOL_SECTION = """\
    /* Phase 3: Expansion Pak slot pool at 0x80400000 (above stock 4 MB).
     * practice_save_slotpool.o(.bss): 4 * 256 KB; unreachable without Pak.
     * practice_save.o BSS (gPracticeSaveDisabled, etc.) remains in .main_bss. */
    .practice_pool_pak 0x80400000 (NOLOAD) : SUBALIGN(8)
    {
        practice_pool_pak_BSS_START = .;
        build/src/practice/practice_save_slotpool.o(.bss);
        . = ALIGN(., 8);
        practice_pool_pak_BSS_END = .;
    }
"""


def migrate_legacy_practice_pool_pak(content):
    """Older scripts parked all of practice_save.o(.bss) at 0x80400000.

    That forced gPracticeSaveDisabled into Expansion Pak DRAM — fatal on stock
    4 MB. Migrate to practice_save_slotpool-only in .practice_pool_pak and
    restore practice_save.o(.bss) inside .main_bss."""

    OLD_MAIN_COMMENT = (
        "        /* practice_save BSS is in .practice_pool_pak "
        "(0x80400000, Pak only). */\n"
    )
    OLD_POOL_LINE = "        build/src/practice/practice_save.o(.bss);\n"
    NEW_POOL_LINE = "        build/src/practice/practice_save_slotpool.o(.bss);\n"

    anchor = ".practice_pool_pak 0x80400000"
    block_start = content.find(anchor)
    if block_start >= 0:
        snippet = content[block_start:block_start + 600]
        if OLD_POOL_LINE in snippet:
            snippet2 = snippet.replace(OLD_POOL_LINE, NEW_POOL_LINE, 1)
            content = content[:block_start] + snippet2 + content[block_start + len(snippet) :]

    if OLD_MAIN_COMMENT in content:
        content = content.replace(
            OLD_MAIN_COMMENT,
            "        build/src/practice/practice_save.o(.bss);\n",
            1,
        )

    return content


def strip_slotpool_bss_from_main_if_pooled(content):
    """When .practice_pool_pak exists, only one practice_save_slotpool.o(.bss)."""
    line = "        build/src/practice/practice_save_slotpool.o(.bss);\n"
    cmt = (
        "        /* practice_save_slotpool BSS is in .practice_pool_pak "
        "(0x80400000, Pak only). */\n"
    )
    if ".practice_pool_pak" not in content:
        return content
    if content.count(line) < 2:
        return content
    return content.replace(line, cmt, 1)


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
        initial_content = f.read()

    content = migrate_legacy_practice_pool_pak(initial_content)

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

    # Practice already patched. Walk PRACTICE_OBJS first, then LIB_IODEV_OBJS,
    # then LIB_TOP_OBJS, then LIB_FATFS_OBJS, injecting any missing entries.
    # Each entry anchors on its predecessor in injection order.
    inject_count = 0

    # Pass 0: missing practice objs anchor on the previous PRACTICE_OBJS entry
    # (the first entry is always present from the initial full-patch).
    for i, obj in enumerate(PRACTICE_OBJS):
        if f"build/src/practice/{obj}.o(.text)" in content:
            continue
        if i == 0:
            # Should never happen -- practice_main was injected during the
            # initial full-patch. If we hit this, the linker script is in
            # an unexpected state.
            raise RuntimeError(
                "First PRACTICE_OBJS entry missing from a partially-patched "
                "linker script. This shouldn't happen."
            )
        predecessor = f"build/src/practice/{PRACTICE_OBJS[i - 1]}"
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{predecessor}.o({section});"
            injection = f"        build/src/practice/{obj}.o({section});"
            content = _replace_after_anchor(content, anchor_line, injection)
        inject_count += 1

    content = strip_slotpool_bss_from_main_if_pooled(content)

    # last_practice_obj: anchor for the first lib/iodev entry. Because Pass 0
    # has now ensured all PRACTICE_OBJS entries are present, the last one is
    # safe to reference.
    last_practice_obj = PRACTICE_OBJS[-1]

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

    # Inject .practice_pool_pak after dma_table_VRAM_END if not present.
    # practice_save_slotpool.o (.bss): Pak-only blob at VMA 0x80400000; strip that
    # line from stock .main_bss (practice_save.o BSS stays below .dma_table).
    if ".practice_pool_pak" not in content:
        # Also remove the old .practice_pool section if it exists (migration).
        if ".practice_pool" in content and ".practice_pool_pak" not in content:
            # Replace the old section header with the new one via PRACTICE_POOL_SECTION.
            old_pool_re_pat = (
                r"    /\* practice_save\.o BSS lives here.*?\}\n?"
            )
            import re as _re
            content = _re.sub(old_pool_re_pat, "", content, count=1, flags=_re.DOTALL)

        anchor_line = "    dma_table_VRAM_END = .;"
        needle = anchor_line + "\n"
        replacement = anchor_line + "\n\n" + PRACTICE_POOL_SECTION
        new_content = content.replace(needle, replacement, 1)
        if new_content == content:
            raise RuntimeError(
                "Linker patcher: dma_table_VRAM_END anchor not found for "
                ".practice_pool_pak injection."
            )
        # Strip practice_save_slotpool.o BSS from stock .main_bss — only the
        # megabyte blob is parked at 0x80400000 (practice_save.o BSS stays below).
        save_bss = "        build/src/practice/practice_save_slotpool.o(.bss);\n"
        save_comment = (
            "        /* practice_save_slotpool BSS is in .practice_pool_pak "
            "(0x80400000, Pak only). */\n"
        )
        if save_bss in new_content:
            new_content = new_content.replace(save_bss, save_comment, 1)
        content = new_content
        inject_count += 1

    dirty = (content != initial_content) or inject_count != 0
    if not dirty:
        print("Linker script already fully patched, skipping.")
        return

    with open(LINKER_SCRIPT, "w") as f:
        f.write(content)

    suffix = ""
    if inject_count:
        suffix = f" injected {inject_count} lib/script entries."
    elif content != initial_content:
        suffix = " (migration)."
    print(f"Patched {LINKER_SCRIPT}{suffix}")

if __name__ == "__main__":
    patch()
