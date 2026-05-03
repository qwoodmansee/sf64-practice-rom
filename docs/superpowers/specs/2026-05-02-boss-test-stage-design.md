# Boss Test Stage

## Summary

Add a "BOSSES" column to the practice level-select screen. Selecting a boss
warps directly into the host level at a progress value just before the boss
event triggers, with a flag forcing the desired boss to spawn (vs. its
route-determined alternative). The Carrier (Corneria flying boss) is the
first and only entry; the data table and code paths are designed so adding
more bosses later is a one-line change.

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

## User-Facing Behavior

The practice level-select screen currently lays levels out in columns 1-7
(`column` field on `LevelEntry`), one column per game-progression group:

| Col | Levels |
|-----|--------|
| 1 | CORNERIA |
| 2 | METEO, SECTOR Y |
| 3 | FORTUNA, KATINA, AQUAS |
| 4 | SECTOR X, SOLAR, ZONESS |
| 5 | TITANIA, MACBETH, SECTOR Z |
| 6 | BOLSE, AREA 6 |
| 7 | VENOM 1, VENOM 2 |

This spec adds **column 8: BOSSES**. The column initially contains one
entry, **CARRIER**. The user navigates to the BOSSES column the same way they
navigate between any other columns (per the existing level-select input
handler), highlights CARRIER, and presses A. The ROM warps to Corneria at a
progress value chosen so the Carrier event fires within ~1-2 seconds of
gameplay. Death and victory behavior follow vanilla scripting (level end →
results → return to level-select via the existing pause-back-to-menu path).

The BOSSES column has no "phase" sub-list — each boss is a single warp
target. The phase navigation UI in `Practice_StateMenu_DrawLevel` shows
nothing beneath the boss name (or shows the boss name in place of the phase
label, depending on render path; see "Open Implementation Questions").

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

`Practice_BossTest_Launch` sets the route-choice flag(s) on the boss entry,
then delegates to `Practice_LaunchLevel(hostLevel, phase, warpProgress)`.

### Modified: `src/practice/practice_level.c`

The level-select rendering and input handlers (`Practice_StateMenu_DrawLevel`,
`Practice_StateMenu_UpdateLevel`) currently iterate `sLevelList` and assume
every column entry has phases. We add column 8 as a special case: when
`sSelectedLevel` points at a synthetic "BOSSES" entry, the UI calls into
`practice_boss_test.c` for the boss list and the A-press path calls
`Practice_BossTest_Launch` instead of `Practice_LaunchLevel`.

Minimal-change implementation: add one synthetic `LevelEntry` to `sLevelList`
with `column = 8`, a sentinel `levelId` (e.g., `LEVEL_INVALID`), and check
for that sentinel in the launch path. The phase navigation arrows are
repurposed to scroll through `sBossList[]` when this entry is selected.

### Modified: `src/overlays/ovl_i1/fox_co.c`

`Corneria_CoCarrier_Init` already contains route-choice scripting that sets
`sFightCarrier = false` or `sFightCarrier = true` based on whether the
player took the Falco shortcut. We add a `#ifdef PRACTICE_ROM` block that
checks `extern bool gPracticeForceCarrier` near the top of init and, if set,
forces `sFightCarrier = true` regardless of the route-choice condition.

The exact patch point will be confirmed during implementation; the contract
is: by the time `Corneria_CoCarrier_Init` returns, `sFightCarrier == true`
when `gPracticeForceCarrier == true`.

### Modified: `include/practice.h`

Add declarations:

```c
extern bool gPracticeForceCarrier;
s32  Practice_BossTest_GetCount(void);
const char* Practice_BossTest_GetName(s32 index);
void Practice_BossTest_Launch(s32 index);
```

### Modified: `src/practice/practice_main.c`

`Practice_Init` resets `gPracticeForceCarrier = false` so that returning to
the level-select and choosing a normal level (e.g., CORNERIA START) does not
inherit the force flag from a previous boss-test run.

### Modified: `tools/patch_linker_script.py`

Add `practice_boss_test` to `PRACTICE_OBJS` so the new object is wired into
all four sections (`.text`, `.data`, `.rodata`, `.bss`) of the generated
linker script.

## Data Flow

