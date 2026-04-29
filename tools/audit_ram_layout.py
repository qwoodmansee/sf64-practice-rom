#!/usr/bin/env python3
"""Audit static RAM layout from the linker map for overlay/asset overlaps.

Parses build/starfox64.us.rev1.map and reports:
  - All static RAM regions (.main_bss, .dma_table, custom practice sections,
    overlay slots, .buffers).
  - The dynamic load window [ovl_i1_VRAM, buffers_VRAM) that holds every
    overlay (ovl_*) and dynamically-DMA'd scene asset at runtime.
  - Address-range overlaps between practice-defined BSS sections and either
    a specific overlay slot or the dynamic load window as a whole.
  - Free RAM gaps between sections.

Why this exists: SF64 streams overlays and scene assets into a fixed slot
starting at SEGMENT_VRAM_START(ovl_i1) = 0x8019ae40. Any practice-owned BSS
allocated into that range gets clobbered the next time an overlay loads, or
clobbers the live overlay's data if a practice init memset happens after a
boot-time overlay load. The map file proves this statically — no need to
reproduce the symptom on hardware.

Status semantics:
  - Overlap with the dynamic load window or any ovl_* slot is reported as a
    failure (exit 1) and printed under '*** ADDRESS RANGE OVERLAPS ***'.
  - This script is informational/warning. The hard pre-commit gate is
    delegated to tools/practice_invariants.py, which calls into this module
    and emits a non-fatal warning so commits keep flowing while the Wave 6
    audit picks a permanent home for save state.

Usage:
    python3 tools/audit_ram_layout.py [path/to/starfox64.map]
    python3 tools/audit_ram_layout.py --suggest-layout [path/to/starfox64.map]

Default map path is build/starfox64.us.rev1.map. Exit code 0 means no
overlaps detected; exit code 1 means at least one overlap was found.

--suggest-layout: additionally compute per-scene dynamic load extents for
all 17 saveable scenes, find the worst-case high-water-mark, and suggest a
safe practice_pool placement above it. Prints machine-readable key=value
lines at the end.
"""
from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass

DEFAULT_MAP_PATH = "build/starfox64.us.rev1.map"

# Sections that are not "practice" but anchor the layout report.
STATIC_ANCHORS = {".main_bss", ".dma_table", ".buffers"}

# Regex to match the symbol-assignment lines that name VRAM bounds:
#   "                0x000000008019ae40                ovl_i1_VRAM = ADDR (.ovl_i1)"
#   "                0x00000000801af9f0                ovl_i1_VRAM_END = ."
_RE_SYMBOL = re.compile(
    r"^\s*0x([0-9a-fA-F]+)\s+(\w+)\s*=\s*(.+?)\s*$"
)

# Regex to match section header lines:
#   ".practice_pool  0x000000008018c930    0x80010 load address 0x..."
#   ".ovl_i1         0x000000008019ae40    0x141a0 load address ..."
_RE_SECTION = re.compile(
    r"^(\.[A-Za-z_][\w.]*)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\b"
)


@dataclass
class Region:
    name: str          # human-friendly label, e.g. ".practice_pool" or "ovl_menu (slot)"
    start: int
    end: int            # exclusive end
    kind: str           # one of: static, practice, overlay, asset_dynamic, wall
    note: str = ""

    @property
    def size(self) -> int:
        return self.end - self.start


def _human_size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / (1024 * 1024):.1f} MB"


