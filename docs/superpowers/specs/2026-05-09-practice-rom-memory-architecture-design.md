# Practice ROM Memory Architecture

## Summary

Restructure the practice ROM so that practice and `lib/` code is no longer
the default occupant of the boot-resident `main` segment. Introduce two new
ROM-loaded segments — `.practice_late_core` (stock 4 MB safe, every cart)
and `.practice_late_pak` (Expansion Pak only) — DMA'd into RDRAM by a
`Practice_Late_Init()` orchestrator that runs as the first instruction of
`Practice_Init`. Route all conditional Pak-only feature access through a
`gLateOps` dispatch struct rather than per-callsite flag checks. Make
`.practice_late_core` the default home for new practice and `lib/` source
files via a tooling change in `tools/patch_linker_script.py`, and enforce
the routing model with six new static invariants.

The immediate trigger is an EverDrive-support work-in-progress branch that
exceeded the IPL-staged boot-safe limit (`main_ROM_END < 0xFD000`) by ~16 KB.
The longer-term goal is to convert the architecture from "main fills up
until the cliff" into one where the cliff is structurally hard to hit:
boot-resident code is a small named set, everything else lives in late
segments with substantial budget headroom.

## Goals

- Stop bursting the `0xFD000` boot-safe limit. Reclaim enough headroom in
  `main` to land the EverDrive iodev + sd_host work, with margin for several
  more years of feature growth.
- Make "the file lives in main" an explicit, justified choice rather than
  the default. New practice and `lib/` files default-route to
  `.practice_late_core` unless explicitly listed.
- Keep stock 4 MB carts working. Pak-only features must degrade cleanly
  (not crash, not silently disappear) when the Expansion Pak is absent.
- Localize the conditional-availability decision to one place
  (`SelectLateOps`). Callers read straight through their logic without
  per-callsite Pak-detection flags.
- Make the architecture self-policing through static invariants. Routing
  violations, boot-time references to late symbols, ungated Pak-only
  calls, and segment-budget overruns all fail at build time.
- Preserve compatibility with the existing splat-based vanilla SF64 linker
  script. Practice-managed segment edits stay in a defined window.

## Non-Goals

