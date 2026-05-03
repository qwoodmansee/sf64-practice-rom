# Boss Test Stage

## Summary

Add a "BOSSES" entry at the bottom of the practice level-select list.
Selecting it lets the user scroll through a list of bosses (initially just
CARRIER) using the same L/R phase-navigation that other levels use, then
press A to warp into the host level at a progress value just before the
boss event triggers, with a flag that forces the desired boss to spawn
(rather than its xPath-determined alternative). The Carrier (Corneria
flying boss) is the first and only entry; the boss data table and code
paths are designed so adding more bosses later is a one-line change.

## Goals

- One new menu entry that warps the player into a fight against the Corneria
  Attack Carrier without requiring them to play through the level or take the
  Falco shortcut.
- A `sBossList[]` data table that scales to additional bosses (Granga,
  Spyborg, Meteo Crusher, etc.) by appending entries.
- Reuse the existing `Practice_LaunchLevel` warp infrastructure rather than
  duplicating it.

## Non-Goals

- A custom void/arena environment with stripped terrain.
- Suppression of pre-boss enemies, missiles, or scenery (the player will fly
  through ~1-2 seconds of run-up corridor; this is acceptable for v1).
- Custom death/victory handling — vanilla level-end behavior is fine.
- Forcing a non-canonical boss in any level except via the listed flag
  pattern (e.g., we are not making Granga spawnable from the BOSSES menu in
  v1, even though the same flag pattern would extend to it).
- Column-based level-select navigation. The existing code uses a single
  linear vertical list; the `column` field on `LevelEntry` is for visual
  grouping in the draw function only and is not read by the input handler.
  Adding column navigation is out of scope.

## Existing Code Touchpoints

Symbols referenced by this spec:

- `src/practice/practice_level.c`
  - `Practice_LevelSelect_Update` — input handler. U/D scrolls
    `sSelectedLevel` through `sLevelList`. L/R scrolls `sSelectedPhase`
    when the selected level has `phaseCount > 1`. A presses launch the
    selected phase via `Practice_LaunchLevel`.
  - `Practice_LevelSelect_Draw` — render function for the list.
  - `Practice_LaunchLevel(LevelId, s32 phase, f32 checkpointProgress)` —
    the single funnel for all level launches; sets `gNextLevel`,
    `gNextLevelPhase`, `gPracticeCheckpointProgress`, etc. and transitions
    to `GSTATE_PLAY`.
- `src/overlays/ovl_i1/fox_co.c`
  - `Corneria_CoCarrier_Init` — at line 1668, the Granga-vs-Carrier
    discriminator is `if (fabsf(gPlayer[0].xPath) < 1.0f)` — a player
    x-position check at the moment carrier-init fires. The "if" branch
    sets `sFightCarrier = false` (Granga path); the "else" branch sets
    `sFightCarrier = true` (Carrier path). `sFightCarrier` is a
    file-static `u8` declared in this same file.
- `include/sf64object.h` — `OBJ_BOSS_CO_CARRIER`, `CARRIER` enum, etc.
- `include/context.h` — `gBosses[4]` array.

## User-Facing Behavior

The practice level-select screen is a single vertical list iterated with
D-pad up/down. After `VENOM 2` (the current last entry) we add one more
entry, **BOSSES**.

When BOSSES is the highlighted level:

- The "level name" line shows `BOSSES`.
- The "phase" line (the same line that normally shows `START` / `CP 1` /
  `WARP` for other levels) shows the currently selected boss name —
  initially `CARRIER`, the only entry.
- L_JPAD / R_JPAD scroll through `sBossList[]` (wrap-around), exactly like
  phase scrolling.
- A_BUTTON dispatches to `Practice_BossTest_Launch(sSelectedPhase)` instead
  of `Practice_LaunchLevel` directly. (The boss-test path internally calls
  `Practice_LaunchLevel`; see Architecture.)

This reuses the existing render and input widgets verbatim — no new menu
screen, no new navigation mode. The "phase" slot on the BOSSES entry is
re-purposed to mean "boss within the boss roster."

