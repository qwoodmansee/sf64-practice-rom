---
name: release
description: Full pre-release checklist for the SF64 practice ROM — build, test, patch, and publish. Run every step before tagging a version.
---

## When to use

Run this skill before every public release. All steps are mandatory. Do not skip
the functional tests — they are what catch silent regressions that compile fine
but break at runtime.

---

## Pre-release checklist

### 1. Full clean build

```bash
rm -rf build/ && make practice -j4
```

Verify: no linker errors, ROM size fits within budget (check `.practice_late_core`
segment usage in map file), build log ends with checksum line.

### 2. Static invariants

```bash
python3 tools/practice_invariants.py
```

All checks must pass. Fix any failures before proceeding.

### 3. Functional tests (mupen64plus)

```bash
python3 tools/m64p_test_runner.py
```

All tests must pass (exit 0). These tests boot the ROM headless and verify memory
state — they catch audio bugs, input bugs, and gameplay regressions that static
analysis cannot find.

**If any test fails:** do not release. File the failure and fix it first. Use the
`/m64p-repro` skill if you need to add a new repro test for a community-reported bug.

Individual test run: `python3 tools/m64p_test_runner.py <test_name>`

Headed run (window + audio, for manual verification): tests already run headed by
default. Look for the mupen64plus window on your desktop while the test runs.

### 4. Patcher validation

```bash
cd tools/patcher
npm test
npm run typecheck
npm run build
cd ../..
```

### 5. Generate release patch

```bash
make practice-patch PATCH_VERSION=<version>
```

This produces:
- `tools/patcher/src/assets/sf64-practice-v<version>.bps`
- `tools/patcher/src/assets/manifest.json` (updated with new hashes)

### 6. Version bump

Update the version string in `include/practice.h`:
```c
#define PRACTICE_ROM_VERSION "V<version>"
```

### 7. Commit and tag

```bash
git add include/practice.h tools/patcher/src/assets/
git commit -m "release: bump to v<version> with patch and manifest"
git tag v<version>
```

---

## Bug reports from the community

When a community member reports a bug, the workflow is:

1. **Write a repro test first** — use `/m64p-repro` skill. The test PASSES when
   bug is present, FAILS when fixed. Commit it to the fix branch.
2. **Hand the red test to an implementor** — "fix the red test" is the complete spec.
3. **Flip the test to green regression format** — once fixed, invert the assertions
   so the test PASSES when correct and FAILS if the bug returns.
4. **The test stays in `tests/`** — it runs on every release via step 3 above,
   permanently guarding against that regression.

See `.claude/skills/m64p-repro/SKILL.md` for the complete recipe.

---

## Existing functional tests

| File | What it guards |
|------|---------------|
| `tests/test_corneria_bgm_preview_silent.py` | BGM plays after same-spec preview → launch (isWaitingForFonts bug) |
| `tests/test_checkpoint_start.py` | Checkpoint start positions are correct |
| `tests/test_config_defaults.py` | PracticeConfig defaults match expected values |
| `tests/test_cutscene_skip.py` | Cutscene skip fires at correct frame |
| `tests/test_e2e_levels.py` | All levels boot and reach PLAY_UPDATE without crash |

---

## Key invariants

- ROM must boot to level select (GSTATE_MAP=4) within 60s
- All levels must reach PLAY_UPDATE without hanging or crashing
- BGM must play after level launch (not silent due to isWaitingForFonts or rescue failure)
- PracticeConfig defaults must match `Practice_Init()` — checked by `test_config_defaults.py`
