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
- **NEVER use `git commit --no-verify`.** The pre-commit hook (invariants → build → tests) is the safety net. Plan task ordering must keep every commit green.
- All practice code is wrapped in `#ifdef PRACTICE_ROM`. Engine hooks live inside an `#ifdef PRACTICE_ROM` block.
- `gPlayer` is a `Player*`, NULL until `gPlayState == PLAY_UPDATE`. The engine's `Corneria_CoCarrier_Init` is called *after* gameplay is active, so the `gPlayer[0].xPath` read at line 1668 is already safe — our patch only changes the condition that selects the branch, not the `gPlayer` read.
- `make practice -j4`, never plain `make`.

## Ordering Strategy (Why It Matters)

Static invariants and the pre-commit hook block any commit that fails them. The new invariant we add (`check_boss_test`) references symbols and source patches that don't exist yet at the start of the work. So we add the invariant **last** — once every target it references is already present in the tree. Every intermediate commit must be green under the *current* invariant set.

Tasks are ordered so each commit:
1. Compiles cleanly (`make practice -j4`).
2. Passes the existing invariants.
3. Either is functionally inert (a pure addition guarded by sentinels nothing reads yet) OR is wired up enough that the existing tests still pass.

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/practice/practice_boss_test.c` | Create | Owns `sBossList[]` and `gPracticeForceCarrier`; exposes `Practice_BossTest_*` API |
| `include/practice.h` | Modify | Declare `gPracticeForceCarrier` + 3 new functions |
| `src/practice/practice_main.c` | Modify | Reset `gPracticeForceCarrier` in `Practice_Init` |
| `src/practice/practice_level.c` | Modify | Append BOSSES entry; route L/R + A through boss-test path; render boss name in phase slot |
| `src/overlays/ovl_i1/fox_co.c` | Modify | Override `xPath` discriminator in `Corneria_CoCarrier_Init` when flag set |
| `tools/patch_linker_script.py` | Modify | Add `practice_boss_test` to `PRACTICE_OBJS` |
| `tools/practice_invariants.py` | Modify | Add `check_boss_test()` invariant (LAST) |
| `tools/extract_symbols.py` | Modify | Add `sFightCarrier`, `gPracticeForceCarrier`, `BOSS_OFFSETS` |
| `tests/test_boss_test_carrier.lua` | Create | Functional test |

`gBosses` is already in `tools/extract_symbols.py:36` — no need to add.

---

## Task 1: Scaffold `practice_boss_test.c`

Create the new source file with stub bodies. This is functionally inert (nothing calls into it yet) so the existing invariants and build still pass.

**Files:**
- Create: `src/practice/practice_boss_test.c`
- Modify: `include/practice.h`
- Modify: `tools/patch_linker_script.py`

- [ ] **Step 1.1: Create `src/practice/practice_boss_test.c`**

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

/* warpProgress is a placeholder; Task 8 fills in the verified value. */
static BossEntry sBossList[] = {
    { "CARRIER", LEVEL_CORNERIA, 0, 145000.0f, true },
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

- [ ] **Step 1.2: Add declarations to `include/practice.h`**

Run: `grep -n "Practice_LaunchLevel\|extern\\b.*g[A-Z]" include/practice.h | head -10` to find an appropriate insertion point near other practice-feature declarations. Add:

```c
extern bool gPracticeForceCarrier;

