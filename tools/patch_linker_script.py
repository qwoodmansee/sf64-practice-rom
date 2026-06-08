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
import re
import sys

LINKER_SCRIPT = "linker_scripts/us/rev1/starfox64.ld"

PRACTICE_OBJS = [
    "practice_main",
    "practice_draw",
    "practice_input",
    "practice_level",
    "practice_state",
    "practice_menu",
    "practice_audio",
    "practice_save",
    "practice_save_slotpool",  # Pak-only BSS; must follow practice_save for patch anchors
    "practice_overlay",     # Phase 4: LevelId -> ovl_iN region map (Wave 1: stubs)
    "practice_input_display",
    "practice_hud",
    "practice_charge_shot",
    "practice_cheats",
    "practice_hitbox",
    "practice_minimap",
    "practice_freecam",
    # practice_logo_tex / practice_owl_tex moved to PRACTICE_LATE_CORE_PRACTICE_OBJS
    # (2026-06-07): level-select-only textures were pushing the vanilla audio
    # tables at the top of main past the IPL boot-staging boundary, freezing
    # Macbeth on hardware. See check_boot_main_rom_budget.
    "practice_icon_tex",
    "practice_test_fatfs",  # Phase 2: gated by IODEV_DIAG_FATFS, otherwise empty .o
    "practice_sd",          # Phase 6: OSK + file browser rendering and glue
    "practice_frame_advance",  # Frame advance / pause feature
    "practice_boss_test",   # Boss test stage: data table + launch API
    "practice_macro",       # Macro recording / playback logic
    "practice_macro_buf",   # Macro frame buffer -- Pak-only BSS in .practice_macro_pak
    "practice_macro_snap",  # Macro snapshot buffer -- Pak-only BSS in .practice_macro_snap_pak
    "practice_enemy_health",  # Enemy/boss health HUD overlay
    "late/loader",          # Phase 1: .practice_late_core loader (Practice_Late_Init)
]

# Phase 1: every lib/iodev/* file moved into .practice_late_core via
# PRACTICE_LATE_CORE_IODEV_OBJS below. Kept as an empty list so the
# patcher's chain anchors and fresh-extract injection still know about
# the legacy slot; future objects that need to live in .main can be
# appended back here.
LIB_IODEV_OBJS = []

# Phase 1: every lib/* (not under lib/iodev/) file moved into
# .practice_late_core via PRACTICE_LATE_CORE_LIB_OBJS below. Empty for
# the same reason as LIB_IODEV_OBJS.
# NOTE: slot_manager and crc32 must stay in main ROM (not .practice_late_core)
# because they're called during gameplay and .practice_late_core is not
# accessible from gameplay on real hardware (regression fix from #17).
LIB_TOP_OBJS = [
    "slot_manager",  # Must stay in main ROM; called during gameplay save/load
    "crc32",         # Used by slot_manager and practice code during gameplay
]

# Phase 1: lib/sd_host/sd_host.o moved into .practice_late_core via
# PRACTICE_LATE_CORE_SD_HOST_OBJS below. Empty for the same reason as
# LIB_IODEV_OBJS.
LIB_SD_HOST_OBJS = []

# lib/fatfs/* objects. Anchored on the last LIB_SD_HOST entry; each subsequent
# entry anchors on the previous LIB_FATFS entry. ff_libc supplies memset/
# memcmp shims for FatFs (the project's libultra doesn't expose them).
LIB_FATFS_OBJS = [
    "ff",            # Phase 2: FatFs core
    "ffunicode",     # Phase 2: FatFs Unicode tables (mostly empty for cp437)
    "ff_libc",       # Phase 2: memset/memcmp shims for FatFs
    "diskio",        # Phase 2: FatFs<->iodev glue
]

# lib/ui/* objects. Anchored on the last LIB_FATFS entry; each subsequent
# entry anchors on the previous LIB_UI entry.
LIB_UI_OBJS = [
    "osk",          # Phase 6: on-screen keyboard state machine
    "file_browser", # Phase 6: SD file picker state machine
]

