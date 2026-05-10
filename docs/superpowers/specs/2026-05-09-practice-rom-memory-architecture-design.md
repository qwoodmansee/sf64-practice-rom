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
0x80400000  .practice_pool_pak  (existing; save snapshots, NOLOAD BSS)
0x80500000  practice_late_pak_VRAM  (NEW. 1.5 MB budget cap.)
0x80680000  practice_macro_pak  (existing)
0x80691940  practice_macro_snap_pak  (existing)
            ... up to 0x80800000 (Pak ceiling)
```

### Segment definitions

**`.practice_late_core`** — stock-RAM safe persistent overlay.
- RAM target: `0x801F4000` (16-byte aligned, above
  `ovl_menu_BSS_END = 0x801F31E0` so no overlay swap can clobber it).
- ROM target: just after `dma_table_ROM_END`.
- Budget: 512 KB (`practice_late_core_ROM_SIZE < 0x80000`).
- Loaded by every build, every cart, every player.
- Contents: every `.c` whose call graph is fully post-`Practice_Late_Init`
  AND that runs correctly on stock 4 MB carts (no Pak-only memory deps).

**`.practice_late_pak`** — Pak-only persistent overlay.
- RAM target: `0x80500000` (above existing `practice_pool_pak`).
- ROM target: just after `practice_late_core_ROM_END`.
- Budget: 1.5 MB (`practice_late_pak_ROM_SIZE < 0x180000`).
- Loaded only when `osMemSize >= 0x800000` (Expansion Pak detected).
- Contents: heavy code (FatFs ~30 KB, OSK + file_browser, large practice
  features) AND code that depends on Pak-only memory regions.

The two segments are mutually exclusive in content. Nothing in `_pak`
may be called when Pak is absent; the dispatch struct (next section) is
the only legal entry path.

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
    bool pakLoaded = (osMemSize >= EXP_PAK_MEM_THRESHOLD) && LoadPakSegment();
    gLateOps = SelectLateOps(osMemSize, pakLoaded);
}

static void LoadCoreSegment(void) {
    Lib_DmaRead(practice_late_core_ROM_START,
                practice_late_core_VRAM_START,
                practice_late_core_ROM_SIZE);
    bzero(practice_late_core_BSS_START, practice_late_core_BSS_SIZE);
}

static bool LoadPakSegment(void) {
    Lib_DmaRead(practice_late_pak_ROM_START,
                practice_late_pak_VRAM_START,
                practice_late_pak_ROM_SIZE);
    bzero(practice_late_pak_BSS_START, practice_late_pak_BSS_SIZE);
    return true;
}
```

`Practice_Late_Init` itself, `Lib_DmaRead`, `osMemSize`, `bzero`, and the
four pairs of segment-boundary symbols are all in `main`. The loader has
zero late-segment dependencies and is fully IPL-staged.

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
3. If clean: check Pak-only memory references. Hit on `practice_pool_pak`
   / `practice_macro_pak` / `practice_macro_snap_pak` → `_pak`.
4. If still clean: assess transitive include weight. Heavy deps (FatFs,
   OSK textures, large rodata) → `_pak`. Otherwise → `_core`.

The same procedure is encoded in the `reclaim-rom-headroom` skill at
`.claude/skills/reclaim-rom-headroom/SKILL.md`.

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
    "practice_main",      # contains Practice_Init + Practice_Late_Init call
    "late/loader",        # the orchestrator itself
    "late/select",        # pure ops-table selector
    "late/ops_tables",    # static dispatch tables (data lives in main)
    "late/stubs",         # stub implementations
    "practice_overlay",   # touches gDmaTable; verify boot-time at audit
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
    "practice_save", "practice_save_slotpool",
    "practice_macro", "practice_macro_buf", "practice_macro_snap",
    "practice_sd", "practice_test_fatfs",
    "practice_logo_tex", "practice_owl_tex",
    "practice_boss_test",
]
```

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
.practice_late_pak 0x80500000 : AT(practice_late_pak_ROM_START) SUBALIGN(16)
{
    /* same shape: TEXT, DATA, RODATA */
}
.practice_late_pak_bss (NOLOAD) : SUBALIGN(16) { /* BSS */ }
__romPos += SIZEOF(.practice_late_pak);
practice_late_pak_ROM_END = __romPos;
```