s32  Practice_BossTest_GetCount(void);
const char* Practice_BossTest_GetName(s32 index);
void Practice_BossTest_Launch(s32 index);
```

- [ ] **Step 1.3: Register the new object in `tools/patch_linker_script.py`**

Open `tools/patch_linker_script.py`, find the `PRACTICE_OBJS = [` list, append `"practice_boss_test",` at the end (after `"practice_sd"`).

- [ ] **Step 1.4: Build**

Run: `make practice -j4`
Expected: clean build. The new `.o` appears at `build/src/practice/practice_boss_test.o`.

If the linker complains about missing sections for `practice_boss_test`, the patcher needs to inject it: re-run `python3 tools/patch_linker_script.py` then rebuild.

- [ ] **Step 1.5: Run all existing invariants and tests**

Run: `python3 tools/practice_invariants.py`
Expected: PASS (no new invariant added yet).

Run: `python3 tools/run_tests.py` (if BizHawk available; otherwise skip).
Expected: previously-green tests still pass.

- [ ] **Step 1.6: Commit (full pre-commit hook MUST pass)**

```bash
git add src/practice/practice_boss_test.c include/practice.h tools/patch_linker_script.py
git commit -m "feat(boss-test): scaffold practice_boss_test.c

Inert v1 of the boss-test feature: data table, API stubs, linker
registration. Nothing calls into it yet."
```

---

## Task 2: Reset `gPracticeForceCarrier` in `Practice_Init`

**Files:**
- Modify: `src/practice/practice_main.c`

- [ ] **Step 2.1: Locate the insertion point**

Run: `grep -n "gPracticeConfig.infBoost\|osSyncPrintf.*PRACTICE ROM boot" src/practice/practice_main.c`
The reset goes right after the last `gPracticeConfig.*` assignment (currently `gPracticeConfig.infBoost = false;` at line 51) and before the first `osSyncPrintf` (line 53).

- [ ] **Step 2.2: Insert the reset**

Add after the `gPracticeConfig.infBoost = false;` line:

```c
    /* Boss-test override flag: runtime-only, reset on every boot.
     * Per-launch resets happen in Practice_LevelSelect_Update's non-boss
     * A-press branch (Task 6) and in Practice_BossTest_Launch (which sets
     * it true after Practice_LaunchLevel returns). */
    gPracticeForceCarrier = false;
```

- [ ] **Step 2.3: Build + test + commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add src/practice/practice_main.c
git commit -m "feat(boss-test): reset gPracticeForceCarrier in Practice_Init"
```

---

## Task 3: Add the engine-side override in `fox_co.c`

Make `Corneria_CoCarrier_Init` honor `gPracticeForceCarrier`. The flag is currently `false` for all execution paths (set false in `Practice_Init`, never written elsewhere yet), so this commit changes no observable behavior. It only adds the override mechanism.

**Files:**
- Modify: `src/overlays/ovl_i1/fox_co.c`

- [ ] **Step 3.1: Re-verify the current state at the patch site**

Run: `sed -n '1656,1700p' src/overlays/ovl_i1/fox_co.c`
Confirm the discriminator at line 1668 is exactly `if (fabsf(gPlayer[0].xPath) < 1.0f) {` and the else-branch contains the Carrier setup (lines 1673–1683 of spec — `obj.rot.y = 180.0f`, `fwork[6] = 800.0f`, `fwork[7] = obj.pos.x`, `fwork[5] = 30.0f`, `swork[10] = 3`, `swork[8] = 3`, `obj.pos.z = gPlayer[0].trueZpos + 2000.0f`, `sFightCarrier = true`, `AUDIO_PLAY_SFX(...)`).

If the line numbers have drifted, find the equivalent block by grep instead.

- [ ] **Step 3.2: Add the practice-aware include if not already present**

Run: `grep -n '#include "practice.h"' src/overlays/ovl_i1/fox_co.c`
If absent, add at the top of the file inside an `#ifdef PRACTICE_ROM` guard:

```c
#ifdef PRACTICE_ROM
#include "practice.h"
#endif
```

- [ ] **Step 3.3: Patch the discriminator**

Replace:

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

The Granga branch contents (the if-body) and the Carrier branch contents (the else-body) are NOT modified — only the condition.

- [ ] **Step 3.4: Build + test + commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add src/overlays/ovl_i1/fox_co.c
git commit -m "feat(boss-test): add gPracticeForceCarrier override in CoCarrier_Init"
```

---

## Task 4: Append BOSSES entry to `sLevelList`

This adds a new entry to the level-select list. The existing input handler will treat it as a normal level and try to launch `LEVEL_INVALID` if A is pressed — so we must verify A is NOT pressable on this entry until Task 6 wires the dispatch. **Workaround for ordering:** the existing A-press path resolves `LEVEL_INVALID` via the `phases[0]` fallback, which is also `LEVEL_INVALID` on our new entry, so `Practice_LaunchLevel(LEVEL_INVALID, 0, 0)` is what gets called. Empirically this is harmless (the engine refuses to start an unknown level), but to be safe we add an early-return guard in this same task.

**Files:**
- Modify: `src/practice/practice_level.c`

- [ ] **Step 4.1: Add the `IsBossTestEntry` helper**

Just above `Practice_LevelSelect_Update` (currently line 158), add:

```c
static bool IsBossTestEntry(s32 levelIndex) {
    return (sLevelList[levelIndex].levelId == LEVEL_INVALID) &&
           (sLevelList[levelIndex].phaseCount == 0);
}
```

- [ ] **Step 4.2: Append the BOSSES entry to `sLevelList`**

Locate the closing `};` of `sLevelList` (after the `VENOM 2` entry, near line 56). Add a new entry as the last element of the array, before the closing brace:

```c
    { "BOSSES",   LEVEL_INVALID,  PLANET_CORNERIA, 8, 0,
      { { "", LEVEL_INVALID, 0 } } },
