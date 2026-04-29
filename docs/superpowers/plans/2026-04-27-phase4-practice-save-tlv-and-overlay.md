# Phase 4 — practice_save TLV rewrite + practice_overlay + heap audit

> **Phase target:** TLV-based `practice_save.c`, new `practice_overlay.c`, real
> game-state save/load wired through `lib/slot_manager`, plus the heap audit
> that nails down `MAX_RAM_SLOTS_NO_PAK` / `MAX_RAM_SLOTS_WITH_PAK`.
> Cross-scene loading is **deferred to Phase 5**.

## 0. Status & input artefacts

### 0.0 Agent handoff — what shipped (successors read this)

The narrative in §0.1–0.3 below is **historical** (Wave 2.x sequencing and the
old low-RAM `.practice_pool` overlap). For **current** behaviour when extending
save/load or debugging hardware:

| Topic | Where / what |
|-------|----------------|
| RAM layout | Save **control globals** in `practice_save.c` `.main_bss`. **Slot TLV bytes** live in `practice_save_slotpool.c` (VMA `0x80400000`, Expansion Pak only). `Practice_Save_Init` gates on `osMemSize >= 0x00800000`. |
| HW verify procedure | `docs/superpowers/plans/HW_VERIFY_phase4.md` — updated for Pak pool, `./tools/sc64dev`, optional save/load with trace. |
| Silent save crashes | Never allocate `PracticeSnapshot` on the **game thread stack** — use static `gPracticeSaveScratch` in `practice_save.c`. |
| ISV bracketing | `make practice PRACTICE_SAVE_TRACE=1` — `[save_tr]` stages; skill `.claude/skills/practice-hw-isv-trace/SKILL.md`. |
| Hotkeys | `PSCREEN_GAMEPLAY` only — player must launch via **practice level select → A** (`Practice_LaunchLevel`). Heap can show `PLAY_UPDATE` while `gPracticeScreen` is still level-select if they used the vanilla map. |
| Flash helper | `./tools/sc64dev` from any repo subdir or worktree (`SF64_REPO_ROOT` if discovery fails). |

### 0.1 Wave-by-wave landed status

- Wave 1 (skeleton + scaffolding) — landed (`62ebae7`).
- Wave 2.1 (overlay region map + `LevelId` coverage invariant) — landed (`560effe`).
- Wave 2.2 (TLV `practice_save` + 2 RAM slots + BizHawk tests) — **landed but
  introduced two boot regressions**, see §0.3.
- Wave 2.3 (heap audit hooks + `HW_VERIFY_phase4` procedure) — landed (`e1c88ee`).
- Wave 2.4–6 (audit run, pin constants) — **blocked**, see §0.3.

### 0.2 Tooling that arrived during this phase

- `tools/audit_ram_layout.py` — parses the linker map, lists every BSS section,
  every `ovl_*` slot, the `.buffers` wall, and free gaps; flags any address-range
  overlap between practice-named sections and overlay/dynamic-load regions.
  Exits non-zero on overlap so it's CI-suitable (currently informational because
  Wave 6 owns the structural fix).
- `tools/practice_invariants.py` gained `check_practice_pool_no_overlay_overlap()`
  which calls into the audit module and emits a non-fatal warning per overlap.
- `.claude/skills/debug-ram-layout/SKILL.md` — agent-facing "boot looks fine but
  renders wrong" workflow that points at the audit tool.

### 0.3 Layout decision — Phase 2 committed

**Implementation today:** matches the **Expansion Pak** branch below via
`practice_save_slotpool.c` + `Practice_Save_Init`; stock stays disabled. See
**§0.0** for filenames and agent shortcuts — this subsection is the original
audit rationale.

**Finding:** Phase 1 audit shows stock 4 MB cannot accommodate a save-state pool above the dynamic load window. Titania setup 5 (worst case) reaches 0x8028a210, which is 37 KB above `buffers_VRAM` (0x80281000). Headroom = −53 KB.

**Decision:**
- **Stock 4 MB (osMemSize == 0x400000):** No same-scene save/load support in Phase 4.
  - `MAX_RAM_SLOTS_NO_PAK = 0` (feature gated at boot time if stock detected)
  - Optional: `Practice_SaveStateSlot` / `Practice_LoadStateSlot` early-return with
    IS-Viewer `osSyncPrintf` warning when stock memory is detected
- **Expansion Pak (osMemSize == 0x800000):** Place 4-slot pool above 0x80400000.
  - `MAX_STATE_SIZE = 0x40000` (256 KB per slot, proven safe)
  - `MAX_RAM_SLOTS_WITH_PAK = 4` (4 × 256 KB = 1 MB, easily fits above 0x80400000)
- **Overlay snapshots:** Keep enabled (`PRACTICE_SAVE_OVERLAY_SNAPSHOT = 1`).
  Titania overlay is ~120 KB, fits well within the 256 KB budget.

**Previous issues (fixed in Wave 2.2–2.3):**
- ✓ Boot-time `bzero(sSlotPool, ...)` removed; cold-boot BSS is zero.
- ✓ `ovl_menu` + `ast_text` no longer clobbered; level-select renders correctly.

### 0.4 Wave 6 inputs

