# Phase 4 — heap audit (hardware verification)

> **Agent handoff:** The old **“do not SAVE / LOAD”** warning applied to an
> earlier design (large `.practice_pool` in low RAM overlapping the dynamic
> load window). The **shipped** layout splits work: save **globals** stay in
> `.main_bss`; the **256 KB × 4 slot TLV buffer** lives in Expansion Pak DRAM
> (`practice_save_slotpool.c`, VMA `0x80400000`). Stock 4 MB (`osMemSize ==
> 0x400000`) disables save at boot. For silent crashes while saving, build with
> `PRACTICE_SAVE_TRACE=1` and read `.claude/skills/practice-hw-isv-trace/SKILL.md`
> (includes **stack rule**: never allocate `PracticeSnapshot` on the game thread
> stack — use static scratch). Flash from any worktree: `./tools/sc64dev`.

## Current state (read first)

| Topic | Behaviour |
|-------|------------|
| Stock 4 MB | `gPracticeSaveDisabled=1`; no slot pool pointer passed; safe to boot and play. |
| Expansion Pak | `osMemSize == 0x00800000`: 4 RAM slots, pool base from `[heap] boot pool=` (expect `80400000`). |
| Checkpoint hotkeys | Wired only when `gPracticeScreen == PSCREEN_GAMEPLAY`. Launch levels with **practice level select → A** (`Practice_LaunchLevel`). Vanilla map → play leaves `PSCREEN_LEVEL_SELECT`; heap lines still show `PLAY_UPDATE` but **hotkey save will not run**. |
| Radial menu save | Allowed while the frozen practice menu is open. `Play_Main()` is paused under `PMENU_OPEN_FROZEN`, so the snapshot point is stable. |
| ISV “no line on save” | Successful save prints **nothing** unless `PRACTICE_SAVE_TRACE=1` (`[save_tr]` bracketing). |
| On-screen save feedback | Gameplay HUD now shows short status toasts: `SAVE OK`, `SAVE REF`, `SAVE FAIL`, `LOAD OK`, `LOAD EMPTY`, `LOAD FAIL`, `SAVE DIS` / `LOAD DIS`. |
| Slot indicator | Root practice radial shows active slot plus `SAVED` / `EMPTY`; L/R trigger while the practice menu is open cycles slots. |
| Boot selftest | `PRACTICE_SAVE_SELFTEST=1` by default; Pak boot runs an isolated corrupt-slot probe and warns only if slot-manager bad-magic rejection regresses. Stock skips slot-manager init. |
| Layout proof | `python3 tools/audit_ram_layout.py` — must exit 0 before adding new large BSS that could overlap overlays / load window. |

## Local status — 2026-04-29

- Phase: still **Phase 4**. The Pak-only slot-pool layout is the current design.
- Implemented locally: corrupt-slot boot selftest, radial slot saved/empty label,
  and gameplay save/load status toast.
- Verified locally: `python3 tools/practice_invariants.py`, `make practice -j4`,
  `make lib-test`, and `git diff --check`.
- Not verified locally: BizHawk functional tests, because `BIZHAWK_PATH` is not
  set and BizHawk is not on `PATH`.
- Still hardware-gated: per-scene heap audit table and any final tightening of
  `MAX_STATE_SIZE` / slot constants from real IS-Viewer telemetry.

## Purpose

Collect IS-Viewer telemetry while visiting saveable scenes to:

1. Bound dynamic-load high-water-mark (overlay + assets) vs free RAM estimates.
2. Validate `osMemSize`, slot pool address, and heap audit numbers on hardware.
3. **Optional:** With Expansion Pak, exercise same-scene **save → load** and
   confirm `[save_tr]` completes; file issues if hang occurs after a specific stage.

## Prerequisites

- SummerCart64 (or equivalent) with IS-Viewer path working.
- Patched `sc64deployer` that flushes stdout after each line (see `CLAUDE.md`
  IS-Viewer / deployer notes).

## Terminal A — IS-Viewer debug

```bash
sc64deployer debug --isv 0x03FF0000
```

Wait until it reports listening.

## Build and flash

From **any directory inside the repo or a git worktree**:

```bash
./tools/sc64dev
```

That runs `make practice -j4` at the discovered root and uploads
`build/starfox64.us.rev1.uncompressed.z64`. See `./tools/sc64dev help` for
`build`-only / `upload`-only and env vars (`SF64_REPO_ROOT`, `SC64_DEPLOYER`,
`PRACTICE_SAVE_TRACE` is **not** passed by default — add
`PRACTICE_SAVE_TRACE=1` to the make step when triaging save, e.g.
`PRACTICE_SAVE_TRACE=1 ./tools/sc64dev build` then upload).