- Raising the `0xFD000` ceiling itself. That requires modifying IPL3
  (signed by the CIC chip on real carts; not part of this repo's source).
  Out of scope; we work around the ceiling, not through it.
- True on-demand overlay loading (per-feature load/unload sharing a single
  RAM slot, vanilla-style). The phase plan reserves this for future work
  if any single feature exceeds its segment budget. The current design is
  always-resident persistent overlays only.
- Migration of vanilla code (`src/sys/`, `src/libultra/`, `src/engine/`,
  `src/audio/`, `src/overlays/`). The architecture only re-homes practice
  and `lib/` code; vanilla layout is preserved.
- BizHawk functional test coverage. The functional tests are designed in
  this spec but are not runnable in current CI/local setup. Boot-safety
  guarantees come from build-time invariants and hardware verification.
- Compression of late-segment text (gzip / lz4 at load time). Possible
  future optimization; not needed at current sizes.

## Background

### Why we hit the limit

The N64 IPL stages roughly the first ~1 MB of cart ROM into RDRAM at boot.
Anything in `main` past the empirical `0xFD000` ceiling is **not** in
RDRAM when boot threads start; the first call into it deadlocks the system
on a uniform blue screen.

The practice ROM has historically piled new code into the `main` segment.
Periodic "reclaim ROM headroom" PRs have shaved bytes from vanilla content
to extend the runway. The most recent such effort (`154dea2`) left only
6,240 bytes of headroom under the cap. Adding the EverDrive backend work
(`lib/iodev/iodev_ed64_v1`, `iodev_ed64_v2`, `lib/sd_host/sd_host`) blew
through it: `main_ROM_END` reached `0x100F10`, putting the build 16,144
bytes over the limit and bricking boot on every cart.

The static invariant `check_boot_main_rom_budget` in
`tools/practice_invariants.py` catches this at build time, blocking the
commit. The architecture defined in this spec is the structural answer.

### Why the obvious answers don't work

- **Strip more vanilla content**: diminishing returns. Each round of
  vanilla reclamation finds smaller and smaller wins; the practice ROM
  keeps growing faster than vanilla shrinks.
- **Compress code**: real but expensive. Adds a decompression pass at boot
  for marginal gain at current sizes.
- **Replace IPL3 with a larger-staging variant**: signed by CIC; requires
  flashcart that bypasses CIC checks (most do, but not all); IPL3 isn't
  part of this repo's source. Separate project.
- **Move code into the existing shared overlay slot**: that slot is reused
  per scene by vanilla level overlays. Practice code parked there is
  clobbered every level transition. Suitable only for true on-demand load,
  which this spec defers.

### What we already have

`Lib_DmaRead(src, dst, size)` (`src/sys/sys_lib.c:104`) is the canonical
chunked DMA helper used by all vanilla overlay loads. It chunks at 256
bytes, syncs on the DMA message queue, and invalidates I-cache + D-cache.
We reuse it; no new DMA primitives.

`SEGMENT_ROM_START(name)`, `SEGMENT_VRAM_START(name)`,
`SEGMENT_ROM_SIZE(name)` macros (`include/sf64dma.h`) give linker-defined
addresses for any named segment via the existing `DECLARE_SEGMENT(name)`
pattern.

`tools/patch_linker_script.py` already separates `PRACTICE_OBJS`,
`LIB_IODEV_OBJS`, and `LIB_TOP_OBJS` into emit groups. Adding a fourth and
fifth group, plus a default-routing classifier, fits the existing shape.

## Memory Layout

The stock 4 MB RAM map after this change:

```
0x80000400  makerom (header / boot stuff)
0x80000450  .main TEXT/DATA/RODATA  (ends at main_ROM_END < 0xFD000)
            .main_bss
0x8019CB70  main_VRAM_END
0x8019CB70  .dma_table  (1,440 bytes)
0x8019D110  dma_table_VRAM_END
            ▼ ~58 KB unused (free for future use; not claimed) ▼
0x801AB620  ovl_i1_VRAM  (start of shared vanilla overlay slot)
0x801F31E0  ovl_menu_BSS_END  (top of overlay region)
            ▼ unused buffer between overlay region and practice late core ▼
0x801F4000  practice_late_core_VRAM  (NEW. 512 KB budget cap.)
0x80274000  practice_late_core_VRAM end (cap)
            ▼ ~52 KB cushion before .buffers ▼
0x80281000  .buffers  (frame buffers, audio buffers; ~1.5 MB)
0x80400000  buffers_VRAM_END / practice_pool_pak_BSS_START  (Pak only)
```

Pak-only RAM (`0x80400000`–`0x80800000`):

```
0x80400000  .practice_pool_pak       (existing; 2.5 MB save snapshots)
0x80680000  .practice_macro_pak      (existing; ~70 KB macro buffer)
0x80691940  .practice_macro_snap_pak (existing; 512 KB macro snapshots)
0x80711940  ─── end of existing Pak segments ───
0x80720000  practice_late_pak_VRAM   (NEW. 768 KB budget cap.)
0x807E0000  practice_late_pak_VRAM end (cap)
            ▼ ~128 KB cushion before Pak ceiling ▼
0x80800000  Pak ceiling (8 MB total)
```

Confirmed sizes (from `build/starfox64.us.rev1.map`): `practice_save_slotpool.o(.bss)`
is `0x280000` (2.5 MB), `practice_macro_buf.o(.bss)` is `0x11940` (~70 KB),
`practice_macro_snap.o(.bss)` is `0x80000` (512 KB). The linker script's
inline comment claiming "4 × 256 KB" is outdated — the real
`practice_pool_pak` extent is 2.5 MB.

### Segment definitions

**`.practice_late_core`** — stock-RAM safe persistent overlay.
- RAM target: `0x801F4000` (16-byte aligned, above
  `ovl_menu_BSS_END = 0x801F31E0` so no overlay swap can clobber it).
- ROM target: just after `dma_table_ROM_END`.
- Budget — ROM (TEXT+DATA+RODATA): `practice_late_core_ROM_SIZE < 0x80000`
  (512 KB).
- Budget — RAM (TEXT+DATA+RODATA+BSS): `practice_late_core_BSS_END <
  0x80274000` (caps the entire VRAM extent at the same 512 KB; preserves
  the 52 KB cushion before `.buffers` at `0x80281000`).
- Loaded by every build, every cart, every player.
- Contents: every `.c` whose call graph is fully post-`Practice_Late_Init`
  AND that runs correctly on stock 4 MB carts (no Pak-only memory deps).

**`.practice_late_pak`** — Pak-only persistent overlay.
- RAM target: `0x80720000` (16-aligned, above
  `practice_macro_snap_pak_BSS_END = 0x80711940`).
- ROM target: just after `practice_late_core_ROM_END`.
- Budget — ROM: `practice_late_pak_ROM_SIZE < 0xC0000` (768 KB).
- Budget — RAM: `practice_late_pak_BSS_END < 0x807E0000` (768 KB extent,
  preserves a 128 KB cushion to the `0x80800000` Pak ceiling).
- Loaded only when `osMemSize >= 0x800000` (Expansion Pak detected).
- Contents: heavy code (FatFs ~30 KB, OSK + file_browser, large practice
  features) AND code that depends on Pak-only memory regions, but **not**
  objects whose `.bss` is intercepted by `practice_pool_pak`,
  `practice_macro_pak`, or `practice_macro_snap_pak` (see "Pinned-BSS
  objects" below).
- The 768 KB budget reflects what's actually free in Pak RAM after the
  three pre-existing pak-pool segments. If a future feature needs more,
  the path is to relocate one of the existing pools (e.g., shrink
  `practice_pool_pak` if its 2.5 MB allocation is over-provisioned for
  the current snapshot count) rather than overflow into the cushion.

Both budgets are dual: a ROM-size check alone is insufficient because
NOLOAD BSS doesn't appear in ROM. A file with a 256 KB BSS array would
pass a ROM-only invariant yet punch through the RAM cap into `.buffers`,
re-creating the Aquas-class corruption that motivated parking
`practice_save_slotpool` BSS in `.practice_pool_pak` originally.

The two segments are mutually exclusive in content. Nothing in `_pak`
may be called when Pak is absent; the dispatch struct (next section) is
the only legal entry path.

### Pinned-BSS objects (special bucket)

Three existing objects have their `.bss` intercepted by Pak-only segments
defined before this work and must be preserved:

| Object | `.bss` lives at | Reason |
|---|---|---|
| `practice_save_slotpool.o` | `practice_pool_pak` (`0x80400000`, 2.5 MB) | Save snapshot pool — actual BSS extent is `0x280000` per the linker map (see "Confirmed sizes" above). The older "4 × 256 KB" rationale in the patcher's inline comment is stale. |
| `practice_macro_buf.o` | `practice_macro_pak` (`0x80680000`) | macro recording buffer |
| `practice_macro_snap.o` | `practice_macro_snap_pak` (`0x80691940`) | macro snapshot pool |

These objects' `.text/.data/.rodata` are tiny (the BSS is the whole
weight) and stay in `PRACTICE_MAIN_OBJS`. The existing pinned-pak
segments in the linker script intercept their `.bss` placement directly,
unchanged by this architecture. **They do not move into
`PRACTICE_LATE_PAK_OBJS`** — putting them there would route their BSS
into `.practice_late_pak_bss` at `0x80720000+`, breaking the address
contracts that `Practice_Save_ScratchBase()` and the macro pools depend
on. Reintroducing the Aquas crash class is the failure mode if this
boundary is missed.

## Boot Sequence and Late-Load Module

### Boot order

```
1. PIF / IPL2 / IPL3            stages first ~0xFD000 of ROM into RDRAM
2. boot.s entry                 stack, __osInitialize
3. libultra OS init             osCreatePiManager, threads, message queues
4. main() → Game_Initialize     vanilla state-machine setup
5. fox_game.c GSTATE_INIT       calls Practice_Init()
6. Practice_Late_Init()         NEW. First call inside Practice_Init.
7. iodev_detect, iodev_sd_init  safe; symbols are now RDRAM-resident
8. rest of Practice_Init        unchanged
```

`Practice_Late_Init` is the only legal entry point into the late-load
machinery. The orchestrator is six lines and reads top-to-bottom as a
narrative.

### Module layout

```
src/practice/late/
├── late.h          public API: Practice_Late_Init, gLateOps
├── ops.h           LateOps struct definition (function pointers + caps)
├── ops_tables.c    static const sLateOpsStub / Core / Full tables
├── stubs.c         clean-failure implementations for Pak-absent path
├── select.c        pure: SelectLateOps(memSize, pakLoaded) -> table*
└── loader.c        Practice_Late_Init orchestrator (six lines)

lib/test/
├── test_late_select.c   exercises SelectLateOps for all input combos
└── test_late_stubs.c    asserts each stub returns its documented sentinel
```

### The orchestrator

```c
void Practice_Late_Init(void) {
    LoadCoreSegment();
    /* Short-circuit: LoadPakSegment only fires when osMemSize qualifies,
     * so its DMA never runs on stock 4 MB carts. Order matters here. */
    bool pakLoaded = (osMemSize >= EXP_PAK_MEM_THRESHOLD) && LoadPakSegment();
    gLateOps = SelectLateOps(osMemSize, pakLoaded);
}

static void LoadCoreSegment(void) {
    Lib_DmaRead(SEGMENT_ROM_START(practice_late_core),
                SEGMENT_VRAM_START(practice_late_core),
                SEGMENT_ROM_SIZE(practice_late_core));
    bzero(practice_late_core_BSS_START,
          practice_late_core_BSS_END - practice_late_core_BSS_START);
}

static bool LoadPakSegment(void) {
    Lib_DmaRead(SEGMENT_ROM_START(practice_late_pak),
                SEGMENT_VRAM_START(practice_late_pak),
                SEGMENT_ROM_SIZE(practice_late_pak));
    bzero(practice_late_pak_BSS_START,
          practice_late_pak_BSS_END - practice_late_pak_BSS_START);
    return true;
}
```

The loader uses the `SEGMENT_*` macros from `include/sf64dma.h` for the
loaded segment's ROM/VRAM bounds. The BSS bounds come from explicit
linker symbols (`practice_late_core_BSS_START` / `_BSS_END`) declared in
`include/practice_late.h` because the BSS segment is a sibling
`(NOLOAD)` block under a different name (`.practice_late_core_bss`)
that the `DECLARE_SEGMENT` macro pattern doesn't fit cleanly.

`Practice_Late_Init` itself, `Lib_DmaRead`, `osMemSize`, `bzero`, and the
four pairs of segment-boundary symbols are all in `main`. The loader has
zero late-segment dependencies and is fully IPL-staged.

The explicit `bzero` of each segment's BSS is **required**, not optional.
The linker emits the late segments' BSS as `(NOLOAD)`, and `FILL(0)` in
the corresponding loaded section header does not zero a NOLOAD region —
NOLOAD bytes never appear in the ROM image and are never staged. Any
static initializer in late code that depends on zero-init will read
garbage if `bzero` is omitted. Vanilla `main_bss` zero-fill happens
earlier in boot via libultra and only covers main's BSS region, not the
late segments.

### Three dispatch tables

```c
static const LateOps sLateOpsStub      = { /* every fn = stub */ };
static const LateOps sLateOpsCoreOnly  = { /* SD prims real, FS = stub */ };
static const LateOps sLateOpsFull      = { /* every fn real */ };

const LateOps *gLateOps = &sLateOpsStub;  // pre-init default
```

`sLateOpsCoreOnly` is the stock-RAM cart's table — SD primitives resolve
to symbols in `.practice_late_core`, filesystem operations cleanly fail
through stubs. `sLateOpsFull` is the Pak cart's table — every function
points at a real implementation. `sLateOpsStub` is the pre-init default
(defense in depth; should never be reached at runtime).

The function pointers in the static tables live in `main`'s `.data` but
point at addresses in the late VRAM regions. The linker resolves these
addresses at link time; they're valid as soon as the loader has DMA'd the
respective segment into RAM.

### Selection is a pure function

```c
const LateOps *SelectLateOps(u32 memSize, bool pakLoaded) {
    if (memSize >= EXP_PAK_MEM_THRESHOLD && pakLoaded) return &sLateOpsFull;
    return &sLateOpsCoreOnly;
}
```

Pure logic, no N64 dependencies, host-testable. The "is this available"
decision lives in exactly one place.

`memSize` is technically redundant given the orchestrator already gates
`pakLoaded` on `memSize >= EXP_PAK_MEM_THRESHOLD`, so a future `pakLoaded`-only
signature would also be correct. Keeping both is defense in depth: if a
future test or fault-injection harness ever sets `pakLoaded = true` without
real Pak RAM, the table selection still refuses `sLateOpsFull` rather than
trusting the caller. The double-check costs one branch.

### LateOps struct shape

The struct is intentionally small — one entry per *operation class* the
late code exposes, not one per individual function. Initial shape:

```c
typedef struct LateOps {
    /* SD primitives — resolve to .practice_late_core in stock-cart builds. */
    iodev_result_t (*sd_init)(void);
    iodev_result_t (*sd_read_sectors)(uint32_t lba, uint32_t cnt, void *buf);
    iodev_result_t (*sd_write_sectors)(uint32_t lba, uint32_t cnt, const void *buf);

    /* FatFs — resolve to .practice_late_pak when Pak present, stubs otherwise. */
    int (*fs_mount)(void);
    int (*fs_open)(const char *path, int flags);
    int (*fs_close)(int fd);
    int (*fs_read)(int fd, void *buf, size_t n);
    int (*fs_write)(int fd, const void *buf, size_t n);

    /* UI — Pak-only. */
    int (*osk_open)(char *out_buf, size_t out_cap);
    int (*file_browser_open)(const char *root, char *out_path, size_t cap);

    /* Capability flags. Bool checks are cheaper than null-pointer compares
     * and let menus render correct labels without invoking a stub first. */
    bool sd_available;
    bool fs_available;
    bool ui_available;
} LateOps;
```

Roughly ~10 function pointers in v1. The struct grows as more
`_pak`-resident features are migrated. Each new entry is a one-line
addition to all three static tables (`sLateOpsStub` / `sLateOpsCoreOnly`
/ `sLateOpsFull`) — that's the "structural indirection" cost.

### Callers read straight through

```c
int rc = gLateOps->fs_open(path, FA_WRITE);
if (rc < 0) {
    ShowMessage("SD UNAVAILABLE");
    return;
}
```

No flag checks. If Pak is absent, `gLateOps->fs_open` is `StubFsFail` and
returns its sentinel; the caller branches on the return value, not on a
"is Pak loaded" flag. The UI layer translates sentinels to user-visible
messages in one place.

### Failure modes explicitly NOT handled

- **DMA timeout / cart read error**: synchronous PI DMA either completes
  or hangs the bus. There is no useful recovery from a corrupt late
  segment, and we have no working UI to report it. If `Lib_DmaRead`
  doesn't return successfully, the cart is dead.
- **Linker-emitted addresses misaligned**: caught at build time by the
  invariants below; never at runtime.
- **Symbol referenced before loader runs**: prevented by the
  `check_no_early_late_refs` invariant. If this slips through, hardware
  blue-screens and the player files an issue.

## File Routing Model and Bucket Decision Rule

Every `.c` file in `src/practice/` and `lib/` lands in exactly one of
three buckets, decided by one criterion.

### The rule

| Bucket | Goes here when |
|---|---|
| **`PRACTICE_MAIN_OBJS`** | Any symbol from this file is reachable from boot code before `Practice_Late_Init` returns. That means: `src/sys/`, `src/libultra/`, exception handlers, the `Lib_DmaRead` / `osMemSize` / `bzero` consumers, the loader itself, or any vanilla code path between `osCreatePiManager` and `fox_game.c:456`. |
| **`PRACTICE_LATE_CORE_OBJS`** | All callers are post-`Practice_Late_Init` AND the file works on stock 4 MB carts (no dependencies on Pak-only memory regions, no transitively-included FatFs/heavy deps that bust the 512 KB budget). |
| **`PRACTICE_LATE_PAK_OBJS`** | Same call-graph requirement as core, BUT either (a) too heavy to fit in core, (b) depends on Pak-only memory (`practice_pool_pak`, `practice_macro_pak`, `practice_macro_snap_pak`), or (c) only meaningful when Pak is present. |

### The audit procedure

For any candidate file `practice_<x>.c`:

1. Enumerate public symbols: `nm -g build/.../<x>.o | awk '/ T | D | R | B / {print $3}'`
2. For each symbol, grep for callers outside `src/practice/`, `lib/`,
   `lib/test/`. Any hit in `src/sys/`, `src/libultra/`, vanilla
   `src/engine/` boot paths → bucket = `MAIN`.
3. If clean: check pinned-BSS membership. Does the file currently appear
   in `practice_pool_pak`, `practice_macro_pak`, or
   `practice_macro_snap_pak` in the linker script? → bucket = `MAIN`
   (with .bss intercepted; see Pinned-BSS objects).
4. If clean: check Pak-only memory references in the .c (does the code
   read or write through `practice_pool_pak`-pinned globals owned by a
   Pinned-BSS object). Hit → `_pak`.
5. If still clean: assess transitive include weight. Heavy deps (FatFs,
   OSK textures, large rodata) → `_pak`. Otherwise → `_core`.

`practice_overlay.c` is a worked example: it reads `gDmaTable` to map
overlay VRAM ranges. Step 2 finds callers from save-subsystem code that
runs as part of `Practice_Init`'s setup, but more importantly the
function that reads `DmaEntry.vRomAddress` is documented in CLAUDE.md as
unsafe to call against ROM/physical addresses on hardware. Decision:
**stays in `MAIN`** — its access pattern to `gDmaTable` is part of the
boot-time-ish surface and the "is this safe" answer is fragile enough
that the audit should not move it.

The audit procedure above is also encoded in the `reclaim-rom-headroom`
skill at `.claude/skills/reclaim-rom-headroom/SKILL.md`.

### Default routing

`tools/patch_linker_script.py` defaults new `.c` files in `src/practice/`
and `lib/` to `_core` unless they're explicitly added to
`PRACTICE_MAIN_OBJS` or `PRACTICE_LATE_PAK_OBJS`. Any file in those trees
not classifiable is a build error. This is the structural change that
prevents the limit-hit from recurring: the path of least resistance for a
new feature is "land in `_core`."

### Initial bucket assignments

```
PRACTICE_MAIN_OBJS = [
    "practice_main",         # contains Practice_Init + Practice_Late_Init call
    "late/loader",           # the orchestrator itself
    "late/select",           # pure ops-table selector
    "late/ops_tables",       # static dispatch tables (.data in main)
    "late/stubs",            # stub implementations (Pak-absent path)
    "practice_overlay",      # reads gDmaTable; called by save subsystem early
                             # in Practice_Init's setup path. Documented gotcha
                             # in CLAUDE.md re: vRomAddress on hardware. Stays
                             # in main; not a candidate for migration.
    # Pinned-BSS objects: .text/.data/.rodata land in main; .bss is intercepted
    # by an existing Pak-only segment defined before this work. See "Pinned-BSS
    # objects" in Memory Layout. Moving these to LATE_PAK breaks the BSS pin.
    "practice_save_slotpool",  # .bss pinned at practice_pool_pak (0x80400000)
    "practice_macro_buf",      # .bss pinned at practice_macro_pak (0x80680000)
    "practice_macro_snap",     # .bss pinned at practice_macro_snap_pak (0x80691940)
]

PRACTICE_LATE_CORE_OBJS = [
    "lib/iodev/iodev", "lib/iodev/iodev_sc64", "lib/iodev/iodev_ed64",
    "lib/iodev/iodev_ed64_v1", "lib/iodev/iodev_ed64_v2", "lib/iodev/iodev_stub",
    "lib/sd_host/sd_host", "lib/sd_crc", "lib/serial",
    "lib/slot_manager", "lib/crc32",
    "practice_charge_shot", "practice_input_display", "practice_hud",
    "practice_input", "practice_draw", "practice_level",
    "practice_menu", "practice_state", "practice_cheats",
    "practice_enemy_health", "practice_frame_advance",
    "practice_hitbox", "practice_minimap", "practice_freecam",
]

PRACTICE_LATE_PAK_OBJS = [
    "lib/fatfs/ff", "lib/fatfs/ffunicode", "lib/fatfs/ff_libc", "lib/fatfs/diskio",
    "lib/ui/osk", "lib/ui/file_browser",
    "practice_save",         # save logic; .bss is small and goes here
    "practice_macro",        # macro coordinator; .bss is small and goes here
    "practice_sd", "practice_test_fatfs",
    "practice_logo_tex", "practice_owl_tex",
    "practice_boss_test",
]
```

`practice_save` (the save coordinator) goes in `_pak` — its `.bss` is
small and Pak-resident already in spirit. `practice_save_slotpool` is
the *separate* object holding the giant snapshot pool BSS that must
stay pinned at `0x80400000`. Same split for `practice_macro` (logic in
`_pak`) vs `practice_macro_buf` and `practice_macro_snap` (BSS-pinned in
existing pak segments, stay in main).

This is the Phase 3 endpoint, not the Phase 1 starting point. Phase 1
moves only `lib/iodev/*`, `lib/sd_host`, `lib/sd_crc`, `lib/serial`,
`lib/slot_manager`, `lib/crc32` into `_core`. Subsequent phases migrate
the rest.

## Linker Script and Patcher Tool Changes

### Linker script (regenerated by patcher)

`linker_scripts/us/rev1/starfox64.ld` is gitignored and rewritten by
`tools/patch_linker_script.py` on every build. Two new LOAD segments
are inserted between `.dma_table` and `.buffers`:

```ld
/* core: stock-RAM safe, every cart loads it */
practice_late_core_ROM_START = __romPos;
.practice_late_core 0x801F4000 : AT(practice_late_core_ROM_START) SUBALIGN(16)
{
    FILL(0x00000000);
    practice_late_core_VRAM_START = .;
    practice_late_core_TEXT_START = .;
    /* PRACTICE_LATE_CORE_OBJS .text in stable order */
    . = ALIGN(., 16);
    practice_late_core_TEXT_END = .;
    practice_late_core_DATA_START = .;
    /* PRACTICE_LATE_CORE_OBJS .data */
    practice_late_core_DATA_END = .;
    practice_late_core_RODATA_START = .;
    /* PRACTICE_LATE_CORE_OBJS .rodata */
    practice_late_core_RODATA_END = .;
}
.practice_late_core_bss (NOLOAD) : SUBALIGN(16)
{
    practice_late_core_BSS_START = .;
    /* PRACTICE_LATE_CORE_OBJS .bss */
    . = ALIGN(., 16);
    practice_late_core_BSS_END = .;
}
__romPos += SIZEOF(.practice_late_core);
practice_late_core_ROM_END = __romPos;

/* pak: only mounts when osMemSize >= 8 MB */
practice_late_pak_ROM_START = __romPos;
.practice_late_pak 0x80720000 : AT(practice_late_pak_ROM_START) SUBALIGN(16)
{
    /* same shape: TEXT, DATA, RODATA */
}
.practice_late_pak_bss (NOLOAD) : SUBALIGN(16) { /* BSS */ }
__romPos += SIZEOF(.practice_late_pak);
practice_late_pak_ROM_END = __romPos;
```

The shape mirrors existing overlays so downstream tools (`audit_ram_layout.py`,
the splat-aware map parser) require no changes.

The `FILL(0x00000000)` directive on the loaded `.practice_late_core`
section applies to padding bytes within the loaded image, not to the
separate `(NOLOAD)` BSS section. NOLOAD bytes are never staged from ROM,
so zero-init for the BSS region must come from the loader's explicit
`bzero` call (see Boot Sequence and Late-Load Module). The `FILL` is
retained because it matches the convention of every other vanilla
overlay segment in the script and prevents stale ROM bytes from leaking
into alignment padding.

### Patcher tool refactor

`tools/patch_linker_script.py` gains three explicit OBJS lists and a
default-routing classifier:

```python
PRACTICE_MAIN_OBJS = [...]        # boot-resident, hand-curated
PRACTICE_LATE_CORE_OBJS = [...]   # stock-RAM safe persistent overlay
PRACTICE_LATE_PAK_OBJS = [...]    # Pak-only persistent overlay

def classify_unlisted(c_path):
    """Files not in any explicit list default to LATE_CORE.
    Hard error if the file is in src/practice/ or lib/ and not classifiable."""
    if c_path.startswith("src/practice/") or c_path.startswith("lib/"):
        return "PRACTICE_LATE_CORE_OBJS"
    raise PatcherError(f"{c_path}: must be in MAIN, LATE_CORE, or LATE_PAK")
```

Auto-routing keeps the build green for casual file additions; a logged
warning makes the auto-routing visible so it can be promoted to an
explicit list entry in PR review.

### Header declarations

`include/practice_late.h` (new):

```c
DECLARE_SEGMENT(practice_late_core);
DECLARE_SEGMENT(practice_late_pak);

/* BSS lives in a sibling .practice_late_core_bss / .practice_late_pak_bss
 * (NOLOAD) section. DECLARE_SEGMENT only emits the loaded-segment
 * bounds, so BSS bounds are declared explicitly. */
extern u8 practice_late_core_BSS_START[];
extern u8 practice_late_core_BSS_END[];
extern u8 practice_late_pak_BSS_START[];
extern u8 practice_late_pak_BSS_END[];
```

`DECLARE_SEGMENT` gives the loader access to
`practice_late_core_VRAM_START[]`, `_ROM_START[]`, `_ROM_END[]`,
`_VRAM_END[]` via the existing `include/sf64dma.h` macros. The four
BSS-bound externs above complete the picture for the explicit `bzero`
in the loader.

### Build system

- `Makefile`: add `src/practice/late/*.c` to source discovery (one-line
  change to the existing `wildcard src/practice/*.c`).
- `lib/test/Makefile`: add `test_late_select`, `test_late_stubs` to the
  test target list. Pure-logic tests; no N64 toolchain.

### Worktree implications

`linker_scripts/us/rev1/starfox64.ld` must be **copied** (not symlinked)
into worktrees per existing CLAUDE.md guidance. The patcher idempotently
rewrites it before each build, so the only thing each worktree needs is
one initial copy from the main repo's already-patched version.

### Splat compatibility

The patcher's edits stay in a defined window between `.dma_table` and
`.buffers`. The splat-generated portion of the linker script (everything
before `.main` and after the last `.ovl_*`) is untouched. This preserves
compatibility with future splat regenerations of the base linker script.

## Tests and Invariants

### Static invariants (added to `tools/practice_invariants.py`)

| Check | What it catches |
|---|---|
| `check_late_default_routing()` | Every `.c` in `src/practice/` and `lib/` resolves to exactly one OBJS list — either explicitly listed or auto-classified by `classify_unlisted()` into `PRACTICE_LATE_CORE_OBJS` (see "Patcher tool refactor" above). The check prints a logged warning for auto-classified files (so they can be promoted to an explicit list entry in PR review) and only hard-errors when a file is in `src/practice/` or `lib/` but the classifier itself refuses to bucket it. New files outside those directories → build error. |
| `check_no_early_late_refs()` | No `.c` outside `src/practice/` and `lib/` references `_core` or `_pak` symbols. (Allowlist exception: `late/loader`, `late/select`, `late/ops_tables`, `late/stubs` may reference late symbols even though they live in `MAIN`.) Catches "I added a hook in fox_play.c that calls into a late-segment file" before it boot-hangs the cart. |
| `check_pak_only_calls_dispatched()` | Every reference to a `_pak`-resident symbol from outside the dispatch table is forbidden. Implemented via `mips-linux-gnu-objdump -dr` over each `_core` and `MAIN`-bucket `.o`, parsing relocations whose target symbol resolves to `_pak` and rejecting any whose source is not `late/ops_tables.o`. Cannot be done with grep alone — relocation-level analysis is the contract. |
| `check_no_pak_refs_from_core()` | `_core` code can't reference `_pak` code. Implemented same way as `check_pak_only_calls_dispatched`, restricted to `_core`-bucket `.o`s. One-way dependency: core works alone, pak depends on core. Reverse dependency means stock carts crash. |
| `check_late_segment_addresses()` | Linker map confirms `practice_late_core_VRAM == 0x801F4000` and `practice_late_pak_VRAM == 0x80720000` (start addresses). Catches accidental relocations from manual `.ld` edits. |
| `check_late_segment_ram_caps()` | `practice_late_core_BSS_END < 0x80274000` (preserves the 52 KB cushion before `.buffers`) and `practice_late_pak_BSS_END < 0x807E0000` (preserves a 128 KB cushion before the Pak ceiling at `0x80800000`). Distinct from the ROM-size budget — covers the BSS-blowout failure mode that motivated parking `practice_save_slotpool` in `practice_pool_pak` originally. |
| `check_late_segment_rom_budgets()` | `practice_late_core_ROM_SIZE < 0x80000` (512 KB). `practice_late_pak_ROM_SIZE < 0xC0000` (768 KB). Catches ROM-image growth before patcher / manifest churn. Distinct from the RAM cap above. |

The existing `check_boot_main_rom_budget` (the `0xFD000` check) stays
unchanged.

The two budget checks (`_ram_caps` and `_rom_budgets`) are
intentionally separate. ROM size and RAM extent diverge the moment a
file adds a large `.bss` array — and silently exceeding the RAM cap is
the same failure class as the Aquas-crash that motivated
`practice_pool_pak` (a hundreds-of-KB scratch allocated in normal BSS
clobbered runtime data). Belt and suspenders.

The two relocation-analysis checks (`_pak_only_calls_dispatched` and
`_no_pak_refs_from_core`) require a real toolchain pass over each `.o`,
not a source-level grep. The cost is one objdump invocation per object
during pre-commit (~hundreds of ms over the practice-tree, acceptable).
Grep cannot distinguish `gLateOps->fs_open(...)` (legal indirect call
whose pointer happens to land in `_pak`) from `fs_open(...)` (illegal
direct call) without parsing relocations.

### Unit tests (host-side)

- **`lib/test/test_late_select.c`**: exercises `SelectLateOps(memSize, pakLoaded)`
  for `{stock 4MB, pak 8MB} × {pakLoaded false, true}`. Asserts each
  combination returns the expected static table.
- **`lib/test/test_late_stubs.c`**: every stub function returns its
  documented sentinel value (`NULL`, `-1`, `false`).

Both compile against `lib_types.h` and run on the host (Linux/macOS) with
no N64 toolchain dependency.

### Functional tests (BizHawk Lua) — DEFERRED

Designed but not currently runnable in CI/local setup. Documented for
reference; will be wired in when BizHawk infrastructure is restored.

- **`tests/test_late_loader_stock.lua`**: boots ROM with 4 MB
  configuration. Asserts core segment text is RDRAM-resident,
  `gLateOps == sLateOpsCoreOnly`, SD primitive returns "no card present".
- **`tests/test_late_loader_pak.lua`**: same boot with 8 MB. Asserts
  `gLateOps == sLateOpsFull` and pak segment text matches expectations.
- **`tests/test_late_loader_no_blue_screen.lua`**: 600-frame post-boot
  framebuffer assertion. Catches the boot-hang regression class directly.

The `check_no_early_late_refs` static invariant covers the same boot-hang
failure class at build time, so functional tests are belt-and-suspenders
rather than the sole guard.

### Hardware verification

Manual checklist documented in `docs/superpowers/plans/HW_VERIFY_practice_late.md`:

1. Cold boot stock cart (no Pak): ROM enters game, no blue screen.
2. Open practice menu: confirms `LoadCoreSegment` succeeded.
3. `_core` features work (charge-shot timer, input display, hitbox toggle).
4. `_pak`-gated features show "Requires Expansion Pak" notice with no crash.
5. Cold boot Pak cart: Pak detected, full SD menu reachable.
6. SD save and SD load succeed: exercises `_pak` code via `gLateOps`.
7. IS-Viewer trace shows `[late] core loaded N bytes / pak loaded M bytes /
   pak detected=1` line from `Practice_Late_Init`.

### CI integration

- Pre-commit hook (`.git/hooks/pre-commit`) runs `practice_invariants.py`
  + `make practice -j4` on every commit. The six new invariants run
  automatically.
- Unit tests added to `lib/test/Makefile`'s default target; run as part of
  `make -C lib/test`.
- Functional tests gate on `BIZHAWK_PATH` env var (existing pattern in
  `tools/run_tests.py`); CI runs them when configured, skips gracefully
  otherwise.

## Phased Rollout

### Phase 1 — Core segment only (unblock the EverDrive commit)

Smallest possible change that gets `main_ROM_END` back under `0xFD000`.

**Phase 1 prerequisite (gate before any code change):**
1. Run `python3 tools/audit_ram_layout.py`. Confirm nothing currently
   resides at `0x801F4000`–`0x80274000`. If a previously-uncatalogued
   region claims any of that span, the architecture's stock-RAM target
   is wrong and addresses must be re-picked before proceeding.
2. Validate function-pointer-across-segments behavior with a single
   throwaway probe. In Phase 1's `loader.c`, behind a
   `#ifdef PRACTICE_LATE_PROBE` guard:
   ```c
   #ifdef PRACTICE_LATE_PROBE
       static void (*const sLateProbe[])(void) =
           { (void (*)(void))iodev_detect };
       sLateProbe[0]();
   #endif
   ```
   Build with `make practice PRACTICE_LATE_PROBE=1`, run on hardware /
   emulator, confirm boot reaches the practice menu without a fault.
   If the linker emits a relocation requiring runtime fixup, boot
   faults before the menu and the dispatch model needs rework before
   Phase 2. Once confirmed, the `#ifdef` block is **kept in the source
   tree** (the macro stays undefined by default) so the probe can be
   re-run on demand against future toolchain changes. No throwaway
   commit / branch needed.

**Phase 1 implementation:**
- New: `.practice_late_core` segment in linker script at `0x801F4000`.
- New: `src/practice/late/loader.c` with a minimal `Practice_Late_Init()`
  that DMAs core only and explicitly `bzero`'s its BSS.
- New: `include/practice_late.h` with `DECLARE_SEGMENT(practice_late_core)`.
- Move: `lib/iodev/*.o` (6 files), `lib/sd_host/sd_host.o`, `lib/sd_crc.o`,
  `lib/serial.o`, `lib/slot_manager.o`, `lib/crc32.o` from main → `_core`.
- No dispatch struct yet, no `_pak` segment, no Pak gating. The iodev
  call sites use direct `jal` calls (MIPS link-resolved within the
  256 MB segment, low risk), not function-pointer dispatch.
- Invariants added: `check_late_segment_addresses`,
  `check_late_segment_rom_budgets`, `check_late_segment_ram_caps`.
  Existing `check_boot_main_rom_budget` keeps running.
- One-line CLAUDE.md update: "ROM memory architecture under restructure
  — see `docs/superpowers/specs/2026-05-09-practice-rom-memory-architecture-design.md`.
  New `.c` in `src/practice/` or `lib/`: ask before adding to
  `PRACTICE_OBJS` (Phase 3 will rename it)."
- Verification: stock-cart cold boot + practice menu reachable + the
  function-pointer probe call succeeds.

**Expected headroom recovered (concrete):** the moved objects'
`.text+.data+.rodata` totals (per `size build/.../*.o`):
- `lib/iodev/iodev.o` ~1.0 KB; `iodev_sc64.o` ~1.5 KB; `iodev_ed64.o` ~3.5 KB;
  `iodev_ed64_v1.o` ~3.5 KB; `iodev_ed64_v2.o` ~4.5 KB; `iodev_stub.o` ~0.5 KB
- `sd_host.o` ~5 KB; `sd_crc.o` ~1 KB; `serial.o` ~1.5 KB;
  `slot_manager.o` ~10 KB; `crc32.o` ~1 KB
- Total: roughly **30–35 KB** moved out of `main`, recovering well above
  the 16,144 bytes needed and leaving `main` with **~14–19 KB of headroom
  under the cap** for incremental future work.

### Phase 2 — Pak segment + LateOps dispatch

The architectural lift.

- New: `.practice_late_pak` segment at `0x80720000`.
- New: `src/practice/late/{ops.h, ops_tables.c, stubs.c, select.c}`.
- Move: `lib/fatfs/*`, `lib/ui/*`, `practice_save` (logic), `practice_macro`
  (logic), `practice_sd`, `practice_test_fatfs`, `practice_logo_tex`,
  `practice_owl_tex`, `practice_boss_test` from main → `_pak`.
- **Do not move**: `practice_save_slotpool`, `practice_macro_buf`,
  `practice_macro_snap` — these are pinned-BSS objects whose `.bss` lives
  in the existing `practice_pool_pak` / `practice_macro_pak` /
  `practice_macro_snap_pak` segments at `0x80400000` / `0x80680000` /
  `0x80691940`. Moving them to `_pak` would re-route their `.bss` to
  `0x80720000+` and break `Practice_Save_ScratchBase()` and the macro
  pools. They stay in `PRACTICE_MAIN_OBJS`. (See "Pinned-BSS objects" in
  the Memory Layout section.)
- Refactor every caller of `_pak`-resident symbols to dispatch through
  `gLateOps->...`. The function-pointer-crossing-segments approach is
  already validated in Phase 1's probe.
- Add `osMemSize`-based Pak detection in `Practice_Late_Init`.
- Add UI gating in practice menu for Pak-required features (driven by
  `gLateOps->sd_available` / `fs_available` / `ui_available` capability
  flags).
- Invariants added: `check_pak_only_calls_dispatched`,
  `check_no_pak_refs_from_core` (both via `objdump -dr` relocation
  analysis as specified in Tests and Invariants).
- Verification: stock cart + Pak cart cold boot, SD save/load on Pak
  cart, Pak-absent stub behavior on stock cart, no regression in save
  snapshot behavior (the BSS-pinned `practice_save_slotpool` should
  function exactly as before).

### Phase 3 — Patcher refactor + bulk audit + default routing

Systemic enforcement so this never regresses.

- Rewrite `tools/patch_linker_script.py` with the three-bucket model and
  `classify_unlisted` default-router.
- Migrate the remaining medium-weight practice features from main → core
  (charge_shot, hud, hitbox, freecam, input_display, frame_advance,
  enemy_health, minimap, draw, level, menu, state, cheats, input).
- Add `check_late_default_routing` and `check_no_early_late_refs`
  invariants.
- Update CLAUDE.md "Adding a new practice source file" checklist to
  reflect default routing.
- Update the `new-feature` skill to default-route to `_core`.
- Verification: full invariant pass; hardware smoke run.

### Phase 4 — Reserved for future on-demand overlays

If any single feature ever exceeds its segment budget, evaluate whether
an on-demand swap-in (using the existing shared overlay slot at
`0x801AB620`) is worth the state-machine cost. Not in scope right now;
flagged so future contributors know the path.

### Release / patcher / manifest impact

Phase boundaries and **public release boundaries are independent**.
Internal phases that don't ship to users don't churn the patcher or
manifest. Only versions that go out as a release regenerate:

- The bps patch (`tools/patcher/src/assets/sf64-practice-vX.Y.Z.bps`).
- `tools/patcher/src/assets/manifest.json` — `patch.fileName`,
  `patch.size`, `patch.sha256`, `target.size`, `target.sha256`, version
  string.

Old bps files for prior versions stay valid for users patching the same
base ROM; they don't gain the new architecture.

**Versioning guidance (not a strict rule):**
- The first public release that includes Phase 1's architectural
  scaffolding bumps the minor version (tentatively **v0.3.0**) — it's
  worth signaling that the cart-RAM model changed.
- Subsequent phases that ship together with new user-visible features
  bump the patch version normally.
- Phases that land internally (no user-facing change) stay on the same
  version; the release ships when there's something for users.

The `-everdrive` suffix is reserved for branch-specific EverDrive
variants; the architecture work targets the base series.

### Rollback plan

- **Phase 1**: revert the linker scaffolding + loader; iodev/sd_host
  objects move back to main. Only viable if we have headroom — likely we
  won't (that's why Phase 1 exists). Realistic rollback: don't merge
  Phase 1 unless we have something else to free 16+ KB.