def parse_map(path: str) -> list[Region]:
    """Parse the linker map and return all regions of interest, sorted by start.

    Picks up:
      - .main_bss, .dma_table, .buffers (anchors)
      - .practice_pool and any other custom NOLOAD-style section after .main_bss
        whose start lies before .buffers (so it competes for the same RAM).
      - All ovl_*_VRAM / ovl_*_VRAM_END pairs (dynamic overlays).
    """
    with open(path) as f:
        text = f.read()

    # Pass 1: section-header lines for sized sections we actually want to
    # report. We rely on the start/size column rather than chasing _VRAM/_END
    # symbol pairs because some sections (e.g. .practice_pool) report cleanly
    # via section headers and others (e.g. ovl_*) only via _VRAM symbols.
    sized_sections: dict[str, tuple[int, int]] = {}
    for line in text.splitlines():
        m = _RE_SECTION.match(line)
        if not m:
            continue
        sec, start_hex, size_hex = m.groups()
        start = int(start_hex, 16)
        size = int(size_hex, 16)
        if start == 0:
            # Skip ROM-side ELF sections that aren't yet relocated to RDRAM.
            continue
        # Only the first occurrence (some overlays show up under multiple
        # alias section names).
        sized_sections.setdefault(sec, (start, start + size))

    # Pass 2: VRAM/VRAM_END symbol pairs for overlays and the .buffers wall.
    vram_starts: dict[str, int] = {}
    vram_ends: dict[str, int] = {}
    for line in text.splitlines():
        m = _RE_SYMBOL.match(line)
        if not m:
            continue
        addr_hex, sym, _rhs = m.groups()
        addr = int(addr_hex, 16)
        if addr == 0:
            continue
        if sym.endswith("_VRAM"):
            base = sym[: -len("_VRAM")]
            vram_starts.setdefault(base, addr)
        elif sym.endswith("_VRAM_END"):
            base = sym[: -len("_VRAM_END")]
            vram_ends[base] = addr

    regions: list[Region] = []

    # main_bss / dma_table / buffers
    for static_name in (".main_bss", ".dma_table"):
        if static_name in sized_sections:
            s, e = sized_sections[static_name]
            regions.append(Region(static_name, s, e, "static"))

    # .buffers wall — show as a zero-or-larger fence; size from .buffers_bss
    # if present, else just from the .buffers header.
    buffers_start = None
    if ".buffers" in sized_sections:
        buffers_start = sized_sections[".buffers"][0]
    elif "buffers" in vram_starts:
        buffers_start = vram_starts["buffers"]
    buffers_end = None
    if ".buffers_bss" in sized_sections:
        buffers_end = sized_sections[".buffers_bss"][1]
    elif "buffers" in vram_ends:
        buffers_end = vram_ends["buffers"]
    if buffers_start is not None:
        regions.append(
            Region(
                ".buffers",
                buffers_start,
                buffers_end if buffers_end is not None else buffers_start,
                "wall",
                note="framebuffer/audio wall",
            )
        )

    # Custom practice-named sections: any section name starting with
    # ".practice" or whose name contains "practice_pool". These are the
    # ones the patch_linker_script tool injects.
    for name, (s, e) in sized_sections.items():
        if name in STATIC_ANCHORS:
            continue
        if name.startswith(".buffers") or name.startswith(".audio") or name.startswith(".ast_") or name.startswith(".ovl_"):
            continue
        if name.startswith(".main") or name == ".dma_table" or name == ".makerom" or name == ".makerom_bss":
            continue
        # Only flag sections that compete for RDRAM with the overlay window
        # (i.e. sit below the .buffers wall). Sections at low addresses
        # (e.g. ELF-only) were already filtered by the start==0 guard above.
        if buffers_start is not None and s >= buffers_start:
            continue
        regions.append(Region(name, s, e, "practice", note="practice"))

    # Overlays: every ovl_*_VRAM / _VRAM_END pair becomes one region. Each
    # overlay is reported as an independent slot — they all share the same
    # base address but consume different ranges.
    for base, start in vram_starts.items():
        if not base.startswith("ovl_"):
            continue
        if base.endswith("_bss"):
            # The bss subsegment of an overlay; the parent _VRAM_END already
            # encompasses it. Skip to keep the report tidy.
            continue
        end = vram_ends.get(base)
        if end is None:
            continue
        regions.append(
            Region(
                f"{base} (slot)",
                start,
                end,
                "overlay",
                note="overlay, dynamic",
            )
        )

    regions.sort(key=lambda r: (r.start, r.end))
    return regions


def overlap(a: Region, b: Region) -> int:
    """Return overlap size (>=0) between two regions."""
    return max(0, min(a.end, b.end) - max(a.start, b.start))


