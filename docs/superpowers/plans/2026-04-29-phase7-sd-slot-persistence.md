# Phase 7 — SD Slot Persistence Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `slot_manager_save_sd_named` and `slot_manager_load_sd_named` in `lib/slot_manager.c` so state files survive power cycles on the flashcart SD card; wire the save/load callbacks in `practice_sd.c`; add atomic-write safety and error handling.

**Architecture:** `slot_manager_save_sd_named(path)` uses the registered `save_cb` to fill the Pak-pool scratch buffer (same buffer as RAM save), then writes it to the SD card via FatFs using a `.tmp`→`rename` atomic pattern. `slot_manager_load_sd_named(path)` reads the file into the same scratch buffer, validates the header, then calls `load_cb`. The scratch buffer (`Practice_Save_ScratchBase()`) is already 256 KB in Pak DRAM — no new allocation needed. `practice_sd.c` already has the OSK/file_browser callbacks from Phase 6; this phase makes them call real SD I/O instead of returning `ERR_UNSUPPORTED`.

**Tech Stack:** FatFs R0.15 (`lib/fatfs/ff.h`), existing `lib/slot_manager.c`, `Practice_Save_ScratchBase()` from `practice_save_slotpool.c`, host-native gcc for unit tests.

---

## 0. Status & input artefacts

Phase 6 delivered:
- `lib/ui/osk.{c,h}` — on-screen keyboard
- `lib/ui/file_browser.{c,h}` — SD file picker
- `src/practice/practice_sd.c` — calls `slot_manager_save_sd_named`/`load_sd_named`, which currently return `ERR_UNSUPPORTED`
- `_` glyph in `sSmallChars[]`

What this phase adds:
- Real implementation of `slot_manager_save_sd_named` and `slot_manager_load_sd_named` in `lib/slot_manager.c`
- Atomic write (`/sf64-practice/states/NAME.tmp` → rename to `NAME.SF64ST`)
- SD scratch buffer plumbed via a new `slot_manager_set_sd_scratch` call (keeps slot_manager.c host-portable without knowing about `Practice_Save_ScratchBase`)
- Host unit test (`lib/test/test_slot_manager_sd.c`) using a RAM-backed fake diskio
- Static invariants confirming SD methods are implemented
- `HW_VERIFY_phase7.md`

## 1. File map

| File | Action | Responsibility |
|------|--------|----------------|
| `lib/slot_manager.h` | Modify | Add `slot_manager_set_sd_scratch` declaration |
| `lib/slot_manager.c` | Modify | Implement `save_sd_named`, `load_sd_named`, `set_sd_scratch` |
| `lib/test/test_slot_manager_sd.c` | Create | Host round-trip test with RAM-backed fake diskio |
| `lib/test/diskio_ram.c` | Create | 512-KB RAM-backed FatFs diskio for tests only |
| `lib/test/Makefile` | Modify | Add test_slot_manager_sd target |
| `src/practice/practice_sd.c` | Modify | Call `slot_manager_set_sd_scratch` in `Practice_Sd_Init` |
| `tools/practice_invariants.py` | Modify | Add `check_sd_methods_implemented` |
| `docs/superpowers/plans/HW_VERIFY_phase7.md` | Create | Manual hardware verification checklist |

## 2. Decisions locked in

| Decision | Choice |
|----------|--------|
| Scratch buffer | `slot_manager.c` does NOT call `Practice_Save_ScratchBase()` directly (that would couple lib/ to game code). Instead, `practice_sd.c` calls `slot_manager_set_sd_scratch(Practice_Save_ScratchBase(), MAX_STATE_SIZE)` in `Practice_Sd_Init`. The slot_manager stores the pointer and uses it for SD I/O. |
| Atomic write | Write to path + `.tmp` suffix (append 4 chars), then `f_rename` to final path. On any write error, `f_unlink` the `.tmp`. |
| Load buffer | SD load reads into the SAME scratch buffer used for save. This means no second allocation. The constraint: you cannot have a load in progress while a save is pending (but the practice ROM is single-threaded, so this is guaranteed). |
| `total_size` validation on load | Read the 4-byte `total_size` field at header offset 0x08, check it against `f_size(fp)`. Refuse if mismatched. |
| FatFs include in slot_manager.c | `slot_manager.c` conditionally includes `ff.h` via `#ifdef SLOT_MANAGER_USE_FATFS`. The ROM build defines `SLOT_MANAGER_USE_FATFS=1` in the Makefile. Host tests link a RAM diskio that makes FatFs functional, so the tests also define `SLOT_MANAGER_USE_FATFS=1`. |
| tmp path length | `tmp_path[FB_PATH_MAX]` on the stack — max 64 bytes, fits in N64 game stack safely (no `PracticeSnapshot` on stack). |
| Error propagation | FatFs `FRESULT` mapped to `SLOT_MANAGER_ERR_NO_STORAGE` for all I/O errors. Callers use the existing error code. |
| Directory creation | `practice_sd.c` calls `f_mkdir(SD_DIR)` before `slot_manager_save_sd_named` (already in `on_save_name_confirmed` from Phase 6). `slot_manager.c` does not create directories. |