```
Boot → level-select screen
    User navigates to BOSSES column
    User highlights CARRIER (only entry)
    User presses A
        → Practice_BossTest_Launch(0)
            → gPracticeForceCarrier = true
            → Practice_LaunchLevel(LEVEL_CORNERIA, 0, warpProgress)
                → sets gNextLevelPhase, gPracticeCheckpointProgress, etc.
                → engine boots Corneria
        → engine spawns actors per script, advances player to warpProgress
        → at scripted Carrier spawn, Corneria_CoCarrier_Init runs
            → reads gPracticeForceCarrier, forces sFightCarrier = true
        → vanilla Carrier fight plays out
    On boss death or player death:
        → vanilla level-end / game-over
        → returns to level-select via existing pause-menu path
    On next Practice_Init (level boot or re-entry):
        → gPracticeForceCarrier = false (reset)
```

## Open Implementation Questions

These are deferred to implementation, not design — listing them so they're
not lost.

1. **Exact `warpProgress` value.** Need to read Corneria's event table /
   actor spawn list to find the progress at which the Carrier event fires,
   then back off ~1-2 seconds of player travel. Existing `CP 1` is at
   `93610.3f`; the Carrier event is later than that.

2. **`sFightCarrier` patch site.** `Corneria_CoCarrier_Init` shows a clear
   `if (...) sFightCarrier = false; else sFightCarrier = true;` pattern. We
   need to confirm whether overriding inside this init is sufficient or
   whether earlier scripting also reads the route-choice condition and
   spawns different actors (e.g., does the route choice cause Granga to
   spawn instead, before the Carrier init even runs?). If the latter, we
   may need to set the route-choice flag (whatever it is — likely a static
   in `fox_co.c` set when the player passes through the shortcut arches)
   from `Practice_BossTest_Launch` rather than overriding in init.

3. **Phase UI repurposing.** The level-select currently shows phase names
   under the level name. For the BOSSES column, the simplest UX is: the
   "level name" line shows "BOSSES" and the "phase" line shows the boss
   name ("CARRIER"). Left/right phase arrows scroll through bosses. This
   reuses the existing render/input code with no new widgets.

If question 2 turns out to be hairier than expected, the fallback is to
short-circuit: spawn the Carrier boss directly via `Boss_Initialize` calls
from the practice layer, bypassing route-choice scripting entirely. This
fallback is recorded here but not chosen unless necessary.

## Testing

Per the project's mandatory testing rule (every fix and feature must have
both a static invariant and a functional test where applicable):

### Static invariants (`tools/practice_invariants.py`)

Add a `check_boss_test()` function asserting:

- `src/practice/practice_boss_test.c` exists and is in `PRACTICE_OBJS`.
- `gPracticeForceCarrier` is referenced in both `practice_boss_test.c` and
  `src/overlays/ovl_i1/fox_co.c`.
- `Practice_BossTest_Launch` is declared in `include/practice.h` and
  defined in `practice_boss_test.c`.
- `gPracticeForceCarrier` is reset in `Practice_Init`.

### Functional test (`tests/test_boss_test_carrier.lua`)

BizHawk Lua script that:

1. Boots the ROM.
2. Navigates the level-select to the BOSSES column (right-press until
   `sSelectedLevel` points at the synthetic boss entry).
3. Presses A.
4. Advances frames until the Carrier boss spawns (poll `gBosses[CARRIER]`
   for non-zero `obj.id`, with timeout).
5. Asserts `gBosses[CARRIER].obj.id == OBJ_BOSS_CO_CARRIER` (i.e., the
   Carrier is loaded, not Granga).

Symbols needed in `tools/extract_symbols.py`:

- `gBosses` base address.
- `Boss` struct offset for `obj.id`.
- `OBJ_BOSS_CO_CARRIER` enum value.
- `gPracticeForceCarrier` address (also useful for any future boss tests).

## Out of Scope (Future Work)

- Adding the rest of the SF64 boss roster (Granga, Spyborg, Meteo Crusher,
  Spyborg, Sarumarine, Goras, Bacoon, Aquas core, etc.).
- Per-boss arena cleanup (suppress pre-boss actors, reposition player to a
  clean spawn next to boss).
- Full custom void arenas.
- Boss-rush mode (chained boss fights).