# Files that live in the .practice_late_core LOAD segment (RAM 0x801F4000).
# Loaded at boot by Practice_Late_Init via Lib_DmaRead. See spec at
# docs/superpowers/specs/2026-05-09-practice-rom-memory-architecture-design.md.
#
# Three sublists, mirroring the existing LIB_IODEV_OBJS / LIB_TOP_OBJS /
# LIB_SD_HOST_OBJS structure. Each sublist's entries are bare names; the
# emit helper prefixes "build/<prefix>/". Wave 4 of the Phase 1 plan moves
# these eleven .o files out of .main (under their original LIB_*_OBJS
# membership) and into the new segment.
PRACTICE_LATE_CORE_IODEV_OBJS = [
    "iodev",
    "iodev_sc64",
    "iodev_ed64",
    "iodev_ed64_v1",
    "iodev_ed64_v2",
    "iodev_stub",
]
PRACTICE_LATE_CORE_LIB_OBJS = [
    "sd_crc",
    "serial",
]
PRACTICE_LATE_CORE_SD_HOST_OBJS = [
    "sd_host",
]
# src/practice/*.c files moved into .practice_late_core (Pak-only, loaded at boot
# by Practice_Late_Init). Their consumers MUST be osMemSize-gated -- on stock 4 MB
# the segment isn't loaded, so the data at 0x80720000+ does not exist. Added
# 2026-06-07 to keep the vanilla audio tables (top of main) below the IPL
# boot-staging boundary; see check_boot_main_rom_budget.
PRACTICE_LATE_CORE_PRACTICE_OBJS = [
    "practice_logo_tex",  # HIT64 logo, level-select only (Practice_Logo_Draw, Pak-gated)
    "practice_owl_tex",   # owl, level-select only (Practice_Owl_Draw, Pak-gated)
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

# Macro frame buffer at 0x80680000 (immediately after the 4-slot pool + scratch
# that occupies 0x80400000–0x80680000; comfortably below the 0x80800000 ceiling).
# 18 000 MacroFrames × 4 bytes = 72 000 bytes ≈ 70 KB.
PRACTICE_MACRO_SECTION = """\
    /* Macro frame buffer: 18 000 frames x 4 bytes at 0x80680000 (Pak only). */
    .practice_macro_pak 0x80680000 (NOLOAD) : SUBALIGN(8)
    {
        practice_macro_pak_BSS_START = .;
        build/src/practice/practice_macro_buf.o(.bss);
        . = ALIGN(., 8);
        practice_macro_pak_BSS_END = .;
    }
"""

# Macro snapshot buffer at 0x80691940 (= 0x80680000 + 18000*4 bytes, aligned 8).
# MAX_STATE_SIZE = 0x80000 (512 KB); end = 0x80711940. Total Pak use: ~3.07 MB.
PRACTICE_MACRO_SNAP_SECTION = """\
    /* Macro snapshot buffer: MAX_STATE_SIZE bytes at 0x80691940 (Pak only). */
    .practice_macro_snap_pak 0x80691940 (NOLOAD) : SUBALIGN(8)
    {
        practice_macro_snap_pak_BSS_START = .;
        build/src/practice/practice_macro_snap.o(.bss);
        . = ALIGN(., 8);
        practice_macro_snap_pak_BSS_END = .;
    }
"""


def emit_practice_late_core_segment(iodev_objs, lib_objs, sd_host_objs,
                                    practice_objs=()):
    """Emit the .practice_late_core (LOAD) + .practice_late_core_bss (NOLOAD)
    block as a single linker-script snippet.

    Returns a multiline string ready for injection into the .ld file. The
    DECLARE_SEGMENT macro chain in include/sf64dma.h consumes the boundary
    symbols (VRAM/ROM/TEXT/DATA/RODATA/BSS START/END) emitted here.

    Three sublists, each prefixed with the appropriate build path:
    - iodev_objs    -> build/lib/iodev/{name}.o
    - lib_objs      -> build/lib/{name}.o
    - sd_host_objs  -> build/lib/sd_host/{name}.o

    Wave 2 of the Phase 1 plan called the predecessor of this helper with
    an empty list to scaffold the segment; Wave 4 splits the membership
    into three prefix-keyed sublists matching the existing LIB_*_OBJS
    convention and populates them with the eleven migration targets.
    """
    lines = []

    def emit_section(section):
        for obj in iodev_objs:
            lines.append(f"        build/lib/iodev/{obj}.o({section});")
        for obj in lib_objs:
            lines.append(f"        build/lib/{obj}.o({section});")
        for obj in sd_host_objs:
            lines.append(f"        build/lib/sd_host/{obj}.o({section});")
        for obj in practice_objs:
            lines.append(f"        build/src/practice/{obj}.o({section});")

    lines.append("    /* practice_late_core: Pak-only persistent overlay (RAM 0x80720000).")
    lines.append("     * Was at 0x801F4000 but Load_SceneFiles' sequential asset loader")
    lines.append("     * (fox_load.c) writes overlay+assets up to ~0x802951F0 worst-case")
    lines.append("     * (Titania setup#5), clobbering any segment below the Pak boundary.")
    lines.append("     * Loaded at boot by Practice_Late_Init, gated by osMemSize.")
    lines.append("     */")
    lines.append("    practice_late_core_ROM_START = __romPos;")
    lines.append("    practice_late_core_VRAM = 0x80720000;")
    lines.append("    .practice_late_core 0x80720000 : AT(practice_late_core_ROM_START) SUBALIGN(16)")
    lines.append("    {")
    lines.append("        FILL(0x00000000);")
    lines.append("        practice_late_core_VRAM_START = .;")
    lines.append("        practice_late_core_TEXT_START = .;")
    emit_section(".text")
    lines.append("        . = ALIGN(., 16);")
    lines.append("        practice_late_core_TEXT_END = .;")
    lines.append("        practice_late_core_DATA_START = .;")
    emit_section(".data")
    lines.append("        practice_late_core_DATA_END = .;")
    lines.append("        practice_late_core_RODATA_START = .;")
    emit_section(".rodata")
    lines.append("        . = ALIGN(., 16);")
    lines.append("        practice_late_core_RODATA_END = .;")
    lines.append("    }")
    lines.append("    practice_late_core_bss_VRAM = ADDR(.practice_late_core_bss);")
    lines.append("    .practice_late_core_bss (NOLOAD) : SUBALIGN(16)")
    lines.append("    {")
    lines.append("        FILL(0x00000000);")
    lines.append("        practice_late_core_BSS_START = .;")
    emit_section(".bss")
    lines.append("        . = ALIGN(., 16);")
    lines.append("        practice_late_core_BSS_END = .;")
    lines.append("        practice_late_core_BSS_SIZE = ABSOLUTE(practice_late_core_BSS_END - practice_late_core_BSS_START);")
    lines.append("    }")
    lines.append("    __romPos += SIZEOF(.practice_late_core);")
    lines.append("    __romPos = ALIGN(__romPos, 16);")
    lines.append("    . = ALIGN(., 16);")
    lines.append("    practice_late_core_ROM_END = __romPos;")
    lines.append("    practice_late_core_VRAM_END = .;")
    return "\n".join(lines) + "\n"


def strip_late_core_objs_from_main(content,
                                   iodev_objs, lib_objs, sd_host_objs,
                                   practice_objs=()):
    """Phase 1 migration scrubber: when .practice_late_core is injected
    with non-empty member sublists, the same object names may also still
    appear inside .main from prior patcher runs (the canonical .ld
    snapshot was produced when LIB_IODEV_OBJS / LIB_TOP_OBJS /
    LIB_SD_HOST_OBJS were non-empty). Each migrated .o would then appear
    twice and the linker would emit duplicate sections.

    Walk every section listing for each migrated target and remove
    occurrences that live OUTSIDE the .practice_late_core /
    .practice_late_core_bss blocks.

    Idempotent: lines that have already been stripped become no-op
    replaces.
    """
    targets = []
    for obj in iodev_objs:
        targets.append(f"build/lib/iodev/{obj}")
    for obj in lib_objs:
        targets.append(f"build/lib/{obj}")
    for obj in sd_host_objs:
        targets.append(f"build/lib/sd_host/{obj}")
    for obj in practice_objs:
        targets.append(f"build/src/practice/{obj}")

    if not targets:
        return content

    # The late_core block is contiguous and starts with the "/* practice_late_core"
    # comment marker emitted by emit_practice_late_core_segment. The block ends
    # at the practice_late_core_VRAM_END line. Split the content around this
    # range so we only strip from the surrounding regions.
    late_start = content.find("/* practice_late_core")
    if late_start < 0:
        # Late-core block hasn't been injected yet; nothing to deduplicate.
        return content
    late_end_marker = "practice_late_core_VRAM_END = .;"
    late_end_pos = content.find(late_end_marker, late_start)
    if late_end_pos < 0:
        return content
    late_end = late_end_pos + len(late_end_marker)

    before_late = content[:late_start]
    late_block = content[late_start:late_end]
    after_late = content[late_end:]

    for target in targets:
        for section in [".text", ".data", ".rodata", ".bss"]:
            line = f"        {target}.o({section});\n"
            before_late = before_late.replace(line, "")
            after_late = after_late.replace(line, "")

    return before_late + late_block + after_late


def chain_predecessor(prefix_pairs, fallback):
    """Walk an ordered sequence of (prefix, list) pairs and return the
    formatted predecessor string from the first non-empty list.
    If all are empty, return `fallback`. Used to compute injection-point
    anchors when one or more OBJS lists has been fully migrated.

    prefix_pairs: list of (build_prefix, objs_list) tuples in priority order.
    fallback: a fully-formatted "build/..." predecessor used when every list
              is empty.
    """
    for prefix, L in prefix_pairs:
        if L:
            return f"{prefix}{L[-1]}"
    return fallback


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
        # Fully unpatched: inject practice + lib/iodev + lib-top + lib/fatfs + lib/ui after fox_save.o.
        # Fall through to the per-entry / Pak-section logic below so the
        # newly-injected practice_macro_buf.o(.bss) and practice_macro_snap.o(.bss)
        # entries get moved into .practice_macro_pak / .practice_macro_snap_pak
        # instead of staying in stock .main_bss. Without this, a fresh
        # `make extract` followed by patch leaves macro buffers in main RAM
        # and overflows the stock 4 MB layout.
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
            sd_host_block = "\n".join(
                f"        build/lib/sd_host/{obj}.o({section});"
                for obj in LIB_SD_HOST_OBJS
            )
            fatfs_block = "\n".join(
                f"        build/lib/fatfs/{obj}.o({section});"
                for obj in LIB_FATFS_OBJS
            )
            ui_block = "\n".join(
                f"        build/lib/ui/{obj}.o({section});"
                for obj in LIB_UI_OBJS
            )
            blocks = [practice_block, iodev_block]
            if top_block:
                blocks.append(top_block)
            if sd_host_block:
                blocks.append(sd_host_block)
            if fatfs_block:
                blocks.append(fatfs_block)
            if ui_block:
                blocks.append(ui_block)
            injection = "\n".join(blocks)
            content = _replace_after_anchor(content, anchor_line, injection)

    fresh_full_patch = not has_practice

    # Walk PRACTICE_OBJS first, then LIB_IODEV_OBJS, then LIB_TOP_OBJS, then
    # LIB_FATFS_OBJS, injecting any missing entries. After a fresh full patch
    # all per-entry checks below no-op; the Pak-section injections that follow
    # are the load-bearing work.
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

    # Anchor pointer for downstream chains. Walk LIB_IODEV_OBJS, then fall
    # back to PRACTICE_OBJS[-1] when LIB_IODEV_OBJS is empty (Phase 1
    # migration empties it). The chain helper handles every "first list is
    # empty" case so a future migration that empties additional sublists
    # doesn't IndexError this file.
    predecessor_after_iodev = chain_predecessor(
        [("build/lib/iodev/", LIB_IODEV_OBJS)],
        fallback=f"build/src/practice/{last_practice_obj}",
    )
    for i, obj in enumerate(LIB_TOP_OBJS):
        if f"build/lib/{obj}.o" in content:
            continue
        if i == 0:
            predecessor = predecessor_after_iodev
        else:
            predecessor = f"build/lib/{LIB_TOP_OBJS[i - 1]}"
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{predecessor}.o({section});"
            injection = f"        build/lib/{obj}.o({section});"
            content = _replace_after_anchor(content, anchor_line, injection)
        inject_count += 1

    # lib/sd_host/* — anchored on the last LIB_TOP entry, falling back
    # through LIB_IODEV_OBJS, then PRACTICE_OBJS[-1].
    last_top_predecessor_for_sd = chain_predecessor(
        [("build/lib/", LIB_TOP_OBJS),
         ("build/lib/iodev/", LIB_IODEV_OBJS)],
        fallback=f"build/src/practice/{last_practice_obj}",
    )
    for i, obj in enumerate(LIB_SD_HOST_OBJS):
        if f"build/lib/sd_host/{obj}.o" in content:
            continue
        if i == 0:
            predecessor = last_top_predecessor_for_sd
        else:
            predecessor = f"build/lib/sd_host/{LIB_SD_HOST_OBJS[i - 1]}"
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{predecessor}.o({section});"
            injection = f"        build/lib/sd_host/{obj}.o({section});"
            content = _replace_after_anchor(content, anchor_line, injection)
        inject_count += 1

    # lib/fatfs/* — anchored on LIB_SD_HOST -> LIB_TOP -> LIB_IODEV ->
    # PRACTICE_OBJS[-1]. Each subsequent entry anchors on the previous
    # LIB_FATFS entry.
    last_top_predecessor = chain_predecessor(
        [("build/lib/sd_host/", LIB_SD_HOST_OBJS),
         ("build/lib/", LIB_TOP_OBJS),
         ("build/lib/iodev/", LIB_IODEV_OBJS)],
        fallback=f"build/src/practice/{last_practice_obj}",
    )
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

    # lib/ui/* — anchored on LIB_FATFS -> LIB_SD_HOST -> LIB_TOP ->
    # LIB_IODEV -> PRACTICE_OBJS[-1]. Each subsequent entry anchors on the
    # previous LIB_UI entry.
    last_fatfs_predecessor = chain_predecessor(
        [("build/lib/fatfs/", LIB_FATFS_OBJS),
         ("build/lib/sd_host/", LIB_SD_HOST_OBJS),
         ("build/lib/", LIB_TOP_OBJS),
         ("build/lib/iodev/", LIB_IODEV_OBJS)],
        fallback=f"build/src/practice/{last_practice_obj}",
    )
    for i, obj in enumerate(LIB_UI_OBJS):
        if f"build/lib/ui/{obj}.o" in content:
            continue
        if i == 0:
            predecessor = last_fatfs_predecessor
        else:
            predecessor = f"build/lib/ui/{LIB_UI_OBJS[i - 1]}"
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{predecessor}.o({section});"
            injection = f"        build/lib/ui/{obj}.o({section});"
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

    # Inject .practice_macro_pak immediately after .practice_pool_pak if absent.
    # practice_macro_buf.o(.bss) must be stripped from .main_bss the same way
    # practice_save_slotpool.o is.
    if ".practice_macro_pak" not in content:
        pool_anchor = "    .practice_pool_pak 0x80400000"
        if pool_anchor not in content:
            raise RuntimeError(
                "Linker patcher: .practice_pool_pak anchor not found for "
                ".practice_macro_pak injection."
            )
        needle = PRACTICE_POOL_SECTION
        replacement = PRACTICE_POOL_SECTION + "\n" + PRACTICE_MACRO_SECTION
        new_content = content.replace(needle, replacement, 1)
        if new_content == content:
            # The pool section text may have been altered (e.g. by migration).
            # Fall back: inject after the closing brace of .practice_pool_pak.
            import re as _re
            pat = r"(    \.practice_pool_pak 0x80400000.*?\})\n"
            new_content = _re.sub(pat, r"\1\n\n" + PRACTICE_MACRO_SECTION, content, count=1, flags=_re.DOTALL)
        macro_bss = "        build/src/practice/practice_macro_buf.o(.bss);\n"
        macro_comment = (
            "        /* practice_macro_buf BSS is in .practice_macro_pak "
            "(0x80680000, Pak only). */\n"
        )
        if macro_bss in new_content:
            new_content = new_content.replace(macro_bss, macro_comment, 1)
        content = new_content
        inject_count += 1

    # Inject .practice_macro_snap_pak immediately after .practice_macro_pak if absent.
    if ".practice_macro_snap_pak" not in content:
        if ".practice_macro_pak" not in content:
            raise RuntimeError(
                "Linker patcher: .practice_macro_pak anchor not found for "
                ".practice_macro_snap_pak injection."
            )
        import re as _re
        pat = r"(    \.practice_macro_pak 0x80680000.*?\})\n"
        new_content = _re.sub(
            pat, r"\1\n\n" + PRACTICE_MACRO_SNAP_SECTION, content, count=1, flags=_re.DOTALL
        )
        snap_bss = "        build/src/practice/practice_macro_snap.o(.bss);\n"
        snap_comment = (
            "        /* practice_macro_snap BSS is in .practice_macro_snap_pak "
            "(0x80691940, Pak only). */\n"
        )
        if snap_bss in new_content:
            new_content = new_content.replace(snap_bss, snap_comment, 1)
        content = new_content
        inject_count += 1

    # Inject .practice_late_core (LOAD) + .practice_late_core_bss (NOLOAD)
    # immediately after dma_table_VRAM_END = .;. Runs LAST among the
    # post-dma_table injections so the textual order ends up:
    #   .dma_table -> .practice_late_core -> .practice_pool_pak ->
    #   .practice_macro_pak -> .practice_macro_snap_pak -> .buffers
    # which keeps the location counter (`.`) moving monotonically through the
    # LOAD segment before the NOLOAD Pak segments jump it to 0x80400000+.
    #
    # Self-heal: if a stale .practice_late_core block exists (e.g. from an
    # earlier patcher version that used the 0x801F4000 VRAM), excise it so
    # the fresh block at 0x80720000 can be reinjected. The block runs from
    # the `/* practice_late_core:` comment through the next
    # `practice_late_core_VRAM_END = .;` line.
    PRACTICE_LATE_CORE_EXPECTED_VRAM = "practice_late_core_VRAM = 0x80720000;"
    if (".practice_late_core" in content
            and PRACTICE_LATE_CORE_EXPECTED_VRAM not in content):
        stale_block = re.compile(
            r"[ \t]*/\* practice_late_core:.*?practice_late_core_VRAM_END = \.;\n",
            re.DOTALL,
        )
        new_content, n = stale_block.subn("", content, count=1)
        if n != 1:
            raise RuntimeError(
                "Linker patcher: stale .practice_late_core block detected but "
                "could not be excised (missing comment marker or VRAM_END "
                "anchor). Manual repair required."
            )
        content = new_content

    if ".practice_late_core" not in content:
        anchor_line = "    dma_table_VRAM_END = .;"
        needle = anchor_line + "\n"
        segment_text = emit_practice_late_core_segment(
            PRACTICE_LATE_CORE_IODEV_OBJS,
            PRACTICE_LATE_CORE_LIB_OBJS,
            PRACTICE_LATE_CORE_SD_HOST_OBJS,
            PRACTICE_LATE_CORE_PRACTICE_OBJS,
        )
        replacement = anchor_line + "\n\n" + segment_text
        new_content = content.replace(needle, replacement, 1)
        if new_content == content:
            raise RuntimeError(
                "Linker patcher: dma_table_VRAM_END anchor not found for "
                ".practice_late_core injection."
            )
        content = new_content
        inject_count += 1

    # If the late_core members are also present inside .main from a prior
    # patcher run (canonical .ld pre-Phase-1 had them in LIB_IODEV_OBJS /
    # LIB_TOP_OBJS / LIB_SD_HOST_OBJS), strip the duplicates so each migrated
    # .o is only listed inside .practice_late_core. Idempotent.
    content = strip_late_core_objs_from_main(
        content,
        PRACTICE_LATE_CORE_IODEV_OBJS,
        PRACTICE_LATE_CORE_LIB_OBJS,
        PRACTICE_LATE_CORE_SD_HOST_OBJS,
        PRACTICE_LATE_CORE_PRACTICE_OBJS,
    )

    dirty = (content != initial_content) or inject_count != 0
    if not dirty:
        print("Linker script already fully patched, skipping.")
        return

    with open(LINKER_SCRIPT, "w") as f:
        f.write(content)

    if fresh_full_patch:
        suffix = (
            " practice + lib/iodev + lib-top + lib/fatfs + lib/ui (full) + "
            f"{inject_count} Pak section entries."
        )
    elif inject_count:
        suffix = f" injected {inject_count} lib/script entries."
    elif content != initial_content:
        suffix = " (migration)."
    else:
        suffix = ""
    print(f"Patched {LINKER_SCRIPT}{suffix}")

if __name__ == "__main__":
    patch()
