---
name: reclaim-rom-headroom
description: |
  Use when the SF64 practice ROM build trips `check_boot_main_rom_budget`
  ("main_ROM_END 0x... exceeds boot-safe limit 0xFD000"), when a recent commit
  has the cart blue-screening or hanging at boot, or proactively when
  `main_ROM_END` is within ~4 KB of the 0xFD000 ceiling and you're about to
  land more code. Walks the canonical migration playbook: diagnose current
  headroom from the linker map, identify candidate `.o` files in main, audit
  each for boot-time references, and move qualifying files into
  `.practice_late_core` (stock 4 MB safe) or `.practice_late_pak` (Expansion
  Pak only). Pairs with the multi-segment memory architecture in
  `docs/superpowers/specs/2026-05-09-practice-rom-memory-architecture-design.md`.

  Do NOT use for: runtime memory corruption, garbage textures, or BSS-overlap
  symptoms — those belong to the `debug-ram-layout` skill, which handles the
  RAM-side overlap problem. This skill is strictly for the ROM-size cliff.
when-to-use:
  - "make practice fails on `check_boot_main_rom_budget`"
  - "ROM hangs on solid blue screen at boot after recent commits"
  - "main_ROM_END is within ~4 KB of 0xFD000"
  - "Adding a new feature that grows main .text/.data/.rodata"
  - "Reviewing a PR that bumps main_ROM_END close to the cap"
arguments:
  - name: file
    description: |
      Optional. A specific `.c` filename (no extension) to audit and migrate
      (e.g. `practice_macro_buf`). If omitted, the skill walks the full
      candidate identification step and lets you pick.
    required: false
related:
  - debug-ram-layout (RAM-overlap problems, not ROM-size problems)
  - new-feature (default-routes new files; this skill is the relief valve)
  - check (runs the boot-budget invariant)
authoritative-spec: docs/superpowers/specs/2026-05-09-practice-rom-memory-architecture-design.md
---

## Why this skill exists

The N64 IPL stages roughly the first ~1 MB of cart ROM into RDRAM at boot.
Anything in `main` past that line is **not in RDRAM** when boot threads start;
calling into it deadlocks on a solid blue screen. The empirical ceiling for
this ROM is `main_ROM_END < 0xFD000`, enforced by
`check_boot_main_rom_budget` in `tools/practice_invariants.py`.

The ceiling itself can't easily move (replacing IPL3 is a project, not a
patch). The relief valve is structural: keep `main` slim, and put practice
code in late segments that DMA in once at runtime, after IPL has finished.

## When to use

- `make practice` or the pre-commit hook fails with
  `main_ROM_END 0x... exceeds boot-safe limit 0xFD000`.
