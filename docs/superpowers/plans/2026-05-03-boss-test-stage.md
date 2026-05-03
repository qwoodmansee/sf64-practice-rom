# Boss Test Stage Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "BOSSES" entry at the bottom of the practice level-select that lets the user warp directly into a Carrier (Corneria flying boss) fight.

**Architecture:** A new `src/practice/practice_boss_test.c` owns a `BossEntry` table and exposes `Practice_BossTest_Launch(index)`. It re-uses the existing `Practice_LaunchLevel` warp infrastructure and sets a `gPracticeForceCarrier` flag that an `#ifdef PRACTICE_ROM` block in `Corneria_CoCarrier_Init` consults to override the existing `gPlayer[0].xPath` route discriminator.

**Tech Stack:** N64/MIPS C (gcc cross-toolchain), Python 3 invariant scripts, BizHawk Lua functional tests.

**Spec:** [`docs/superpowers/specs/2026-05-02-boss-test-stage-design.md`](../specs/2026-05-02-boss-test-stage-design.md)

---

## Pre-Flight Checks

Before starting any task, verify:

- You're in the worktree: `/Users/qwoodmansee/code/sf64-practice-rom/.claude/worktrees/fun-challenge-hack` — symlinks to `bin/`, `include/assets/`, etc. must already exist.
- A clean baseline build works: `make practice -j4` succeeds.
- Static invariants pass on the baseline: `python3 tools/practice_invariants.py` exits 0.

If any of these fail, fix them before continuing — do not start implementation on a broken baseline.

## Critical Project Conventions (from CLAUDE.md)

- **NEVER run `make clean` or `make init`** — they delete generated asset headers that take ~10 minutes to regenerate.
- All practice code is wrapped in `#ifdef PRACTICE_ROM`. Engine hooks live inside an `#ifdef PRACTICE_ROM` block.
- `gPlayer` is a `Player*`, NULL until `gPlayState == PLAY_UPDATE`. Any read of `gPlayer[0].*` requires the guard from CLAUDE.md. (This plan does not read `gPlayer` from new practice code, but `Corneria_CoCarrier_Init` already runs after the engine has set `gPlayer` up — no extra guard needed in the engine-hook patch.)
- `make practice -j4`, never plain `make`.
- The pre-commit hook runs invariants + build + functional tests automatically. Do **not** use `--no-verify`.

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/practice/practice_boss_test.c` | Create | Owns `sBossList[]` and `gPracticeForceCarrier`; exposes `Practice_BossTest_*` API |
| `include/practice.h` | Modify | Declare `gPracticeForceCarrier` + 3 new functions |
| `src/practice/practice_main.c` | Modify | Reset `gPracticeForceCarrier` in `Practice_Init` |
| `src/practice/practice_level.c` | Modify | Append BOSSES entry to `sLevelList`; route L/R + A through boss-test path; render boss name in phase slot |
| `src/overlays/ovl_i1/fox_co.c` | Modify | Override `xPath` discriminator in `Corneria_CoCarrier_Init` when flag set |
| `tools/patch_linker_script.py` | Modify | Add `practice_boss_test` to `PRACTICE_OBJS` |
| `tools/practice_invariants.py` | Modify | Add `check_boss_test()` invariant |
| `tools/extract_symbols.py` | Modify | Add `gBosses`-already-present, `sFightCarrier`, `gPracticeForceCarrier` symbols + boss obj.id offset |
| `tests/test_boss_test_carrier.lua` | Create | Functional test: navigate to BOSSES → A → assert Carrier spawned |

`gBosses` is already in `SYMBOLS`; verify before adding. `Boss` struct offset for `obj.id` and the `OBJ_BOSS_CO_CARRIER` enum value need to be exposed to the Lua test.

---

## Task 1: Add static invariant for boss-test files

Tests-first. We add a `check_boss_test()` invariant that asserts the new file exists, the linker registration is present, the engine hook references the flag, and `Practice_Init` resets the flag. We expect this to FAIL initially.

**Files:**
- Modify: `tools/practice_invariants.py` (append a new check function and register it in `main()`)

- [ ] **Step 1.1: Read existing invariant patterns**

Run: `grep -n "def check_" tools/practice_invariants.py | head -10`
Read one example (e.g., `check_engine_hooks`) to match style. Reuse helpers like `read_text(path)`, `fail(msg)`.

- [ ] **Step 1.2: Add `check_boss_test()` to `tools/practice_invariants.py`**

Append a new function (placement: after the last `check_*` function but before `main`). Match the style of neighboring checks (return failure list, use `fail()`):

```python
def check_boss_test():
    """Boss-test feature: file exists, flag is wired, reset paths are present."""
    failures = []

    boss_test_path = "src/practice/practice_boss_test.c"
    if not os.path.isfile(boss_test_path):
        failures.append(f"Boss-test source missing: {boss_test_path}")
        return failures

    boss_test_src = read_text(boss_test_path)
    fox_co_src    = read_text("src/overlays/ovl_i1/fox_co.c")
    practice_h    = read_text("include/practice.h")
    main_src      = read_text("src/practice/practice_main.c")
    level_src     = read_text("src/practice/practice_level.c")
    patch_src     = read_text("tools/patch_linker_script.py")

    if "gPracticeForceCarrier" not in boss_test_src:
        failures.append("practice_boss_test.c missing gPracticeForceCarrier")
    if "gPracticeForceCarrier" not in fox_co_src:
        failures.append("fox_co.c missing gPracticeForceCarrier override")
    if "Practice_BossTest_Launch" not in practice_h:
        failures.append("practice.h missing Practice_BossTest_Launch declaration")
    if "Practice_BossTest_Launch" not in boss_test_src:
        failures.append("practice_boss_test.c missing Practice_BossTest_Launch definition")
    if "gPracticeForceCarrier = false" not in main_src:
        failures.append("Practice_Init missing gPracticeForceCarrier reset")
    if "gPracticeForceCarrier = false" not in level_src:
        failures.append("Practice_LevelSelect_Update missing gPracticeForceCarrier reset on non-boss A-press")
    if '"practice_boss_test"' not in patch_src:
        failures.append("tools/patch_linker_script.py missing practice_boss_test in PRACTICE_OBJS")

    # Negative check: gPracticeForceCarrier must NOT be a PracticeConfig field (runtime only)
    config_block_match = re.search(r"typedef struct\s+PracticeConfig\s*{(.+?)}\s*PracticeConfig",
                                    practice_h, re.S)
    if config_block_match and "gPracticeForceCarrier" in config_block_match.group(1):
        failures.append("gPracticeForceCarrier must not be a PracticeConfig field (runtime-only state)")

    return failures
