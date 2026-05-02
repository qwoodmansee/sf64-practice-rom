---
description: Walk the 8-step checklist for adding a new practice feature to the SF64 ROM. Provide a feature name (snake_case) and description as arguments, e.g. /new-feature lag_counter "counts lag frames per level".
---

Add a new practice feature following the CLAUDE.md checklist. Work through each step in order; do not skip or reorder.

## Inputs

The user provides: **feature-name** (snake_case, e.g. `lag_counter`) and a **brief description**.

If not provided, ask before proceeding.

Derive from the feature name:
- `<Name>` = PascalCase version (e.g. `LagCounter`)
- `<name>` = snake_case (e.g. `lag_counter`)
- `<NAME>` = UPPER_CASE (e.g. `LAG_COUNTER`)

---

## Step 1 — Source file

Create `src/practice/practice_<name>.c`:

```c
#ifdef PRACTICE_ROM
#include "practice.h"

void Practice_<Name>_Init(void) {
}

void Practice_<Name>_Update(void) {
}

void Practice_<Name>_Draw(void) {
}

#endif
```

Read existing `src/practice/practice_hud.c` for style reference before writing.

---

## Step 2 — Header declarations

Add to `include/practice.h` (inside the existing practice declarations block):

```c
void Practice_<Name>_Init(void);
void Practice_<Name>_Update(void);
void Practice_<Name>_Draw(void);
```

---

## Step 3 — Config field

Add `bool show<Name>;` to `PracticeConfig` in `include/practice.h`.

---

## Step 4 — Init default

In `Practice_Init()` in `src/practice/practice_main.c`, add:

```c
gPracticeConfig.show<Name> = false;
Practice_<Name>_Init();
```

---

## Step 5 — Lifecycle wiring

In `Practice_Update()` and `Practice_Draw()` in `practice_main.c`:

```c
// in Update:
if (gPracticeConfig.show<Name>) { Practice_<Name>_Update(); }

// in Draw:
if (gPracticeConfig.show<Name>) { Practice_<Name>_Draw(); }
```

---

## Step 6 — Menu entries

In `src/practice/practice_state.c`:

1. Add `OOPT_<NAME>` to the `OptionsOption` enum (after the last existing entry).
2. In `StateMenu_UpdateOptions()`, add a toggle case:
   ```c
   case OOPT_<NAME>:
       gPracticeConfig.show<Name> ^= 1;
       break;
   ```
3. In `StateMenu_DrawOptions()`, add a draw line (follow the existing pattern).
4. Increment `boxHeight` and `helpY` if the menu grew.

---

## Step 7 — Linker registration

In `tools/patch_linker_script.py`, add `"practice_<name>"` to `PRACTICE_OBJS` (after `practice_save` or similar, keeping alphabetical grouping).

---

## Step 8 — Linker script (if already patched)

Check if `linker_scripts/us/rev1/starfox64.ld` already contains `practice_main`:

```bash
grep -c practice_main linker_scripts/us/rev1/starfox64.ld
```

If it does (count > 0), manually add `practice_<name>.o` to all four sections: `.text`, `.data`, `.rodata`, `.bss` — following the pattern of `practice_hud.o` entries.

---

## Verification

After all 8 steps, run:

```bash
python3 tools/practice_invariants.py && make practice -j4
```

Report PASS or the first error.

---

## Tests (mandatory)

Before finishing:
1. Add a static invariant to `tools/practice_invariants.py` verifying the new feature's engine hook (if any) exists in the right engine file.
2. Add `tests/test_<name>.lua` — at minimum a smoke test that the config field is false by default and toggling it doesn't crash.