---

## Chunk 1: slot_manager SD I/O implementation

### Task 1: Add `slot_manager_set_sd_scratch` to header

**Files:**
- Modify: `lib/slot_manager.h`

- [ ] **Step 1: Add declaration after `slot_manager_prev_slot`**

```c
/* Phase 7: SD persistence. Call once after slot_manager_init() with a
 * caller-owned scratch buffer of at least max_state_size bytes.
 * Required before slot_manager_save_sd_named / load_sd_named will work. */
void slot_manager_set_sd_scratch(void *buf, uint32_t buf_size);
```

Also add a note that SD methods require `SLOT_MANAGER_USE_FATFS` to be defined (otherwise they return `ERR_UNSUPPORTED`):

```c
/* SD methods are compiled only when SLOT_MANAGER_USE_FATFS is defined.
 * Without it, save_sd_named and load_sd_named return ERR_UNSUPPORTED. */
int slot_manager_save_sd_named(const char *path);
int slot_manager_load_sd_named(const char *path);
```

(The existing declarations stay; just add the comment.)

---

### Task 2: Implement SD methods in `lib/slot_manager.c`

**Files:**
- Modify: `lib/slot_manager.c`

- [ ] **Step 1: Add scratch buffer fields to `slot_manager_state_t`**

After the existing fields in `slot_manager_state_t`, add:
```c
    uint8_t *sd_scratch;        /* caller-provided; min max_state_size bytes */
    uint32_t sd_scratch_size;
```

- [ ] **Step 2: Implement `slot_manager_set_sd_scratch`**

After the existing `slot_manager_prev_slot` function, add:

```c
void slot_manager_set_sd_scratch(void *buf, uint32_t buf_size) {
    sSlotManager.sd_scratch      = (uint8_t *)buf;
    sSlotManager.sd_scratch_size = buf_size;
}
```

- [ ] **Step 3: Add FatFs conditional include at top of file**

After `#include "slot_manager.h"`, add:

```c
#ifdef SLOT_MANAGER_USE_FATFS
#include "../fatfs/ff.h"
#endif
```

- [ ] **Step 4: Implement `slot_manager_save_sd_named`**

Replace the existing stub:

```c
#ifdef SLOT_MANAGER_USE_FATFS
static int tmp_path_from(const char *path, char *tmp, uint32_t tmp_size) {
    uint32_t i;
    for (i = 0; path[i] && i + 4 < tmp_size; i++) tmp[i] = path[i];
    tmp[i++] = '.';
    tmp[i++] = 't';
    tmp[i++] = 'm';
    tmp[i++] = 'p';
    tmp[i]   = '\0';
    return (path[i - 4] == '\0') ? -1 : 0;  /* -1 if path was truncated */
}
#endif

int slot_manager_save_sd_named(const char *path) {
#ifndef SLOT_MANAGER_USE_FATFS
    (void)path;
    return SLOT_MANAGER_ERR_UNSUPPORTED;
#else
    char     tmp[FF_MAX_LFN + 5];  /* path + ".tmp" + NUL */
    FIL      fp;
    FRESULT  res;
    UINT     written;
    uint8_t *base;
    uint8_t *payload;
    uint32_t payload_cap;
    uint32_t payload_size;
    uint32_t total_size;

    if (!path || !sSlotManager.save_cb)      return SLOT_MANAGER_ERR_PARAM;
    if (!sSlotManager.sd_scratch ||
        sSlotManager.sd_scratch_size < sSlotManager.max_state_size)
        return SLOT_MANAGER_ERR_NO_STORAGE;

    base        = sSlotManager.sd_scratch;
    payload     = base + SLOT_MANAGER_HEADER_SIZE;
    payload_cap = sSlotManager.sd_scratch_size - SLOT_MANAGER_HEADER_SIZE;

    /* Fill header + payload via save callback */
    slot_zero(base, sSlotManager.max_state_size);
    payload_size = sSlotManager.save_cb(payload, payload_cap);
    if (payload_size > payload_cap) return SLOT_MANAGER_ERR_OVERFLOW;

    total_size = SLOT_MANAGER_HEADER_SIZE + payload_size;
    base[0x00] = SLOT_MAGIC_0;
    base[0x01] = SLOT_MAGIC_1;
    base[0x02] = SLOT_MAGIC_2;
    base[0x03] = SLOT_MAGIC_3;
    slot_put_le16(&base[0x04], sSlotManager.lib_version);
    slot_put_le16(&base[0x06], sSlotManager.state_version);
    slot_put_le32(&base[0x08], total_size);

    /* Atomic write: write to .tmp then rename */
    if (tmp_path_from(path, tmp, sizeof(tmp)) != 0) return SLOT_MANAGER_ERR_PARAM;

    res = f_open(&fp, tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) return SLOT_MANAGER_ERR_NO_STORAGE;

    res = f_write(&fp, base, total_size, &written);
    f_close(&fp);

    if (res != FR_OK || written != total_size) {
        f_unlink(tmp);
        return SLOT_MANAGER_ERR_NO_STORAGE;
    }

    /* Rename .tmp → final path (atomic on FAT) */
    f_unlink(path);  /* remove existing if any */
    res = f_rename(tmp, path);
    if (res != FR_OK) {
        f_unlink(tmp);
        return SLOT_MANAGER_ERR_NO_STORAGE;
    }

    return SLOT_MANAGER_OK;
#endif
}
```

- [ ] **Step 5: Implement `slot_manager_load_sd_named`**

Replace the existing stub:

```c
int slot_manager_load_sd_named(const char *path) {
#ifndef SLOT_MANAGER_USE_FATFS
    (void)path;
    return SLOT_MANAGER_ERR_UNSUPPORTED;
#else
    FIL      fp;
    FRESULT  res;
    FSIZE_t  fsize;
    UINT     bytes_read;
    uint8_t *base;
    uint32_t total_size;
    uint32_t payload_size;
    int      cb_result;

    if (!path || !sSlotManager.load_cb)      return SLOT_MANAGER_ERR_PARAM;
    if (!sSlotManager.sd_scratch ||
        sSlotManager.sd_scratch_size < sSlotManager.max_state_size)
        return SLOT_MANAGER_ERR_NO_STORAGE;

    base = sSlotManager.sd_scratch;

    res = f_open(&fp, path, FA_READ);
    if (res != FR_OK) return SLOT_MANAGER_ERR_NO_STORAGE;

    fsize = f_size(&fp);
    if (fsize < SLOT_MANAGER_HEADER_SIZE || fsize > sSlotManager.sd_scratch_size) {
        f_close(&fp);
        return SLOT_MANAGER_ERR_CORRUPT;
    }

    res = f_read(&fp, base, (UINT)fsize, &bytes_read);
    f_close(&fp);

    if (res != FR_OK || bytes_read != (UINT)fsize) return SLOT_MANAGER_ERR_NO_STORAGE;

    /* Validate magic */
    if (base[0x00] != SLOT_MAGIC_0 || base[0x01] != SLOT_MAGIC_1 ||
        base[0x02] != SLOT_MAGIC_2 || base[0x03] != SLOT_MAGIC_3) {
        return SLOT_MANAGER_ERR_MAGIC;
    }

    /* Version check */
    if (slot_get_le16(&base[0x04]) != sSlotManager.lib_version ||
        slot_get_le16(&base[0x06]) != sSlotManager.state_version) {
        return SLOT_MANAGER_ERR_VERSION;
    }

    /* total_size must match file size */
    total_size = slot_get_le32(&base[0x08]);
    if (total_size != (uint32_t)fsize || total_size < SLOT_MANAGER_HEADER_SIZE) {
        return SLOT_MANAGER_ERR_CORRUPT;
    }

    payload_size = total_size - SLOT_MANAGER_HEADER_SIZE;
    cb_result = sSlotManager.load_cb(&base[SLOT_MANAGER_HEADER_SIZE], payload_size);
    if (cb_result != 0) return SLOT_MANAGER_ERR_CALLBACK;
    return SLOT_MANAGER_OK;
#endif
}
```