```

Then in `main()` (or wherever the check list is built), register it. Look for the existing pattern — most likely a list of `(name, fn)` pairs; append `("boss test", check_boss_test)`.

- [ ] **Step 1.3: Run the invariants and verify they fail with the expected message**

Run: `python3 tools/practice_invariants.py`
Expected: non-zero exit, error mentioning `practice_boss_test.c` missing.

- [ ] **Step 1.4: Commit**

```bash
git add tools/practice_invariants.py
git commit -m "test(boss-test): add failing static invariant"
```

Pre-commit hook will run the invariants and **fail** on this commit because the boss-test files don't exist yet. Use `git commit --no-verify` ONLY for this single commit, with an explicit note in the commit message — this is the rare case where TDD's "red" phase intentionally breaks the build:

```bash
git commit --no-verify -m "test(boss-test): add failing static invariant (TDD red phase)"
```

NOTE TO IMPLEMENTER: If the project policy disallows `--no-verify` even for TDD red phases, instead reorder Tasks 1 and 2 — create the empty stub file first so invariants pass when added. Confirm with the user before using `--no-verify`.

---

## Task 2: Create the boss-test source file (stub)

Make the failing invariant pass with the smallest possible content.

**Files:**
- Create: `src/practice/practice_boss_test.c`
- Modify: `include/practice.h`
- Modify: `tools/patch_linker_script.py`

- [ ] **Step 2.1: Create `src/practice/practice_boss_test.c`**

```c
#include "practice.h"

#ifdef PRACTICE_ROM

bool gPracticeForceCarrier = false;

typedef struct {
    const char* name;
    LevelId hostLevel;
    s32 phase;
    f32 warpProgress;
    bool forceCarrier;
} BossEntry;

/* warpProgress is a placeholder; Task 6 fills it in once we measure the
 * Corneria progress at which the Carrier event fires. */