Hard-reset the N64 after upload so the IS-Viewer buffer resets cleanly.

## What you should see at boot

Heap-audit print format (`PRACTICE_HEAP_AUDIT=1` default for practice builds):

```text
[heap] boot memSz=<u32> bss~<u32> free~<u32>
[heap] boot bump=<u32> pool=<hex8> poolsz=<u32> slotcnt=<n> disabled=<0|1>
[heap] ovl i1=<bytes> i2=<bytes> i3=<bytes>
[heap] ovl i4=<bytes> i5=<bytes> i6=<bytes>
```

- `memSz`: `0x400000` stock, `0x00800000` with Expansion Pak.
- `pool=`: runtime base of the RAM slot pool (`Practice_Save_SlotPoolBase()`),
  **Pak builds expect `80400000`**.
- `slotcnt` / `disabled`: from `Practice_Save_Init` (`0` slots / `disabled=1` on stock).

Also expect:

```text
[practice_save] Expansion Pak: 4 slots at pool=0x80400000 (osMemSize=0x00800000)
```

(or the stock-disabled printf on 4 MB only).

On level changes or every 60 frames during gameplay:

```text
[heap] enter level=<id> bump=<u32> gfx_peak=<u32> audio=<u32> audio_peak=<u32> free~<u32> bump_hwm=<s32> free_low=<s32>
[heap] tick60 level=<id> bump=<u32> gfx_peak=<u32> audio=<u32> audio_peak=<u32> free~<u32> bump_hwm=<s32> free_low=<s32>
```

`level=` is `gCurrentLevel` (engine). `free_low` is the lowest `free~` since boot.

## Per-scene pass (17 saveable levels)

For each saveable level:

1. From practice level select, choose the level and press **A** (ensures
   `PSCREEN_GAMEPLAY` and correct audio spec path).
2. Play ~10 seconds with heavy action (charge shots, dense spawns, bombs).
3. Note the lowest `free~` (and `free_low` after leaving the scene).

**Expansion Pak only — optional save/load row:**

4. With `PRACTICE_SAVE_TRACE=1` build, press **L + D-pad Left** (save) and **L +
   D-pad Right** (load). Paste the `[save_tr]` tail into the table notes if
   anything fails.

| LevelId | Lowest `free~` | Pak? | Save/load notes |
|---------|----------------|------|-------------------|
| CORNERIA | 5872832 | yes | 2026-04-29: entered level 0; bump_hwm=1248, gfx_peak=82496, audio_peak=716368, free_low=5872832. |
| METEO | 5813472 | yes | 2026-04-29: entered level 1; bump_hwm=13248, gfx_peak=129856, audio_peak=716368, free_low=5813472. |
| SECTOR_X | | | |
| AREA_6 | 5840480 | yes | 2026-04-29: level 3 tick60 sample; bump_hwm=18848, gfx_peak=102848, audio_peak=716368, free_low=5840480. |
| SECTOR_Y | | | |
| VENOM_1 | | | |
| SOLAR | | | |
| ZONESS | | | |
| VENOM_ANDROSS | | | |
| MACBETH | | | |
| TITANIA | 5788384 | yes | 2026-04-29: level 12; bump_hwm=13248, gfx_peak=154944, audio_peak=716368, free_low=5788384. ISV emitted garbage bytes after several clean tick60 lines; watch for repeat/desync. |
| AQUAS | 5878400 | yes | 2026-04-29 partial/crash: level 13 reached tick60 once; bump_hwm=1248, gfx_peak=76928, audio_peak=716368, free_low=5878400. Then crashed / ISV reported `Debug data write dropped due to timeout`; do not use as final pass. Candidate fix: guard torpedo slot `D_i3_801C4190[5]` before indexing `gPlayerShots[slot - 1]`. Needs hardware retest. |
| FORTUNA | | | |
| KATINA | | | |
| BOLSE | | | |
| SECTOR_Z | 5797472 | yes | 2026-04-29: level 18; bump_hwm=30848, gfx_peak=128256, audio_peak=716368, free_low=5797472. |
| VENOM_2 | 5844288 | yes | 2026-04-29: level 19; bump_hwm=18848, gfx_peak=93440, audio_peak=716368, free_low=5844288. |

## Aquas hardware regression checkpoints

Purpose: isolate the Aquas freeze/crash separately from the earlier menu/audio
regression that polluted fine-grained bisect.