The heap audit (`HW_VERIFY_phase4.md`) now also exists to give Wave 6 the data
to pick the right layout — not just to pin `MAX_STATE_SIZE`. Required outputs:

- Highest `ramPtr` reached by `Load_SceneFiles` for each saveable scene
  (overlay + 15 asset segments). This is the dynamic-load high-water-mark.
- Whether `0x80281000 - hwm` exceeds 256 KB on any scene (1-slot pool fits
  below `.buffers`).
- Whether Expansion Pak presence is detectable on the user's setup
  (`osMemSize == 0x800000`).

- Spec: `docs/superpowers/specs/2026-04-27-gz-style-features-design.md`
- Phase 3 completion: `docs/superpowers/plans/2026-04-27-phase3-tlv-slot-manager.md`
- Existing portable codec: `lib/serial.{c,h}`, `lib/slot_manager.{c,h}`
- Existing snapshot logic to replace: `src/practice/practice_save.c`
- In-ROM smoke test that initialises and tears down `slot_manager`:
  `src/practice/practice_slot_test.c` (kept; runs ahead of Phase 4 wiring)

Phase 4 does **not** touch:

- `iodev_*` (Phase 1/2 done)
- `lib/fatfs/*` (Phase 2 done)
- SD save/load entry points (still return `SLOT_MANAGER_ERR_UNSUPPORTED` —
  unchanged from Phase 3; Phase 7 wires them up)
- Cross-scene transition state machine (Phase 5)
- File browser / OSK (Phase 6)
- Watches / config persistence (Phases 8 / 9)

## 1. Decisions locked in for this phase

These were brainstormed up front; everything below assumes them.

| Decision | Choice |
|---|---|
| Slot storage | Static `.bss` pool sized at link time |
| Audio scope | Emit new audio TLV tags (spec packed, seq, voice) but **don't apply** spec on load — keeps Phase 4 same-scene behaviour identical to today's `Practice_LoadState` while making Phase 4 saves forward-compatible with Phase 5's full apply |
| Overlay build-id hash | CRC32-IEEE (poly 0xEDB88320) computed once at boot, cached per `ovl_iN` |
| Tag granularity for arrays | One TAG per whole array, payload is the raw bcopy bytes (matches existing snapshot strategy) |
| Heap-audit telemetry | IS-Viewer log via `osSyncPrintf` only; user runs through the 17 saveable scenes with the live deployer terminal open and pastes results into `HW_VERIFY_phase4.md` |
| Slot count exposed in Phase 4 | **2 slots**, slot cycle wired via `slot_manager_next_slot` / `slot_manager_prev_slot` (exercises the cycle path end-to-end; provisional value, may shrink after audit) |

## 2. Goal & exit criteria

**Goal**: rewrite `Practice_SaveState` / `Practice_LoadState` on top of the
TLV codec + RAM slot manager, prove it round-trips inside the same scene
with no regressions, and quantify how much .bss the slot pool can occupy.

**Exit criteria** (each is independently verifiable):

- [ ] `practice_save.c` no longer uses the bespoke `PracticeSnapshot` global;
      instead it uses `slot_manager_save_ram(slot)` /
      `slot_manager_load_ram(slot)` with the practice-side save/load callbacks.
- [ ] `practice_overlay.c` exists, returns the right `(vram_start, size)` for
      every saveable `LevelId`, and exposes a CRC32 `build_id` per `ovl_iN`
      that's stable across boots of the same ROM and changes when any byte in
      the segment changes.
- [ ] Same-scene save → restore on Corneria preserves: `gPlayer[0].pos`,
      `gPlayer[0].baseSpeed`, `gHitCount`, `gPathProgress`, all snapshot
      arrays — bit-for-bit equal to a baseline run, with no regressions vs.
      the existing snapshot behaviour.
- [ ] Save in non-saveable contexts (cutscene, menu, level-load transition,
      `gPlayer == NULL`, `LEVEL_INVALID`, `LEVEL_VERSUS`/`LEVEL_TRAINING`) is
      refused with an IS-Viewer log line and no game-state mutation.
- [ ] The slot manager's existing version/magic checks reject corrupt saves
      without crashing the game. (Phase 3 host tests already cover this; we
      add an in-ROM equivalent.)
- [ ] Static invariants pass, including new `check_overlay_table_complete`,
      `check_tag_registry`, `check_serializer_parity`,
      `check_state_version_defined_once`,
      `check_max_state_size_budget`, `check_phase4_engine_hooks`.
- [ ] Host unit tests pass: existing TLV / slot manager + new
      `test_practice_save_tags.c` (compile-time check that every tag has both
      a save and a load site, via the registry header).
- [ ] BizHawk functional test `test_state_save_load_same_scene.lua` passes
      (build, drop into Corneria, save, mutate state, load, assert state
      restored).
- [ ] BizHawk functional test `test_state_refuse_during_cutscene.lua` passes.
- [ ] BizHawk functional test `test_state_two_slot_cycle.lua` passes — saves
      different states into slots 0 and 1, cycles between them with the
      slot-cycle action, and asserts each load restores the matching state.
- [ ] `HW_VERIFY_phase4.md` exists with: heap-audit results table for all 17
      saveable scenes, refuse-context smoke test, slot cycle smoke test, and
      atomic-restore "load on bad save" smoke test.
