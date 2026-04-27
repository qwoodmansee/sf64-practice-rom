# gz-style Features Design

## Purpose

Bring [gz](https://github.com/glankk/gz)-style practice tooling to the SF64 practice ROM:

- **Save states**: multi-slot in-RAM, plus SD-persisted named files. Cross-level save/load (save in Corneria, load on Sector Z and end up in Corneria).
- **Watches**: user-defined memory address watches with type-aware display, anchored to a list panel or freely positioned, persisted across sessions.
- **SD card persistence**: states, watches, and `PracticeConfig` settings live as files on the flashcart's SD card, copyable via PC.

Macros are explicitly **deferred** — out of scope for this design.

## Constraints & assumptions

- **Hardware target**: SC64 and EverDrive 64 X7/X8. Runtime detection at boot.
- **RAM budget**: 4 MB stock with optional 8 MB Expansion Pak. Slot count adapts at boot (2 slots stock, 8 with Pak).
- **Build size**: ~6 KLoC new C, plus ~3.5 KLoC vendored FatFs.
- **Portability goal**: the lower half of this stack must be liftable into other N64 ROM-hack practice tools without modification.

## Architecture

Two layers with hard separation:

```
sf64-practice-rom/
├── src/                          (existing — base game + src/practice/)
├── include/                      (existing — game headers)
├── lib/                          (NEW — top-level portable library)
│   ├── iodev/
│   │   ├── iodev.h
│   │   ├── iodev.c               (registry, runtime detect)
│   │   ├── iodev_sc64.c
│   │   ├── iodev_ed64.c
│   │   └── iodev_stub.c          (build-flag, used in BizHawk tests)
│   ├── fatfs/
│   │   ├── ff.c, ff.h, ffconf.h, ffunicode.c   (vendored, BSD-3)
│   │   └── diskio.h, diskio.c    (glue: FatFs → iodev)
│   ├── ui/
│   │   ├── osk.h, osk.c          (on-screen keyboard)
│   │   └── file_browser.h, file_browser.c
│   ├── serial.h, serial.c        (TLV codec — encode/decode helpers)
│   ├── slot_manager.h, slot_manager.c
│   ├── watch_engine.h, watch_engine.c
│   └── test/                     (host-side unit tests)
│       ├── iodev_test.c          (mock backend over a host file)
│       └── test_*.c
└── src/practice/                 (game-specific layer, expanded)
    ├── practice_save.c           (REWRITE: TLV serializer)
    ├── practice_overlay.c        (NEW: scene→VRAM region map, reload driver)
    ├── practice_watch_presets.c  (NEW: SF64-specific preset addresses)
    ├── practice_config_persist.c (NEW: PracticeConfig save/load)
    └── practice_state.c          (EXTEND: menu wiring)
```

**Hard wall**: `lib/` may not include `global.h`, `practice.h`, `variables.h`, `sf64*.h`, `fox_*.h`, or anything from `include/`. Enforced by static invariant. The lib only knows about its own headers and the C standard library.

**Game-side glue**: `src/practice/` includes both lib headers (via `-Ilib`) and game headers freely. This is the only layer that bridges the two worlds.

## Component contracts

### `lib/iodev/`

```c
typedef enum {
    IODEV_NONE,
    IODEV_SC64,
    IODEV_ED64,
} iodev_id_t;

iodev_id_t iodev_detect(void);
int iodev_sd_init(void);
int iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf);
int iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf);
```

- SC64 backend uses cart-bus PI commands: `'i'` `SD_CARD_OP`, `'I'` `SD_SECTOR_SET`, `'s'` `SD_READ`, `'S'` `SD_WRITE`. Reference: `~/code/SummerCart64/sw/bootloader/src/sc64.c:463-485`.
- ED64 backend uses FPGA register I/O for the X7/X8 SDIO controller. Clean-room reimplementation guided by gz's `ed64_x.c` (avoiding GPL contamination — study, don't copy).
- Detection probes both carts at known register addresses; first match wins. `IODEV_NONE` on emulators without flashcart simulation.

### `lib/fatfs/`