static BossEntry sBossList[] = {
    { "CARRIER", LEVEL_CORNERIA, 0, 100000.0f, true },
};

#define BOSS_COUNT ARRAY_COUNT(sBossList)

s32 Practice_BossTest_GetCount(void) {
    return BOSS_COUNT;
}

const char* Practice_BossTest_GetName(s32 index) {
    if ((index < 0) || (index >= (s32)BOSS_COUNT)) {
        return "";
    }
    return sBossList[index].name;
}

void Practice_BossTest_Launch(s32 index) {
    BossEntry* e;

    if ((index < 0) || (index >= (s32)BOSS_COUNT)) {
        return;
    }
    e = &sBossList[index];

    Practice_LaunchLevel(e->hostLevel, e->phase, e->warpProgress);
    /* Set force flags AFTER Practice_LaunchLevel: the non-boss A-press
     * branch in Practice_LevelSelect_Update clears gPracticeForceCarrier,
     * but only on its own path. Setting after the launch ensures the flag
     * is set when the engine reaches Corneria_CoCarrier_Init. */
    gPracticeForceCarrier = e->forceCarrier;
}

#endif
```

- [ ] **Step 2.2: Add declarations to `include/practice.h`**

Find an appropriate section (near other practice-feature function declarations — `grep -n "Practice_.*_Init\|Practice_.*_Launch\|Practice_LaunchLevel" include/practice.h` to locate). Add:

```c
extern bool gPracticeForceCarrier;