- Real hardware boot blue-screens or hangs after a recent commit.
- `main_ROM_END` is within ~4 KB of `0xFD000` (less than ~1 average `.c`
  file's worth) and you're about to land more code.
- Reviewing a PR whose diff would bump `main_ROM_END` close to the cap.

## When NOT to use

- Garbage textures, invisible HUD text, scrambled overlays, "level-select
  looks wrong on first boot but recovers after playing a level" — that's the
  RAM overlap problem, see `debug-ram-layout`.
- A feature that needs to be boot-resident (called from `Game_Initialize` or
  earlier, exception handlers, libultra). Those have to stay in main; the
  answer is "shrink, don't move."
- Vanilla file size growth (audio bank, level assets). Those live in their
  own segments past `main_ROM_END` already.

## Quick workflow

1. **Diagnose** — `make practice -j4` and read the budget error, OR
   `python3 tools/audit_ram_layout.py` for current `main_ROM_END` vs cap.
2. **Pick a candidate file** — sort main `.o`s by size, intersect with files
   provably never called before `Practice_Init`'s loader stub.
3. **Audit the candidate** — grep every public symbol; confirm no caller in
   `src/sys/`, `src/libultra/`, vanilla `src/engine/` boot paths, or any
   `Game_Initialize`-reachable code.
4. **Migrate** — move the entry from `PRACTICE_OBJS` (or `LIB_*_OBJS`) to
   `PRACTICE_LATE_CORE_OBJS` or `PRACTICE_LATE_PAK_OBJS` in
   `tools/patch_linker_script.py`. Re-run the patcher.
5. **Verify** — `make practice -j4`, run `python3 tools/practice_invariants.py`,
   then run a hardware/IS-Viewer smoke test (boot, enter practice menu,
   exercise the migrated feature).

## Step 1 — Diagnose

```bash
# Current main_ROM_END from latest build:
grep -E "main_ROM_END\s*=" build/starfox64.us.rev1.map

# Or run the invariant standalone:
python3 tools/practice_invariants.py 2>&1 | grep -A1 boot_main_rom_budget

# Or the full layout (includes free-RAM gaps + ROM-cap delta):
python3 tools/audit_ram_layout.py
```

Read off three numbers:
- Current `main_ROM_END` (e.g. `0x100F10`)
- Cap (`0xFD000`)
- Delta (over or under, in bytes)

A **negative delta** (over the cap) blocks commits and bricks boot. A
delta of `< 0x1000` (under 4 KB) is in the danger zone for any new feature.

## Step 2 — Pick a candidate file

Goal: a `.c` whose `.o` contributes meaningfully to main and whose symbols
are provably untouched until `Practice_Init` runs.

```bash
# Largest .o files currently linked into main (descending by size):
grep -E "build/(lib|src/practice)/.+\.o" build/starfox64.us.rev1.map \
  | grep -oE "build/[^[:space:]]+\.o" | sort -u \
  | xargs -I{} sh -c 'test -f "{}" && printf "%s\t%s\n" "$(stat -f %z "{}")" "{}"' \
  | sort -rn | head -20
```

Filter that list against three rules:

1. **Not boot-resident.** No symbol from this file may be referenced from
   `src/sys/`, `src/libultra/`, exception handlers, the DMA loader stub, or
   any code path between `osCreatePiManager` and the first call to
   `Practice_Init` in `fox_game.c`.
2. **Not Pak-strict if going to `_core`.** Anything in `.practice_late_core`
   must work on stock 4 MB carts. If the file uses Pak-only memory regions
   (e.g. references `practice_pool_pak_BSS_START`), it routes to `_pak`.
3. **No early static init dependencies.** C has no constructors, but if the
   file's globals are read by another translation unit during `main_bss`
   zero-fill or before the loader runs, you have a problem.

Tip: practice features that only run inside the practice menu (macro recorder,
freecam UI, hitbox overlay, file browser, OSK, FatFs glue) are almost always
safe candidates.

## Step 3 — Audit a candidate

For a chosen file `practice_<foo>.c`:

```bash
# Every public symbol in the .o:
nm -g build/src/practice/practice_<foo>.o 2>/dev/null \
  | awk '/ T | D | R | B / {print $3}'

# For each symbol, find every caller outside the file itself and the
# practice menu surface:
SYM=Practice_Foo_Update
grep -rn "\b${SYM}\b" --include="*.c" --include="*.h" src/ lib/ include/ \
  | grep -v "src/practice/\|lib/" | head -20
```

A clean audit shows zero hits in `src/sys/`, `src/libultra/`,
`src/engine/fox_game.c` boot paths, or any `Game_Initialize`-reachable code.
If a hit exists, the file is not a migration candidate; pick another.

Special cases worth checking:
- `osSyncPrintf` / IS-Viewer paths — those run very early; do not migrate.
- DMA loader symbols themselves (the loader for `.practice_late_*`) — those
  must be in main, period.
- `practice_overlay.c` — touches `gDmaTable` early; likely stays in main.

## Step 4 — Migrate

Edit `tools/patch_linker_script.py`:

```python
PRACTICE_OBJS = [
    "practice_main",
    # ...
    # remove "practice_<foo>" from here
]

PRACTICE_LATE_CORE_OBJS = [
    # add "practice_<foo>" here for stock-RAM-safe code
]

PRACTICE_LATE_PAK_OBJS = [
    # or add it here for Pak-only code
]
```

Re-run the patcher (the Makefile invokes it automatically before linking,
but you can run it explicitly):

```bash
python3 tools/patch_linker_script.py
```

The patcher rewrites `linker_scripts/us/rev1/starfox64.ld` so the file's
`.text/.data/.rodata/.bss` move into the new segment. The default-routing
invariant in `practice_invariants.py` flags any `src/practice/*.c` or
`lib/*.c` not in any of the three lists.

## Step 5 — Verify

```bash
# Clean build to make sure no stale .o pulls the file into main:
rm -rf build/ && make practice -j4

# Re-run all invariants:
python3 tools/practice_invariants.py

# Re-run the layout audit:
python3 tools/audit_ram_layout.py
```

Expected outcome:
- `main_ROM_END` drops by approximately the file's `.text+.data+.rodata`
  size (BSS does not move ROM bytes).
- `practice_late_core_ROM_SIZE` (or `_pak_`) grows by the same.
- All invariants pass.

Then on real hardware (SC64 or EverDrive):
1. Reboot the cart and confirm the ROM starts (no blue screen).
2. Enter the practice menu — confirms the loader stub fired.
3. Exercise the migrated feature — confirms the late segment is live.
4. Watch the IS-Viewer / serial output for any crash frame.

If boot blue-screens after migration, you missed a boot-time reference; the
feature was not actually post-`Practice_Init`-only. Revert the migration and
re-audit step 3 with that hint.

## The hard ceiling — why we can't just raise `0xFD000`

The cap reflects what IPL3 (the boot ROM in the cartridge header) DMAs into
RDRAM before handing control to the game. The DMA size is set by IPL3
itself. Stock libultra IPL3 stages roughly 1 MB; a few homebrew toolchains
have hand-tuned IPL3 variants that stage more, but:

- IPL3 is signed by the CIC chip on real carts; replacing it requires
  matching the CIC variant or using a flashcart that bypasses CIC checks
  (most do).
- The IPL3 source isn't part of this repo; the boot blob is part of the
  base ROM.
- Even if we raised the staging size, the practical RDRAM ceiling still
  caps everything that has to be resident at boot — not infinite headroom.

Net: this skill is the right answer in 99% of cases. Raising IPL3 staging
is a separate phase that hasn't been scoped.

## Cross-references

- **Architecture spec**: `docs/superpowers/specs/2026-05-09-practice-rom-memory-architecture-design.md`
  — defines the segment layout, RAM addresses, loader contract, and
  enforcement invariants this skill operates against.
- **Adjacent skill**: `debug-ram-layout` — same `audit_ram_layout.py` tool,
  different problem (RAM overlap, not ROM size).
- **Default-routing rule** lives in `tools/practice_invariants.py` as
  `check_late_default_routing()` (added by the architecture spec).
- **Patcher integration**: every late-segment migration changes
  `main_ROM_END`, which changes the bps and `tools/patcher/src/assets/manifest.json`
  SHA256. Regenerate the bps as part of the same PR if you ship a release
  build alongside the migration.