def find_overlaps(
    regions: list[Region],
) -> list[tuple[Region, Region, int]]:
    """Return overlaps between practice regions and overlays, plus
    practice-vs-dynamic-load-window overlaps as synthetic Region pairs."""
    practice = [r for r in regions if r.kind == "practice"]
    overlays = [r for r in regions if r.kind == "overlay"]

    pairs: list[tuple[Region, Region, int]] = []
    for p in practice:
        for o in overlays:
            n = overlap(p, o)
            if n > 0:
                pairs.append((p, o, n))

    # Synthetic dynamic-load window: [min ovl start, .buffers start).
    if overlays:
        ovl_min_start = min(o.start for o in overlays)
        wall = next(
            (r for r in regions if r.kind == "wall" and r.name == ".buffers"),
            None,
        )
        if wall is not None:
            window = Region(
                "dynamic-load window",
                ovl_min_start,
                wall.start,
                "overlay",
                note="overlay+asset DMA target",
            )
            for p in practice:
                n = overlap(p, window)
                if n > 0:
                    pairs.append((p, window, n))

    return pairs


def find_gaps(regions: list[Region]) -> list[tuple[int, int, str, str]]:
    """Return free-RAM gaps between consecutive non-overlapping static
    regions. Overlays all share a base address so we only walk the
    static/practice/wall regions for gap analysis."""
    fixed = sorted(
        (r for r in regions if r.kind in ("static", "practice", "wall")),
        key=lambda r: r.start,
    )
    gaps: list[tuple[int, int, str, str]] = []
    for i in range(len(fixed) - 1):
        a, b = fixed[i], fixed[i + 1]
        if b.start > a.end:
            gaps.append((a.end, b.start, a.name, b.name))
    return gaps


def fmt_addr(n: int) -> str:
    return f"0x{n:08x}"


def render_report(map_path: str, regions: list[Region]) -> str:
    out: list[str] = []
    out.append(f"STATIC RAM LAYOUT ({map_path})")
    out.append("-" * 56)

    # Right-pad the name column to the widest label so the address columns
    # line up regardless of section name length.
    name_w = max((len(r.name) for r in regions), default=14)

    # Build a sorted list with overlays inline so the user sees the
    # interleaved picture.
    for r in regions:
        size_str = _human_size(r.size)
        if r.kind == "wall":
            line = (
                f"  {r.name:<{name_w}}  {fmt_addr(r.start)} -            "
                f"  (size {size_str:>10})"
            )
        else:
            line = (
                f"  {r.name:<{name_w}}  {fmt_addr(r.start)} - {fmt_addr(r.end)}  "
                f"(size {size_str:>10})"
            )
        if r.note:
            line += f"  [{r.note}]"
        out.append(line)

    overlays = [r for r in regions if r.kind == "overlay"]
    wall = next((r for r in regions if r.kind == "wall"), None)
    if overlays and wall is not None:
        ovl_min_start = min(o.start for o in overlays)
        out.append("")
        out.append(
            f"DYNAMIC LOAD WINDOW: {fmt_addr(ovl_min_start)} to "
            f"{fmt_addr(wall.start)}  "
            f"({_human_size(wall.start - ovl_min_start)} free for overlay+asset DMA)"
        )

    overlaps = find_overlaps(regions)
    out.append("")
    if overlaps:
        out.append("*** ADDRESS RANGE OVERLAPS ***")
        # Group: report each (practice, overlay) pair on its own line, plus
        # the dynamic-load-window synthetic pair (kept separate for clarity).
        for p, o, n in overlaps:
            out.append(
                f"  {p.name} [{fmt_addr(p.start)}-{fmt_addr(p.end)}] "
                f"OVERLAPS {o.name} "
                f"[{fmt_addr(o.start)}-{fmt_addr(o.end)}] by {_human_size(n)}"
            )
    else:
        out.append("No overlaps detected between practice BSS and overlay slots.")

    gaps = find_gaps(regions)
    if gaps:
        out.append("")
        out.append("FREE RAM (between static regions):")
        for s, e, before, after in gaps:
            out.append(
                f"  {fmt_addr(s)} to {fmt_addr(e)} ({_human_size(e - s)}) "
                f"-- between {before} and {after}"
            )

    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Scene-extent analysis for --suggest-layout
# ---------------------------------------------------------------------------

# BUFFERS_VRAM: fixed wall address (framebuffer/audio buffers). This is the
# hard upper bound that the dynamic load window must stay below.
BUFFERS_VRAM = 0x80281000

# Safety margin added above the max scene extent before rounding.
POOL_SAFETY_MARGIN = 0x4000  # 16 KB

# Pool alignment (must be power-of-two multiple of cache line / DMA alignment).
POOL_ALIGNMENT = 0x10  # 16 bytes

# Headroom warning threshold.
HEADROOM_WARN_BYTES = 256 * 1024  # 256 KB