- Vendored FatFs R0.15 (BSD-3-Clause), unmodified.
- `ffconf.h` configured: 1 volume, 512-byte sectors, FAT16+FAT32 (no exFAT), LFN Mode 1 (heap-free LFN buffer), no RTC, no malloc.
- `diskio.c` is the only authored file — ~50 LoC mapping FatFs's `disk_*` callbacks onto `iodev_sd_*` calls.

### `lib/serial.{c,h}` — TLV codec

```c
typedef struct {
    uint8_t *buf;
    size_t   capacity;
    size_t   pos;
} serial_writer_t;

typedef struct {
    const uint8_t *buf;
    size_t         size;
    size_t         pos;
} serial_reader_t;

void serial_writer_init(serial_writer_t *w, void *buf, size_t cap);
int  serial_put_tag(serial_writer_t *w, uint16_t tag, const void *data, uint32_t len);
size_t serial_writer_size(const serial_writer_t *w);

void serial_reader_init(serial_reader_t *r, const void *buf, size_t size);
int  serial_get_next(serial_reader_t *r, uint16_t *tag, uint32_t *len, const void **data);
/* Returns 1 if a tag was read, 0 at end of buffer, -1 on truncation. */
```

Wire format per entry:

```
u16 tag
u16 flags        (reserved, 0)
u32 length
byte[length]
```

All multi-byte fields little-endian on disk.

### `lib/slot_manager.{c,h}`

```c
typedef uint32_t (*save_state_fn)(void *buf, uint32_t buf_size);
typedef int      (*load_state_fn)(const void *buf, uint32_t size);

void slot_manager_init(uint16_t state_version,
                       uint16_t lib_version,
                       save_state_fn save_cb,
                       load_state_fn load_cb,
                       uint8_t ram_slots);

int  slot_manager_save_ram(int slot);
int  slot_manager_load_ram(int slot);
bool slot_manager_ram_valid(int slot);
void slot_manager_clear_ram(int slot);
int  slot_manager_save_sd_named(const char *path);
int  slot_manager_load_sd_named(const char *path);
```

Owns: RAM slot pointer table, the `state_meta_t` header (magic + lib_version + state_version + size + name + scene_idx slot), version validation. The game injects two callbacks; everything else is game-agnostic.

### `lib/ui/osk.{c,h}` — on-screen keyboard

```c
typedef void (*osk_callback_t)(const char *text, void *user_data);
void osk_open(const char *prompt, const char *default_text,
              uint8_t max_len, osk_callback_t cb, void *user_data);
void osk_open_canceled(osk_callback_t cb, void *user_data);
void osk_update(void);
void osk_draw(void);
bool osk_is_open(void);
```

- D-pad navigates a 6×8 char grid; A selects, B backspace, START confirm, Z cancel.
- Char set: existing `sSmallChars` (`A-Z`, `0-9`, `!:-.`) plus `_` (added to the small-text font).
- Single-line edit, max 31 chars + NUL.

### `lib/ui/file_browser.{c,h}` — file picker

```c
typedef enum { FB_LOAD, FB_SAVE_PROMPT } fb_mode_t;
typedef void (*fb_callback_t)(const char *path, void *user_data);
void file_browser_open(fb_mode_t mode, const char *dir,
                       const char *suffix,
                       fb_callback_t cb, void *user_data);
void file_browser_update(void);
void file_browser_draw(void);
bool file_browser_is_open(void);
```

- Drives `f_opendir`/`f_readdir`; suffix filter, scrollable list, 14 visible rows.
- `FB_LOAD`: select file → callback with full path.
- `FB_SAVE_PROMPT`: select target dir → opens OSK for filename → callback with full path.

### `lib/watch_engine.{c,h}`

```c
typedef enum {
    WATCH_S8, WATCH_U8, WATCH_S16, WATCH_U16, WATCH_S32, WATCH_U32,
    WATCH_F32, WATCH_X8, WATCH_X16, WATCH_X32,
} watch_type_t;

typedef struct {
    uint32_t     address;
    watch_type_t type;
    bool         anchored;
    bool         visible;
    int16_t      x, y;        /* screen px if !anchored, list slot if anchored */
    char         label[48];
} watch_t;

int  watch_engine_add(uint32_t addr, watch_type_t type, const char *label);
void watch_engine_remove(int idx);
void watch_engine_anchor(int idx);
void watch_engine_set_pos(int idx, int16_t x, int16_t y);
int  watch_engine_save(const char *path);
int  watch_engine_load(const char *path);
void watch_engine_draw(void);
size_t watch_engine_count(void);
watch_t *watch_engine_get(int idx);
```