- **Phase 2**: revert the dispatch refactor + `_pak` segment; restore
  direct calls to FatFs/OSK/etc. (which move back to main but blow the
  budget — same caveat). The important rollback unit is per-feature: if
  dispatch breaks one feature, restore that feature's direct call and
  leave the segment infrastructure in place.
- **Phase 3**: per-file. If a migrated file misbehaves, move its `.o`
  back to `PRACTICE_MAIN_OBJS` (until budget pressure forces another
  solution).

## Risks

- **The stock-RAM gap I'm claiming for `_core` may have hidden
  occupants I haven't enumerated.** *Treat this as a Phase 1
  prerequisite, not a Phase 1 task.* Run `tools/audit_ram_layout.py`
  against the current map and verify nothing lives in
  `0x801F4000–0x80274000` before any code is written. Five-minute
  check that gates the entire architecture; if it finds an occupant,
  the addresses must be re-picked before proceeding.
- **Linker-resolved function pointers may behave unexpectedly across
  segments.** The static dispatch tables in `main`'s `.data` reference
  symbols in late VRAM. If the linker emits relocations that need
  runtime fixup (rather than static absolute addresses), the dispatch
  call sites would reference a not-yet-fixed-up pointer. Mitigation:
  Phase 1's prerequisite includes a function-pointer probe — declare a
  single `static const fn_ptr_t [] = { iodev_detect }` table in
  `loader.c`, call through it after the loader runs. If that succeeds,
  Phase 2's full dispatch model is sound. *Direct calls (MIPS `jal`)
  resolve at link time and are not the risky path; only data-resident
  function pointers are.*