# Each entry describes one saveable scene:
#   name        : human-readable label
#   ovl         : 'ovl_i1' .. 'ovl_i6' (overlay segment name in the map)
#   setups      : list of asset lists, one per scene-setup index. Each asset
#                 list contains the asset segment names that appear as
#                 non-NO_SEGMENT entries in that setup's Scene struct literal.
#                 Mirrors fox_load_inits.c exactly; keep in sync if it changes.
#
# Asset indices 0x1..0xF map to assets[0..14] in the Scene struct.
# Only non-NULL segments contribute bytes; NO_SEGMENT entries have size 0.
#
# For extent purposes we take the worst-case (maximum) setup for each scene,
# since Load_SceneFiles loads whatever setup the game selects at runtime.
_SAVEABLE_SCENES: list[dict] = [
    {
        "name": "CORNERIA",
        "ovl": "ovl_i1",
        "setups": [
            ["ast_common", "ast_bg_planet", "ast_arwing", "ast_enmy_planet",
             "ast_text", "ast_corneria"],
        ],
    },
    {
        "name": "METEO",
        "ovl": "ovl_i2",
        "setups": [
            # setup 0: no great_fox
            ["ast_common", "ast_bg_space", "ast_arwing", "ast_enmy_space",
             "ast_text", "ast_meteo", "ast_warp_zone"],
            # setup 1: + great_fox
            ["ast_common", "ast_bg_space", "ast_arwing", "ast_enmy_space",
             "ast_text", "ast_meteo", "ast_warp_zone", "ast_great_fox"],
        ],
    },
    {
        "name": "SECTOR_X",
        "ovl": "ovl_i2",
        "setups": [
            # setup 0: no great_fox
            ["ast_common", "ast_bg_space", "ast_arwing", "ast_enmy_space",
             "ast_text", "ast_sector_x", "ast_warp_zone", "ast_allies"],
            # setup 1: + great_fox
            ["ast_common", "ast_bg_space", "ast_arwing", "ast_enmy_space",
             "ast_text", "ast_sector_x", "ast_warp_zone", "ast_allies",
             "ast_great_fox"],
        ],
    },
    {
        "name": "AREA_6",
        "ovl": "ovl_i3",
        "setups": [
            ["ast_common", "ast_bg_space", "ast_arwing", "ast_enmy_space",
             "ast_text", "ast_area_6", "ast_great_fox"],
        ],
    },
    {
        "name": "SECTOR_Y",
        "ovl": "ovl_i6",
        "setups": [
            ["ast_common", "ast_bg_space", "ast_arwing", "ast_enmy_space",
             "ast_text", "ast_sector_y"],
        ],
    },
    {
        "name": "VENOM_1",
        "ovl": "ovl_i1",
        "setups": [
            ["ast_common", "ast_bg_planet", "ast_arwing", "ast_enmy_planet",
             "ast_text", "ast_venom_1", "ast_ve1_boss", "ast_allies"],
        ],
    },
    {
        "name": "SOLAR",
        "ovl": "ovl_i3",
        "setups": [
            ["ast_common", "ast_bg_planet", "ast_arwing", "ast_enmy_planet",
             "ast_text", "ast_solar", "ast_allies"],
        ],
    },
    {
        "name": "ZONESS",
        "ovl": "ovl_i3",
        "setups": [
            ["ast_common", "ast_bg_planet", "ast_arwing", "ast_enmy_planet",
             "ast_text", "ast_zoness", "ast_allies"],
        ],
    },
    {
        "name": "VENOM_ANDROSS",
        "ovl": "ovl_i6",
        "setups": [
            ["ast_common", "ast_bg_planet", "ast_arwing",
             "ast_text", "ast_venom_2", "ast_andross", "ast_allies"],
        ],
    },
    {
        "name": "MACBETH",
        "ovl": "ovl_i5",
        "setups": [
            # setup 0: no great_fox
            ["ast_common", "ast_bg_planet", "ast_landmaster",
             "ast_enmy_planet", "ast_text", "ast_macbeth", "ast_allies"],
            # setup 1: + great_fox
            ["ast_common", "ast_bg_planet", "ast_landmaster",
             "ast_enmy_planet", "ast_text", "ast_macbeth", "ast_great_fox"],
        ],
    },
    {
        "name": "TITANIA",
        "ovl": "ovl_i5",
        "setups": [
            # setup 0: ti_1 only
            ["ast_common", "ast_bg_planet", "ast_landmaster",
             "ast_enmy_planet", "ast_text", "ast_titania", "ast_7_ti_1",
             "ast_great_fox"],
            # setup 1: ti_2 only
            ["ast_common", "ast_bg_planet", "ast_landmaster",
             "ast_enmy_planet", "ast_text", "ast_titania", "ast_7_ti_2"],
            # setup 2: ti_2 + ti_8
            ["ast_common", "ast_bg_planet", "ast_landmaster",
             "ast_enmy_planet", "ast_text", "ast_titania", "ast_7_ti_2",
             "ast_8_ti"],
            # setup 3: ti_2 + ti_8 + ti_9
            ["ast_common", "ast_bg_planet", "ast_landmaster",
             "ast_enmy_planet", "ast_text", "ast_titania", "ast_7_ti_2",
             "ast_8_ti", "ast_9_ti"],
            # setup 4: ti_2 + ti_8 + ti_9 + ti_A
            ["ast_common", "ast_bg_planet", "ast_landmaster",
             "ast_enmy_planet", "ast_text", "ast_titania", "ast_7_ti_2",
             "ast_8_ti", "ast_9_ti", "ast_A_ti"],
            # setup 5: ti_2 + ti_8 + ti_9 + ti_A + great_fox
            ["ast_common", "ast_bg_planet", "ast_landmaster",
             "ast_enmy_planet", "ast_text", "ast_titania", "ast_7_ti_2",
             "ast_8_ti", "ast_9_ti", "ast_A_ti", "ast_great_fox"],
        ],
    },
    {
        "name": "AQUAS",
        "ovl": "ovl_i3",
        "setups": [
            ["ast_common", "ast_bg_planet", "ast_blue_marine",
             "ast_enmy_planet", "ast_text", "ast_aquas", "ast_great_fox"],
        ],
    },
    {
        "name": "FORTUNA",
        "ovl": "ovl_i4",
        "setups": [
            # setup 0: star_wolf instead of great_fox
            ["ast_common", "ast_bg_planet", "ast_arwing",
             "ast_enmy_planet", "ast_text", "ast_fortuna", "ast_star_wolf"],
            # setup 1: great_fox instead of star_wolf
            ["ast_common", "ast_bg_planet", "ast_arwing",
             "ast_enmy_planet", "ast_text", "ast_fortuna", "ast_great_fox"],
        ],
    },
    {
        "name": "KATINA",
        "ovl": "ovl_i4",
        "setups": [
            ["ast_common", "ast_bg_planet", "ast_arwing",
             "ast_enmy_planet", "ast_text", "ast_katina", "ast_allies",
             "ast_star_wolf"],
        ],
    },
    {
        "name": "BOLSE",
        "ovl": "ovl_i4",
        "setups": [
            ["ast_common", "ast_bg_space", "ast_arwing",
             "ast_enmy_space", "ast_text", "ast_bolse", "ast_star_wolf"],
        ],
    },
    {
        "name": "SECTOR_Z",
        "ovl": "ovl_i4",
        "setups": [
            ["ast_common", "ast_bg_space", "ast_arwing",
             "ast_enmy_space", "ast_text", "ast_sector_z", "ast_allies",
             "ast_great_fox"],
        ],
    },
    {
        "name": "VENOM_2",
        "ovl": "ovl_i6",
        "setups": [
            # setup 0: star_wolf instead of great_fox
            ["ast_common", "ast_bg_planet", "ast_arwing",
             "ast_enmy_planet", "ast_text", "ast_venom_2", "ast_star_wolf"],
            # setup 1: great_fox instead of star_wolf
            ["ast_common", "ast_bg_planet", "ast_arwing",
             "ast_enmy_planet", "ast_text", "ast_venom_2", "ast_great_fox"],
        ],
    },
]


