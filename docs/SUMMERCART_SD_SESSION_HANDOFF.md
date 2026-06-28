# SummerCart64 SD save state — session handoff (2026-05-29)

Branch: `summercart-sd` (off v0.6.2).
Status: **partially working** — see [Status matrix](#status-matrix) below.

This document captures everything learned in one extended debugging session so a
future session (human or AI) can pick up without re-deriving the context.

---

## TL;DR for the next session

1. **The SC64 cart-bus race is mostly tamed** by a stack of three fixes
   (PI access serialization, gz's cart_lock pattern, audio thread quiesce).
   Save succeeds *most* of the time on Corneria; the trace
   `[sd] slot_save=0` is the success marker.
2. **The original load-apply NaN bug is now reachable.** Save→load completes
   end-to-end (`slot_load=0`, `pending=0`, all `[save_tr] apply ...` markers
   fire), but the next frame's HUD shows `---` indicating gPlayer floats are
   NaN. Per OpenViking memory this matches the 2026-05-07 symptom on Corneria.
3. **Two unresolved bugs remain:**
   - The flaky `f_mount` wedge during save (stripe diagnostic confirms hang
     at slot 11 BLUE = right before `f_mount`, or slot 12 YELLOW = right
     after `f_mount`).
   - `sSdCrossPending` becoming `true` at boot without any user load action —
     surfaces as "SD XLD T/O" HUD message. Source unknown.
4. **Next session should** try one of the directions in
   [Recommended next steps](#recommended-next-steps).

---

## Session 2 update (2026-05-29)

**The SD load corruption is root-caused and fixed** (Bug B below), plus Bug C
fixed and a new latent bug (Bug D) documented. The fix is verified by a host
lib regression test; awaiting hardware confirmation that SD save→load round-trips
cleanly on Corneria.

### Bug C: FIXED

`Practice_Save_Init` now explicitly clears `sSdCrossPending` and the rest of the
`sSdCross*` statics before the stock-4MB early return. A soft reset does not
re-run IPL3's BSS clear, so a stale `sSdCrossPending==true` survived warm
resets and fired a phantom load ("SD XLD T/O") at the next level entry. Guarded
by a new static invariant `check_save_init_clears_sd_cross_state`
(`tools/practice_invariants.py`).

### Bug B: ROOT-CAUSED and FIXED — SD-save serialization buffer overlap

The always-on `[apply]` diagnostic on hardware printed:

```
[apply] sn.bs=8cc30000 sn.boost=0c0015c4 gp.bs=8cc30000 gp.boost=0c0015c4
```

`sn` == `gp` (faithful struct copy), and `0x8cc30000` decodes to a **MIPS
opcode** (`lw $v1, 0($a2)`) — the "player data" loaded from SD is actually engine
**code**. Not NaN, but garbage; some *other* corrupt float in the payload trips
the next-frame `(s32)` FPE → freeze.

**Root cause:** `slot_manager_save_sd_named` staged the serialized image in
`sd_scratch` (`base = sd_scratch`, `payload = sd_scratch + 0x3C`). But
`Practice_Save_Cb` fills its `PracticeSnapshot` working copy into that **same**
`sd_scratch` (via `Practice_SaveScratch()`) and serializes *out of it*. The large
early `TAG_OVERLAY_BYTES` write into `payload` overwrites the snapshot's
`playerData`/actor region **before** `TAG_PLAYER_ARRAY` reads it — so the file
stores overlay code where player state should be. The RAM-slot save path uses
`&storage[slot*size]` as `base` (disjoint from `sd_scratch`), which is exactly
why the mupen RAM repro (`tests/test_save_load_nan_repro.py`) stayed clean.
The **load** path had already been fixed for this identical collision (it stages
in `storage`); **save had not**.

**Fix (`lib/slot_manager.c`):** `slot_manager_save_sd_named` now stages into
`storage` (the RAM slot pool) when it is distinct from `sd_scratch`, keeping the
serializer's source (`sd_scratch`) and destination (`storage`) disjoint —
mirroring the load path. Clobbers slot 0's RAM image (marked invalid), same as
load. Regression test: `lib/test/test_slot_manager_sd.c ::
test_save_cb_scratch_not_clobbered` — RED before the fix, GREEN after; reproduces
the overlap on the host with a `save_cb` that serializes out of `sd_scratch`.

Still **no payload CRC** (`slot_manager_load_sd_named` checks only
magic+version+size); adding one would have localized this far faster and is worth
doing (`lib/sd_crc.c` already exists but is dead code).

### Bug D: FIXED — SD DMA scratch overlapped the ROM

`SC64_SD_DMA_SCRATCH` was `0x10F00000` in `lib/iodev/iodev_sc64.c`. Its comment
assumed "ROM ≈ 10 MiB", but the ROM is now **15.1 MiB** (`0x10000000`–
`0x10F22FC0`), so the 64 KiB scratch window `0x10F00000`–`0x10F10000` sat
**inside the ROM image**. Every SD op DMAs through it and clobbered ROM-in-SDRAM
at file offset `0xF00000`–`0xF10000`; if the game later streamed an asset from
that region it would get SD-scratch bytes. This did **not** cause Bug B (the data
still round-trips through the scratch correctly), but it is a real latent
corruption bug.

**Fix:** moved the scratch to `0x12000000` (cart offset 32 MiB) — ~17 MiB above
the ROM tail and ~31 MiB below the flash shadow (`0x13FE0000`), so neither ROM
growth nor the flash region can reach it. New static invariant
`check_sc64_sd_scratch_clear_of_rom` enforces both bounds against the **built
ROM size** (verified it flags the old `0x10F00000` as an overlap and passes the
new value).

**Hardware caveat:** this changes the cart address the SD transport DMAs
through. It is logically valid SC64 SDRAM, but it could not be exercised in
mupen. If SD save/load suddenly fails with a *read/write* error (e.g.
`LOAD READ FAIL`, not corruption) after this build, the scratch move is the
suspect — revert the one-line `SC64_SD_DMA_SCRATCH` define to bisect.

### Decisive hardware diagnostic added (gated by `PRACTICE_SAVE_TRACE=1`)

`Snapshot_ApplyToGame` now prints, right after the player struct copy:

```
[apply] sn.bs=%08x sn.boost=%08x gp.bs=%08x gp.boost=%08x
```

Raw IEEE-754 bits (no `%f`/`%g` dependency). `sn.*` = the loaded snapshot bytes
(source), `gp.*` = what just landed in `gPlayer` (dest).

- valid `baseSpeed` (~40.0) reads **`0x42200000`**.
- NaN/inf reads exponent `0xFF`: **`0x7Fxxxxxx` / `0xFFxxxxxx`**.

**Interpretation on the next hardware SD-load run:**
- `sn.bs` already NaN  → the SD bytes are corrupt → fix the SD transport
  (add a payload CRC to localize; suspect the write side too — save is flaky).
- `sn.bs` valid, HUD still shows `---` a frame later → NaN is produced after
  apply (threading/gameplay), not the bytes.

Trace build verified: compiles + links, `main_ROM_END = 0xf8710` (under the
0xFD000 boot-safe ceiling). Build it with:
`rm -rf build/src/practice build/lib && make practice -j4 PRACTICE_SAVE_TRACE=1`

### Bug A: untouched

`f_mount` flaky wedge is hardware-only (cannot repro in mupen — no SC64
backend). Still open; see Bug A below.

---

## Status matrix

| Behavior | Status | Evidence |
|----------|--------|----------|
| SD menu open (`Practice_Sd_StartSave`) | ✅ works | `[sd] sv<` reached |
| Lazy SD init (`iodev_sd_init`) | ✅ works | `[lz] init> r=0` |
| `f_mount` (RAM-only registration) | ⚠️ flaky | wedges intermittently; stripes show last visible at slot 11/12 |
| `f_mkdir` chain | ⚠️ flaky | works when reached |
| Save file write (`slot_manager_save_sd_named`) | ✅ data fixed (Bug B) / ⚠️ still race-flaky | wrote corrupt payload (serialization overlap) — fixed; `SD Rename fail` timeout is the separate cart-bus race |
| Load file read (`slot_manager_load_sd_named`) | ✅ works | `[sd] slot_load=0`; read faithfully returned the (previously corrupt) file |
| Cross-scene `Practice_Save_Tick` state machine | ✅ fixed (Bug C) | `sSdCrossPending` cleared in `Practice_Save_Init` (warm-reset safe) |
| Same-scene apply (`Snapshot_ApplyToGame`) | ✅ clean | mupen RAM-slot save→load keeps floats valid; corruption was the save serializer, not apply |
| SD payload integrity (player array) | ✅ fixed (Bug B) | was overlay code (`0x8cc30000` = MIPS `lw`); fixed by disjoint staging buffer |
| SD DMA scratch placement | ✅ fixed (Bug D) | moved `0x10F00000`→`0x12000000` (was inside the 15.1 MB ROM); invariant `check_sc64_sd_scratch_clear_of_rom` |
| IS-Viewer trace channel | ⚠️ flaky | self-disables after 5 consecutive drain timeouts; hides progress |
| HUD-style stripe diagnostic (`Lib_DebugFillScreen`) | ✅ bulletproof | survives IS-Viewer death, last visible color = last code that ran |

---

## What's in this branch (uncommitted)

```text
modified:   lib/iodev/iodev_sc64.c
modified:   src/mods/isviewer.c
modified:   src/practice/practice_save.c
modified:   src/practice/practice_sd.c
modified:   linker_scripts/us/rev1/starfox64.ld  (gitignored, see below)
```

The `.ld` is gitignored. Its changes — moving `practice_save.o(.text|.data|.rodata)`
into `.practice_late_core` to fit the `PRACTICE_SAVE_TRACE=1` build under
the boot-safe ROM limit — are reapplied automatically every `make extract`,
so as long as the matching `PRACTICE_OBJS` change is committed to
`tools/patch_linker_script.py`, the change persists. **It is NOT yet committed
to the patcher**, so a `make extract` will revert the `.ld` and break the
`PRACTICE_SAVE_TRACE=1` build. See [Open hygiene tasks](#open-hygiene-tasks).

---

## Fixes applied (defense-in-depth stack)

All necessary; empirically removing any one re-triggers a regression somewhere.

### 1. `sc64_cart_lock` / `sc64_cart_unlock` (`lib/iodev/iodev_sc64.c`)

Mirrors `~/code/gz/src/gz/sc64.c:17-35`. Around every SC64 register access:

```c
static void sc64_cart_lock(void) {
    if (__osPiAccessQueueEnabled) __osPiGetAccess();  /* serialize vs other PI users */
    sc64_lock_irqf = __osDisableInt();                /* no preemption */
    sc64_lock_lat = IO_READ(PI_BSD_DOM1_LAT_REG);     /* save PI timing */
    sc64_lock_pwd = IO_READ(PI_BSD_DOM1_PWD_REG);
}
static void sc64_cart_unlock(void) {
    IO_WRITE(PI_BSD_DOM1_LAT_REG, sc64_lock_lat);     /* restore PI timing */
    IO_WRITE(PI_BSD_DOM1_PWD_REG, sc64_lock_pwd);
    __osRestoreInt(sc64_lock_irqf);
    if (sc64_lock_pi_acquired) __osPiRelAccess();
}
```

**Why:** the SC64 firmware needs specific PI dom1 latency/pulse-width to respond
to register reads/writes. The audio thread's PI manager configures dom1
differently for ROM data DMAs. If our SC64 access runs under audio's timing
values, firmware wedges (red LED stuck on).

Used by `sc64_execute_cmd` and `sc64_detect`. Replaces the older inline
`__osDisableInt` + ad-hoc PI access guards.

### 2. `PI_WAIT()` macro (`lib/iodev/iodev_sc64.c`, `src/mods/isviewer.c`)

Mirrors `~/code/gz/src/gz/pi.h __pi_wait()`:

```c
#define PI_WAIT() do { \
    while (IO_READ(PI_STATUS_REG) & (PI_STATUS_IO_BUSY | PI_STATUS_DMA_BUSY)) ; \
} while (0)
```

Used inside `PI_WRITE_FLUSH` (sc64) and `PI_WRITE` (isviewer) before every raw
`IO_READ`/`IO_WRITE`. **Why:** even with the PI access semaphore held, the PI
hardware can still be servicing a DMA queued before we acquired. Raw CPU PIO
without this wait collides with in-flight transfers and wedges the SC64.

**Invariant note:** the `check_isviewer_sc64` regex requires `IO_WRITE` and the
draining `IO_READ` to appear on consecutive macro lines, so `PI_WAIT();`
prefixes are written on the same line as the access (see current macro form).

### 3. `__osPiGetAccess` in `ISViewer_Write` (`src/mods/isviewer.c`)

Wraps the rp/wp dance with `__osPiGetAccess` / `__osPiRelAccess` so IS-Viewer
flushes serialize against audio thread DMA. Guarded with
`if (__osPiAccessQueueEnabled)` for pre-osInitialize safety.

**Why:** without it, audio DMA fills the bus during our drain-wait poll, the
SC64 firmware sees a stale wp value before our update lands, interprets the
pair as wrap, dumps 64 KB of garbage to USB. After 5 such failures the
channel self-disables (`sISViewerFailureCount >= ISV_FAILURE_LIMIT`) and ALL
subsequent prints are silently dropped, hiding real progress.

### 4. `osStopThread(&gAudioThread)` quiesce wrapping (`src/practice/practice_sd.c`)

Around `Practice_Sd_LazyInit`, `on_save_name_confirmed`,
`on_load_file_selected`, and `Practice_Sd_StartLoad`'s `file_browser_open`:

```c
static void sd_audio_pause(void)  { osStopThread(&gAudioThread); }
static void sd_audio_resume(void) { osStartThread(&gAudioThread); }
```

**Why:** the gz cart_lock pattern is necessary but empirically not sufficient
on SF64 — code-alignment shifts from any small edit toggle whether the race
wins. Hard-stopping audio gives the SC64 firmware a quiet bus regardless of
build alignment. Brief audio glitch (~1–2 s) per SD operation.

### 5. gz audio-queue-drain pattern (`src/practice/practice_save.c`)

```c
static void Snapshot_DrainAudioForApply(void) {
    sSeqCmdReadPos = sSeqCmdWritePos;       /* drain seq cmd queue (audio_general.c) */
    gThreadCmdReadPos = gThreadCmdWritePos;  /* drain thread cmd queue (audio_thread.c) */
    Audio_ClearVoice();                      /* stop active SFX samples */
}
```

Called at the START of `Snapshot_ApplyToGame`, BEFORE any bcopy.

**Why:** gz `src/gz/state.c:1406-1413` does this. Comment in gz: prevents "FPE
crashes due to dangling pointers." Queued audio commands hold pointers into
gPlayer/gActors that we are about to overwrite; processing them post-apply
reads stale pointers and produces NaN that the HUD surfaces as `---`.

### 6. gz GFX-queue-drain pattern (`src/practice/practice_save.c`)

At the END of `Snapshot_ApplyToGame`, just before return:

```c
osRecvMesg(&gGfxTaskMesgQueue, NULL, OS_MESG_BLOCK);
osSendMesg(&gGfxTaskMesgQueue, NULL, OS_MESG_NOBLOCK);
```

**Why:** gz `state.c:1470-1471`. The in-flight RSP display list is processing
PRE-APPLY state. We must wait for it to drain before the audio thread resumes
and starts referencing the NEW state. Without this drain, RSP's working FP
registers can write back NaN-tainted values that propagate into gPlayer on
the next frame's matrix build.

**Untested at handoff** — the user reported the NaN dash still appears in a
trace that didn't include `[save_tr] apply after gfx drain`. Possible reasons:
the build with this fix wasn't reached during the session, or the GFX drain
alone is not sufficient.

### 7. Linker move (`linker_scripts/us/rev1/starfox64.ld` — gitignored)

`build/src/practice/practice_save.o(.text|.data|.rodata)` moved from `.main`
to `.practice_late_core`. The `.bss` MUST stay in `.main_bss` per the
`check_practice_pool_placement` invariant (stock 4 MB needs
`gPracticeSaveDisabled` reachable, and `.practice_late_core_bss` lives at
0x80720000 above the 4 MB ceiling).

**Why:** the `PRACTICE_SAVE_TRACE=1` build pushed `main_ROM_END` over the
0xFD000 boot-safe limit. Moving `.text` etc. to `.practice_late_core`
reclaimed ~4.4 KB of main ROM headroom.

---

## Diagnostic infrastructure in place

### IS-Viewer trace markers

Compact short codes throughout `Practice_Sd_*` so the format strings don't
blow the ROM budget:

- `[sd] sv<` / `[sd] ld<` — StartSave/StartLoad entry
- `[lz] in av=%d r=%d` — LazyInit entry showing cached state
- `[lz] init<` / `[lz] init> r=%d` — `iodev_sd_init` bracket
- `[lz] ok=%d` — `iodev_sd_was_ok` result
- `[lz] mnt<` / `[lz] mnt>` — `f_mount` bracket
- `[lz] mk1=%d` / `[lz] mk2=%d` / `[lz] mk3=%d` — each `f_mkdir`
- `[lz] umnt` / `[lz] out av=%d` — done

### `[save_tr]` markers (gated by `PRACTICE_SAVE_TRACE=1`)

14+ stage markers throughout `Practice_Load_Cb` and `Snapshot_ApplyToGame`.
Last marker before freeze identifies the corrupt apply stage. Currently:

```
apply begin
apply after audio drain        ← gz pattern added in this session
apply after players
apply after world arrays
apply after path..loadobjs
apply after loadout bcopy
apply after combat scalars
apply after team/wings
apply after cam/proj
apply after boss/allrange
apply after hud/fill/kill
apply after gPlayer[0].state=ACTIVE
apply after Practice_Hud_Reset
apply queued AUDIO_PLAY_BGM
apply after gfx drain          ← gz pattern added in this session
load_cb ok return 0
```

### `Lib_DebugFillScreen` stripes (`Practice_Sd_LazyInit`)

Bulletproof — survives IS-Viewer death because `osViSwapBuffer` is independent
of PI bus. Last visible stripe color identifies wedge point.

| Slot # | Color | Position in code |
|---|---|---|
| (1–8) | boot prints (yellow, green, royal blue, orange, red, purple, magenta, turquoise) | `sys_main.c` + `practice_main.c` |
| (9–10) | cyan, dark gray | `fox_game.c` level transition |
| 11 | **BLUE** (0x001F) | before `f_mount` |
| 12 | **YELLOW** (0xFFE0) | after `f_mount` |
| 13 | **ORANGE** (0xFC00) | after `[lz] mnt>` print |
| 14 | **RED** (0xF800) | after `f_mkdir(SD_ROOT)` |
| 15 | **MAGENTA** (0xF81F) | after `f_mkdir(SD_APP)` |
| 16 | **CYAN** (0x07FF) | after `f_mkdir(SD_DIR)` |
| (17 — would be) | **GREEN** (0x07E0) | after `f_unmount` — but cursor caps at 16 |

`DEBUG_BC_SLOTS = 16` (sys_lib.c:154). Slot >= 16 silently drops.

---

## Open hygiene tasks (must do before merge)

1. **Commit the linker-script-patcher change.** Add `practice_save` (or
   parts of it) to a routing list in `tools/patch_linker_script.py` so the
   `.text/.data/.rodata → .practice_late_core` move survives `make extract`.
2. **Remove diagnostic-only code** before shipping:
   - All `Lib_DebugFillScreen` calls in `Practice_Sd_LazyInit`.
   - Verbose `[lz]`/`[sd]` IS-Viewer prints (or keep gated by a debug flag).
   - The `Snapshot_DrainAudioForApply` SAVE_TR_STAGE marker name should
     match the style of the rest.
3. **Remove the audio-thread stop UX cost** if a less invasive fix is found.
   The brief 1–2 s glitch every save/load is noticeable.
4. **Revisit the v0.6.2 lazy `iodev_sd_init`** — with cart_lock + PI_WAIT
   in place, the original cold-boot wedge may be fixed and `iodev_sd_init`
   could move back to boot, eliminating the menu-open stall.

---

## Open bugs (must investigate)

### Bug A: `f_mount` flakey wedge

**Symptom:** stripe diagnostic shows last visible stripe at slot 11 BLUE
(before `f_mount`) or slot 12 YELLOW (after `f_mount`). Hard freeze, no
recovery, must reset.

**What `f_mount(&sFatfsWork, "0:", 0)` does** (from
`lib/fatfs/ff.c:3657-3708` with our ffconf):

```c
vol = get_ldnumber(&rp);   /* string parse, no IO */
cfs = FatFs[vol];           /* RAM read, NULL first time */
/* FF_FS_LOCK == 0: skip clear_share */
/* FF_FS_REENTRANT == 0: skip mutex create */
fs->pdrv = LD2PD(vol);      /* RAM write to sFatfsWork */
fs->fs_type = 0;            /* RAM write */
FatFs[vol] = fs;            /* RAM write to global */
if (opt == 0) return FR_OK; /* no mount */
```

Pure RAM. Cannot wedge per code inspection. Yet it does.

**Working theories:**
- The audio thread was osStopThread'd but RSP/PI hardware still has in-flight
  work. The `osWritebackDCacheAll` inside the preceding `Lib_DebugFillScreen`
  is interleaving with a DMA on a shared cache line. Try
  `osInvalDCache(&sFatfsWork, sizeof(sFatfsWork))` immediately before
  `f_mount` to force a known-good cache state.
- VI manager is starved because audio thread is stopped but holds a queue
  consumer slot. Try `osSetThreadPri(&gAudioThread, 0)` instead of
  `osStopThread` to demote without removing from queues.
- `FatFs[vol] = fs` is writing to a stale TLB-mapped page. Investigate
  whether `__osBaseTimer`-driven TLB shootdowns can race here.

### Bug B: load-apply NaN

**Symptom:** save→load completes (`slot_load=0`, `pending=0`, all apply stages
fire), then next frame's HUD shows `---` for `gPlayer[0].baseSpeed`/`boostSpeed`.

**What `Practice_DrawFloat` does:**

```c
if (value != value) draw_dash();  /* NaN check */
```

So `gPlayer[0].baseSpeed` or `boostSpeed` is NaN AFTER apply returns.

**Working theories:**
- Some scalar restored by apply is the *divisor* for a physics computation,
  and the saved value is 0 → produces NaN one frame later.
- The save-side `Snapshot_FillFromGame` runs while gPlayer is in a
  mid-physics state with a not-yet-finalized speed value. Should fillfn be
  gated on `gPlayState == PLAY_UPDATE` exclusively?
- The GFX queue drain we added is insufficient — RSP might be writing back
  state to a different memory region that propagates into the next frame's
  player calc.
- Some field in the Player struct was added since the snapshot tag list was
  authored, and it ends up uninitialized after apply.

**Recommended diagnostic:** add an `osSyncPrintf("[apply] p0.bs=%g p0.boost=%g\n",
gPlayer[0].baseSpeed, gPlayer[0].boostSpeed)` immediately after the
`gPlayer[i] = sn->playerData[i]` loop in `Snapshot_ApplyToGame` (and again
right before `load_cb ok return 0`). If NaN is present immediately, the
problem is the snapshot bytes. If it appears between the two prints, apply
ordering issue. If neither shows NaN but HUD shows dash, NaN is produced
AFTER apply by gameplay code.

### Bug C: `sSdCrossPending` true at boot

**Symptom:** "SD XLD T/O" HUD message appears at level entry without any
user save/load. The only place that sets `sSdCrossPending = true` is
`Practice_Load_Cb` line 1254, which should not run without a load.

**Working theories:**
- BSS not being zeroed cleanly on warm reset. The N64 IPL3 zeroes BSS, but
  the user's repro is on soft reset (sc64 reboot), not full power cycle.
- Practice_Save_Init should explicitly clear `sSdCrossPending` — currently
  it doesn't.
- The recent audio-queue-drain edit accidentally writes to a wrong address.
  Confirmed NOT (the audio cursor symbols resolve to 0x800db6b4 area,
  `sSdCrossPending` is at 0x80192e34 — no overlap).

**Quick fix worth trying:** add `sSdCrossPending = false;` (and friends)
explicitly to `Practice_Save_Init` so warm-reset state is always clean.

---

## Recommended next steps

In priority order:

1. **Add explicit BSS reset to `Practice_Save_Init`.** Defensive zero of
   `sSdCrossPending`, `sSdCrossLevel`, `sSdCrossPhase`, etc. Eliminates Bug C
   for free.

2. **Verify the gz GFX drain actually fires.** Build with
   `PRACTICE_SAVE_TRACE=1`. If save succeeds once, the `[save_tr] apply after
   gfx drain` marker will appear in the load trace. If NaN persists past
   that marker, GFX drain wasn't the missing piece.

3. **Add `gPlayer[0]` float prints in `Snapshot_ApplyToGame`.** Pin down
   whether NaN is present right after the bcopy or appears later. This is
   the single fastest diagnostic for Bug B.

4. **Try `osSetThreadPri(&gAudioThread, 0)` instead of `osStopThread`.**
   Demotion is less invasive than stopping. If save reliability improves,
   that's the win.

5. **Move SD ops to a dedicated high-priority thread.** Run all SD writes on
   a thread at priority 90+ (above audio's 80), so SD ops never get preempted
   by audio. The thread is single-purpose and short-lived per operation.
   Eliminates the audio/SD race fundamentally. This is the "real" fix that
   makes the defense-in-depth stack unnecessary.

6. **Investigate the FatFs Player-struct race directly.** If Bug B persists,
   instrument `Snapshot_FillFromGame` to capture `gPlayer[0].baseSpeed` at
   save time. Compare with the loaded value. If they differ → the snapshot
   bytes on disk are wrong. If they match → some apply step or post-apply
   code corrupts the value.

---

## Architectural learnings worth keeping

### gz patterns are the gold standard (`~/code/gz`)

- `src/gz/sc64.c` — `cart_lock` / `cart_unlock` with PI timing save/restore
- `src/gz/pi.h` — `__pi_wait()` before every raw access
- `src/gz/state.c` — audio queue drain BEFORE bcopy, GFX queue drain AFTER

These are the universal patterns for N64 ROM hacks doing cart-bus I/O
alongside an active audio engine. gz uses NO `osStopThread` — the cart_lock
+ pi_wait pattern alone is sufficient for OoT. SF64's heavier continuous
audio activity (SFX, doppler, all-range) appears to require the extra
quiesce on top, OR a different threading model.

### IS-Viewer self-disable is a major debugging hazard

`src/mods/isviewer.c:75-80` — after 5 consecutive drain timeouts, all
subsequent prints are silently dropped. **Trace ending at line X does NOT
mean code wedged at line X.** Use `Lib_DebugFillScreen` for definitive
last-known-good-line evidence.

### Code alignment shifts can mask race conditions

Several builds during this session "got lucky" — same code paths but
different binary layouts changed the cache/timing enough to win the race.
**Any apparent fix should be verified across multiple builds with different
content sizes to confirm robustness.**

### Stack-allocated `OSIoMesg` in iodev_sc64 is theoretically wrong

`sc64_sd_read_sectors` / `sc64_sd_write_sectors` use `OSIoMesg mb;` on the
stack and pass `&mb` to `osPiStartDma`. The N64 SDK pattern (per gz, libdragon)
uses a static/global `OSIoMesg`. This works in practice because
`__osDevMgrMain` finishes touching `mb` before `osRecvMesg` unblocks, but
it's a footgun. Consider hardening.

---

## How to resume this work

1. `git checkout summercart-sd` then check out the worktree at
   `.claude/worktrees/summercart-sd`.
2. Read this doc end-to-end.
3. Pick a path from [Recommended next steps](#recommended-next-steps).
4. To capture a `[save_tr]` apply trace:
   ```
   make practice -j4 PRACTICE_SAVE_TRACE=1
   ```
5. To upload + listen via IS-Viewer (audio quiesced during SD ops, so brief
   ~1–2 s audio glitch is expected per save/load):
   ```
   sc64deployer upload --direct --reboot build/starfox64.us.rev1.uncompressed.z64
   sc64deployer debug --isv 0x03FF0000
   ```
   Then press the physical N64 reset button. See `CLAUDE.md` for sc64dev
   wrapper details.

---

## Reference

- gz repo: https://github.com/glankk/gz
- libdragon: https://github.com/DragonMinded/libdragon
- SF64 practice ROM CLAUDE.md (in this repo) — the SC64 protocol gotchas
  section is essential context.
- OpenViking memories tagged "summercart" / "save state" — many session
  learnings stored over multiple debugging sessions.