s32  Practice_BossTest_GetCount(void);
const char* Practice_BossTest_GetName(s32 index);
void Practice_BossTest_Launch(s32 index);
```

- [ ] **Step 2.3: Register the new object in `tools/patch_linker_script.py`**

Find `PRACTICE_OBJS = [`. Append `"practice_boss_test",` at the end of the list (order matters per the comment, but appending preserves all existing anchors).

- [ ] **Step 2.4: Build and verify**

Run: `make practice -j4`
Expected: clean build. The new `.o` file appears at `build/src/practice/practice_boss_test.o`.

If linker errors mention missing sections, the patch script needs to re-run. Run: `python3 tools/patch_linker_script.py` then rebuild.

- [ ] **Step 2.5: Run static invariants — most should now pass**

Run: `python3 tools/practice_invariants.py`
Expected: still failing on the four checks that need fox_co.c, practice_main.c, and practice_level.c changes:
- `fox_co.c missing gPracticeForceCarrier override`
- `Practice_Init missing gPracticeForceCarrier reset`
- `Practice_LevelSelect_Update missing gPracticeForceCarrier reset on non-boss A-press`

These are addressed in Tasks 3, 4, 5.

- [ ] **Step 2.6: Commit (skipping pre-commit if needed because invariants are still failing)**

If invariants still red:
```bash
git add src/practice/practice_boss_test.c include/practice.h tools/patch_linker_script.py
git commit --no-verify -m "feat(boss-test): scaffold practice_boss_test.c (invariants still red)"
```

Otherwise (invariants passed because Tasks 1 and 2 reordered):
```bash
git add src/practice/practice_boss_test.c include/practice.h tools/patch_linker_script.py
git commit -m "feat(boss-test): scaffold practice_boss_test.c"
```

---

## Task 3: Add the engine-side override hook

Make `Corneria_CoCarrier_Init` honor `gPracticeForceCarrier`.

**Files:**
- Modify: `src/overlays/ovl_i1/fox_co.c` (around line 1668)

- [ ] **Step 3.1: Read the current condition**

Run: `sed -n '1656,1700p' src/overlays/ovl_i1/fox_co.c`
Confirm the structure: `if (fabsf(gPlayer[0].xPath) < 1.0f) { /* Granga */ } else { /* Carrier */ }`.

- [ ] **Step 3.2: Patch the condition**

Wrap the condition in an `#ifdef PRACTICE_ROM` block. Locate the exact line (currently `1668`) and replace:

```c
    if (fabsf(gPlayer[0].xPath) < 1.0f) {
```

with:

```c
#ifdef PRACTICE_ROM
    if (!gPracticeForceCarrier && (fabsf(gPlayer[0].xPath) < 1.0f)) {
#else
    if (fabsf(gPlayer[0].xPath) < 1.0f) {
#endif
```

This keeps the **logic identical** for vanilla builds. In practice builds: when `gPracticeForceCarrier` is true, the condition is forced false, taking the Carrier `else` branch regardless of `xPath`. The Carrier branch contents (lines 1673–1683 — `obj.rot.y = 180.0f`, `fwork[6] = 800.0f`, `fwork[7] = obj.pos.x`, `fwork[5] = 30.0f`, `swork[10] = 3`, `swork[8] = 3`, `obj.pos.z = gPlayer[0].trueZpos + 2000.0f`) are NOT modified.

- [ ] **Step 3.3: Verify the patch matches the spec's polarity expectation**

Re-read lines 1668–1690 and visually confirm: the if-branch is Granga (`sFightCarrier = false`), the else-branch is Carrier (`sFightCarrier = true`). With our patch and `gPracticeForceCarrier == true`, the if-condition is forced false → else-branch runs → `sFightCarrier = true`.

- [ ] **Step 3.4: Add `#include "practice.h"` if not already present**

Run: `grep -n '#include "practice.h"' src/overlays/ovl_i1/fox_co.c`
If absent, add inside an `#ifdef PRACTICE_ROM` block at the top of file or near other `#ifdef PRACTICE_ROM` includes. The header pulls in `extern bool gPracticeForceCarrier;` declared in Task 2.

- [ ] **Step 3.5: Build**

Run: `make practice -j4`
Expected: clean build.

- [ ] **Step 3.6: Run static invariants**

Run: `python3 tools/practice_invariants.py`
Expected: the `fox_co.c missing gPracticeForceCarrier override` failure is gone. Two failures remain (Practice_Init reset, level-select non-boss reset).

- [ ] **Step 3.7: Commit**

```bash
git add src/overlays/ovl_i1/fox_co.c
git commit --no-verify -m "feat(boss-test): override carrier route discriminator on PRACTICE_ROM"
```

(Still `--no-verify` because two invariants remain red.)

---

## Task 4: Add Practice_Init reset

**Files:**
- Modify: `src/practice/practice_main.c` (in `Practice_Init`, near other config defaults)

- [ ] **Step 4.1: Locate `Practice_Init`**

Run: `grep -n "Practice_Init\|gPracticeConfig.expertMode" src/practice/practice_main.c`
Plan to insert the reset just after the last `gPracticeConfig.*` assignment line (around line 51) but before `osSyncPrintf("=== PRACTICE ROM boot...`.

- [ ] **Step 4.2: Insert the reset**

After the line `gPracticeConfig.infBoost = false;` and before the `osSyncPrintf` line, add:

```c
    /* Boss-test override flag: runtime-only, reset on every boot.
     * Per-launch resets happen in Practice_LevelSelect_Update's non-boss
     * A-press branch and in Practice_BossTest_Launch (which sets it). */
    gPracticeForceCarrier = false;
```

- [ ] **Step 4.3: Build**

Run: `make practice -j4`
Expected: clean build.

- [ ] **Step 4.4: Run invariants**

Run: `python3 tools/practice_invariants.py`
Expected: `Practice_Init missing gPracticeForceCarrier reset` failure gone. One remaining: the level-select reset.

- [ ] **Step 4.5: Commit**

```bash
git add src/practice/practice_main.c
git commit --no-verify -m "feat(boss-test): reset gPracticeForceCarrier in Practice_Init"
```

---

## Task 5: Wire the BOSSES entry into level-select

This is the largest task. Three sub-changes inside `src/practice/practice_level.c`:

1. Append `BOSSES` entry to `sLevelList`.
2. In `Practice_LevelSelect_Update`'s A-press branch, route through the boss-test path when the BOSSES entry is selected, AND clear `gPracticeForceCarrier` on the non-boss A-press path.
3. In `Practice_LevelSelect_Update`'s L/R-press branch, scroll `sBossList` instead of phases when BOSSES is selected.
4. In `Practice_LevelSelect_Draw`'s phase-line render, draw the current boss name when BOSSES is selected.

**Files:**
- Modify: `src/practice/practice_level.c`

- [ ] **Step 5.1: Append the BOSSES entry to `sLevelList`**

Locate the closing `}` of `sLevelList` (immediately after `{ "VENOM 2", ... }` near the top of the file). Add a final entry:

```c
    { "BOSSES",   LEVEL_INVALID,  PLANET_CORNERIA, 8, 0,
      { { "", LEVEL_INVALID, 0 } } },
```

(`phaseCount = 0` is the sentinel marking this as the boss-test entry. The empty single phase entry exists only to satisfy struct array bounds; it is never read because the BOSSES branch short-circuits.)

- [ ] **Step 5.2: Add a helper macro or inline check for the BOSSES sentinel**

Right above `Practice_LevelSelect_Update`, add:

```c
static bool IsBossTestEntry(s32 levelIndex) {
    return (sLevelList[levelIndex].levelId == LEVEL_INVALID) &&
           (sLevelList[levelIndex].phaseCount == 0);
}
```

(Using a function rather than a macro to keep it debuggable. The double-condition match is defensive against the `LEVEL_INVALID`-on-PhaseEntry semantic overload.)

- [ ] **Step 5.3: Patch the A-press branch**

Locate the current A-press code (around line 225–229 of `practice_level.c`):

```c
    if (press->button & A_BUTTON) {
        phase = &sLevelList[sSelectedLevel].phases[sSelectedPhase];
        levelId = (phase->levelId == LEVEL_INVALID) ? sLevelList[sSelectedLevel].levelId : phase->levelId;
        Practice_LaunchLevel(levelId, phase->phase, phase->checkpointProgress);
        return;
    }
```

Replace with:

```c
    if (press->button & A_BUTTON) {
        if (IsBossTestEntry(sSelectedLevel)) {
            Practice_BossTest_Launch(sSelectedPhase);
            return;
        }
        /* Non-boss launch: clear the override so a previous boss-test run
         * does not leak its force flag into a subsequent vanilla launch. */
        gPracticeForceCarrier = false;
        phase = &sLevelList[sSelectedLevel].phases[sSelectedPhase];
        levelId = (phase->levelId == LEVEL_INVALID) ? sLevelList[sSelectedLevel].levelId : phase->levelId;
        Practice_LaunchLevel(levelId, phase->phase, phase->checkpointProgress);
        return;
    }
```

- [ ] **Step 5.4: Patch the L/R-press branch**

Locate the L/R block (around line 200–214):

```c
    phaseCount = sLevelList[sSelectedLevel].phaseCount;
    if (phaseCount > 1) {
        if (press->button & L_JPAD) {
            sSelectedPhase--;
            if (sSelectedPhase < 0) {
                sSelectedPhase = phaseCount - 1;
            }
        }
        if (press->button & R_JPAD) {
            sSelectedPhase++;
            if (sSelectedPhase >= phaseCount) {
                sSelectedPhase = 0;
            }
        }
    }
```

Replace with:

```c
    if (IsBossTestEntry(sSelectedLevel)) {
        s32 bossCount = Practice_BossTest_GetCount();
        if (bossCount > 0) {
            if (press->button & L_JPAD) {
                sSelectedPhase--;
                if (sSelectedPhase < 0) {
                    sSelectedPhase = bossCount - 1;
                }
            }
            if (press->button & R_JPAD) {
                sSelectedPhase++;
                if (sSelectedPhase >= bossCount) {
                    sSelectedPhase = 0;
                }
            }
        }
    } else {
        phaseCount = sLevelList[sSelectedLevel].phaseCount;
        if (phaseCount > 1) {
            if (press->button & L_JPAD) {
                sSelectedPhase--;
                if (sSelectedPhase < 0) {
                    sSelectedPhase = phaseCount - 1;
                }
            }
            if (press->button & R_JPAD) {
                sSelectedPhase++;
                if (sSelectedPhase >= phaseCount) {
                    sSelectedPhase = 0;
                }
            }
        }
    }
```

- [ ] **Step 5.5: Patch `Practice_LevelSelect_Draw` phase line**

Locate the phase rendering block (around line 281–289):

```c
    phaseCount = sLevelList[sSelectedLevel].phaseCount;
    Practice_DrawText(20, 180, "PHASE:");
    if (phaseCount > 1) {
        Practice_DrawTextColor(72, 180,
            sLevelList[sSelectedLevel].phases[sSelectedPhase].name,
            255, 220, 0);
    } else {
        Practice_DrawTextColor(72, 180, "START", 150, 150, 150);
    }
```

Replace with:

```c
    if (IsBossTestEntry(sSelectedLevel)) {
        Practice_DrawText(20, 180, "BOSS:");
        Practice_DrawTextColor(72, 180,
            Practice_BossTest_GetName(sSelectedPhase), 255, 220, 0);
    } else {
        phaseCount = sLevelList[sSelectedLevel].phaseCount;
        Practice_DrawText(20, 180, "PHASE:");
        if (phaseCount > 1) {
            Practice_DrawTextColor(72, 180,
                sLevelList[sSelectedLevel].phases[sSelectedPhase].name,
                255, 220, 0);
        } else {
            Practice_DrawTextColor(72, 180, "START", 150, 150, 150);
        }
    }
```

(Label changes from "PHASE:" to "BOSS:" when on the BOSSES entry, which is friendlier UX.)

- [ ] **Step 5.6: Build**

Run: `make practice -j4`
Expected: clean build.

- [ ] **Step 5.7: Run all invariants**

Run: `python3 tools/practice_invariants.py`
Expected: PASS. All boss-test invariants now satisfied.

- [ ] **Step 5.8: Commit**

```bash
git add src/practice/practice_level.c
git commit -m "feat(boss-test): wire BOSSES entry into practice level-select"
```

(No `--no-verify` — the full pre-commit hook should now pass.)

---

## Task 6: Determine and set the correct `warpProgress` value

The placeholder `100000.0f` from Task 2 is a guess. We need to find the actual progress at which the Carrier event fires.

**Files:**
- Modify: `src/practice/practice_boss_test.c` (the literal in `sBossList[0]`)

- [ ] **Step 6.1: Find the Carrier spawn trigger in fox_co.c**

Run: `grep -n "Corneria_CoCarrier_Init\|gActorSpawnPoint\|gPathProgress\|MEVENT" src/overlays/ovl_i1/fox_co.c | head -30`

Look for where a `Boss_Initialize(&gBosses[CARRIER])` or `Corneria_CoCarrier_Init` is called, and trace back to find the progress condition. Likely candidates: an event table indexed by `gPathProgress`, or an actor spawn entry with a `zPos` that's compared to player progress.

If the search above doesn't yield it, also check:
- `src/overlays/ovl_i1/fox_i1.c`
- `assets/yaml/us/rev1/co_*.yaml` (level scripting tables)
- The level event/message table emitted as a header (`include/assets/co_*.h` if any).

- [ ] **Step 6.2: Choose `warpProgress`**

Pick a progress value ~5000–8000 units before the trigger (≈1–2 seconds at default arwing speed; reference: existing `CP 1` is at 93610.3f, so the Carrier event progress is somewhere larger — `gPathProgress` numbers in the 100k–200k range are typical for late-Corneria events).

If you cannot find the exact value confidently, fall back to a reasonable empirical value (e.g., `145000.0f`) and verify in Task 7 that the boss spawns. Adjust based on observation.

- [ ] **Step 6.3: Update the literal**

Edit `src/practice/practice_boss_test.c` line `{ "CARRIER", LEVEL_CORNERIA, 0, 100000.0f, true },` to use the chosen value.

- [ ] **Step 6.4: Build and run a smoke test**

Run: `make practice -j4` (clean build).

If you have access to BizHawk locally, manual smoke: launch the ROM, navigate to BOSSES → CARRIER → A. Observe whether the Carrier appears within ~5 seconds of gameplay.

- [ ] **Step 6.5: Commit**

```bash
git add src/practice/practice_boss_test.c
git commit -m "feat(boss-test): set Carrier warpProgress to <chosen-value>"
```

---

## Task 7: Functional test (BizHawk Lua)

End-to-end regression test that boots the ROM, drives the menu, and asserts the Carrier spawns.

**Files:**
- Create: `tests/test_boss_test_carrier.lua`
- Modify: `tools/extract_symbols.py` (add `sFightCarrier` and `gPracticeForceCarrier` symbols; verify `gBosses` is already present; add `OBJ_BOSS_CO_CARRIER` and `Boss.obj.id` offset)

- [ ] **Step 7.1: Read `tools/extract_symbols.py` to understand the format**

Run: `head -200 tools/extract_symbols.py`
Identify:
- The `SYMBOLS = [...]` list (linker-map symbol names).
- The `CONFIG_OFFSETS`, `PLAYER_OFFSETS`, `ACTOR_OFFSETS` dicts (struct offsets).
- Any enum/const dict (search for `OBJ_BOSS\|consts =`); if there is no enum-export mechanism, hardcode the boss obj.id value in the Lua test with a comment citing `include/sf64object.h:619-620`.

- [ ] **Step 7.2: Add the new symbols**

Append to `SYMBOLS`:
```python
    "sFightCarrier",
    "gPracticeForceCarrier",
```

(Verify `gBosses` is already in `SYMBOLS` — it appears in the existing list near `gActors`.)

If a `BOSS_OFFSETS` dict does not exist, add one alongside `PLAYER_OFFSETS` / `ACTOR_OFFSETS`:
```python
# Boss struct offsets (include/sf64object.h Boss)
# Confirm offsets by inspecting include/sf64object.h Boss struct definition
# and Object struct (Boss has an Object field at offset 0).
BOSS_OFFSETS = {
    "obj_id":   0x00 + <object_id_offset>,  # Object.id offset within Boss.obj
}
```

Run: `grep -n "typedef struct\\|Object\\|Object_id\\|obj.id\\|s32 id" include/sf64object.h | head -30` to find the exact byte offset of `Object.id` within `Object`, and the offset of `obj` within `Boss`. (If `Boss.obj` is at offset 0 and `Object.id` is at offset 0, then `obj_id = 0`. Verify with the actual struct.)

If `BOSS_OFFSETS` is added, also emit it in the Lua output (search for how `PLAYER_OFFSETS` is emitted, mirror that).

If determining the offset is tricky, alternative: read the Boss struct as `u32`s and look for the boss `id` (which for `OBJ_BOSS_CO_CARRIER` is 293) at small offsets. Hardcode the offset once observed.

- [ ] **Step 7.3: Regenerate symbols**

Run: `python3 tools/extract_symbols.py > tests/symbols.lua`
Expected: completes without errors. Verify the new symbols appear: `grep -E "sFightCarrier|gPracticeForceCarrier" tests/symbols.lua`.

- [ ] **Step 7.4: Write the failing functional test**

Create `tests/test_boss_test_carrier.lua`:

```lua
-- Test: Selecting BOSSES → CARRIER from the level select warps into Corneria
-- with the Attack Carrier spawning instead of Granga.

local H = dofile("tests/harness.lua")
H.test_name = "boss_test_carrier"

local OBJ_BOSS_CO_CARRIER = 293  -- include/sf64object.h:620
local OBJ_BOSS_CO_GRANGA  = 292  -- include/sf64object.h:619
local LEVEL_CORNERIA      = 0    -- include/sf64level.h (verify)
local CARRIER_BOSS_INDEX  = 0    -- gBosses[0] is the main Carrier slot (CARRIER enum, fox_co.h:158)

-- Wait for level select
local ok = H.wait_until(function()
    return H.practice_screen() == H.S.const.PSCREEN_LEVEL_SELECT
        and H.game_state() == H.S.const.GSTATE_MAP
end, 600, "level select")
H.assert_true(ok, "reached level select")

-- BOSSES is the last entry in sLevelList. Press D-pad down repeatedly
-- until we wrap around or reach the end. The current count is fixed in
-- practice_level.c; we navigate by counting. To be robust, press Down
-- many times so we definitely land on BOSSES (the entry's name is
-- "BOSSES"; we can verify by reading sSelectedLevel against LEVEL_COUNT - 1
-- if exposed, or just trust the count).
local LEVEL_COUNT = 17  -- 16 vanilla + 1 BOSSES; UPDATE if sLevelList grows
for i = 1, LEVEL_COUNT - 1 do
    H.press({Down = true})
    H.advance(2)
end

-- A: launch the boss test
H.press({A = true})

-- Wait for gameplay to be active
ok = H.wait_for_gameplay(900)
H.assert_true(ok, "gameplay became active")

-- gNextLevel should have been LEVEL_CORNERIA
H.assert_eq(H.read_s32(H.S.gCurrentLevel), LEVEL_CORNERIA, "current level is Corneria")

-- Force-flag should be set
H.assert_eq(H.read_u8(H.S.gPracticeForceCarrier), 1, "gPracticeForceCarrier set")

-- Wait for the Carrier boss to spawn (timeout 600 frames ≈ 10s @ 60fps)
local boss_ok = H.wait_until(function()
    -- gBosses[0].obj.id != 0 means a boss has been initialized
    -- For the offset, use the BOSS_OFFSETS exposed by symbols.lua
    -- (or hardcode below; comment cites struct layout).
    local id = H.read_s32(H.S.gBosses + 0 + (H.S.boss and H.S.boss.obj_id or 0))
    return id ~= 0
end, 600, "boss spawn")
H.assert_true(boss_ok, "a boss spawned within 600 frames")

-- The boss must be the Carrier, not Granga
local boss_id = H.read_s32(H.S.gBosses + 0 + (H.S.boss and H.S.boss.obj_id or 0))
H.assert_eq(boss_id, OBJ_BOSS_CO_CARRIER, "boss is Carrier, not Granga")

-- And sFightCarrier must be 1 (true)
H.assert_eq(H.read_u8(H.S.sFightCarrier), 1, "sFightCarrier == 1")

H.finish()
```

- [ ] **Step 7.5: Run the test**

Run: `python3 tools/run_tests.py test_boss_test_carrier`
Expected: PASS (Carrier spawns, all assertions hold).

If the test fails on "boss spawn" timeout, revisit Task 6 — the `warpProgress` is wrong. Adjust and re-run.

If the test fails on "current level is Corneria", `LEVEL_CORNERIA` is not 0 — fix the constant by checking `include/sf64level.h`.

If the test fails on `sFightCarrier == 1` but Carrier spawned: the engine hook in Task 3 was applied wrong; re-read fox_co.c around line 1668 and confirm the polarity.

- [ ] **Step 7.6: Commit**

```bash
git add tests/test_boss_test_carrier.lua tools/extract_symbols.py tests/symbols.lua
git commit -m "test(boss-test): add functional test for Carrier boss warp"
```

---

## Task 8: Integration verification

Make sure nothing else broke.

- [ ] **Step 8.1: Full pre-commit pipeline**

Run: `python3 tools/practice_invariants.py && make practice -j4 && python3 tools/run_tests.py`
Expected: all green.

- [ ] **Step 8.2: Spot-check no other test regressed**

Run: `python3 tools/run_tests.py` (without filter, runs all tests).
Expected: every test passes. If any previously-green test now fails, the level-select changes in Task 5 likely broke navigation. Re-read Task 5's diffs and confirm L_JPAD/R_JPAD behavior is unchanged for non-BOSSES entries.

- [ ] **Step 8.3: Manual smoke (optional but recommended)**

Boot the ROM in Dolphin or BizHawk:
1. Verify the level-select shows BOSSES at the bottom.
2. Highlight BOSSES — phase line should read "BOSS: CARRIER".
3. Press L/R — name doesn't change (only one boss in v1; if you had time you could add a stub second entry to verify scroll, but that's out of scope).
4. Press A — Corneria loads, Carrier appears within a few seconds, no Granga.
5. Restart, pick CORNERIA START, A — vanilla Granga path runs (force flag was cleared on the non-boss A-press).

If step 5 fails (Carrier spawns instead of Granga on a vanilla launch), the reset in Task 5 step 5.3 is missing or misplaced.

- [ ] **Step 8.4: No commit needed unless something was fixed**

If you fixed anything during Step 8, commit it as a follow-up.

---

## Done Criteria

- [ ] `python3 tools/practice_invariants.py` exits 0.
- [ ] `make practice -j4` succeeds.
- [ ] `python3 tools/run_tests.py test_boss_test_carrier` passes.
- [ ] `python3 tools/run_tests.py` (full suite) passes.
- [ ] Manual smoke confirms: BOSSES → CARRIER warps to Carrier fight; vanilla CORNERIA START still spawns Granga.
- [ ] Spec's open questions 1 (warpProgress), 2 (xPath consistency), and 4 (planet id audio leak) are resolved or explicitly accepted as v1 quirks (with a follow-up note in code or commit).

## Out of Scope

- Adding more bosses (Granga, Spyborg, etc.) — easy follow-up by appending to `sBossList[]`.
- Per-boss arena cleanup (suppress pre-boss enemies).
- Custom void arenas.
- Replacing the `LEVEL_INVALID` sentinel with a dedicated `isBossList` flag.
- Column-based level-select navigation.