def parse_segment_sizes(map_text: str) -> dict[str, dict[str, int]]:
    """Parse ROM_START/ROM_END and BSS_START/BSS_END symbol pairs from the
    map, returning a dict:  segment_name -> {'rom': bytes, 'bss': bytes}.

    We use _ROM_START/_ROM_END pairs (not section headers) because some
    sections like .ast_landmaster span two lines in the map with the size on
    the continuation line, making section-header parsing unreliable for those.
    The *_ROM_START / *_ROM_END / *_BSS_START / *_BSS_END symbols are always
    on their own single line and are fully reliable.
    """
    rom_starts: dict[str, int] = {}
    rom_ends: dict[str, int] = {}
    bss_starts: dict[str, int] = {}
    bss_ends: dict[str, int] = {}

    for line in map_text.splitlines():
        m = _RE_SYMBOL.match(line)
        if not m:
            continue
        addr_hex, sym, _rhs = m.groups()
        addr = int(addr_hex, 16)

        if sym.endswith("_ROM_START"):
            base = sym[: -len("_ROM_START")]
            rom_starts[base] = addr
        elif sym.endswith("_ROM_END"):
            base = sym[: -len("_ROM_END")]
            rom_ends[base] = addr
        elif sym.endswith("_BSS_START"):
            base = sym[: -len("_BSS_START")]
            bss_starts[base] = addr
        elif sym.endswith("_BSS_END"):
            base = sym[: -len("_BSS_END")]
            bss_ends[base] = addr

    sizes: dict[str, dict[str, int]] = {}
    all_segs = set(rom_starts) | set(bss_starts)
    for seg in all_segs:
        rom = 0
        if seg in rom_starts and seg in rom_ends:
            rom = rom_ends[seg] - rom_starts[seg]
        bss = 0
        if seg in bss_starts and seg in bss_ends:
            bss = bss_ends[seg] - bss_starts[seg]
        sizes[seg] = {"rom": rom, "bss": bss}

    return sizes