```

`phaseCount = 0` is the sentinel that `IsBossTestEntry` looks for. The empty single phase exists only to satisfy the struct array bound; it is never read.

- [ ] **Step 4.3: Add a defensive A-press guard for BOSSES**

In `Practice_LevelSelect_Update`, the existing A-press handler (around line 225) currently runs unconditionally. Add a temporary guard at the top of the A-press branch that prevents launching when on the BOSSES entry. Task 6 replaces this with the real dispatch.

Replace:

```c
    if (press->button & A_BUTTON) {
        phase = &sLevelList[sSelectedLevel].phases[sSelectedPhase];
        levelId = (phase->levelId == LEVEL_INVALID) ? sLevelList[sSelectedLevel].levelId : phase->levelId;
        Practice_LaunchLevel(levelId, phase->phase, phase->checkpointProgress);
        return;
    }
```

with:

```c
    if (press->button & A_BUTTON) {
        if (IsBossTestEntry(sSelectedLevel)) {
            /* Task 6 will replace this with Practice_BossTest_Launch dispatch. */
            return;
        }
        phase = &sLevelList[sSelectedLevel].phases[sSelectedPhase];
        levelId = (phase->levelId == LEVEL_INVALID) ? sLevelList[sSelectedLevel].levelId : phase->levelId;
        Practice_LaunchLevel(levelId, phase->phase, phase->checkpointProgress);
        return;
    }
```

- [ ] **Step 4.4: Build + test + commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add src/practice/practice_level.c
git commit -m "feat(boss-test): add BOSSES entry to level-select list"
```

---

## Task 5: Wire L/R navigation for the boss list

When BOSSES is highlighted, L/R scrolls through `sBossList` instead of phases.

**Files:**
- Modify: `src/practice/practice_level.c`

- [ ] **Step 5.1: Patch the L/R block**

Locate the L/R block in `Practice_LevelSelect_Update` (around line 200):

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

- [ ] **Step 5.2: Build + test + commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add src/practice/practice_level.c
git commit -m "feat(boss-test): scroll boss list with L/R on BOSSES entry"
```

---

## Task 6: Wire A-press dispatch + non-boss reset

**Files:**
- Modify: `src/practice/practice_level.c`

- [ ] **Step 6.1: Replace the A-press guard with the real dispatch**

In `Practice_LevelSelect_Update`, replace the temporary guard from Task 4.3:

```c
    if (press->button & A_BUTTON) {
        if (IsBossTestEntry(sSelectedLevel)) {
            /* Task 6 will replace this with Practice_BossTest_Launch dispatch. */
            return;
        }
        phase = &sLevelList[sSelectedLevel].phases[sSelectedPhase];
        ...
    }