- [ ] **Step 6: Add `SLOT_MANAGER_USE_FATFS` to Makefile**

Open `Makefile`. Find the line that defines `PRACTICE_ROM` (something like `CFLAGS += -DPRACTICE_ROM=1`). Add after it:
```makefile
CFLAGS += -DSLOT_MANAGER_USE_FATFS=1
```
This ensures the ROM build compiles the SD methods.

- [ ] **Step 7: Build check**

```bash
make practice -j4 2>&1 | tail -10
```
Expected: clean build.

- [ ] **Step 8: Commit**

```bash
git add lib/slot_manager.h lib/slot_manager.c Makefile
git commit -m "feat(slot_manager): implement save/load_sd_named with atomic write via FatFs"
```

---

### Task 3: Wire scratch buffer from practice_sd.c

**Files:**
- Modify: `src/practice/practice_sd.c`

- [ ] **Step 1: Add the `slot_manager_set_sd_scratch` call in `Practice_Sd_Init`**

In `src/practice/practice_sd.c`, in `Practice_Sd_Init()`, add after the `sSdAvailable` line:

```c
void Practice_Sd_Init(void) {
    sSdAvailable = (iodev_detect() != IODEV_NONE);
    /* Provide Pak scratch to slot_manager for SD I/O (same buffer as RAM save) */
    slot_manager_set_sd_scratch(Practice_Save_ScratchBase(), MAX_STATE_SIZE);
}
```

Also add `#include "../lib/slot_manager.h"` at the top of `practice_sd.c` if not already present (it should be from Phase 6).

- [ ] **Step 2: Build check**

```bash
make practice -j4 2>&1 | tail -5
```
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add src/practice/practice_sd.c
git commit -m "feat(practice_sd): wire Pak scratch buffer into slot_manager SD I/O"
```

---

## Chunk 2: Host unit tests

### Task 4: RAM-backed fake diskio for testing

**Files:**
- Create: `lib/test/diskio_ram.c`

This replaces `lib/fatfs/diskio.c` in the test binary, providing a 512-KB in-memory "disk" so FatFs can actually read and write during host tests.

- [ ] **Step 1: Write diskio_ram.c**

```c
/* lib/test/diskio_ram.c — RAM-backed FatFs diskio for host unit tests.
 * Provides a 512 KB virtual disk formatted as FAT16 at test startup.
 * Link this INSTEAD OF lib/fatfs/diskio.c in test binaries. */
#include "../fatfs/ff.h"
#include "../fatfs/diskio.h"
#include <string.h>
#include <stdlib.h>

#define RAM_DISK_SECTORS  1024u   /* 512 KB = 1024 × 512 */
#define SECTOR_SIZE       512u

static uint8_t *sDisk = NULL;
static bool     sDiskReady = false;

/* Call once before FatFs operations. Returns 0 on success. */
int diskio_ram_init(void) {
    if (!sDisk) {
        sDisk = (uint8_t *)malloc(RAM_DISK_SECTORS * SECTOR_SIZE);
        if (!sDisk) return -1;
    }
    memset(sDisk, 0, RAM_DISK_SECTORS * SECTOR_SIZE);
    sDiskReady = false;
    return 0;
}

/* FatFs hooks */
DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; sDiskReady = (sDisk != NULL); return sDiskReady ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return sDiskReady ? 0 : STA_NOINIT; }

DRESULT disk_read(BYTE pdrv, BYTE *buf, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!sDiskReady || sector + count > RAM_DISK_SECTORS) return RES_ERROR;
    memcpy(buf, sDisk + sector * SECTOR_SIZE, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buf, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!sDiskReady || sector + count > RAM_DISK_SECTORS) return RES_ERROR;
    memcpy(sDisk + sector * SECTOR_SIZE, buf, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buf) {
    (void)pdrv;
    if (!sDiskReady) return RES_NOTRDY;
    switch (cmd) {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t *)buf = RAM_DISK_SECTORS; return RES_OK;
        case GET_SECTOR_SIZE:  *(WORD *)buf  = SECTOR_SIZE;      return RES_OK;
        case GET_BLOCK_SIZE:   *(DWORD *)buf = 1;                return RES_OK;
        default: return RES_PARERR;
    }
}