@dataclass
class SceneExtent:
    name: str
    ovl: str
    best_setup_idx: int   # index of worst-case (largest) setup
    ovl_rom: int
    ovl_bss: int
    asset_bytes: int
    total: int            # ovl_rom + ovl_bss + asset_bytes
    vram_base: int        # VRAM start address of the shared overlay slot
    high_water: int       # vram_base + total


def compute_scene_extents(
    map_text: str,
    vram_base: int,
) -> list[SceneExtent]:
    """For each saveable scene compute the worst-case RDRAM high-water-mark
    that Load_SceneFiles reaches when loading it.

    Load_SceneFiles logic (from fox_load.c):
        ramPtr = SEGMENT_VRAM_START(ovl_i1)   # shared base
        ramPtr += SEGMENT_SIZE(scene->ovl.rom)
        ramPtr += SEGMENT_SIZE(scene->ovl.bss)
        for each of 15 asset slots:
            if asset.start != 0:
                ramPtr += SEGMENT_SIZE(asset)
    """
    seg_sizes = parse_segment_sizes(map_text)

    extents: list[SceneExtent] = []
    for scene in _SAVEABLE_SCENES:
        ovl_name = scene["ovl"]
        ovl_info = seg_sizes.get(ovl_name, {"rom": 0, "bss": 0})
        ovl_rom = ovl_info["rom"]
        ovl_bss = ovl_info["bss"]

        worst_asset_bytes = 0
        worst_idx = 0
        for idx, asset_list in enumerate(scene["setups"]):
            ab = sum(
                seg_sizes.get(a, {"rom": 0})["rom"] for a in asset_list
            )
            if ab > worst_asset_bytes:
                worst_asset_bytes = ab
                worst_idx = idx

        total = ovl_rom + ovl_bss + worst_asset_bytes
        hw = vram_base + total
        extents.append(
            SceneExtent(
                name=scene["name"],
                ovl=ovl_name,
                best_setup_idx=worst_idx,
                ovl_rom=ovl_rom,
                ovl_bss=ovl_bss,
                asset_bytes=worst_asset_bytes,
                total=total,
                vram_base=vram_base,
                high_water=hw,
            )
        )

    extents.sort(key=lambda e: e.high_water, reverse=True)
    return extents


def suggest_pool_placement(
    extents: list[SceneExtent],
) -> tuple[int, int, int, bool]:
    """Return (max_extent, suggested_vma, headroom_kb, fits_256kb).

    suggested_vma = (max_extent + POOL_SAFETY_MARGIN) rounded up to
    POOL_ALIGNMENT.  headroom = BUFFERS_VRAM - suggested_vma.
    """
    max_extent = max(e.high_water for e in extents) if extents else 0
    raw = max_extent + POOL_SAFETY_MARGIN
    suggested = (raw + POOL_ALIGNMENT - 1) & ~(POOL_ALIGNMENT - 1)
    headroom = BUFFERS_VRAM - suggested
    fits = headroom >= 256 * 1024
    return max_extent, suggested, headroom // 1024, fits