```

with the final form:

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

- [ ] **Step 6.2: Build + test + commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add src/practice/practice_level.c
git commit -m "feat(boss-test): dispatch A on BOSSES entry; clear flag on non-boss"
```

---

## Task 7: Render boss name in the phase slot

**Files:**
- Modify: `src/practice/practice_level.c`

- [ ] **Step 7.1: Patch the phase line render block**

Locate (around line 281):

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

The "BOSS:" label uses 5 glyphs all in the supported set (`B O S S :` per `sSmallChars[]` in `fox_std_lib.c`).

- [ ] **Step 7.2: Build + test + commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add src/practice/practice_level.c
git commit -m "feat(boss-test): render BOSS: <name> on level-select phase line"
```

After this commit, the feature is **functionally complete** — pressing A on BOSSES → CARRIER warps to Corneria with the override flag set. The remaining tasks tighten correctness (warpProgress) and lock down regressions (invariants + functional test).

---

## Task 8: Determine and set the correct `warpProgress`

The placeholder `145000.0f` from Task 1 is a guess. Find the actual progress at which the Carrier event fires.

**Files:**
- Modify: `src/practice/practice_boss_test.c` (the literal in `sBossList[0]`)

- [ ] **Step 8.1: Targeted greps to find the spawn trigger**

Run these in order:

```bash
grep -n "Boss_Initialize.*CARRIER\|OBJ_BOSS_CO_CARRIER\b" src/overlays/ovl_i1/fox_co.c
grep -rn "OBJ_BOSS_CO_CARRIER\|sCorneriaBossEventActor" src/overlays/ovl_i1/ include/assets/
ls include/assets/co_*.h 2>/dev/null
```

The Carrier and Granga are scripted into Corneria's event/object table. Look for an `ObjectInit`/`event_*` table entry whose `id` is `OBJ_BOSS_CO_GRANGA` or `OBJ_BOSS_CO_CARRIER`. The entry will have a `zPos` (the player's progress at which the event fires; `gPathProgress` advances roughly linearly with player Z).

If the spawn table entries name a different progress field than `gPathProgress`, look for the consumer that compares them.

- [ ] **Step 8.2: Choose `warpProgress`**

Pick a value ~5000–8000 progress units before the Carrier trigger. Reference: existing CORNERIA `CP 1` is 93610.3f; the Carrier event is later. Late-Corneria progress values are typically 100k–200k.

If you cannot find an exact value confidently, run a quick empirical pass:
1. Edit `sBossList[0].warpProgress` to e.g. `140000.0f`.
2. `make practice -j4` and boot the ROM in BizHawk.
3. Navigate BOSSES → CARRIER → A.
4. If the Carrier appears within ~3 seconds of gameplay, accept the value. If not, adjust ±10000 and retry.

- [ ] **Step 8.3: Update the literal and commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add src/practice/practice_boss_test.c
git commit -m "feat(boss-test): set Carrier warpProgress to <chosen-value>"
```

---

## Task 9: Lock down with static invariants

Now that every target referenced by the invariant exists, add the invariant. This commit is green out of the gate.

**Files:**
- Modify: `tools/practice_invariants.py`

- [ ] **Step 9.1: Add `check_boss_test()` matching the project's actual style**

Read the existing pattern first: `sed -n '32,60p' tools/practice_invariants.py` shows the module uses a global `errors[]`, `error(msg)`, and `read(path)`. There is no `(name, fn)` registration — `main()` calls `check_*()` directly.

Append after the last `check_*` function (currently `check_practice_text_glyphs` and friends near line 1068+; pick a logical neighbor — e.g., right after `check_practice_text_glyphs` is fine):