DWORD get_fattime(void) { return 0; }
```

- [ ] **Step 2: Verify it compiles**

```bash
gcc -I.. -Ilib -c lib/test/diskio_ram.c -o /tmp/diskio_ram.o && echo OK
```

---

### Task 5: Host round-trip test (`lib/test/test_slot_manager_sd.c`)

**Files:**
- Create: `lib/test/test_slot_manager_sd.c`
- Modify: `lib/test/Makefile`

- [ ] **Step 1: Write test**

```c
/* lib/test/test_slot_manager_sd.c — slot_manager SD round-trip with RAM diskio */
#define PRACTICE_ROM 1
#define SLOT_MANAGER_USE_FATFS 1
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../slot_manager.h"
#include "../fatfs/ff.h"

/* Forward declared in diskio_ram.c */
int diskio_ram_init(void);

/* ---- Fake game state for save/load callbacks ---- */
static uint32_t sLastSaved = 0xDEADBEEFu;
static uint32_t sLastLoaded = 0;

static uint32_t fake_save(void *buf, uint32_t cap) {
    uint32_t *p = (uint32_t *)buf;
    (void)cap;
    p[0] = sLastSaved;
    return sizeof(uint32_t);
}

static int fake_load(const void *buf, uint32_t size) {
    const uint32_t *p = (const uint32_t *)buf;
    (void)size;
    sLastLoaded = p[0];
    return 0;
}

/* ---- Scratch buffer (mimics Pak-pool scratch) ---- */
#define SCRATCH_SIZE (256u * 1024u)
static uint8_t sScratch[SCRATCH_SIZE];

/* ---- FatFs volume ---- */
static FATFS sFatFs;

static void format_and_mount(void) {
    BYTE work[FF_MAX_SS];
    MKFS_PARM opt = { FM_FAT, 0, 0, 0, 0 };
    FRESULT res;

    res = f_mkfs("", &opt, work, sizeof(work));
    assert(res == FR_OK);
    res = f_mount(&sFatFs, "", 1);
    assert(res == FR_OK);
    f_mkdir("/sf64-practice");
    f_mkdir("/sf64-practice/states");
}

static void test_save_and_load_roundtrip(void) {
    int r;
    sLastSaved  = 0x12345678u;
    sLastLoaded = 0;

    slot_manager_init(1, 1, fake_save, fake_load, 0);
    slot_manager_set_sd_scratch(sScratch, SCRATCH_SIZE);

    r = slot_manager_save_sd_named("/sf64-practice/states/TEST.SF64ST");
    assert(r == SLOT_MANAGER_OK);

    r = slot_manager_load_sd_named("/sf64-practice/states/TEST.SF64ST");
    assert(r == SLOT_MANAGER_OK);
    assert(sLastLoaded == sLastSaved);

    printf("  test_save_and_load_roundtrip: PASS\n");
}

static void test_load_truncated_file(void) {
    FIL fp; UINT bw; int r;
    /* Write garbage shorter than header */
    f_open(&fp, "/sf64-practice/states/BAD.SF64ST", FA_WRITE | FA_CREATE_ALWAYS);
    f_write(&fp, "JUNK", 4, &bw);
    f_close(&fp);

    slot_manager_init(1, 1, fake_save, fake_load, 0);
    slot_manager_set_sd_scratch(sScratch, SCRATCH_SIZE);
    r = slot_manager_load_sd_named("/sf64-practice/states/BAD.SF64ST");
    assert(r != SLOT_MANAGER_OK);
    printf("  test_load_truncated_file: PASS\n");
}

static void test_load_bad_magic(void) {
    FIL fp; UINT bw; int r;
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    buf[0] = 'X'; buf[1] = 'X'; buf[2] = 'X'; buf[3] = 'X';
    /* put valid total_size */
    buf[8] = 64; buf[9] = 0; buf[10] = 0; buf[11] = 0;

    f_open(&fp, "/sf64-practice/states/BADMAG.SF64ST", FA_WRITE | FA_CREATE_ALWAYS);
    f_write(&fp, buf, sizeof(buf), &bw);
    f_close(&fp);

    slot_manager_init(1, 1, fake_save, fake_load, 0);
    slot_manager_set_sd_scratch(sScratch, SCRATCH_SIZE);
    r = slot_manager_load_sd_named("/sf64-practice/states/BADMAG.SF64ST");
    assert(r == SLOT_MANAGER_ERR_MAGIC);
    printf("  test_load_bad_magic: PASS\n");
}