After A-press: the ROM warps into Corneria at a progress value chosen so
the Carrier event fires within ~1-2 seconds of gameplay. Death and victory
behavior follow vanilla scripting (level end → results → return to
level-select via the existing pause-back-to-menu path).

## Architecture

### New file: `src/practice/practice_boss_test.c`

Owns the boss data table and the launch entry point.

```c
typedef struct {
    const char* name;        // Display name, e.g. "CARRIER"
    LevelId hostLevel;       // Level to warp into (LEVEL_CORNERIA)
    s32 phase;               // Phase argument for Practice_LaunchLevel (0)
    f32 warpProgress;        // Progress value to warp to
    bool forceCarrier;       // If true, set gPracticeForceCarrier before launch
    // Future flags as more bosses get added (forceGrangaShortcut, etc.)
} BossEntry;

static BossEntry sBossList[] = {
    { "CARRIER", LEVEL_CORNERIA, 0, /* warpProgress TBD */, true },
};

s32  Practice_BossTest_GetCount(void);
const char* Practice_BossTest_GetName(s32 index);
void Practice_BossTest_Launch(s32 index);
```

Body of `Practice_BossTest_Launch`:

```c
void Practice_BossTest_Launch(s32 index) {
    BossEntry* e = &sBossList[index];
    if (index < 0 || index >= ARRAY_COUNT(sBossList)) return;
    Practice_LaunchLevel(e->hostLevel, e->phase, e->warpProgress);
    /* Set force flags AFTER Practice_LaunchLevel so a normal-path A-press
     * (which clears flags via the path in Practice_LevelSelect_Update,
     * see below) cannot leave them stale across a subsequent boss launch. */
    gPracticeForceCarrier = e->forceCarrier;
}
```

### Modified: `src/practice/practice_level.c`

1. Add a synthetic `LevelEntry` to the end of `sLevelList`:

   ```c
   { "BOSSES", LEVEL_INVALID, PLANET_CORNERIA, 8, 0,
     { /* phases unused; boss list is consulted instead */ } },
   ```

   The `levelId == LEVEL_INVALID` and `phaseCount == 0` together act as a
   sentinel. (`PLANET_CORNERIA` is a placeholder; the planet id is only
   used by visual/audio code that won't fire for this entry. If audio
   triggers at runtime, switch to a SAFE_NONE-style sentinel — see
   Open Implementation Question 4.)

2. In `Practice_LevelSelect_Update`, treat the BOSSES entry as a special
   case for L/R and A:

   ```c
   if (sLevelList[sSelectedLevel].levelId == LEVEL_INVALID) {
       /* BOSSES entry: phase slot indexes sBossList */
       s32 bossCount = Practice_BossTest_GetCount();
       if (press->button & L_JPAD) {
           sSelectedPhase = (sSelectedPhase - 1 + bossCount) % bossCount;
       }
       if (press->button & R_JPAD) {
           sSelectedPhase = (sSelectedPhase + 1) % bossCount;
       }
       if (press->button & A_BUTTON) {
           Practice_BossTest_Launch(sSelectedPhase);
           return;
       }
   } else {
       /* existing phase/A-press logic, plus a flag-clear */
       if (press->button & A_BUTTON) {
           gPracticeForceCarrier = false;  // ensure clean for non-boss launches
           ...existing launch code...
       }
   }
   ```

3. In `Practice_LevelSelect_Draw`, when rendering the phase line for the
   BOSSES entry, draw `Practice_BossTest_GetName(sSelectedPhase)` instead
   of the phase name.

4. Add a guard so U/D handlers reset `sSelectedPhase = 0` when navigating
   into or out of the BOSSES entry (existing code already does this for
   all level changes, so no new code needed — just verify it still works
   when `phaseCount == 0`).

### Modified: `src/overlays/ovl_i1/fox_co.c`

In `Corneria_CoCarrier_Init`, wrap the existing xPath check in a
practice-aware override:

```c
#ifdef PRACTICE_ROM
    extern bool gPracticeForceCarrier;
    if (gPracticeForceCarrier || (fabsf(gPlayer[0].xPath) >= 1.0f)) {
#else
    if (fabsf(gPlayer[0].xPath) >= 1.0f) {
#endif
        /* Carrier branch: sFightCarrier = true, etc. */
    } else {
        /* Granga branch: sFightCarrier = false, etc. */
    }
```

(Note: the existing condition is inverted — `if (fabsf(...) < 1.0f)` is
the Granga branch. The patched form here flips the polarity for clarity;
implementation should match the existing structure.)

`sFightCarrier` is a `u8` and remains `static` to `fox_co.c`. It is set
only inside this init function; the `gPracticeForceCarrier` override
ensures the same `sFightCarrier = true; ...` block runs that vanilla
Carrier route runs.

### Modified: `include/practice.h`

Add declarations:

```c
extern bool gPracticeForceCarrier;
s32  Practice_BossTest_GetCount(void);
const char* Practice_BossTest_GetName(s32 index);
void Practice_BossTest_Launch(s32 index);
```

`gPracticeForceCarrier` is a runtime-only global — explicitly NOT part of
`PracticeConfig`. It is not persisted to slots, not surfaced in the
options menu, and is reset on every non-boss A-press from the
level-select.

### Modified: `src/practice/practice_main.c`

`Practice_Init` initializes `gPracticeForceCarrier = false` (one-time
default at boot).

### Modified: `tools/patch_linker_script.py`

Add `practice_boss_test` to `PRACTICE_OBJS` so the new object is wired into
all four sections (`.text`, `.data`, `.rodata`, `.bss`) of the generated
linker script.

## Data Flow

```
Boot → level-select screen
    User D-pad-down to BOSSES entry (last in sLevelList)
    User L/R to scroll boss list (only CARRIER for now)
    User presses A
        → Practice_BossTest_Launch(sSelectedPhase)
            → Practice_LaunchLevel(LEVEL_CORNERIA, 0, warpProgress)
                → sets gNextLevel, gNextLevelPhase, gPracticeCheckpointProgress
                → triggers GSTATE_PLAY transition
            → gPracticeForceCarrier = true
        → engine boots Corneria, advances player to warpProgress
        → at scripted Carrier spawn, Corneria_CoCarrier_Init runs
            → reads gPracticeForceCarrier; takes Carrier branch
              regardless of gPlayer[0].xPath value
        → vanilla Carrier fight plays out
    On boss death or player death:
        → vanilla level-end / game-over
        → returns to level-select via existing pause-menu path
    Next user action:
        → If user picks any non-boss-test entry and presses A,
          gPracticeForceCarrier is cleared in that A-press branch.
        → If user picks BOSSES again, the flag is re-set by
          Practice_BossTest_Launch.
```

State-reset contract: `gPracticeForceCarrier` is true *only* between a
boss-test A-press and the next non-boss A-press (or the next
boot). No other code path may leave it set.

## Open Implementation Questions

These are deferred to implementation, not design.

1. **Exact `warpProgress` value.** Need to read Corneria's event table /
   actor spawn list to find the progress at which the Carrier event fires,
   then back off ~1-2 seconds of player travel. Existing `CP 1` is at
   `93610.3f`; the Carrier event is later than that.

2. **xPath-consistency check at warp-in.** Overriding `sFightCarrier` in
   `Corneria_CoCarrier_Init` is sufficient for that one function, but
   other Corneria scripting may also branch on `gPlayer[0].xPath` near
   the boss-event trigger (e.g., to spawn pre-boss enemies on the Falco
   shortcut path). Implementation should grep `fox_co.c` for other
   `xPath` reads near the boss spawn region and either accept the
   inconsistency (small visual quirk) or also pin `gPlayer[0].xPath` to
   a non-zero value at warp-in via a similar `gPracticeForceCarrier`
   gate. v1 acceptance bar: as long as the Carrier itself spawns
   correctly, minor pre-boss visual quirks are acceptable.

