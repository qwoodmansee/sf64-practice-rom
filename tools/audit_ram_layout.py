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

Default map path is build/starfox64.us.rev1.map. Exit code 0 means no
overlaps detected; exit code 1 means at least one overlap was found.
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


def audit(map_path: str = DEFAULT_MAP_PATH) -> tuple[str, int]:
    """Run the audit. Returns (rendered_report, exit_code)."""
    if not os.path.isfile(map_path):
        return (f"audit_ram_layout: map file not found: {map_path}\n", 2)
    regions = parse_map(map_path)
    report = render_report(map_path, regions)
    has_overlap = bool(find_overlaps(regions))
    return (report, 1 if has_overlap else 0)


def main(argv: list[str]) -> int:
    map_path = argv[1] if len(argv) > 1 else DEFAULT_MAP_PATH
    report, code = audit(map_path)
    sys.stdout.write(report)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