```python
def check_boss_test():
    """Boss-test feature: file exists, flag is wired, reset paths are present."""
    boss_test_path = os.path.join(SRC_PRACTICE, "practice_boss_test.c")
    if not os.path.isfile(boss_test_path):
        error(f"Boss-test source missing: {boss_test_path}")
        return

    boss_test_src = read(boss_test_path)
    fox_co_src    = read("src/overlays/ovl_i1/fox_co.c")
    practice_h    = read(INCLUDE_PRACTICE)
    main_src      = read(os.path.join(SRC_PRACTICE, "practice_main.c"))
    level_src     = read(os.path.join(SRC_PRACTICE, "practice_level.c"))
    patch_src     = read("tools/patch_linker_script.py")

    if "gPracticeForceCarrier" not in boss_test_src:
        error("practice_boss_test.c missing gPracticeForceCarrier definition")
    if "gPracticeForceCarrier" not in fox_co_src:
        error("fox_co.c missing gPracticeForceCarrier override")
    if "Practice_BossTest_Launch" not in practice_h:
        error("practice.h missing Practice_BossTest_Launch declaration")
    if "Practice_BossTest_Launch" not in boss_test_src:
        error("practice_boss_test.c missing Practice_BossTest_Launch definition")
    if "gPracticeForceCarrier = false" not in main_src:
        error("Practice_Init missing gPracticeForceCarrier = false reset")
    if "gPracticeForceCarrier = false" not in level_src:
        error("Practice_LevelSelect_Update missing gPracticeForceCarrier = false on non-boss A-press")
    if '"practice_boss_test"' not in patch_src:
        error('tools/patch_linker_script.py missing "practice_boss_test" in PRACTICE_OBJS')

    # Negative check: gPracticeForceCarrier must NOT be a PracticeConfig field (runtime only)
    config_match = re.search(
        r"typedef struct PracticeConfig\s*\{(.*?)\}\s*PracticeConfig;",
        practice_h, re.DOTALL
    )
    if config_match and "gPracticeForceCarrier" in config_match.group(1):
        error("gPracticeForceCarrier must not be a PracticeConfig field (runtime-only state)")
```

- [ ] **Step 9.2: Register the new check in `main()`**

In `main()` (around line 1370+, in the long sequence of `check_*()` calls), add:

```python
    check_boss_test()
```

Place it near other practice-feature checks (e.g., after `check_hit64_logo()` or `check_owl_logo()`).

- [ ] **Step 9.3: Run the invariants**

Run: `python3 tools/practice_invariants.py`
Expected: PASS. If any `error(...)` fires, fix the corresponding source — don't relax the invariant.

- [ ] **Step 9.4: Commit**

```bash
git add tools/practice_invariants.py
git commit -m "test(boss-test): add static invariants for boss-test wiring"
```

---

## Task 10: Functional test (BizHawk Lua)

End-to-end regression test that boots the ROM, drives the menu, asserts the Carrier spawns, and then asserts the force flag is cleared by a subsequent vanilla A-press.

**Files:**
- Create: `tests/test_boss_test_carrier.lua`
- Modify: `tools/extract_symbols.py` (add `sFightCarrier` and `gPracticeForceCarrier` to `SYMBOLS`; add `BOSS_OFFSETS` dict + emit it to Lua)

- [ ] **Step 10.1: Add new symbols to `tools/extract_symbols.py`**

Append to the `SYMBOLS = [...]` list:

```python
    "sFightCarrier",
    "gPracticeForceCarrier",
```

(`gBosses` is already present at line 36 — do not duplicate.)

- [ ] **Step 10.2: Add `BOSS_OFFSETS` and emit it**

Just below `PLAYER_OFFSETS` (or `ACTOR_OFFSETS`, whichever is closer to the bottom), add:

```python
# Boss struct offsets (include/sf64object.h Boss).
# Boss.obj is at offset 0; Object.id is u16 at offset 0x02.
BOSS_OFFSETS = {
    "obj_id": 0x02,  # u16
}
BOSS_SIZEOF = 0x408  # explicit comment in include/sf64object.h
```