Persistence: TLV file (`/sf64-practice/watches.dat`).

### `src/practice/practice_overlay.c` — game-specific bridge

```c
/* Exhaustive lookup over SceneId enum.
 * Returns 0 on success. */
int  practice_overlay_get_region(SceneId scene_id,
                                 void **vram_start,
                                 uint32_t *size);

/* Computes a content hash of the overlay's ROM bytes for build-id checking. */
uint32_t practice_overlay_build_id(SceneId scene_id);

/* Drives Practice_LaunchLevel-style transition for cross-scene state load.
 * Caller must wait until practice_overlay_loaded() before applying state. */
void practice_overlay_request_load(SceneId scene_id, int32_t scene_setup);
bool practice_overlay_loaded(SceneId scene_id);
```

This is the only file in the SF64-specific layer that knows the scene→VRAM map. It reads from `gDmaTable` to compute regions dynamically (avoids hardcoding addresses that change with code edits).

## Data flow & file formats

### State file (`.sf64st`)

```
offset  size   field
------  ----   -----
0x00    4      magic                "SF64"
0x04    2      lib_version          slot_manager format version (==1)
0x06    2      state_version        SF64 game-state version (==1)
0x08    4      total_size           total file size in bytes
0x0C    32     name                 user-supplied label, NUL-padded
0x2C    2      scene_id             duplicated from payload (for browse without parse)
0x2E    2      scene_setup          duplicated
0x30    4      reserved             zero
0x34    ----   game_payload         TLV-encoded, opaque to slot_manager
```

`game_payload` is a sequence of TLV entries. Tag registry below. Critical entries:

| Tag | Type | Notes |
|-----|------|-------|
| `TAG_SCENE_ID` | u16 | redundant with header, included for self-contained parse |
| `TAG_SCENE_SETUP` | u16 | |
| `TAG_OVERLAY_BUILD_ID` | u32 | hash of the overlay's ROM bytes for the current scene; mismatch → partial restore |
| `TAG_OVERLAY_VRAM` | u32 | sanity check |
| `TAG_OVERLAY_BYTES` | bytes | verbatim copy of the active overlay region (text+data+rodata+bss) |
| `TAG_AUDIO_SEQ_ID` | u32 | |
| `TAG_AUDIO_SPEC` | u32 | |
| `TAG_SEGMENTS` | bytes[16*15] | gSegments[] snapshot |
| `TAG_PLAYER_DATA` | bytes | sizeof(Player)*4 |
| `TAG_ACTORS` / `TAG_BOSSES` / etc. | bytes | full PracticeSnapshot field-by-field |
| `TAG_PATH_PROGRESS` | f32 | one tag per scalar field |
| ... | ... | ~80 entries total |

Tag IDs are stable forever. Removed fields' tags are reserved (never reused). Tag registry lives in `src/practice/practice_save_tags.h`.

### Watches file (`watches.dat`)

```
0x00   4    magic              "WTCH"
0x04   2    version            1
0x06   2    count
0x08   N    TLV entries        per watch_t field, repeated count times
```

Each watch is a sub-record: a series of TLV entries terminated by `TAG_WATCH_END`.

### Config file (`config.dat`)

Same shape as watches: magic `"CFG_"`, then TLV entries — one per `PracticeConfig` field. Tag registry in `src/practice/practice_config_tags.h`.

### SD layout

```
/sf64-practice/
  states/
    *.sf64st
  watches.dat
  config.dat
```

Created lazily on first save. We don't claim the SD; we coexist with anything else.

## TLV migration semantics

The TLV format gives us forward/backward compat by default:

- **Add a field**: assign a new tag, add encode + decode lines. Old saves load fine — the field keeps its `Practice_Init` default.
- **Remove a field**: stop emitting the tag, mark `// REMOVED` in the registry, never reuse the ID. Old saves still load — the tag is silently ignored.
- **Rename a field**: keep the tag ID, change the C symbol. Wire format unchanged.
- **Change a field's TYPE or SEMANTICS**: bump `state_version`. This is the only case where wire format breaks. Optional: add a `TAG_OLD_*` migration entry that decodes the old type and converts.

`state_version` only bumps for breaking changes — most dev churn is invisible to users.

`lib_version` only bumps if the slot_manager header layout changes (rare).

**Overlay build mismatch handling**: `TAG_OVERLAY_BUILD_ID` is a hash of the overlay's ROM bytes for the saved scene. On load, if the current ROM's hash differs, we **refuse the overlay byte restore but still apply the non-overlay TLV fields**, and log "incompatible overlay snapshot — partial restore". This is the one place where partial application is genuinely safer than refusing entirely (it covers the common case of "I rebuilt the practice ROM since saving").

## Save/load flow

### Save (RAM slot) — single frame

1. Action triggered.
2. `slot_manager_save_ram(slot)` frees prior slot, mallocs `MAX_STATE_SIZE` (320 KB), writes header, calls game's `save_state()` callback.
3. Callback walks `PracticeSnapshot` + overlay region, emits TLV entries, returns total bytes.
4. `realloc()` shrinks to actual size; pointer parked in slot table.

### Save (SD) — multi-frame (interactive)

1. Action triggered → `osk_open("name?")`. Game freezes during OSK.
2. User confirms → callback fires.
3. Callback writes to RAM via slot_manager's save path, then `f_open(path, FA_WRITE | FA_CREATE_ALWAYS)`, `f_write`, `f_close`. ~50–200 ms depending on SD speed.
4. Display "saved" in HUD log.

### Load (same scene) — single frame