def render_scene_extents(
    extents: list[SceneExtent],
    suggested_vma: int,
    headroom_kb: int,
    fits: bool,
) -> str:
    out: list[str] = []
    out.append("")
    out.append("SCENE DYNAMIC LOAD EXTENTS (saveable scenes, worst-case setup)")
    out.append("-" * 72)
    name_w = max(len(e.name) for e in extents)
    ovl_w = max(len(e.ovl) for e in extents)
    for e in extents:
        ovl_label = f"{e.ovl}"
        out.append(
            f"  {e.name:<{name_w}}  {ovl_label:<{ovl_w}}"
            f"  ovl={_human_size(e.ovl_rom)}+bss={_human_size(e.ovl_bss)}"
            f"  ast={_human_size(e.asset_bytes)}"
            f"  total={_human_size(e.total)}"
            f"  hw={fmt_addr(e.high_water)}"
        )

    max_hw = extents[0].high_water if extents else 0
    out.append("")
    out.append(f"  Worst scene: {extents[0].name} (setup {extents[0].best_setup_idx})")
    out.append(f"  Max high-water: {fmt_addr(max_hw)}")
    out.append(f"  Suggested pool VMA (+{_human_size(POOL_SAFETY_MARGIN)} margin, "
               f"aligned {POOL_ALIGNMENT:#x}): {fmt_addr(suggested_vma)}")
    out.append(f"  Headroom to buffers_VRAM ({fmt_addr(BUFFERS_VRAM)}): "
               f"{headroom_kb} KB  "
               f"({'OK -- fits 256 KB pool' if fits else 'WARNING: < 256 KB, cannot fit a 256 KB pool'})")
    return "\n".join(out) + "\n"


def render_kv(
    max_extent: int,
    suggested_vma: int,
    headroom_kb: int,
    fits: bool,
) -> str:
    lines = [
        "",
        "# --suggest-layout machine-readable output",
        f"max_scene_extent={fmt_addr(max_extent)}",
        f"suggested_pool_vma={fmt_addr(suggested_vma)}",
        f"headroom_kb={headroom_kb}",
        f"fits_256kb_stock={'yes' if fits else 'no'}",
    ]
    return "\n".join(lines) + "\n"


def audit(map_path: str = DEFAULT_MAP_PATH) -> tuple[str, int]:
    """Run the audit. Returns (rendered_report, exit_code)."""
    if not os.path.isfile(map_path):
        return (f"audit_ram_layout: map file not found: {map_path}\n", 2)
    regions = parse_map(map_path)
    report = render_report(map_path, regions)
    has_overlap = bool(find_overlaps(regions))
    return (report, 1 if has_overlap else 0)


def main(argv: list[str]) -> int:
    suggest = "--suggest-layout" in argv
    args = [a for a in argv[1:] if a != "--suggest-layout"]
    map_path = args[0] if args else DEFAULT_MAP_PATH

    if not os.path.isfile(map_path):
        sys.stdout.write(f"audit_ram_layout: map file not found: {map_path}\n")
        return 2

    regions = parse_map(map_path)
    report = render_report(map_path, regions)
    sys.stdout.write(report)

    exit_code = 1 if find_overlaps(regions) else 0

    if suggest:
        with open(map_path) as f:
            map_text = f.read()

        # Determine the shared overlay VRAM base from the parsed regions.
        overlay_regions = [r for r in regions if r.kind == "overlay"]
        if overlay_regions:
            vram_base = min(r.start for r in overlay_regions)
        else:
            # Fallback: known constant for SF64 US rev1.
            vram_base = 0x8019AE40

        extents = compute_scene_extents(map_text, vram_base)
        if extents:
            max_extent, suggested_vma, headroom_kb, fits = suggest_pool_placement(extents)
            sys.stdout.write(render_scene_extents(extents, suggested_vma, headroom_kb, fits))
            sys.stdout.write(render_kv(max_extent, suggested_vma, headroom_kb, fits))
        else:
            sys.stdout.write("\nNo scene extent data available.\n")

    return exit_code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