3. **Render-time drawing of the BOSSES "phase" slot.** The existing
   `Practice_LevelSelect_Draw` reads from `sLevelList[i].phases[...]`
   for phase rendering. The BOSSES entry has `phaseCount == 0` so the
   existing phase loop will skip it; we add a one-line conditional:
   if rendering the highlighted BOSSES entry, draw
   `Practice_BossTest_GetName(sSelectedPhase)` in the phase slot.
   Verify no other consumer of `sLevelList[*].phases` (e.g., audio
   preview) breaks on `phaseCount == 0`.

4. **Planet-id for the BOSSES entry.** `LevelEntry.planetId` is used
   somewhere (likely the audio preview / map graphic). If setting
   `PLANET_CORNERIA` causes Corneria audio to preview when BOSSES is
   highlighted, we either accept that or add a `PLANET_NONE` sentinel
   path that suppresses preview.

If question 2 turns out to be hairier than expected (Corneria scripting
spawns Granga as an actor *before* `Corneria_CoCarrier_Init` ever runs,
based on xPath at an earlier progress), the fallback is to short-circuit:
spawn the Carrier boss directly via `Boss_Initialize` calls from the
practice layer, bypassing route-choice scripting entirely. This fallback
is recorded here but not chosen unless necessary.

## Testing

Per the project's mandatory testing rule (every fix and feature must have
both a static invariant and a functional test where applicable):

### Static invariants (`tools/practice_invariants.py`)

Add a `check_boss_test()` function asserting:

- `src/practice/practice_boss_test.c` exists.
- `practice_boss_test` is in `PRACTICE_OBJS` in `tools/patch_linker_script.py`.
- `gPracticeForceCarrier` is referenced in both `practice_boss_test.c` and
  `src/overlays/ovl_i1/fox_co.c`.
- `Practice_BossTest_Launch` is declared in `include/practice.h` and
  defined in `practice_boss_test.c`.
- `gPracticeForceCarrier = false` (or equivalent reset) appears in the
  non-boss A-press branch of `Practice_LevelSelect_Update`. (Grep for
  the line in `src/practice/practice_level.c`.)
- `gPracticeForceCarrier` is NOT a field of `PracticeConfig` in
  `include/practice.h` (negative check — confirms runtime-only).

### Functional test (`tests/test_boss_test_carrier.lua`)

BizHawk Lua script that:

1. Boots the ROM.
2. Navigates the level-select to the BOSSES entry by pressing D-pad down
   N times where N = `LEVEL_COUNT - 1` (BOSSES is the last entry).
3. Presses A.
4. Advances frames until the Carrier boss spawns (poll `gBosses[CARRIER]`
   for non-zero `obj.id`, with a generous timeout — e.g., 600 frames).
5. Asserts `gBosses[CARRIER].obj.id == OBJ_BOSS_CO_CARRIER` (i.e., the
   Carrier is loaded, not Granga; `OBJ_BOSS_CO_GRANGA` is the negative
   case).
6. Bonus assertion: `sFightCarrier == 1` (cast `u8`).

Symbols needed in `tools/extract_symbols.py`:

- `gBosses` base address.
- `Boss` struct offset for `obj.id`.
- `OBJ_BOSS_CO_CARRIER` and `OBJ_BOSS_CO_GRANGA` enum values.
- `gPracticeForceCarrier` address.
- `sFightCarrier` address (file-static; may need an explicit symbol
  emission tweak if the linker map omits statics — fallback is to drop
  the bonus assertion).

## Out of Scope (Future Work)

- Adding the rest of the SF64 boss roster (Granga, Spyborg, Meteo Crusher,
  Sarumarine, Goras, Bacoon, Aquas core, etc.).
- Per-boss arena cleanup (suppress pre-boss actors, reposition player to a
  clean spawn next to boss).
- Full custom void arenas.
- Boss-rush mode (chained boss fights).
- Column-based level-select navigation.