static void test_atomic_write_produces_final_file(void) {
    FILINFO fi;
    FRESULT res;
    int r;
    sLastSaved = 0xCAFEBABEu;

    slot_manager_init(1, 1, fake_save, fake_load, 0);
    slot_manager_set_sd_scratch(sScratch, SCRATCH_SIZE);
    r = slot_manager_save_sd_named("/sf64-practice/states/ATOMIC.SF64ST");
    assert(r == SLOT_MANAGER_OK);

    /* Final file must exist */
    res = f_stat("/sf64-practice/states/ATOMIC.SF64ST", &fi);
    assert(res == FR_OK);
    /* Temp file must NOT exist */
    res = f_stat("/sf64-practice/states/ATOMIC.SF64ST.tmp", &fi);
    assert(res == FR_NO_FILE);

    printf("  test_atomic_write_produces_final_file: PASS\n");
}

int main(void) {
    printf("test_slot_manager_sd:\n");
    assert(diskio_ram_init() == 0);
    format_and_mount();
    test_save_and_load_roundtrip();
    test_load_truncated_file();
    test_load_bad_magic();
    test_atomic_write_produces_final_file();
    printf("All slot_manager_sd tests PASS.\n");
    return 0;
}
```

- [ ] **Step 2: Add to lib/test/Makefile**

```makefile
FATFS_SRCS = ../fatfs/ff.c ../fatfs/ffunicode.c ../fatfs/ff_libc.c

test_slot_manager_sd: test_slot_manager_sd.c ../slot_manager.c diskio_ram.c $(FATFS_SRCS)
	$(CC) $(CFLAGS) -DPRACTICE_ROM -DSLOT_MANAGER_USE_FATFS=1 -I.. -o $@ $^

```
Find the existing `run-all` target in the Makefile and append `test_slot_manager_sd` as a dependency and add `./test_slot_manager_sd` to its recipe, following the exact pattern used by other test targets in that file.
```
```

Note: do NOT link `../fatfs/diskio.c` here (we use `diskio_ram.c` instead).

- [ ] **Step 3: Run tests**

```bash
make -C lib/test test_slot_manager_sd && ./lib/test/test_slot_manager_sd
```
Expected:
```
test_slot_manager_sd:
  test_save_and_load_roundtrip: PASS
  test_load_truncated_file: PASS
  test_load_bad_magic: PASS
  test_atomic_write_produces_final_file: PASS
All slot_manager_sd tests PASS.
```

- [ ] **Step 4: Commit**

```bash
git add lib/test/diskio_ram.c lib/test/test_slot_manager_sd.c lib/test/Makefile
git commit -m "test(slot_manager): SD save/load round-trip host unit tests with RAM diskio"
```

---

## Chunk 3: Static invariants + HW_VERIFY

### Task 6: Static invariants

**Files:**
- Modify: `tools/practice_invariants.py`

Add two new checks:

```python
def check_sd_save_implemented():
    src = read("lib/slot_manager.c")
    # The stub has (void)path; return ERR_UNSUPPORTED. The implementation has f_open.
    if "f_open" not in src:
        errors.append(
            "slot_manager_save_sd_named appears to still be a stub (no f_open call) "
            "(check_sd_save_implemented)"
        )
    if "f_rename" not in src:
        errors.append(
            "slot_manager_save_sd_named missing atomic rename (check_sd_save_implemented)"
        )

def check_sd_load_implemented():
    src = read("lib/slot_manager.c")
    if "f_read" not in src:
        errors.append(
            "slot_manager_load_sd_named appears to still be a stub (no f_read call) "
            "(check_sd_load_implemented)"
        )
    if "f_size" not in src:
        errors.append(
            "slot_manager_load_sd_named missing f_size sanity check "
            "(check_sd_load_implemented)"
        )