In the Lua emission block (search for how `PLAYER_OFFSETS` is written out to the Lua table — usually a `print("S.player = { ... }")`-style block), mirror the pattern for `BOSS_OFFSETS`:

```python
print("-- Boss (gBosses[0]) field offsets")
print("S.boss = {")
for name, off in BOSS_OFFSETS.items():
    print(f"    {name} = 0x{off:02X},")
print(f"    sizeof = 0x{BOSS_SIZEOF:X},")
print("}")
```

- [ ] **Step 10.3: Regenerate `tests/symbols.lua`**

Run: `python3 tools/extract_symbols.py > tests/symbols.lua`
Expected: completes; verify with `grep -E "sFightCarrier|gPracticeForceCarrier|S\\.boss" tests/symbols.lua`.

- [ ] **Step 10.4: Write the functional test**

Create `tests/test_boss_test_carrier.lua`:

```lua
-- Test: Selecting BOSSES → CARRIER from the level select warps into Corneria
-- with the Attack Carrier spawning instead of Granga, and the force flag is
-- cleared by a subsequent vanilla A-press.

local H = dofile("tests/harness.lua")
H.test_name = "boss_test_carrier"

local OBJ_BOSS_CO_CARRIER = 293  -- include/sf64object.h:620
-- local OBJ_BOSS_CO_GRANGA  = 292  -- include/sf64object.h:619
local LEVEL_CORNERIA      = 0    -- include/sf64level.h
local CARRIER_INDEX       = 0    -- gBosses[CARRIER]: include/fox_co.h:158

-- Wait for level select.
local ok = H.wait_until(function()
    return H.practice_screen() == H.S.const.PSCREEN_LEVEL_SELECT
        and H.game_state() == H.S.const.GSTATE_MAP
end, 600, "level select")
H.assert_true(ok, "reached level select")

-- BOSSES is the last entry in sLevelList. Press D-pad down enough times
-- to definitely land on it. Current LEVEL_COUNT = 17 (16 vanilla + BOSSES);
-- updating this constant if sLevelList grows is part of feature work, not
-- this test.
local LEVEL_COUNT = 17
for i = 1, LEVEL_COUNT - 1 do
    H.press({Down = true})
    H.advance(2)
end

-- A: launch the boss test.
H.press({A = true})

-- Wait for gameplay to be active.
ok = H.wait_for_gameplay(900)
H.assert_true(ok, "gameplay became active")

-- Verify we landed in Corneria.
H.assert_eq(H.read_s32(H.S.gCurrentLevel), LEVEL_CORNERIA,
    "current level is Corneria")

-- Force-flag should be set during the warp.
H.assert_eq(H.read_u8(H.S.gPracticeForceCarrier), 1,
    "gPracticeForceCarrier set during boss-test launch")

-- Wait for the Carrier boss to spawn (timeout 600 frames ≈ 10s).
local boss_addr = H.S.gBosses + CARRIER_INDEX * H.S.boss.sizeof
local boss_ok = H.wait_until(function()
    return H.read_u16(boss_addr + H.S.boss.obj_id) ~= 0
end, 600, "boss spawn")
H.assert_true(boss_ok, "a boss spawned within 600 frames")

-- The boss must be the Carrier, not Granga.
local boss_id = H.read_u16(boss_addr + H.S.boss.obj_id)
H.assert_eq(boss_id, OBJ_BOSS_CO_CARRIER,
    "boss id is OBJ_BOSS_CO_CARRIER (293), not Granga (292)")

-- And sFightCarrier must be 1 (true).
H.assert_eq(H.read_u8(H.S.sFightCarrier), 1, "sFightCarrier == 1")

H.finish()
```

NOTE on `H.read_u16`: if the harness does not yet expose `read_u16`, add it to `tests/harness.lua` alongside `read_u8`/`read_s32`:

```lua
function H.read_u16(addr)
    return mainmemory.read_u16_be(addr)
end
```

(Verify with `grep -n "read_u16" tests/harness.lua` first.)

- [ ] **Step 10.5: Run the test**

Run: `python3 tools/run_tests.py test_boss_test_carrier`
Expected: PASS.

Failure-mode triage:
- **"boss spawn" timeout** → `warpProgress` (Task 8) is wrong. Lower it (warp earlier) or raise it (warp closer to the trigger).
- **"current level is Corneria" fails** → `LEVEL_CORNERIA != 0`. Check `include/sf64level.h` and update the constant.
- **`sFightCarrier == 1` fails but Carrier spawned** → engine hook polarity bug. Re-read the patch from Task 3.
- **Boss obj.id is 0 forever** → `BOSS_SIZEOF` or `BOSS_OFFSETS.obj_id` wrong. Re-verify against `include/sf64object.h:244-249`.
- **`gPracticeForceCarrier == 1` after the test (and test wants 0)** → not applicable here; this test only checks the set side. Manual smoke (Task 11) covers the reset.

- [ ] **Step 10.6: Commit**

```bash
git add tests/test_boss_test_carrier.lua tools/extract_symbols.py tests/symbols.lua tests/harness.lua
git commit -m "test(boss-test): add functional test for Carrier warp"
```

---

## Task 11: Integration verification

- [ ] **Step 11.1: Full pre-commit pipeline**

Run: `python3 tools/practice_invariants.py && make practice -j4 && python3 tools/run_tests.py`
Expected: all green.

- [ ] **Step 11.2: Manual smoke (REQUIRED, not optional)**

Boot the ROM in BizHawk or Dolphin:

1. Verify the level-select shows BOSSES at the bottom of the list.
2. Highlight BOSSES — phase line reads `BOSS: CARRIER`.
3. Press L/R — name doesn't change (only one boss in v1).
4. Press A — Corneria loads. Within ~3 seconds the Attack Carrier (flying ship) appears. Granga (the walker) does NOT appear.
5. Pause → return to level-select.
6. Highlight CORNERIA, START, press A — vanilla Corneria runs. **Granga appears at the boss event, NOT the Carrier.** This proves the non-boss A-press cleared `gPracticeForceCarrier`.
7. Optional: highlight BOSSES while paying attention to BGM. Per Open Question 4 (spec), `planetId = PLANET_CORNERIA` may cause the Corneria audio preview to start when BOSSES is highlighted. If it does, that's an acceptable v1 quirk; record the observation and move on.

If step 6 fails (Carrier appears on a vanilla CORNERIA START launch), the reset in Task 6.1 is missing or misplaced — re-inspect `Practice_LevelSelect_Update`.

- [ ] **Step 11.3: Commit any fixes from manual smoke**

If the smoke surfaced bugs, fix and commit. Otherwise no commit needed.

---

## Done Criteria

- [ ] `python3 tools/practice_invariants.py` exits 0.
- [ ] `make practice -j4` succeeds.
- [ ] `python3 tools/run_tests.py test_boss_test_carrier` passes.
- [ ] `python3 tools/run_tests.py` (full suite) passes.
- [ ] No commit in this branch used `--no-verify`.
- [ ] Manual smoke (Task 11.2) confirmed: BOSSES → CARRIER spawns Carrier; CORNERIA START still spawns Granga.
- [ ] Spec's Open Question 1 (warpProgress) resolved (Task 8).
- [ ] Spec's Open Question 4 (planet audio preview) observed and either accepted as v1 quirk or fixed.

## Out of Scope

- Adding more bosses (Granga, Spyborg, etc.) — easy follow-up by appending to `sBossList[]`.
- Per-boss arena cleanup (suppress pre-boss enemies).
- Custom void arenas.
- Replacing the `LEVEL_INVALID` sentinel with a dedicated `isBossList` flag.
- Column-based level-select navigation.