- [ ] `MAX_RAM_SLOTS_NO_PAK` / `MAX_RAM_SLOTS_WITH_PAK` / `MAX_STATE_SIZE`
      constants in `practice_save_config.h` are pinned to the audited values
      and the static invariant `check_max_state_size_budget()` enforces them.

## 3. State payload sizing (what we know without running)

This drives `MAX_STATE_SIZE` and the slot pool sizing.

Snapshot arrays (sizes from `include/sf64*.h`):

| Array | Count | Element size | Bytes |
|---|---|---|---|
| `Player` | 4 | 0x4E0 | 4992 |
| `Actor` | 60 | 0x2F4 | 45 360 |
| `Boss` | 4 | 0x408 | 4128 |
| `Scenery` | 50 | 0x80 | 6400 |
| `Sprite` | 40 | 0x4C | 3040 |
| `Effect` | 100 | 0x8C | 14 000 |
| `Item` | 20 | 0x6C | 2160 |
| `PlayerShot` | 16 | 0x70 | 1792 |
| `TexturedLine` | 100 | 0x30 | 4800 |
| `RadarMark` | 65 | 0x28 | 2600 |
| `BonusText` | 10 | 0x1C | 280 |
| **Subtotal** | | | **89 552 ≈ 88 KB** |

Plus scalars (~50 × ≤ 8 B = ~400 B), ~80 TLV headers × 8 B = 640 B, slot
header 0x3C = 60 B. Snapshot non-overlay total ≈ 90 KB.

Overlay budget — the spec points us at the containing `ovl_iN` segment for
the active scene. We don't know the exact per-segment sizes statically (they
shift per build), but we'll measure all six during the heap audit by
inspecting `gDmaTable` at boot. Worst case observed in practice tools for
SF64 hovers around 80–120 KB per segment.

**Provisional**: `MAX_STATE_SIZE = 0x40000` (256 KB). Headroom is
`256 − 90 − 120 = 46 KB` for slop. The heap-audit step (§7) confirms or
trims this. Two slots @ 256 KB = 512 KB of `.bss` — significant on stock
4 MB but should fit; the audit decides.

The static invariant from the spec is restated here:

```
MAX_STATE_SIZE * MAX_RAM_SLOTS_NO_PAK   <= 1 048 576   (1 MB)
MAX_STATE_SIZE * MAX_RAM_SLOTS_WITH_PAK <= 2 621 440   (2.5 MB)
```

## 4. File-by-file work

### 4.1 New files

- `src/practice/practice_save.c` — **rewrite** (existing file, keeps name).
- `src/practice/practice_overlay.{c,h}` — new.
- `src/practice/practice_save_tags.h` — new (tag registry).
- `src/practice/practice_save_config.h` — new (size + slot constants;
  one place for `MAX_STATE_SIZE`, `RAM_SLOT_COUNT`, `STATE_VERSION`,
  `LIB_VERSION`).
- `lib/crc32.{c,h}` — new (host-portable CRC32-IEEE; tiny, no table or
  256-entry table built at first call). Goes under `lib/` because the
  overlay build-id check sits inside `practice_overlay.c` but the CRC
  primitive is generic.
- `lib/test/test_crc32.c` — host unit test (known-answer vectors).
- `tests/test_state_save_load_same_scene.lua`
- `tests/test_state_refuse_during_cutscene.lua`
- `tests/test_state_two_slot_cycle.lua`
- `docs/superpowers/plans/HW_VERIFY_phase4.md`

### 4.2 Modified files

- `include/practice.h` — add slot-cycle public API + new globals for
  diagnostics. Existing `Practice_SaveState`/`Practice_LoadState` /
  `Practice_HasCheckpoint`/`Practice_ClearCheckpoint` keep their signatures
  (they become thin wrappers that call into slot 0 by default). Add
  `Practice_SaveStateSlot(slot)` / `Practice_LoadStateSlot(slot)` and
  `Practice_GetActiveSlot()` / `Practice_CycleSlot(+/-1)`.
- `src/practice/practice_main.c` — call new `Practice_Save_Init()` from
  `Practice_Init()` AFTER `Practice_SlotTest_Run()` (the smoke test
  currently calls `slot_manager_init(0,0,...)` to reset; Phase 4 takes
  ownership from there). Add the heap-audit one-shot probe (also after).
- `src/practice/practice_state.c` — add slot indicator / slot cycle to
  the state menu (subtle UI; the spec menu wiring lands in Phase 5, so
  Phase 4 just adds a tiny indicator + L/R cycle in the gameplay HUD path
  — see §4.5).
- `tools/patch_linker_script.py` — add `practice_overlay` to
  `PRACTICE_OBJS` (after `practice_save`); add `crc32` to `LIB_TOP_OBJS`
  (after `slot_manager`). The patcher's "missing entry" pass handles
  partially-patched scripts automatically.
- `tools/extract_symbols.py` — add: `gPracticeActiveSlot`,
  `gPracticeSlotValid` (bitfield), `gPracticeLastSaveResult`,
  `gPracticeLastLoadResult`, `gPracticeMaxMemAllocHWM`,
  `gPracticeFreeRamLow`, plus `OVL_I1_VRAM`/`OVL_I1_VRAM_END`
  (and i2..i6) so Lua tests can sanity-check the overlay region map.