1. Validate header (magic, lib_version, state_version, scene_id sanity). Refuse on mismatch with descriptive log.
2. `load_state(payload, payload_size)`:
   a. Initialize all PracticeSnapshot fields to defaults (calls into `Practice_Init`'s reset path).
   b. Walk TLV: apply known tags, skip unknown.
   c. If `TAG_OVERLAY_BUILD_ID` matches current build: `bcopy(saved_overlay → vram_start, size)`.
   d. Audio: `Audio_ClearVoice()`; `AUDIO_SET_SPEC(saved.audio_spec)`; `AUDIO_PLAY_BGM(saved.audio_seq)`.
   e. `gPlayer[0].state = PLAYERSTATE_ACTIVE`; `Practice_Hud_Reset()`.

### Load (cross-scene) — state machine across frames

```
state: IDLE
  on load_request(slot):
    if scene_matches(slot): goto APPLY
    else: pin buffer; goto AWAIT_SCENE_LOAD

state: AWAIT_SCENE_LOAD
  enter:
    practice_overlay_request_load(state.scene_id, state.scene_setup)
    /* internally: gNextLevel = ...; gNextLevelPhase = ...;
                   gNextGameState = GSTATE_PLAY; gDrawMode = DRAW_NONE; */
  per-frame:
    if (gPlayState == PLAY_UPDATE
        && gPlayer != NULL
        && sCurrentScene.id == state.scene_id): goto APPLY
    if (frames_waited > MAX_LOAD_FRAMES): goto FAIL

state: APPLY
  apply overlay bytes (if build ID matches)
  apply audio
  apply non-overlay TLV fields
  free pinned buffer (SD load only; RAM slots stay)
  goto IDLE

state: FAIL
  log "load timeout"
  reset transition; goto IDLE
```

The pinned buffer survives across the AWAIT frames. SD-loaded buffers get freed on APPLY; RAM slot buffers stay alive for re-loads.

## Edge cases

| Case | Behavior |
|------|----------|
| Save during cutscene (`gPlayState != PLAY_UPDATE`) | Refuse; log "can't save here". `gPlayer == NULL`, NaN floats — guaranteed bad save. |
| Save during pause menu | Refuse (engine is in transition). |
| Save while practice menu open | Auto-close menu, then save. |
| Save during boss death | Allowed (user's choice; state captures mid-explosion). |
| Save right after `Practice_LaunchLevel` (before `PLAY_UPDATE`) | Refuse via `gPlayState` gate. |
| Load into Versus/training scene from real-game save | Refuse via scene_id range check. |
| State file with truncated body | `total_size` field validated against `f_size`; mismatch refuses load. |
| State file from `state_version` mismatch | Refuse with clear message. |
| `TAG_OVERLAY_BUILD_ID` mismatch | Apply non-overlay tags; skip overlay restore; log warning. |
| Power loss during save | Atomic via `.tmp` + `f_rename`; partial file ignored on next boot. |
| SD card removed mid-write | iodev returns error; partial file unlinked; log error. |
| SD card removed mid-read | Aborted load, no game state mutated (parse to temp buffer first, apply atomically). |
| Two saves in same frame | slot_manager busy flag; second call no-ops. |
| Save to existing filename | Overwrite (`FA_CREATE_ALWAYS`); log it. |
| FatFs `FR_NO_FILESYSTEM` | One-time HUD message: "format SD as FAT32 to enable SD features"; SD features disabled. RAM slots still work. |
| Out of RAM during state malloc | Refuse save; log. Without Pak this is more likely → cap slots at 2 there. |
| Restored audio sequence requires different audio bank | `AUDIO_SET_SPEC()` reloads the bank — same machinery as `Practice_LaunchLevel`. |
| Float NaN in restored state | `Practice_DrawFloat` NaN guard catches display; gameplay floats were valid at save → valid at restore. |

## Static invariants (additions to `tools/practice_invariants.py`)

```
check_lib_isolation()
  - Every .c/.h under lib/ is forbidden from including: practice.h, global.h,
    variables.h, sf64*.h, fox_*.h, anything from include/.

check_tag_registry()
  - Every TAG_* in lib/serial.h or src/practice/practice_*_tags.h is unique.
  - Tag IDs marked // REMOVED may not be reused.
  - Every PracticeSnapshot field has exactly one TAG_*.
  - Every PracticeConfig field has exactly one TAG_*.

check_serializer_parity()
  - For each tag in practice_save_tags.h: matching SAVE_TAG and LOAD_TAG_INTO
    calls exist in practice_save.c. No orphan tags, no duplicate emits.

check_iodev_layering()
  - iodev_sd_read_sectors / iodev_sd_write_sectors are only called from
    lib/fatfs/diskio.c and lib/test/iodev_test.c. Anything else fails the build.

check_overlay_table_complete()
  - practice_overlay.c's scene_id→region table covers every value of SceneId enum.

check_state_version_defined_once()
  - PRACTICE_STATE_VERSION defined exactly once across the codebase.

check_max_state_size_budget()
  - MAX_STATE_SIZE * MIN_RAM_SLOTS_NO_PAK <= 1MB
  - MAX_STATE_SIZE * MAX_RAM_SLOTS_WITH_PAK <= 2.5MB
```

## Testing strategy

### Layers

| Layer | Where it runs | Purpose | When |
|-------|---------------|---------|------|
| Static invariants | host (Python) | layering, tag uniqueness, struct↔serializer parity | every commit (pre-commit) |
| Host unit tests (NEW) | host (gcc native) | TLV codec, FatFs+diskio, slot_manager, watch_engine | every commit |
| BizHawk functional | emulator | in-ROM behavior with stub iodev | every commit (when BizHawk available) |
| Hardware verification | real SC64 + real ED64 | iodev backends + full SD round-trips | manual, per phase |

### Host unit tests (NEW infrastructure)

`lib/test/` with `Makefile` target `make lib-test`. Tests link against `iodev_test.c` (mock backend reading/writing a host file via `pread`/`pwrite`).

| Test | Asserts |
|------|---------|
| `test_serial.c` | TLV roundtrip; unknown tag ignored; truncation detected; size mismatch detected |
| `test_slot_manager.c` | Save/load roundtrip; version mismatch refused; magic mismatch refused; slot cycle wraps |
| `test_diskio.c` | FatFs glue round-trip via mock iodev |
| `test_fatfs_smoke.c` | Format 64MB FAT32 image, mount, write 100 files, read back, list directory |
| `test_watch_engine.c` | Add/remove/anchor/drag; save+load roundtrip via TLV |

### BizHawk functional tests

| Test | Notes |
|------|-------|
| `test_state_save_load_same_scene.lua` | Mid-Corneria save+restore; verify gPlayer pos + gHitCount |
| `test_state_save_load_cross_scene.lua` | Save Corneria → navigate to map → load → back in Corneria |
| `test_state_refuse_during_cutscene.lua` | Save attempt while `gPlayState != PLAY_UPDATE` is refused |
| `test_state_refuse_corrupt_header.lua` | Bad-magic buffer refused, no crash |
| `test_state_tlv_unknown_tag_skipped.lua` | Crafted file with unknown tag loads successfully (unknown tag ignored) |
| `test_state_overlay_build_id_mismatch.lua` | Modified build ID → partial restore (non-overlay applies, overlay rejected) |
| `test_state_slot_count_no_pak.lua` | 4MB mode → 2 slots |
| `test_state_slot_count_with_pak.lua` | 8MB mode → 8 slots |
| `test_watch_engine_add_remove.lua` | Add 3 via API, remove one, count=2 |
| `test_watch_persistence_stub.lua` | Stub-SD: save → reset → load → present |
| `test_config_persistence_stub.lua` | Toggle field → save → reset → field preserved |

iodev is stubbed via `IODEV_STUB=1` build flag linking `iodev_stub.c`. SD-dependent tests run against an in-RAM fake-SD buffer.

### Hardware tests (manual, per phase)

Each phase ships with a `HW_VERIFY.md` checklist. Examples:

- **iodev**: boot ROM, IS-Viewer logs `iodev_detect → SC64`, `iodev_sd_init → 0`. Read sector 0, dump bytes, compare against `dd if=/dev/diskN bs=512 count=1`.
- **State save/load**: round-trip per scene, cross-scene, hard-reset persistence.
- **Watches**: add via picker, anchor, drag, persist across reboot.
- **Config**: toggle settings, reboot, verify persistence.

## Implementation phases

Bottom-up, mapped to build order A. Each phase ships in isolation: builds, passes prior tests, adds new tests, gets manual hardware verification before next phase begins.

### Phase 1: iodev foundation (~600 LoC)

- `lib/iodev/iodev.{c,h}`, `iodev_sc64.c`, `iodev_ed64.c`, `iodev_stub.c`.
- Tests: host unit (mock), static invariants, hardware verify on both carts.
- **Done**: real SC64 reads sector 0 with matching bytes; real ED64 same.

### Phase 2: FatFs + diskio (~50 LoC + ~3.5 KLoC vendored)

- Vendor FatFs R0.15; author `diskio.c`.
- Tests: host unit (mount FAT32 image, R/W); hardware (round-trip 1 KB SC64↔PC↔ED64).
- **Done**: round-trip a known file across both carts.

### Phase 3: TLV serial + slot_manager RAM-only (~500 LoC)

- `lib/serial.{c,h}`, `lib/slot_manager.{c,h}` without SD methods.
- Tests: host unit (TLV roundtrip, slot manager state machine), BizHawk functional (fake state struct).
- **Done**: in-BizHawk save/load of a fake game-state struct.

### Phase 4: practice_save rewrite + practice_overlay (~800 LoC)

- Rewrite `practice_save.c` to use TLV macros.
- New `practice_overlay.c` with scene→VRAM table and build-ID hash.
- Tests: BizHawk same-scene save/load on Corneria.
- **Done**: in-ROM save/load works on Corneria; no regressions.

### Phase 5: cross-scene load state machine + menu wiring (~400 LoC)

- Load state machine (IDLE → AWAIT_SCENE_LOAD → APPLY → FAIL).
- Menu entries: slot picker, slot cycle, save/load buttons.
- Tests: BizHawk cross-scene save/load.
- **Done**: save Corneria, navigate to Sector Z, load Corneria save, end up in Corneria.

### Phase 6: file_browser + osk (~600 LoC)

- `lib/ui/osk.{c,h}`, `lib/ui/file_browser.{c,h}`.
- Add `_` glyph to small-text font (~5 lines in `fox_std_lib.c`).
- Tests: BizHawk input scripting; hardware visual.
- **Done**: type a name in OSK, see it confirmed; pick a file in browser, see it selected.

### Phase 7: SD slot persistence (~300 LoC)

- `slot_manager_save_sd_named` / `load_sd_named` wired through OSK + file_browser.
- Tests: BizHawk stub-SD; hardware full round-trip.
- **Done**: state file saved via OSK survives reboot, loads via file_browser.

### Phase 8: watch_engine + presets (~800 LoC)

- `lib/watch_engine.{c,h}`, `src/practice/practice_watch_presets.c`.
- Address picker UI (hex input).
- Tests: BizHawk add/remove/persist; hardware visual.
- **Done**: add watches via picker, anchor + drag, persist across reboot.

### Phase 9: config persistence (~200 LoC)

- `src/practice/practice_config_persist.c` for PracticeConfig TLV save/load.
- Debounced write hook on menu toggles.
- Tests: BizHawk toggle → reset → preserved; hardware confirmation.
- **Done**: every menu toggle persists across reboot.

### Total scope

- New code: ~6000 LoC (lib/) + ~1500 LoC (src/practice/) + ~500 LoC tests
- Vendored: ~3500 LoC (FatFs)
- Visible save state functionality by end of Phase 5
- Full SD support by Phase 7
- Full feature set by Phase 9

## Risk register

| Risk | Likelihood | Mitigation |
|------|------------|-----------|
| ED64 SDIO protocol underspecified or hard to clean-room | Medium | Time-box ED64 reverse-engineering at 3-5 days; if blocked, ship SC64-only first, mark ED64 as future. |
| FatFs has MIPS/N64 gotchas (cache coherence, alignment) | Low-Medium | FatFs is widely ported; libdragon has a working reference. Native MIPS support upstream. |
| Cross-scene load doesn't work via programmatic Practice_LaunchLevel-style hook | Medium | Audit `Practice_LaunchLevel` early in Phase 5; same machinery already proven for menu-driven launch. |
| Audio glitches when loading mid-song | Medium | gz's full sequencer state save is more thorough; our "set spec + play seq" may pop on transitions. Acceptable for v1; finer audio state save is a future enhancement. |
| `sCurrentScene`/`gSegments[]` desync after manual overlay byte-restore | Medium | Header includes `gSegments[]` snapshot; restored verbatim. Smoke-test in BizHawk. |
| Overlay byte-restore writes a region the audio thread is DMA-ing | Low | SF64 has separate audio heap; verify during impl. |
| Save format churn during dev | High | TLV makes most churn invisible. `state_version` bump only on breaking changes. |
| State buffer fragmentation over alloc/free cycles | Low | v1 uses `malloc`; switch to dedicated arena if it manifests. |
| Tag ID collision when adding new fields | N/A | Static invariant rejects build with duplicate tags. |
| Tag ID accidentally reused after removal | N/A | Static invariant scans for `// REMOVED` markers; refuses reuse. |

## Explicit non-goals

- **No macros** (input recording/playback) — separate project, deferred.
- **No multi-region overlay save** — SF64 has one active scene overlay at a time.
- **No state streaming for low-RAM** — fully-formed buffers in RAM are required.
- **No cross-version state migration via field name matching** — only TLV tag matching, which is far more reliable.
- **No claim on the SD card's filesystem** — we coexist as a normal subdirectory.
- **No bootstrap** (this is a development tool, not production) — version bumps may invalidate old saves; the TLV design minimizes this but does not eliminate it for `state_version` bumps.

## License notes

- FatFs is BSD-3-Clause. Compatible with any project licensing. Vendored verbatim.
- gz is GPL-2. We study its architecture freely but **do not copy code**. ED64 backend is a clean-room reimplementation guided by, but not derived from, gz's `ed64_x.c`.
- All new lib/ code is original.

## Open questions (defer to implementation)

- Exact ffconf settings (LFN buffer size, code page).
- Overlay build-ID hash algorithm (CRC32 vs FNV-1a vs xxhash).
- Maximum directory depth in file browser (default to 1, expand if user demand).
- Watch picker UX details (hex grid input vs OSK with hex chars only).

These are implementation details that don't affect the architecture; they get nailed down during the relevant phase.