The shape mirrors existing overlays so downstream tools (`audit_ram_layout.py`,
the splat-aware map parser) require no changes.

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
```

That gives the loader access to `practice_late_core_VRAM_START[]`,
`_ROM_START[]`, `_ROM_END[]`, `_BSS_START[]`, `_BSS_END[]` via the existing
`include/sf64dma.h` macros.

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
| `check_late_default_routing()` | Every `.c` in `src/practice/` and `lib/` is in exactly one OBJS list. New file added without classification → build error. |
| `check_no_early_late_refs()` | No symbol from `_core` or `_pak` is referenced by any `.c` in `src/sys/`, `src/libultra/`, or the `Game_Initialize`-reachable subset of `src/engine/`. Catches "I added a hook in fox_play.c that calls into a late-segment file" before it boot-hangs the cart. |
| `check_pak_only_calls_dispatched()` | Every call site of a `_pak`-resident symbol goes through `gLateOps->...`, never a direct call. Direct call → build error with file + symbol + caller. |
| `check_no_pak_refs_from_core()` | `_core` code can't call `_pak` code. One-way dependency: core works alone, pak depends on core. Reverse dependency means stock carts crash. |
| `check_late_segment_addresses()` | Linker map confirms `practice_late_core_VRAM == 0x801F4000` and `practice_late_pak_VRAM == 0x80500000`. Catches accidental relocations from manual `.ld` edits. |
| `check_late_segment_budgets()` | `practice_late_core_ROM_SIZE < 0x80000` (512 KB). `practice_late_pak_ROM_SIZE < 0x180000` (1.5 MB). Hits before runtime failures. |

The existing `check_boot_main_rom_budget` (the `0xFD000` check) stays
unchanged.

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

- New: `.practice_late_core` segment in linker script at `0x801F4000`.
- New: `src/practice/late/loader.c` with a minimal `Practice_Late_Init()`
  that DMAs core only.
- Move: `lib/iodev/*.o`, `lib/sd_host/sd_host.o`, `lib/sd_crc.o`,
  `lib/serial.o`, `lib/slot_manager.o`, `lib/crc32.o` from main → `_core`.
- No dispatch struct yet, no `_pak` segment, no Pak gating —
  `iodev_detect()` etc. are direct calls (the linker resolves them to
  `_core` VRAM, which the loader has populated by the time the call fires).
- Invariants added: `check_late_segment_addresses`,
  `check_late_segment_budgets`. Existing `check_boot_main_rom_budget`
  keeps running.
- Verification: stock-cart cold boot + practice menu reachable.
- Expected headroom recovered: 16 KB+, leaves `main` with significant
  margin.

### Phase 2 — Pak segment + LateOps dispatch

The architectural lift.

- New: `.practice_late_pak` segment at `0x80500000`.
- New: `src/practice/late/{ops.h, ops_tables.c, stubs.c, select.c}`.
- Move: `lib/fatfs/*`, `lib/ui/*`, `practice_save*`, `practice_macro*`,
  `practice_sd`, `practice_test_fatfs`, `practice_logo_tex`,
  `practice_owl_tex`, `practice_boss_test` from main → `_pak`.
- Refactor every caller of `_pak`-resident symbols to dispatch through
  `gLateOps->...`.
- Add `osMemSize`-based Pak detection in `Practice_Late_Init`.
- Add UI gating in practice menu for Pak-required features.
- Invariants added: `check_pak_only_calls_dispatched`,
  `check_no_pak_refs_from_core`.
- Verification: stock cart + Pak cart cold boot, SD save/load on Pak
  cart, Pak-absent stub behavior on stock cart.

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

Every phase that ships changes the ROM byte layout:

- Regenerate the bps patch
  (`tools/patcher/src/assets/sf64-practice-vX.Y.Z.bps`).
- Update `tools/patcher/src/assets/manifest.json` — `patch.fileName`,
  `patch.size`, `patch.sha256`, `target.size`, `target.sha256`, version
  string.
- Old bps files for prior versions stay valid for users patching the same
  base ROM; they don't gain the new architecture.
- Recommend bumping minor version to **v0.3.0** at the end of Phase 1
  (architectural change worth signaling) and incrementing patch version
  per phase thereafter.
- The `-everdrive` suffix is reserved for branch-specific EverDrive
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

- **The 60 KB stock-RAM gap I'm claiming for `_core` may have hidden
  occupants I haven't enumerated.** Every gap I see in the linker map
  could be reserved by something I missed. Phase 1 starts with verifying
  no occupant exists at `0x801F4000`–`0x80274000`.
- **Linker-resolved function pointers may behave unexpectedly across
  segments.** The static dispatch tables in `main`'s `.data` reference
  symbols in late VRAM. If the linker emits relocations that need
  runtime fixup (rather than static absolute addresses), boot will
  reference a not-yet-fixed-up pointer. Mitigation: verify in Phase 1
  with a single function pointer crossing segments before doing the
  full dispatch refactor in Phase 2.
- **`practice_overlay.c`'s read of `gDmaTable` may run earlier than I
  think.** If it runs during boot before `Practice_Late_Init`, then any
  symbol it references must be in main. The audit in Phase 1 verifies
  this.
- **Phase 3's medium-feature audit is volume-heavy and easy to get
  wrong.** Each migrated file is a potential boot-hang if the audit
  misses a boot-time reference. Mitigation: the
  `check_no_early_late_refs` invariant added in Phase 3 is the safety
  net; it catches what the human audit misses.
- **The 1.5 MB `_pak` budget may eventually be tight if we add more
  features.** Mitigation: budgets are easy to grow within the available
  Pak RAM; the cap is just a tripwire, not a hard ceiling.

## Open Questions

- Should `_core` pre-zero its BSS via FILL(0) in the linker (as the
  current overlay segments do) AND the explicit `bzero` in the loader,
  or just one of those? Probably explicit `bzero` is sufficient and the
  FILL is redundant; verify in Phase 1.
- Does the IS-Viewer print buffer need to be in `_core`, or can it stay
  in main? `osSyncPrintf` runs very early; the buffer should stay in
  main for safety.
- Should the loader signal load failure to the user UI, or just hard-hang
  on DMA failure? Current design: hard-hang (no useful recovery path).
  Worth revisiting if real hardware ever surfaces partial DMA failures.

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