- `tools/practice_invariants.py` — add the new checks (§5).
- `lib/test/Makefile` — add `test_crc32` to `TESTS`.

### 4.3 Tag registry — `practice_save_tags.h`

Single source of truth for tag IDs. Includes a built-in mechanism for
the `check_tag_registry` and `check_serializer_parity` invariants.

```c
/* practice_save_tags.h */
#ifndef PRACTICE_SAVE_TAGS_H
#define PRACTICE_SAVE_TAGS_H

/* Stable, append-only. Removed entries become // REMOVED comments and
 * the tag ID is reserved forever. Static invariant enforces this. */
typedef enum {
    /* Header / overlay */
    TAG_LEVEL_ID            = 0x0001,
    TAG_LEVEL_PHASE         = 0x0002,
    TAG_OVERLAY_BUILD_ID    = 0x0003,
    TAG_OVERLAY_VRAM        = 0x0004,
    TAG_OVERLAY_BYTES       = 0x0005,
    TAG_SEGMENTS            = 0x0006,

    /* Audio (emit-only in Phase 4; Phase 5 turns on apply) */
    TAG_AUDIO_SEQ_ID        = 0x0010,
    TAG_AUDIO_SPEC_PACKED   = 0x0011,
    TAG_AUDIO_BANK_VOICE    = 0x0012, /* reserved; emitted as 0 in Phase 4 */

    /* Bulk arrays (per_array_blob granularity) */
    TAG_PLAYER_ARRAY        = 0x0020,
    TAG_ACTORS              = 0x0021,
    TAG_BOSSES              = 0x0022,
    TAG_SCENERY             = 0x0023,
    TAG_SPRITES             = 0x0024,
    TAG_EFFECTS             = 0x0025,
    TAG_ITEMS               = 0x0026,
    TAG_PLAYER_SHOTS        = 0x0027,
    TAG_TEXTURED_LINES      = 0x0028,
    TAG_RADAR_MARKS         = 0x0029,
    TAG_BONUS_TEXT          = 0x002A,

    /* Scalars (one tag each — these survive struct field reorders) */
    TAG_PATH_PROGRESS       = 0x0040,
    TAG_SAVED_PATH_PROGRESS = 0x0041,
    TAG_OBJECT_LOAD_INDEX   = 0x0042,
    /* ...full enumeration of every PracticeScalarState field... */
} practice_save_tag_t;
#endif
```

### 4.4 Save/load callbacks — TLV emission/decoding pattern

`practice_save.c` keeps its scope tight: define the save callback, the
load callback, the in-RAM `PracticeSnapshot` struct (still useful as a
scratch buffer to assemble state pre-emit), and the slot pool. Use macros
to shrink the per-tag boilerplate:

```c
#define SAVE_TAG_BLOB(w, tag, src) \
    serial_put_tag(w, tag, &(src), (uint32_t)sizeof(src))

#define LOAD_TAG_INTO(tag_id, dst) \
    case tag_id: \
        if (len != sizeof(dst)) { return -1; } \
        bcopy(data, &(dst), sizeof(dst)); \
        break;
```

The save callback walks `PracticeSnapshot` once, emitting tags. The load
callback initialises `PracticeSnapshot` to a default state, walks TLV
entries, applies known tags, ignores unknown tags, then performs an
**atomic apply step** at the end (only after the whole stream parses
cleanly does it copy the snapshot into the live globals — same reason
the existing code uses a `valid` flag).

If the parse hits `SERIAL_ERR_*`, the callback returns `-1` and the
slot manager bubbles it up as `SLOT_MANAGER_ERR_CALLBACK`. The
existing live game state is **not mutated** in that case.

### 4.5 Practice-side glue — slot pool, init, menu

`src/practice/practice_save.c` (selected new structure):

```c
#include "practice.h"
#include "slot_manager.h"
#include "serial.h"
#include "practice_save_config.h"
#include "practice_save_tags.h"
#include "practice_overlay.h"

/* Slot pool. Fixed .bss; one block per slot. */
static u8 sSlotPool[RAM_SLOT_COUNT * MAX_STATE_SIZE]
    __attribute__((aligned(8)));

s32 gPracticeActiveSlot;
s32 gPracticeSlotValidBits;     /* bit i set <=> slot i is valid */
s32 gPracticeLastSaveResult;    /* last slot_manager_save_ram() return */
s32 gPracticeLastLoadResult;    /* last slot_manager_load_ram() return */

/* practice_save callbacks (uint32_t/int return types match slot_manager). */
static uint32_t Practice_Save_Cb(void *buf, uint32_t buf_size);
static int       Practice_Load_Cb(const void *buf, uint32_t size);

void Practice_Save_Init(void) {
    slot_manager_init(STATE_VERSION, LIB_VERSION,
                      Practice_Save_Cb, Practice_Load_Cb,
                      RAM_SLOT_COUNT);
    slot_manager_set_ram_storage(sSlotPool,
                                 sizeof(sSlotPool),
                                 MAX_STATE_SIZE);
    gPracticeActiveSlot = 0;
    gPracticeSlotValidBits = 0;
    gPracticeLastSaveResult = 0;
    gPracticeLastLoadResult = 0;
}
```