- **Phase 3's medium-feature audit is volume-heavy and easy to get
  wrong.** Each migrated file is a potential boot-hang if the audit
  misses a boot-time reference. Mitigation: the
  `check_no_early_late_refs` invariant added in Phase 3 is the safety
  net; it catches what the human audit misses.
- **The 768 KB `_pak` budget may eventually be tight if we add more
  features.** Mitigation: budgets are easy to grow within the available
  Pak RAM; the cap is just a tripwire, not a hard ceiling.

## Open Questions

All previously-listed open questions are resolved; section retained as a
landing zone for new questions surfaced during implementation.

*Resolved during spec review:*
- BSS zero-init: explicit `bzero` in the loader is required, not
  optional. `FILL(0)` on a `(NOLOAD)` section is a no-op (NOLOAD bytes
  never enter the ROM image). Captured in Boot Sequence section.
- IS-Viewer print buffer location: stays in main. `osSyncPrintf` runs
  before `Practice_Late_Init` (it's the diagnostic channel for boot
  itself), so its buffer must be IPL-staged.
- Loader DMA failure UI: no recovery path. Synchronous PI DMA either
  completes or hangs the bus; we have no working UI to report a failure
  during boot. The cart is dead in either case. Documented for future
  revisit if real hardware ever surfaces partial DMA failures, but no
  in-scope work.

## References

- `tools/practice_invariants.py` — boot budget, late-segment invariants
- `tools/patch_linker_script.py` — patcher tool
- `tools/audit_ram_layout.py` — RAM overlap detection
- `.claude/skills/reclaim-rom-headroom/SKILL.md` — operational playbook
- `.claude/skills/debug-ram-layout/SKILL.md` — RAM-overlap debugging
- `src/sys/sys_lib.c:104` — `Lib_DmaRead`, the DMA primitive
- `include/sf64dma.h` — `DECLARE_SEGMENT` macro pattern
- Boot-budget commit: `154dea2` "perf(practice): reclaim ROM headroom
  (5,552 -> 6,240 bytes)"
- Triggering branch: `everdrive-saving` — the EverDrive iodev + sd_host
  work that exceeded `0xFD000` and prompted this design