```

Register in `main()`:
```python
check_sd_save_implemented()
check_sd_load_implemented()
```

- [ ] **Step 1: Add functions and register them**

- [ ] **Step 2: Run**

```bash
python3 tools/practice_invariants.py
```
Expected: `Practice ROM invariant checks passed.`

- [ ] **Step 3: Commit**

```bash
git add tools/practice_invariants.py
git commit -m "test(invariants): add Phase 7 SD persistence checks"
```

---

### Task 7: Run full test suite

- [ ] **Step 1: Run all lib tests**

```bash
make -C lib/test run-all
```
Expected: all pass.

- [ ] **Step 2: Run BizHawk functional tests**

```bash
python3 tools/run_tests.py
```
Expected: all pass (no new regressions; SD-specific tests need hardware for full validation).

- [ ] **Step 3: Run static invariants**

```bash
python3 tools/practice_invariants.py
```
Expected: pass.

- [ ] **Step 4: Full clean build**

```bash
rm -rf build/ && make practice -j4 2>&1 | tail -5
```
Expected: clean build.

---

### Task 8: HW_VERIFY_phase7.md

**Files:**
- Create: `docs/superpowers/plans/HW_VERIFY_phase7.md`

```markdown
# HW_VERIFY Phase 7 — SD Slot Persistence

## Prerequisites
- SC64 v2 with FAT32 SD card
- IS-Viewer session running (`sc64deployer debug --isv 0x03FF0000`)
- Latest ROM built and uploaded via `./tools/sc64dev`
- Phase 6 HW_VERIFY passed (OSK and file_browser render correctly)

## T1 — Save to SD
1. Launch Corneria via practice level select.
2. Open practice menu (L+R).
3. Press Z. OSK appears with prompt "SD SAVE NAME:".
4. Type "CORNERIA" using D-pad + A. Press START.
5. IS-Viewer should show no error. HUD shows "SD SAVE OK".
6. Verify on PC: insert SD in reader, check for `/sf64-practice/states/CORNERIA.SF64ST`. File size should be non-zero.

## T2 — Load from SD (same scene)
1. Without rebooting, open practice menu, press Z+B.
2. File browser shows "CORNERIA.SF64ST".
3. Press A. HUD shows "SD LOAD OK".
4. Verify game state restored (position, hit count match what was saved).

## T3 — SD persistence across reboot
1. Power cycle the N64 (or press reset).
2. Navigate to Corneria.
3. Open practice menu, press Z+B.
4. File browser shows "CORNERIA.SF64ST" (survives reboot).
5. Press A. HUD shows "SD LOAD OK". State restored.

## T4 — Overwrite existing file
1. Save a new state to "CORNERIA" (different position this time).
2. IS-Viewer should show no `.tmp` file left behind.
3. Load it — verify it loads the NEW state, not the old one.

## T5 — Cancel save
1. Open OSK (Z in menu). Press Z to cancel.
2. HUD shows "SD CANCEL". No file written (check SD — no new .tmp files).

## T6 — Cancel load
1. Open file browser (Z+B). Press B to cancel.
2. HUD shows "SD CANCEL". Game state unchanged.

## T7 — SD error path (optional)
1. Remove SD card mid-session (while menu is open but before pressing Z).
2. Press Z. HUD shows "NO SD CART" or "SD SAVE FAIL".
3. No crash.

## PASS criteria
- T1–T6 all pass without crash or hang
- IS-Viewer shows no unexpected error output
- SD card file visible on PC after T1
- State correctly restored after T3 reboot
```

- [ ] **Step 1: Write and commit**

```bash
git add docs/superpowers/plans/HW_VERIFY_phase7.md
git commit -m "docs: Phase 7 hardware verification checklist"
```

---

## Exit criteria

- [ ] `slot_manager_save_sd_named` writes a valid TLV file to the SD card (atomic via `.tmp` → rename)
- [ ] `slot_manager_load_sd_named` reads, validates (magic, version, size), and applies via `load_cb`
- [ ] `slot_manager_set_sd_scratch` is declared in `slot_manager.h` and called from `practice_sd.c`
- [ ] `SLOT_MANAGER_USE_FATFS=1` is set in the ROM Makefile; SD methods compile without stubs
- [ ] Host round-trip test passes: save + load returns same data
- [ ] Atomic write test confirms: final file exists, `.tmp` does not
- [ ] `make -C lib/test run-all` passes all tests
- [ ] `python3 tools/practice_invariants.py` passes (including `check_sd_save_implemented` and `check_sd_load_implemented`)
- [ ] `make practice -j4` produces clean build
- [ ] `python3 tools/run_tests.py` passes (no regressions)
- [ ] `HW_VERIFY_phase7.md` exists
- [ ] On real hardware (SC64): save in Corneria → reboot → load from file browser → state restored