`practice_main.c` Init order becomes:

```
Practice_SlotTest_Run();   /* Phase 3 smoke test; resets slot_manager */
Practice_Save_Init();      /* Phase 4 takes ownership */
Practice_HeapAudit_Boot(); /* Phase 4 §7; one-shot at boot */
```

`Practice_SaveState()` (existing API) is preserved as a thin wrapper:

```c
void Practice_SaveState(void) {
    if (!Practice_CanSaveHere()) {
        gPracticeLastSaveResult = SLOT_MANAGER_ERR_INVALID_SLOT; /* repurposed */
        osSyncPrintf("[save] refuse: not saveable here (level=%d state=%d)\n",
                     gCurrentLevel, gPlayState);
        return;
    }
    gPracticeLastSaveResult = slot_manager_save_ram(gPracticeActiveSlot);
    if (gPracticeLastSaveResult == SLOT_MANAGER_OK) {
        gPracticeSlotValidBits |= (1 << gPracticeActiveSlot);
    }
}
```

Same shape for `Practice_LoadState`. New public APIs (added to
`practice.h`):

```c
void Practice_Save_Init(void);
bool Practice_CanSaveHere(void);
void Practice_SaveStateSlot(s32 slot);
void Practice_LoadStateSlot(s32 slot);
s32  Practice_GetActiveSlot(void);
void Practice_CycleSlot(s32 delta);   /* delta = +1 or -1 */
```

Menu wiring for Phase 4 stays minimal: a single line in the gameplay HUD
shows "SLOT N (saved/empty)" and L/R while the practice menu is OPEN
cycles slots. Full slot picker UI lands in Phase 5. This keeps the
Phase 4 surface area honest — we're proving plumbing, not designing UX.

### 4.6 Overlay region map — `practice_overlay.{c,h}`

Public API (from spec §"`src/practice/practice_overlay.c`"):

```c
int      practice_overlay_get_region(LevelId id, void **vram, uint32_t *size);
bool     practice_overlay_is_saveable(LevelId id);
uint32_t practice_overlay_build_id(LevelId id);
void     practice_overlay_request_load(LevelId id, s32 phase);  /* Phase 5 */
```

Implementation strategy:

1. Static table `LevelId → SceneId` mirrored from `Load_SceneSetup` in
   `src/engine/fox_load.c`. One entry per saveable LevelId. The set is
   the 17 enums listed in the spec.
2. Static table `SceneId → ovl_iN segment index` mirrored from the
   `sOvliN_*` use-sites in the same file.
3. `practice_overlay_get_region` indexes `gDmaTable` at the `ovl_iN`
   slot to get `vRomAddress` (== VRAM in our build, since splat aligns
   them). The size is `SEGMENT_VRAM_SIZE(ovl_iN)`, computed from the
   linker-defined `ovl_iN_VRAM` / `ovl_iN_VRAM_END` symbols.