| Commit | Menu | Aquas | Notes |
|--------|------|-------|-------|
| `8e7875a` | pass | pass | Fine-grained bisect baseline. |
| `183d69c` | pass | pass | Wave 2.2 boot/layout checkpoint; user verified no crash. |
| `92b9517` | pass | pass | Pak pool isolated / stock-safe globals; user verified no crash. |
| `280ecb5` | pass | pass | Snapshot player copy bound; user verified no crash. |
| `f165d0e` | pass | fail | Snapshot static scratch / HW save trace; user verified Aquas crash. |
| `f165d0e` + scratch-in-Pak fix | pass | pass | User verified Aquas no longer crashes. |
| `1cf18a2` | pass | fail | Later known-bad checkpoint; Aquas crashes. |

### Aquas crash trace

2026-04-29: the first torpedo-slot guard fix did not stop the Aquas crash.
The serial `[aq_tr]` build overwhelmed/desynced IS-Viewer, so the current
diagnostic is an on-screen breadcrumb only. During Aquas, a small `AQ` box shows:

- top number: last completed stage
- `F`: frame when that stage was written
- `S`: player/practice state captured with that stage

Report the final visible `AQ` values after the freeze. Stage map:

| Stage | Meaning |
|---|---|
| 10/11 | practice launch enter/done |
| 20/21 | `Play_Setup` enter/reset |
| 30/31 | environment enter/done |
| 40/41 | Aquas level setup enter/done |
| 42/43 | `Aquas_InitLevel` enter/done |
| 50/51 | practice start conditions enter/done |
| 100/110 | Blue Marine update begin / after boost |
| 115/119 | `Aquas_BlueMarineMove` enter/done |
| 120/130 | after move / after path update |
| 135/139 | `Aquas_BlueMarineShoot` enter/done |
| 140/150/160 | after shoot / after collision / after floor+alarm |
| 170/180 | `Object_Update` enter/done |
| 190/195 | player shots done / bonus text done |
| 200/210 | camera update enter/done |
| 220/230 | level update enter/done |
| 290/291 | level object loader enter/done |
| 3000+n | scenery update slot `n`; `S` is object id |
| 4000+n | sprite update slot `n`; `S` is object id |
| 5000+n | boss update slot `n`; `S` is object id |
| 6000+n | actor update slot `n`; `S` is object id |
| 7000+n | item update slot `n`; `S` is object id |
| 8000+n | effect update slot `n`; `S` is object id |
| 8998/8999 | effects done / textured lines done |
| 890/900/910/920/930 | game draw entry / `Game_Draw` / `Display_Update` enter+done / post draw |
| 940/950/960/970 | post HUD status / practice update enter+done / practice draw done |
| 961/979 | practice draw enter/done |
| 964/965 | practice HUD draw enter/done |
| 966/967 | input display draw enter/done |
| 968/969 | freecam draw enter/done |
| 971/972 | practice menu draw enter/done |
| 981/982/989 | HUD trace/status done / stats panel enter / stats panel done |
| 1000/1010/1020 | `Display_Update` start / lights / backdrop+sun done |
| 1030+n/1039 | player draw slot `n` / player draw pass done |
| 1040+n/1049 | reflected player draw slot `n` / reflected pass done |
| 1050/1060 | `Object_Draw(1)` enter/done |
| 1070/1080 | player-shot draw enter/done |
| 1090/1100 | reflected player-shot draw enter/done |
| 1110/1120 | Aquas `Effect_Draw(0)` enter/done |
| 1130/1140/1150 | water `Effect_Draw(1)` enter/done / ground done |
| 1160+n | visible player features slot `n` |
| 1170/1180 | Blue Marine reticle enter/done |
| 1190/1200/1210/1220/1230 | bonus text / actor marks / lock-on / lens flare / display done |

## Optional: stock 4 MB

Confirm `memSz=4194304`, `disabled=1`, and no pool writes. Save hotkey should
refuse with `[save] disabled` if invoked (menu path may still not call save).

## After the run

- Worst-case `free~` / `free_low` across levels feeds future tightening of
  `MAX_STATE_SIZE` / slot count if telemetry shows pressure.
- `python3 tools/audit_ram_layout.py` must exit 0 after any layout change.
- `python3 tools/practice_invariants.py` and `make practice -j4` after constant
  or linker changes.

## Compile-out audit (release / tournament)

```bash
make practice -j4 PRACTICE_HEAP_AUDIT=0
```

This keeps `Practice_HeapAudit_Boot` / `PerFrame` as empty stubs and drops
`PRACTICE_HEAP_AUDIT=1` telemetry from the translation unit.

## Historical note (pre–slot-pool-split)

Older trees placed a **512 KB** `.practice_pool` in low RAM; `audit_ram_layout`
reported overlap with `ovl_menu` and the dynamic-load window. That design is
**not** what current `Practice_Save_Init` ships. If you see those overlap lines,
you are on an old revision — rebase or compare `practice_save_slotpool.c` /
`Practice_Save_Init` / `HW_VERIFY_phase4.md` (this file).