4. `practice_overlay_build_id`: lazy-computed CRC32-IEEE over the
   `ovl_iN` ROM bytes, cached in a static `u32 sBuildIds[6]`. We
   compute by direct read from KSEG0 (the segment is already DMA'd into
   RDRAM at boot — `Load_InitDmaAndMsg` runs in `Game_Init`). The CRC
   is not over actual ROM (we don't want a PI DMA on every save), it's
   over the in-RAM copy at the time of first request. That's still a
   build-stable value within a single boot, which is all we need: a
   load on the **same** boot's ROM matches; a load made by a different
   build (different commit, different layout) doesn't.
5. `practice_overlay_request_load` is **stubbed** in Phase 4. Its body
   logs and returns without touching `gNextLevel`. Phase 5 fills it in.
6. Exclusion table: `LEVEL_INVALID`, `LEVEL_TRAINING`, `LEVEL_VERSUS`,
   `LEVEL_WARP_ZONE`, `LEVEL_UNK_4`, `LEVEL_UNK_15`,
   `LEVEL_VENOM_ANDROSS` (the spec keeps Andross saveable; we'll mark
   it explicit in the table — Andross is an `ovl_i6` scene).

The static invariant `check_overlay_table_complete()` enforces every
`LevelId` enum value appears in **either** the saveable table or the
exclusion table; presence in neither fails the build.

### 4.7 Refuse rules — `Practice_CanSaveHere()`

Mirrors the spec's Edge Cases table for save-time refusals. Must return
false in any of:

- `gGameState != GSTATE_PLAY`
- `gPlayState != PLAY_UPDATE`
- `gPlayer == NULL`
- `gPlayer[0].state != PLAYERSTATE_ACTIVE`
- `practice_overlay_is_saveable(gCurrentLevel) == false`
- `gPracticeMenuState == PMENU_OPEN_FROZEN` and the menu has captured
  state for restore (deferred guard; Phase 4 just refuses outright if
  the menu is open via the existing `gPracticeMenuState != PMENU_CLOSED`)

Logging on refuse uses `osSyncPrintf` so the IS-Viewer terminal shows
why a save failed. The HUD also gets a brief on-screen indicator
("SAVE: REFUSED") via the existing `Practice_DrawText` path; we
intentionally keep this short — full UX polish is out of phase.

## 5. Static invariants (additions to `tools/practice_invariants.py`)

Grep-based, identical style to existing checks. Concrete additions:

```python
def check_overlay_table_complete():
    """Every LevelId enum value must appear in practice_overlay.c's
    saveable or exclusion table. Missing entries fail the build."""

def check_tag_registry():
    """Every TAG_* in practice_save_tags.h has a unique numeric value.
    Tags marked '// REMOVED' may not have a current numeric value
    matching any active TAG_*. The save+load callbacks each reference
    every active tag exactly once."""

def check_serializer_parity():
    """For every active TAG_* in practice_save_tags.h there is one
    SAVE_TAG/serial_put_tag call AND one case clause referencing that
    tag in practice_save.c."""

def check_state_version_defined_once():
    """STATE_VERSION (in practice_save_config.h) appears only there;
    not redefined anywhere else under src/practice or lib."""

def check_max_state_size_budget():
    """Read MAX_STATE_SIZE * RAM_SLOT_COUNT from practice_save_config.h.
    Assert <= 1 MB. (When the audit lands, we tighten to the audited
    value.)"""

def check_phase4_engine_hooks():
    """Practice_Save_Init must be called from Practice_Init in
    practice_main.c, and AFTER Practice_SlotTest_Run."""

def check_overlay_libultra_scope():
    """practice_overlay.c may include game headers freely
    (it is the bridge layer); but lib/crc32.c may NOT include them.
    Reuses check_lib_isolation logic."""
```

Each new check has a unit test in
`tools/test_practice_invariants.py` (existing pattern) so the checker
itself can't regress silently.

## 6. Tests

### 6.1 Host unit tests (lib/test/)

- `test_crc32.c`: known-answer CRC32-IEEE vectors (empty input,
  "123456789" → 0xCBF43926, etc.) plus an end-to-end test that
  `crc32_init() + crc32_update() + crc32_finalize()` matches a
  one-shot helper.
- Existing `test_serial.c`, `test_slot_manager.c` need no edits;
  they're already comprehensive.

### 6.2 Static-invariant unit tests

- Synthetic fixtures under `tools/test/` (the pattern already used by
  the existing checks) — tag registry with a missing-decode case, a
  duplicate-tag case, a missing-overlay-table-entry case. Each must
  fail the corresponding new check; control fixtures must pass.

### 6.3 BizHawk Lua tests (tests/)

- `test_state_save_load_same_scene.lua`
  1. Boot, navigate to Corneria via menu.
  2. Wait for `gPlayState == PLAY_UPDATE`.
  3. Stash baseline values: `gHitCount`, `gPlayer[0].pos.x/y/z`,
     `gPathProgress`, `gActors[0].state`.
  4. Press the SAVE button binding (defaults to `D_JPAD`).
  5. Idle 60 frames; perturb state (move, take damage, fire).
  6. Confirm baseline ≠ current state.
  7. Press the LOAD button binding (defaults to `U_JPAD`).
  8. Wait 1 frame, assert all stashed values restored.
  9. Assert `gPracticeLastLoadResult == 0` and `gPracticeSlotValidBits & 1`.

- `test_state_refuse_during_cutscene.lua`
  1. Boot, drop into Corneria but DO NOT skip the cutscene
     (force `gPracticeConfig.skipCutscenes = false` via Lua write to
     RDRAM before launch).
  2. While `gPlayState != PLAY_UPDATE`, attempt save.
  3. Assert `gPracticeLastSaveResult != 0` and `gPracticeSlotValidBits == 0`.

- `test_state_two_slot_cycle.lua`
  1. Save state A in slot 0.
  2. Cycle to slot 1, perturb state, save state B.
  3. Cycle back to slot 0, load. Assert state A.
  4. Cycle to slot 1, load. Assert state B.

`tests/symbols.lua` regenerates with the new `gPractice*` symbols.

### 6.4 BizHawk negative test (corruption resilience)

Reuse existing magic/version mismatch coverage from host tests — for
in-ROM, we add a small probe in `practice_slot_test.c` (or a separate
`practice_save_corrupt_test.c` if it grows beyond a few lines): after
`Practice_Save_Init`, scribble a byte in the slot pool's header and
attempt load; assert it returns `SLOT_MANAGER_ERR_MAGIC` and game
state was untouched. Behind a `PRACTICE_SAVE_SELFTEST` define, default
on, to stay self-checking like the existing slot test.

## 7. Heap audit

The audit is a **measured, repeatable activity**, not a guess. It
produces a table that goes into `HW_VERIFY_phase4.md` and pins the
final values in `practice_save_config.h`.

### 7.1 Instrumentation

Add `src/practice/practice_heap_audit.{c,h}` (gated by
`PRACTICE_HEAP_AUDIT=1`, default on for development builds; can be
compiled out for tournament release). Exposes:

```c
extern s32 gPracticeMaxMemAllocHWM;  /* peak sMemoryPtr - sMemoryBuffer */
extern s32 gPracticeFreeRamLow;      /* smallest osMemSize - bss_end - hwm */
extern s32 gPracticeOverlaySizes[6]; /* ovl_i1..i6 in bytes */

void Practice_HeapAudit_Boot(void);    /* one-shot at boot */
void Practice_HeapAudit_PerFrame(void); /* called from Practice_Update */
```

What gets logged per scene transition (one IS-Viewer line each):

```
[heap] enter scene=CORNERIA  bss_end=0x80...  memptr_hwm=0x...  free=0x...
[heap] ovl_i1=0x... ovl_i2=0x... ovl_i3=0x... ovl_i4=0x... ovl_i5=0x... ovl_i6=0x...
[heap] peak gfxpool=... peak audio_heap=... slot_pool_base=0x... slot_pool_size=0x...
```

`Practice_HeapAudit_PerFrame` polls `sMemoryPtr` (we already access it
indirectly via globals; we'll either expose it explicitly via a small
helper in `sys_memory.c` under `#ifdef PRACTICE_ROM`, or read it via
its `&sMemoryBuffer[0]` extern + a watermark). The HWM is monotonic
within a scene and resets on `Memory_FreeAll`.

### 7.2 Audit procedure (from `HW_VERIFY_phase4.md`)

1. Build with `PRACTICE_HEAP_AUDIT=1` (default).
2. Start `sc64deployer debug --isv 0x03FF0000` in terminal A.
3. Upload ROM, hard reset N64.
4. For each of the 17 saveable scenes:
   a. Pick the scene in level select.
   b. Play for ~10 seconds, including: enemies on screen, charge shot,
      bombs, max object load (e.g., Corneria base flyover, Sector Z
      missile salvo, Aquas dense terrain).
   c. Note the lowest "free" value in the IS-Viewer log.
5. Tabulate worst-case-of-worst-cases. Subtract overlay-segment max
   size (largest seen across all six). Divide remaining headroom by
   `MAX_STATE_SIZE`. That floor is `MAX_RAM_SLOTS_NO_PAK`. Spec's
   conservative fallback if the number drops below 2: tighten
   `MAX_STATE_SIZE` (try 192 KB) and re-run.
6. With Expansion Pak active (set by holding L+R+Z on boot or via
   environment-specific switch — to be confirmed during this step;
   if libultra `osMemSize` doesn't auto-grow on the user's setup we
   stick with 4 MB and document Pak headroom as a future step).
7. Update `practice_save_config.h` with the audited constants.
8. Re-run `python3 tools/practice_invariants.py` to confirm
   `check_max_state_size_budget` still passes with the tightened
   values.
9. Commit the audit log + updated constants together.

### 7.3 What "free" actually means here

The practice ROM doesn't have a heap walker; we approximate "free RAM"
as:

```
free = osMemSize - (link_bss_end_kseg0 - 0x80000000) - sysHeapWatermarks
```

where `sysHeapWatermarks` is the sum of `sMemoryPtr - sMemoryBuffer`
(bump arena), the audio heap usage, and the gfx pool usage. The
audit log emits each component separately so we can see what's
dominating. This is the best we can do without rebuilding libultra's
allocator; for the conservative slot-count decision it's plenty.

## 8. Subagent-driven decomposition (sequenced for the implementer)

The plan is large enough that the executor should split it into
sequential subagents. Suggested boundaries (each ends with a build +
test pass):

1. **Skeleton** — add `practice_save_config.h`, `practice_save_tags.h`,
   stub `practice_overlay.{c,h}`, `lib/crc32.{c,h}`, wire into
   linker patcher and Makefile. Existing `practice_save.c` untouched
   yet. Goal: build green, no behaviour change.
2. **Overlay table** — fill in `practice_overlay.c` saveable / exclusion
   tables, `LevelId → SceneId → ovl_iN`, `practice_overlay_get_region`,
   `practice_overlay_build_id` (CRC32 of in-RAM copy). Add
   `check_overlay_table_complete` and run it. No save changes yet.
3. **Save callback rewrite** — author `Practice_Save_Cb` emitting all
   tags in registry order. Author `Practice_Load_Cb` with atomic apply.
   `Practice_SaveState` / `Practice_LoadState` become thin wrappers
   over slot 0 only. Same-scene round trip works on Corneria.
4. **Slot 2 + cycle** — flip `RAM_SLOT_COUNT = 2`, expose
   `Practice_CycleSlot`, add the small HUD indicator and the cycle
   binding. `test_state_two_slot_cycle.lua` passes.
5. **Refuse paths + corruption test** — implement
   `Practice_CanSaveHere()`, the IS-Viewer + on-screen log, the
   `PRACTICE_SAVE_SELFTEST` magic-corruption probe.
   `test_state_refuse_during_cutscene.lua` passes.
6. **Heap audit & pin constants** — instrument, run the audit, fill in
   `HW_VERIFY_phase4.md`, tighten `MAX_STATE_SIZE` and slot count,
   tighten `check_max_state_size_budget`. Final commit.

Each subagent commits its own changes and runs the full test stack
(`make lib-test`, `python3 tools/practice_invariants.py`,
`make practice -j4`, `python3 tools/run_tests.py`) before signaling
completion to the executor.

## 9. Risk register (Phase 4 specific)

| Risk | Likelihood | Mitigation |
|---|---|---|
| `MAX_STATE_SIZE * 2` doesn't fit in 4 MB on stock | Medium | Audit pins the number; if it doesn't fit at 256 KB × 2 we tighten the overlay-bytes payload to "diff vs ROM image" (out of scope for v1) or drop to 1 slot on stock |
| Overlay byte snapshot captures more than `ovl_iN` (e.g. .bss touched by other code) | Low | We only capture the linker-defined VRAM range of the active overlay; static invariant verifies the address range comes from `SEGMENT_VRAM_*` macros, not hardcoded |
| CRC32 cost on save/load | Low | Computed once per boot per overlay (cached); save/load just compares cached u32 |
| Atomic apply forgets a field | Medium | The static `check_serializer_parity()` invariant guarantees save+load symmetry; the apply step copies from a fully-populated `PracticeSnapshot` exactly like the current code |
| The slot-cycle UI lands ahead of the menu wiring spec | Low | Phase 4 keeps the UI to a single HUD line + L/R cycle; the full slot picker is Phase 5's responsibility |
| Audit numbers are wrong on a Pak machine because the user can't reproduce Pak boot | Low | If the maintainer doesn't have a Pak, document the 4 MB result and leave Pak slot count at 2 with a TODO; Phase 8/9 testing on Pak hardware can revisit |
| `Practice_HeapAudit_PerFrame` overhead skews timing | Low | The probe is a few u32 reads + an osSyncPrintf only on scene transition (not every frame); the per-frame call is just a watermark update |
| osSyncPrintf flood breaks the IS-Viewer protocol | Low | Existing `MODS_ISVIEWER` machinery is hardened (per `CLAUDE.md`); audit logs are scene-transition triggered, not per-frame |
| Phase 4 changes save semantics for users mid-development | Low | TLV format already insulates against this; `STATE_VERSION = 1` from the start, so any subsequent v2 bump is a deliberate decision |

## 10. Open questions (deferred to implementation, not blocking)

- Exact decision on whether `PRACTICE_SAVE_SELFTEST` runs every boot or
  only on first boot (probably every boot, like the Phase 3 slot
  smoke test, since it's microseconds).
- HUD glyph for "saved" vs "empty" — placeholder text in Phase 4,
  better visual in Phase 5 when the slot picker proper lands.
- Whether the slot-cycle binding should default to L/R while the menu
  is OPEN, or always-on. Default for Phase 4: **only when the practice
  menu is open** (matches how the BGM cycle works in level select).
- Whether the overlay snapshot should be **omitted** on same-scene
  saves to save 80–120 KB per slot — tempting, but introduces an
  asymmetry between Phase 4 (omit) and Phase 5 (must include for
  cross-scene). Decision: always include; the audit accounts for it.

## 11. Verification matrix at end of phase

### 2026-04-29 local progress

- Still on Phase 4; Phase 5 cross-scene loading remains deferred.
- Added boot-on `PRACTICE_SAVE_SELFTEST` corrupt-slot probe. It uses isolated
  fake callbacks/storage, corrupts slot magic, and verifies load rejection
  does not invoke the load callback before the real slot manager is initialized.
- Added minimal Phase 4 UX: root radial slot indicator now shows `SAVED` /
  `EMPTY`, and gameplay save/load attempts show short HUD status toasts.
- Fixed radial-menu save refusal: `Practice_CanSaveHere()` no longer rejects
  solely because the frozen practice menu is open. The hotkey path still only
  runs outside the menu; radial save is intentionally allowed because
  `Play_Main()` is paused under `PMENU_OPEN_FROZEN`.
- Added `check_radial_menu_save_allowed()` to the static invariants so this
  radial-save gate cannot regress silently.
- Patched Aquas Blue Marine torpedo bookkeeping to validate the active torpedo
  slot before reading `gPlayerShots[slot - 1]`; the old order could read slot
  `-1` immediately after Aquas init when no torpedo existed.
- Replaced noisy Aquas serial tracing with an on-screen `AQ` breadcrumb because
  the serial build overwhelmed/desynced IS-Viewer before the freeze.
- Local verification passed: static invariants, practice ROM build, host
  `lib-test`, and whitespace check.
- BizHawk rows below are still pending on this machine because BizHawk is not
  installed or `BIZHAWK_PATH` is unset.

| Item | Command | Pass condition |
|---|---|---|
| Static invariants | `python3 tools/practice_invariants.py` | exit 0 |
| Host lib tests | `make lib-test` | all pass |
| Build | `make practice -j4` | exit 0 |
| Symbols regenerate | `python3 tools/extract_symbols.py > tests/symbols.lua` | new symbols present |
| Same-scene save/load | `python3 tools/run_tests.py test_state_save_load_same_scene` | PASSED |
| Refuse during cutscene | `python3 tools/run_tests.py test_state_refuse_during_cutscene` | PASSED |
| Two-slot cycle | `python3 tools/run_tests.py test_state_two_slot_cycle` | PASSED |
| Heap audit captured | `HW_VERIFY_phase4.md` checked in | non-empty results table |
| Constants pinned | `practice_save_config.h` has audit-derived values | matches HW_VERIFY |

When every row is green and the maintainer has signed off the heap
audit row, Phase 4 is complete and Phase 5 (cross-scene state machine
+ menu wiring) can start from a known-good base.
