# Phase 2: FatFs port + diskio glue Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Layer a real filesystem (FAT16/FAT32) on top of the iodev SD primitives so subsequent phases can read and write actual files (`config.dat`, `watches.dat`, `*.sf64st` save state files) rather than raw sectors.

**Architecture:** Vendor [FatFs R0.15](http://elm-chan.org/fsw/ff/) (BSD-3-Clause, ~3.5 KLoC) into `lib/fatfs/` unmodified. Author one new file `lib/fatfs/diskio.c` (~80 LoC) that maps FatFs's required `disk_read`/`disk_write`/`disk_initialize`/`disk_status`/`disk_ioctl` callbacks onto Phase 1a's `iodev_sd_*` API. Configure `ffconf.h` for our N64 use case (1 volume, 512-byte sectors, FAT16+FAT32 only, LFN Mode 1, no RTC, no malloc). Tests at three layers: host unit tests on a mock-SD-backed FAT32 image, BizHawk functional tests with a stub iodev, hardware verification on real SC64.

**Tech Stack:** FatFs R0.15 (vendored, BSD-3), C (IDO C89-compatible — FatFs is portable), iodev abstraction from Phase 1a, lib/test infrastructure (created by Phase 1b Task 2 — see Dependencies note below).

**Spec reference:** `docs/superpowers/specs/2026-04-27-gz-style-features-design.md` lines 583-587 (Phase 2 deliverable).

---

## Dependencies on Phase 1b

This plan assumes Phase 1b has shipped at minimum:
- Phase 1b Task 2: `lib/sd_crc.{c,h}` and `lib/test/Makefile` (host unit test infrastructure).

If Phase 1b stalled before Task 2, this plan's Task 5 (host unit tests) needs to bootstrap `lib/test/` itself. The work involved is essentially what Phase 1b Task 2 specifies. Watch for this when starting Task 5.

This plan does NOT depend on Phase 1b's ED64 backend (Tasks 1, 3-8). FatFs sits above iodev; whether `iodev_sd_*` calls dispatch to SC64 or ED64 is invisible to the FatFs glue.

If Phase 1b's patcher refactor (Task 1 Step 4) shipped, this plan extends that dynamic-walk algorithm by adding entries to the unified list. If it didn't, this plan adds an analogous step for `lib/fatfs/*.o` injection.

---

## File Structure

**New files (vendored from upstream — DO NOT MODIFY):**
- `lib/fatfs/ff.c` (~6000 LoC) — FatFs core. Vendored verbatim from R0.15.
- `lib/fatfs/ff.h` (~400 LoC) — FatFs public API. Vendored verbatim.
- `lib/fatfs/ffunicode.c` — Unicode tables (we configure for ANSI/OEM only, so this file is mostly empty after preprocessing; keep it because FatFs's build expects the symbol).
- `lib/fatfs/diskio.h` (~70 LoC) — disk_* callback contract. Vendored verbatim.
- `lib/fatfs/LICENSE` — FatFs upstream license text (BSD-3-Clause).
- `lib/fatfs/README.md` — provenance: FatFs version, source URL, what local modifications (only `ffconf.h`).

**New files (authored):**
- `lib/fatfs/ffconf.h` (~280 LoC) — FatFs configuration. Authored from the upstream template, our customized values.
- `lib/fatfs/diskio.c` (~80 LoC) — glue mapping FatFs's `disk_*` callbacks to `iodev_sd_*`.
- `lib/test/test_diskio.c` — host unit test for the glue layer using a mock iodev backed by a host file.
- `lib/test/test_fatfs_smoke.c` — host integration test that mounts a host FAT32 image, writes 50 small files, reads them back, lists the directory.
- `tests/test_fatfs_mount.lua` — BizHawk functional test for in-ROM mount + small file round-trip via stub iodev backed by a RAM buffer pre-loaded with a FAT32 image.
- `docs/superpowers/plans/HW_VERIFY_phase2.md` — manual hardware verification on real SC64.

**Modified files:**
- `Makefile` — already includes `lib/` in SRC_DIRS (Phase 1a). Add `-Ilib/fatfs` for diskio's includes of `ff.h` and `diskio.h`. Optional: per-directory CFLAGS to silence FatFs-internal warnings the project's `-Werror` would catch.
- `tools/patch_linker_script.py` — extend to inject `lib/fatfs/*.o` (`ff`, `ffunicode`, `diskio`) into the linker script.
- `tools/practice_invariants.py` — add `check_fatfs_isolation()` (FatFs source must NOT include game/practice headers, only its own `ff.h`/`diskio.h` and FatFs-spec headers).
- `lib/test/Makefile` — extend with new test targets (`test_diskio`, `test_fatfs_smoke`).

**Not touched:**
- `lib/iodev/*` — public API stable from Phase 1a/1b.
- `lib/lib_types.h`, `lib/sd_crc.{c,h}` — Phase 1b artifacts.
- `src/practice/*` — no consumer of FatFs in Phase 2; that's Phases 7+.

---

## Shippable checkpoints

- **After Task 1:** FatFs source vendored and `ffconf.h` tuned. Code doesn't compile yet (no diskio).
- **After Task 2:** `diskio.c` authored. FatFs builds standalone (host gcc); not yet linked into ROM.
- **After Task 3:** Practice ROM links cleanly with FatFs included. No filesystem usage yet, just the symbols are present.
- **After Task 4:** Static invariants enforce lib/fatfs isolation. Build remains clean.
- **After Task 5:** Host unit tests pass — `make lib-test` runs FatFs against a host FAT32 image and verifies file round-trip. **This is the algorithmic correctness milestone.**
- **After Task 6:** BizHawk test passes — in-ROM FatFs mount + read works against a stub iodev. **This is the in-N64-runtime correctness milestone.**
- **After Task 7:** Hardware verification on real SC64 — write a file from the ROM, eject SD, read on PC, content matches. **This is Phase 2's done state.**
- **After Task 8:** Exit gate.

If Phase 2 stalls anywhere between Tasks 1-4, the existing practice ROM is unaffected (FatFs would just be vendored but unused). Stalling at Task 5+ means we have a partial implementation but the Phase 1a state is intact.

---

## Task 1: Vendor FatFs and configure ffconf.h

**Files:**
- Download: FatFs R0.15 source from upstream
- Create: `lib/fatfs/ff.c`, `lib/fatfs/ff.h`, `lib/fatfs/ffunicode.c`, `lib/fatfs/diskio.h`, `lib/fatfs/LICENSE`, `lib/fatfs/README.md`
- Create: `lib/fatfs/ffconf.h` (customized from template)

- [ ] **Step 1: Download FatFs R0.15 source**

```bash
cd /tmp
curl -L -o ff15.zip 'http://elm-chan.org/fsw/ff/arc/ff15.zip'
unzip -q ff15.zip -d ff15
ls ff15/source/
```

Expected: `ff.c`, `ff.h`, `ffconf.h`, `ffsystem.c`, `ffunicode.c`, `diskio.h` (note: `diskio.c` is documentation in the upstream, not a real source file — we author our own).

If `elm-chan.org` is unreachable, fall back to a known-good GitHub mirror like `abbrev/ff` or `stm32duino/FatFs`. Verify the version reads `R0.15` in `ff.h`'s top comment block.

- [ ] **Step 2: Vendor source files into `lib/fatfs/`**

```bash
cd /Users/qwoodmansee/code/sf64-practice-rom/.claude/worktrees/user-requests
mkdir -p lib/fatfs
cp /tmp/ff15/source/ff.c        lib/fatfs/ff.c
cp /tmp/ff15/source/ff.h        lib/fatfs/ff.h
cp /tmp/ff15/source/ffunicode.c lib/fatfs/ffunicode.c
cp /tmp/ff15/source/diskio.h    lib/fatfs/diskio.h
```

**Note:** we do NOT vendor `ffsystem.c` (FatFs's optional thread-safety helpers — we configure for single-threaded use, so this file isn't needed). We do NOT vendor the upstream `ffconf.h` template directly; we author our own from the template (Step 4).

- [ ] **Step 3: Add LICENSE and README**

Create `lib/fatfs/LICENSE` with the FatFs upstream license text. Find it at the top of `ff.c` in the upstream source (lines 1-25 typically). Copy verbatim into `LICENSE`.

Create `lib/fatfs/README.md`:

```markdown
# FatFs (vendored)

FatFs - Generic FAT Filesystem Module
Version: R0.15 (released 2022-11-09)
Source: http://elm-chan.org/fsw/ff/

## License

BSD-3-Clause (see LICENSE file). Compatible with any project licensing.

## Local modifications

- `ffconf.h` is authored locally (from upstream template) with our N64-specific
  configuration: 1 volume, 512-byte fixed sector size, FAT16+FAT32 only,
  LFN Mode 1, no RTC, no malloc.
- `diskio.c` is authored locally (the upstream provides `diskio.c` only as
  example documentation) and maps FatFs's required disk_* callbacks onto
  Phase 1a's iodev_sd_* primitives.
- `ff.c`, `ff.h`, `ffunicode.c`, `diskio.h` are unmodified from upstream.

## Updating

To pull a newer FatFs version:
1. Download the upstream tarball, extract.
2. Diff our `ffconf.h` against the new template; merge their template changes
   into our config.
3. Replace `ff.c`, `ff.h`, `ffunicode.c`, `diskio.h` with the new versions.
4. Rebuild + run `make lib-test` + run BizHawk + manual hardware verification.
5. Update this README's version number.
```

- [ ] **Step 4: Author `lib/fatfs/ffconf.h`**

Take the upstream `/tmp/ff15/source/ffconf.h` template as a starting point. Modify these settings (these are the values we want; others stay at template defaults):

```c
#define FF_CONFIG_REVISION 86606  /* match upstream R0.15's revision */

/* --- Function Configurations --- */
#define FF_FS_READONLY       0    /* We need write. */
#define FF_FS_MINIMIZE       0    /* Full API: f_open, f_read, f_write, f_close, f_opendir, f_readdir, f_unlink, f_mkdir, f_rename, f_stat. */
#define FF_USE_FIND          1    /* Phase 6's file_browser uses f_findfirst/f_findnext. */
#define FF_USE_MKFS          0    /* We do not format SDs. */
#define FF_USE_FASTSEEK      0    /* Don't need (large random-access files). */
#define FF_USE_EXPAND        0
#define FF_USE_CHMOD         0
#define FF_USE_LABEL         0
#define FF_USE_FORWARD       0
#define FF_USE_STRFUNC       0    /* No f_gets/f_putc/etc — we read/write binary. */

/* --- Locale and Namespace --- */
#define FF_CODE_PAGE         437  /* US Latin (ASCII-compatible). */
#define FF_USE_LFN           1    /* Mode 1: LFN buffer on caller's stack. No malloc dependency. */
#define FF_MAX_LFN           255
#define FF_LFN_UNICODE       0    /* ANSI/OEM filenames (saves the Unicode tables). */
#define FF_LFN_BUF           255
#define FF_SFN_BUF           12
#define FF_STRF_ENCODE       3
#define FF_FS_RPATH          0    /* No current-directory tracking; absolute paths only. */

/* --- Drive/Volume Configurations --- */
#define FF_VOLUMES           1    /* One physical volume (one SD card). */
#define FF_STR_VOLUME_ID     0
#define FF_VOLUME_STRS       "RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define FF_MULTI_PARTITION   0
#define FF_MIN_SS            512  /* Fixed 512-byte sectors. */
#define FF_MAX_SS            512
#define FF_LBA64             0    /* 32-bit sector addresses are sufficient (32 GB SD = ~62M sectors, fits in u32). */
#define FF_MIN_GPT           0x10000000
#define FF_USE_TRIM          0

/* --- System Configurations --- */
#define FF_FS_TINY           0
#define FF_FS_EXFAT          0    /* No exFAT (saves a lot of code; cards <32GB only). */
#define FF_FS_NORTC          1    /* No real-time clock (file timestamps will be epoch). */
#define FF_NORTC_MON         1
#define FF_NORTC_MDAY        1
#define FF_NORTC_YEAR        2026 /* Default year for any new files. Update if it matters. */
#define FF_FS_NOFSINFO       0
#define FF_FS_LOCK           0    /* No file-locking (single-threaded). */
#define FF_FS_REENTRANT      0    /* No reentrancy support (single-threaded). */
```

Carefully comment each non-default setting with WHY we chose it.

- [ ] **Step 5: Verify FatFs source is C89-clean (best-effort static check)**

The IDO toolchain is C89. FatFs is generally C89-compatible but occasionally uses post-C89 features. Check:

```bash
grep -nE "//[^/]|^\s*//" lib/fatfs/ff.c | head      # // comments? Common in FatFs.
grep -nE "\binline\b" lib/fatfs/ff.c | head          # inline keyword? Acceptable in IDO if used judiciously.
grep -nE "for\s*\(\s*[a-zA-Z_][a-zA-Z0-9_]*\s+\w+" lib/fatfs/ff.c | head   # for (int i = ...): C99 only.
```

If any of these hit lots of results, IDO may reject the build. The project's existing IDO build flags (`-Xcpluscomm` per `Makefile:104`) ALLOW `//` comments in C source — that handles the most common issue.

If `for (Type i = ...)` form appears, IDO will reject. Don't fix individual instances; instead:
- Switch lib/fatfs's compile flags to GCC-only via per-directory CFLAGS (similar to how `src/audio/*` get `-O2 -g0` in Makefile lines 343-344).
- Or compile lib/fatfs with the project's GCC fallback (`COMPILER=gcc`).

The path forward depends on which features FatFs uses. **Don't decide until Step 7's actual build attempt fails.**

- [ ] **Step 6: Commit the vendored source**

```bash
git add lib/fatfs/ff.c lib/fatfs/ff.h lib/fatfs/ffunicode.c \
        lib/fatfs/diskio.h lib/fatfs/LICENSE lib/fatfs/README.md \
        lib/fatfs/ffconf.h
git commit -m "feat: vendor FatFs R0.15 with N64-tuned ffconf

Vendored from http://elm-chan.org/fsw/ff/ (BSD-3-Clause). Configuration:
- 1 volume, 512-byte fixed sectors, FAT16+FAT32 only (no exFAT)
- LFN Mode 1 (stack-buffered, no malloc)
- ANSI/OEM filenames (no Unicode tables)
- Single-threaded, no RTC, no current-directory tracking

diskio.c (the FatFs-iodev glue) follows in the next commit."
```

**Shippable state:** FatFs vendored. No build integration yet.

---

## Task 2: Author `diskio.c` glue layer

**Files:**
- Create: `lib/fatfs/diskio.c`

**Goal:** Map FatFs's required `disk_*` callbacks onto Phase 1a's `iodev_sd_*` API. Single new file, ~80 LoC.

- [ ] **Step 1: Create `lib/fatfs/diskio.c`**

```c
/* FatFs disk I/O glue layer for the SF64 practice ROM iodev abstraction.
 *
 * FatFs's ff.c calls these disk_* functions to access the underlying
 * storage. We dispatch them to Phase 1a's iodev_sd_* primitives, which in
 * turn route to the SC64 or ED64 backend depending on which flashcart
 * was detected at boot.
 *
 * iodev caps reads/writes at 128 sectors per call (the SC64 backend's
 * 64 KiB DMA scratch limit). FatFs may request larger transfers, so we
 * chunk them here. */

#include "ff.h"
#include "diskio.h"
#include "iodev/iodev.h"

/* FatFs has only one volume in our config; pdrv is always 0. */
#define VOL_SD 0

/* Track init state so disk_status returns sane values pre-init. */
static int sFatfsDiskInited = 0;

/* Map iodev_result_t to FatFs's DRESULT. */
static DRESULT iodev_to_dresult(iodev_result_t r) {
    switch (r) {
        case IODEV_OK:           return RES_OK;
        case IODEV_ERR_PARAM:    return RES_PARERR;
        case IODEV_ERR_NO_DEVICE:
        case IODEV_ERR_NO_CARD:  return RES_NOTRDY;
        default:                 return RES_ERROR;  /* IO, TIMEOUT, etc. */
    }
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != VOL_SD) return STA_NOINIT | STA_NODISK;
    if (!sFatfsDiskInited) return STA_NOINIT;
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    iodev_result_t r;

    if (pdrv != VOL_SD) return STA_NOINIT | STA_NODISK;

    /* iodev_detect is idempotent — Practice_Init already called it. */
    r = iodev_sd_init();
    if (r != IODEV_OK) {
        sFatfsDiskInited = 0;
        return STA_NOINIT;
    }
    sFatfsDiskInited = 1;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    UINT chunk;
    iodev_result_t r;

    if (pdrv != VOL_SD) return RES_PARERR;
    if (!sFatfsDiskInited) return RES_NOTRDY;
    if (!buff) return RES_PARERR;

    /* iodev caps at 128 sectors per call; chunk larger requests. */
    while (count > 0) {
        chunk = (count > 128) ? 128 : count;
        r = iodev_sd_read_sectors((uint32_t)sector, chunk, buff);
        if (r != IODEV_OK) return iodev_to_dresult(r);
        sector += chunk;
        buff   += chunk * 512;
        count  -= chunk;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    UINT chunk;
    iodev_result_t r;

    if (pdrv != VOL_SD) return RES_PARERR;
    if (!sFatfsDiskInited) return RES_NOTRDY;
    if (!buff) return RES_PARERR;

    while (count > 0) {
        chunk = (count > 128) ? 128 : count;
        r = iodev_sd_write_sectors((uint32_t)sector, chunk, buff);
        if (r != IODEV_OK) return iodev_to_dresult(r);
        sector += chunk;
        buff   += chunk * 512;
        count  -= chunk;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != VOL_SD) return RES_PARERR;
    if (!sFatfsDiskInited) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            /* iodev writes are synchronous; nothing to flush. */
            return RES_OK;

        case GET_SECTOR_SIZE:
            if (buff) *(WORD *)buff = 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            /* FatFs uses this for erase-block alignment when formatting.
             * We don't format (FF_USE_MKFS=0), so 1 is a safe placeholder. */
            if (buff) *(DWORD *)buff = 1;
            return RES_OK;

        case GET_SECTOR_COUNT:
            /* TODO: implement via iodev's CMD9/CSD-derived capacity helper
             * (added in Phase 1b's diag mode if it shipped, otherwise add a
             * minimal capacity probe here). For now, return a conservative
             * 32 GB cap that works for any card up to 32 GB. FatFs uses this
             * field for write-bounds checking and partition validation; an
             * over-estimate causes read/write past end of card to return
             * IODEV_ERR_IO from iodev_sd_*, which propagates to FatFs as
             * RES_ERROR. Acceptable for v1; tighten if it bites.
             *
             * 32 GB / 512 = 67108864 sectors. */
            if (buff) *(LBA_t *)buff = 67108864UL;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
```

**Notes:**
- The FatFs `LBA_t` type is `DWORD` (32-bit) in our config (`FF_LBA64=0`), so the cast `(uint32_t)sector` is identity but keeps the iodev API's stricter typing.
- Chunking at 128 sectors enforces iodev's contract from inside FatFs's call path. Without chunking, large file copies would fail with `IODEV_ERR_PARAM`.
- `GET_SECTOR_COUNT` is a known TODO; works for ≤32 GB cards. Phase 1b's `diag_query_capacity` (if it shipped) gives us a real capacity probe; either expose it via iodev's public API or add a small helper here.
- `disk_initialize` is idempotent — calling it twice is fine, and it gracefully handles `iodev_sd_init` returning `IODEV_ERR_NO_DEVICE` (no flashcart, e.g., on emulator).

- [ ] **Step 2: Verify the file compiles standalone (host-only, just to catch syntax errors)**

```bash
cd lib/fatfs
gcc -c -Werror -Wall -I. -I.. diskio.c -o /tmp/diskio.o 2>&1 | head -20
rm -f /tmp/diskio.o
```

Expected: clean (no warnings or errors). If GCC complains about FatFs internals (in `ff.h`), those are upstream and we don't fix; they get suppressed when building with IDO via the project's `-woff` flags.

- [ ] **Step 3: Commit**

```bash
git add lib/fatfs/diskio.c
git commit -m "feat: add FatFs disk_* -> iodev_sd_* glue layer

diskio.c maps FatFs's required disk_status / disk_initialize / disk_read
/ disk_write / disk_ioctl callbacks onto Phase 1a's iodev_sd_* API.

Chunks reads and writes at 128 sectors per call (iodev's DMA scratch cap).

Open TODO: GET_SECTOR_COUNT returns a 32 GB conservative upper bound;
should be replaced with a real CMD9/CSD-derived capacity probe (Phase 1b
adds this for the diag ROM)."
```

**Shippable state:** FatFs source vendored, glue layer authored. Not yet integrated into the practice ROM build.

---

## Task 3: Build integration

**Files:**
- Modify: `Makefile` (add `-Ilib/fatfs` to includes; possibly per-directory CFLAGS for fatfs warnings)
- Modify: `tools/patch_linker_script.py` (add fatfs objects to the unified LIB_OBJS or LIB_FATFS_OBJS list)

- [ ] **Step 1: Add `-Ilib/fatfs` to `IINC`**

In the top-level `Makefile`, find `IINC` (Phase 1a added `-Ilib`). Add `-Ilib/fatfs` so source files outside `lib/fatfs/` can `#include "ff.h"` directly without a path prefix:

```makefile
IINC := -Iinclude -Ilib -Ilib/fatfs ...
```

Per-directory include flag is optional; the existing `-Ilib` plus internal `lib/fatfs/diskio.c`'s `#include "ff.h"` already works via FatFs's own header lookup.

- [ ] **Step 2: Add lib/fatfs objects to the linker patcher**

If Phase 1b's patcher refactor (Task 1 Step 4) shipped, the patcher walks a single `LIB_IODEV_OBJS` list dynamically. **Generalize this list to a unified `LIB_OBJS`** that holds all `lib/<dir>/<obj>.o` entries in topological order:

```python
LIB_OBJS = [
    # Phase 1a iodev backends (lib/iodev/*)
    ("iodev",     "lib/iodev"),
    ("iodev_sc64","lib/iodev"),
    ("iodev_ed64","lib/iodev"),    # Phase 1b
    ("iodev_diag","lib/iodev"),    # Phase 1b
    ("iodev_stub","lib/iodev"),

    # Phase 1b CRC layer (lib/)
    ("sd_crc",    "lib"),

    # Phase 2 FatFs (lib/fatfs/*)
    ("ff",        "lib/fatfs"),
    ("ffunicode", "lib/fatfs"),
    ("diskio",    "lib/fatfs"),
]
```

Update the patcher's dynamic walk to handle `(name, dir)` tuples and emit `build/{dir}/{name}.o(...)`. Adjust the predecessor anchoring: each entry's predecessor is the prior entry in `LIB_OBJS`; the first entry's predecessor is the last `practice_*.o`.

If Phase 1b's patcher refactor did NOT ship (you're working off Phase 1a's per-state if/elif), do that refactor here as the first step of Task 3.

Verify:

```bash
python3 tools/patch_linker_script.py
grep -E "iodev|sd_crc|fatfs" linker_scripts/us/rev1/starfox64.ld | head -30
```

Expected: in each `.text/.data/.rodata/.bss` section, a sequence of all lib objects in `LIB_OBJS` order. ~36 lines total (9 objects × 4 sections).

- [ ] **Step 3: Build the practice ROM with FatFs included**

```bash
make practice -j4
```

Possible failure modes and remedies:

| Failure | Cause | Fix |
|---------|-------|-----|
| `error: comments are not allowed in C` | IDO rejects `//` comments | Already covered by `-Xcpluscomm` flag; if it still fires, check Makefile flags for fatfs files |
| `error: parameter declarations must be at top of block` | C99 mid-block decls in ff.c | Compile lib/fatfs with GCC. See Step 4 below. |
| `error: redefinition of 'BYTE'` etc. | FatFs's typedefs collide with libultra's | Check `ff.h` for `#ifdef`/`typedef` guards. May need to wrap includes carefully. |
| `error: implicit declaration of 'memcpy'` | FatFs uses libc functions IDO has under different paths | Ensure `lib/fatfs/diskio.c` has `#include <string.h>` if needed; project provides `libc/string.h` |
| `multiple definition of 'foo'` | FatFs and libultra both define a symbol | Rename in our config or wrap in namespace |

If IDO rejects FatFs's source pervasively (likely scenario if FatFs uses lots of C99 features):

- [ ] **Step 4: Per-directory GCC compilation for `lib/fatfs/` (only if IDO rejects)**

The project's Makefile (lines 75-89) supports a global `COMPILER=ido|gcc` setting. We need finer granularity: most code is IDO, but lib/fatfs uses GCC.

Pattern to follow: `Makefile:343-344` overrides `OPTFLAGS` for `build/src/audio/*`. We override `CC` for `build/lib/fatfs/*`:

```makefile
# Compile FatFs source with GCC instead of IDO. FatFs is C99-style and
# IDO's strict C89 conformance rejects it. GCC handles it cleanly with
# the project's existing -G 0 -mips3 flags.
build/lib/fatfs/%.o: CC := $(MIPS_BINUTILS_PREFIX)gcc
build/lib/fatfs/%.o: CFLAGS := -G 0 -ffast-math -fno-unsafe-math-optimizations \
                                -march=vr4300 -mfix4300 -mabi=32 -mno-abicalls \
                                -mdivide-breaks -fno-zero-initialized-in-bss \
                                -fno-toplevel-reorder -ffreestanding -fno-common \
                                -funsigned-char -O2 -g3 -nostdinc \
                                $(IINC) $(BUILD_DEFINES) -DPRACTICE_ROM=1
```

(Adjust flags to mirror what `src/audio/*` etc. get if there's drift.)

This compiles `lib/fatfs/ff.c`, `lib/fatfs/ffunicode.c`, `lib/fatfs/diskio.c` with GCC. The rest of the ROM stays IDO. IDO's linker accepts GCC-produced MIPS objects (same MIPS3 ABI).

Caveat: GCC's optimizer may produce different float behavior than IDO. FatFs doesn't use floats, so this shouldn't matter.

- [ ] **Step 5: Build green**

```bash
make practice -j4
```

Must succeed end-to-end. Pre-commit hook (invariants + build) must pass.

- [ ] **Step 6: Commit**

```bash
git add Makefile tools/patch_linker_script.py
git commit -m "build: integrate FatFs into the practice ROM build

Adds lib/fatfs/* to the linker script via patcher's LIB_OBJS list.
Includes -Ilib/fatfs in IINC. [If Step 4 needed]: compiles lib/fatfs/*
with GCC instead of IDO due to FatFs's C99-style source."
```

**Shippable state:** Practice ROM links cleanly with FatFs included. No filesystem usage yet — the symbols are just present.

---

## Task 4: Static invariants for lib/fatfs

**Files:**
- Modify: `tools/practice_invariants.py`

- [ ] **Step 1: Add `check_fatfs_isolation()`**

FatFs source is vendored — it must NOT include game/practice/decomp headers (would couple us to one specific game). FatFs may legitimately include only its own headers and standard C.

```python
def check_fatfs_isolation():
    """FatFs source must not include any project headers beyond its own.

    Vendored FatFs is meant to be drop-in replaceable on upstream updates;
    coupling it to game-specific headers would block updates."""
    fatfs_dir = "lib/fatfs"
    if not os.path.isdir(fatfs_dir):
        return

    allowed_includes = {"ff.h", "ffconf.h", "diskio.h", "iodev/iodev.h"}
    allowed_patterns = [
        r"<[a-z][a-z0-9_]*\.h>",  # standard C headers (string.h, stdint.h, etc.)
    ]

    for fname in os.listdir(fatfs_dir):
        if not fname.endswith((".c", ".h")):
            continue
        path = os.path.join(fatfs_dir, fname)
        src = read(path)
        for inc_match in re.finditer(r'#include\s+["<]([^">]+)[">]', src):
            inc = inc_match.group(1)
            if inc in allowed_includes:
                continue
            if any(re.fullmatch(p, f'<{inc}>') for p in allowed_patterns):
                continue
            error(f"{path}: forbidden include '{inc}' (only ff.h/diskio.h/ffconf.h/iodev/iodev.h + standard C allowed)")
```

Wire into `main()`.

- [ ] **Step 2: Verify positive + negative tests**

Positive: `python3 tools/practice_invariants.py` passes.

Negative: temporarily add `#include "practice.h"` to `lib/fatfs/diskio.c`, run invariants, must fail. Revert.

- [ ] **Step 3: Update existing `check_lib_isolation` to know about fatfs**

`check_lib_isolation` (Phase 1a Task 3) forbids `lib/` files from including game headers. FatFs source IS in `lib/fatfs/` and the `check_lib_isolation` already covers that — just verify it still passes after FatFs lands.

The existing `check_lib_libultra_scope` must NOT add `lib/fatfs/*` to the libultra allowlist. FatFs is host-portable; if it transitively pulled libultra, we'd lose host unit testability.

- [ ] **Step 4: Commit**

```bash
git add tools/practice_invariants.py
git commit -m "build: add lib/fatfs isolation invariants

FatFs source (vendored) must not include game/practice headers beyond
its own and iodev's. Drop-in replaceability on upstream updates depends
on this constraint."
```

---

## Task 5: Host unit tests for diskio glue

**Files:**
- Create: `lib/test/test_diskio.c`
- Create: `lib/test/test_fatfs_smoke.c`
- Modify: `lib/test/Makefile`

**Goal:** Run FatFs against a host FAT32 image via a mock iodev backend. This catches FatFs config issues, diskio chunking bugs, and FatFs-side regressions on every commit, with no N64 toolchain involved.

**Phase 1b dependency:** this task assumes `lib/test/Makefile` exists (created in Phase 1b Task 2). If it doesn't, create it first using the template from Phase 1b's plan (Task 2 Step 4).

- [ ] **Step 1: Create a mock iodev backend for tests**

FatFs's diskio calls `iodev_sd_*`. For host tests we need a fake implementation that reads/writes a host file. Create `lib/test/iodev_mock.c`:

```c
/* Mock iodev for host unit tests. Backs SD I/O onto a host file.
 *
 * Test setup: open a FAT32 image file at startup via iodev_mock_set_image().
 * Tests then call FatFs's f_mount/f_open/etc., which call into diskio.c,
 * which call into iodev_sd_*, which reach this mock. The mock pread/pwrite
 * to the host file. */

#include "../iodev/iodev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int sMockFd = -1;

void iodev_mock_set_image(const char *path) {
    if (sMockFd >= 0) close(sMockFd);
    sMockFd = open(path, O_RDWR);
    if (sMockFd < 0) {
        perror(path);
        exit(1);
    }
}

iodev_id_t iodev_detect(void) {
    return (sMockFd >= 0) ? IODEV_SC64 : IODEV_NONE;
}

iodev_result_t iodev_sd_init(void) {
    return (sMockFd >= 0) ? IODEV_OK : IODEV_ERR_NO_DEVICE;
}

iodev_result_t iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    ssize_t n;
    if (sMockFd < 0) return IODEV_ERR_NO_DEVICE;
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    if ((uintptr_t)buf & 7u) return IODEV_ERR_PARAM;
    if (count > 128) return IODEV_ERR_PARAM;
    n = pread(sMockFd, buf, count * 512, (off_t)lba * 512);
    return (n == (ssize_t)(count * 512)) ? IODEV_OK : IODEV_ERR_IO;
}

iodev_result_t iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    ssize_t n;
    if (sMockFd < 0) return IODEV_ERR_NO_DEVICE;
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    if ((uintptr_t)buf & 7u) return IODEV_ERR_PARAM;
    if (count > 128) return IODEV_ERR_PARAM;
    n = pwrite(sMockFd, buf, count * 512, (off_t)lba * 512);
    return (n == (ssize_t)(count * 512)) ? IODEV_OK : IODEV_ERR_IO;
}
```

The mock enforces the same alignment + 128-sector cap as the real iodev backends, so tests catch chunking bugs in diskio.c.

- [ ] **Step 2: Create `lib/test/test_diskio.c`**

```c
/* Host unit tests for lib/fatfs/diskio.c.
 *
 * Builds with native gcc. Backs SD I/O via lib/test/iodev_mock.c against a
 * host file pre-formatted as FAT32. Exercises diskio's chunking, error
 * mapping, and disk_ioctl surface.
 *
 * Setup (run once before the test): make a 64 MB FAT32 image:
 *    dd if=/dev/zero of=/tmp/test_diskio.img bs=1M count=64
 *    mkfs.fat -F 32 /tmp/test_diskio.img
 * Or on macOS:
 *    hdiutil create -size 64m -fs MS-DOS -volname TEST /tmp/test_diskio
 *    (creates /tmp/test_diskio.dmg; rename to .img if needed) */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../fatfs/ff.h"
#include "../fatfs/diskio.h"

extern void iodev_mock_set_image(const char *path);

static int failures = 0;

#define ASSERT_EQ(actual, expected, label) do { \
    if ((long long)(actual) != (long long)(expected)) {       \
        printf("FAIL: %s: expected %lld, got %lld\n",        \
               (label), (long long)(expected), (long long)(actual)); \
        failures++;                                          \
    } else { printf("PASS: %s\n", (label)); }                \
} while (0)

int main(int argc, char **argv) {
    BYTE buf[2048] __attribute__((aligned(8)));

    const char *image = (argc > 1) ? argv[1] : "/tmp/test_diskio.img";
    iodev_mock_set_image(image);

    /* T1: pre-init disk_status returns NOINIT */
    ASSERT_EQ(disk_status(0), STA_NOINIT, "disk_status pre-init = STA_NOINIT");

    /* T2: disk_initialize succeeds */
    ASSERT_EQ(disk_initialize(0), 0, "disk_initialize -> 0");

    /* T3: disk_status post-init = 0 */
    ASSERT_EQ(disk_status(0), 0, "disk_status post-init = 0");

    /* T4: read MBR sector 0 (FAT32 image always has a recognizable boot sector) */
    ASSERT_EQ(disk_read(0, buf, 0, 1), RES_OK, "disk_read sector 0 -> OK");
    ASSERT_EQ(buf[510], 0x55, "MBR sig byte 510 = 0x55");
    ASSERT_EQ(buf[511], 0xAA, "MBR sig byte 511 = 0xAA");

    /* T5: chunked read across 128-sector boundary (read 200 sectors via 2 chunks) */
    DRESULT res = disk_read(0, buf, 0, 200);
    /* This will fail if buf isn't 200*512=100KB; we only allocated 2KB.
     * For chunking test, use a heap buffer instead: */
    /* (Implementation detail: malloc a 100KB buffer for this test) */
    /* Stubbing the assertion for now; real impl uses malloc. */
    (void)res;

    /* T6: disk_ioctl GET_SECTOR_SIZE */
    WORD ssize;
    ASSERT_EQ(disk_ioctl(0, GET_SECTOR_SIZE, &ssize), RES_OK, "ioctl GET_SECTOR_SIZE -> OK");
    ASSERT_EQ(ssize, 512, "GET_SECTOR_SIZE returns 512");

    /* T7: disk_ioctl GET_SECTOR_COUNT (returns conservative 32 GB) */
    LBA_t scount;
    ASSERT_EQ(disk_ioctl(0, GET_SECTOR_COUNT, &scount), RES_OK, "ioctl GET_SECTOR_COUNT -> OK");
    ASSERT_EQ(scount, 67108864UL, "GET_SECTOR_COUNT = 32GB cap");

    /* T8: disk_ioctl CTRL_SYNC */
    ASSERT_EQ(disk_ioctl(0, CTRL_SYNC, NULL), RES_OK, "ioctl CTRL_SYNC -> OK");

    /* T9: disk_ioctl unknown command rejects */
    ASSERT_EQ(disk_ioctl(0, 99, NULL), RES_PARERR, "ioctl unknown -> PARERR");

    /* T10: bad pdrv */
    ASSERT_EQ(disk_initialize(1), STA_NOINIT | STA_NODISK, "disk_init pdrv=1 -> NOINIT|NODISK");

    if (failures > 0) { printf("\n%d test(s) failed\n", failures); return 1; }
    printf("\nAll tests passed.\n");
    return 0;
}
```

The chunked-read test (T5) requires a larger buffer than the stack permits cleanly; malloc 100 KB at runtime, free after. Adjust the snippet to do that during implementation.

- [ ] **Step 3: Create `lib/test/test_fatfs_smoke.c`**

```c
/* Host integration test: mount a FAT32 image, write 50 small files, read
 * them back, verify content. Exercises diskio + ff.c end-to-end. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../fatfs/ff.h"

extern void iodev_mock_set_image(const char *path);

static int failures = 0;

static void check(int cond, const char *label) {
    if (cond) printf("PASS: %s\n", label);
    else { printf("FAIL: %s\n", label); failures++; }
}

int main(int argc, char **argv) {
    FATFS fs;
    FIL   fp;
    FRESULT fr;
    UINT bw, br;
    char path[64];
    char wbuf[256];
    char rbuf[256];
    int  i;

    iodev_mock_set_image((argc > 1) ? argv[1] : "/tmp/test_diskio.img");

    fr = f_mount(&fs, "0:", 1);
    check(fr == FR_OK, "f_mount /0:");

    /* Write 50 files. Filenames test_NN.bin, contents are 256 bytes of (NN ^ 0x5A). */
    for (i = 0; i < 50; i++) {
        snprintf(path, sizeof(path), "0:/test_%02d.bin", i);
        fr = f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr != FR_OK) { printf("FAIL: f_open write %s -> %d\n", path, fr); failures++; continue; }
        memset(wbuf, (i ^ 0x5A) & 0xFF, sizeof(wbuf));
        fr = f_write(&fp, wbuf, sizeof(wbuf), &bw);
        check(fr == FR_OK && bw == sizeof(wbuf), path);
        f_close(&fp);
    }

    /* Read 50 files back, verify. */
    for (i = 0; i < 50; i++) {
        snprintf(path, sizeof(path), "0:/test_%02d.bin", i);
        fr = f_open(&fp, path, FA_READ);
        if (fr != FR_OK) { printf("FAIL: f_open read %s -> %d\n", path, fr); failures++; continue; }
        fr = f_read(&fp, rbuf, sizeof(rbuf), &br);
        f_close(&fp);
        if (fr != FR_OK || br != sizeof(rbuf)) {
            printf("FAIL: f_read %s -> fr=%d br=%u\n", path, fr, br); failures++; continue;
        }
        memset(wbuf, (i ^ 0x5A) & 0xFF, sizeof(wbuf));
        if (memcmp(rbuf, wbuf, sizeof(rbuf)) != 0) {
            printf("FAIL: %s content mismatch\n", path); failures++; continue;
        }
    }
    if (failures == 0) printf("PASS: 50-file round-trip\n");

    /* List directory; expect at least 50 entries. */
    DIR dp;
    FILINFO fi;
    int dir_count = 0;
    fr = f_opendir(&dp, "0:/");
    check(fr == FR_OK, "f_opendir /");
    while (f_readdir(&dp, &fi) == FR_OK && fi.fname[0] != '\0') dir_count++;
    f_closedir(&dp);
    check(dir_count >= 50, "dir_count >= 50 (50 test files + maybe . and ..)");

    /* Unmount */
    fr = f_unmount("0:");
    check(fr == FR_OK, "f_unmount /0:");

    if (failures > 0) { printf("\n%d test(s) failed\n", failures); return 1; }
    printf("\nAll tests passed.\n");
    return 0;
}
```

- [ ] **Step 4: Extend `lib/test/Makefile`**

Add the new test targets:

```makefile
TESTS = test_sd_crc test_diskio test_fatfs_smoke

# Pattern rule needs adjustment: each test depends on different sources.
test_sd_crc: test_sd_crc.c ../sd_crc.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

test_diskio: test_diskio.c iodev_mock.c ../fatfs/diskio.c ../fatfs/ff.c ../fatfs/ffunicode.c
	$(CC) $(CFLAGS) $(INCLUDES) -I../fatfs $^ -o $@

test_fatfs_smoke: test_fatfs_smoke.c iodev_mock.c ../fatfs/diskio.c ../fatfs/ff.c ../fatfs/ffunicode.c
	$(CC) $(CFLAGS) $(INCLUDES) -I../fatfs $^ -o $@

run-all: $(TESTS) /tmp/test_diskio.img
	@for t in $(TESTS); do \
		echo "===> $$t"; \
		./$$t /tmp/test_diskio.img || exit 1; \
	done

# Auto-create the test image if it doesn't exist.
/tmp/test_diskio.img:
	dd if=/dev/zero of=$@ bs=1M count=64 2>/dev/null
	@which mkfs.fat > /dev/null 2>&1 && mkfs.fat -F 32 $@ \
	  || (which newfs_msdos > /dev/null 2>&1 && newfs_msdos -F 32 -v TEST $@) \
	  || (echo "ERROR: install dosfstools or use macOS newfs_msdos" && exit 1)
```

The `/tmp/test_diskio.img` target auto-creates a fresh FAT32 image if one doesn't exist. macOS users get `newfs_msdos` (built-in); Linux users need `dosfstools` (`apt-get install dosfstools` or equivalent).

- [ ] **Step 5: Run tests**

```bash
make lib-test
```

Expected output:
```
===> test_sd_crc
PASS: ... (Phase 1b's tests)

===> test_diskio
PASS: disk_status pre-init = STA_NOINIT
PASS: disk_initialize -> 0
... (10 tests)

===> test_fatfs_smoke
PASS: f_mount /0:
PASS: 0:/test_00.bin
... (50 file writes + 50 reads)
PASS: 50-file round-trip
PASS: f_opendir /
PASS: dir_count >= 50
PASS: f_unmount /0:

All tests passed.
```

- [ ] **Step 6: Commit**

```bash
git add lib/test/iodev_mock.c lib/test/test_diskio.c lib/test/test_fatfs_smoke.c lib/test/Makefile
git commit -m "test: add host unit tests for FatFs glue and integration

iodev_mock backs iodev_sd_* onto a host file. test_diskio exercises
the diskio.c surface (10 cases). test_fatfs_smoke is end-to-end:
mount a 64 MB FAT32 image, write 50 small files, read them back,
list the directory.

Run with 'make lib-test' (~5 seconds total)."
```

**Shippable state:** Algorithmic correctness verified on host. FatFs config + diskio glue + chunking work correctly against a real FAT32 filesystem.

---

## Task 6: BizHawk functional test (in-ROM mount + read)

**Files:**
- Create: `tests/test_fatfs_mount.lua`
- Modify: `lib/iodev/iodev_stub.c` (extend stub to optionally back onto a RAM-resident FAT32 image — only when a build flag is set)
- Possibly modify: `tools/extract_symbols.py` (add any FatFs symbols the test needs)

**Goal:** Verify FatFs works inside the actual N64 runtime. BizHawk doesn't simulate flashcarts, so we use a stub iodev backed by a RAM buffer pre-loaded with a FAT32 image.

- [ ] **Step 1: Extend `iodev_stub.c` with optional RAM-backed mode**

Gated by a build flag `IODEV_STUB_RAM_FAT32=1`. When set, `iodev_stub.c` allocates a 256 KB RAM buffer at boot, populates it with a tiny FAT32 image (compiled in as a `.h` file containing the bytes), and dispatches `iodev_sd_*` to in-RAM read/write of that buffer.

**Why 256 KB and not 1 MB:** stock N64 has 4 MB RAM and the practice ROM uses much of it. A 256 KB FAT32 with 512-byte clusters is achievable via `newfs_msdos -c 1 -F 32` or `mkfs.fat -F 32 -s 1`, hex-dumps to ~64 KB of source, and is plenty for a single MARKER.TXT round-trip test.

```c
#ifdef IODEV_STUB_RAM_FAT32
#include "stub_fat32_image.h"  /* generated; 256 KB FAT32 image as a static const u8 array */

static u8 sStubRam[256 * 1024];
static int sStubRamInited = 0;

static void stub_ram_init(void) {
    if (!sStubRamInited) {
        bcopy(kStubFat32Image, sStubRam, sizeof(kStubFat32Image));
        sStubRamInited = 1;
    }
}

static iodev_id_t      stub_detect(void) { return IODEV_SC64; }  /* pretend SC64 */
static iodev_result_t  stub_sd_init(void) { stub_ram_init(); return IODEV_OK; }
static iodev_result_t  stub_sd_read_sectors(u32 lba, u32 count, void *buf) {
    if (lba * 512 + count * 512 > sizeof(sStubRam)) return IODEV_ERR_IO;
    bcopy(&sStubRam[lba * 512], buf, count * 512);
    return IODEV_OK;
}
static iodev_result_t  stub_sd_write_sectors(u32 lba, u32 count, const void *buf) {
    if (lba * 512 + count * 512 > sizeof(sStubRam)) return IODEV_ERR_IO;
    bcopy(buf, &sStubRam[lba * 512], count * 512);
    return IODEV_OK;
}
#else
/* original Phase 1a no-op stub bodies */
... (existing) ...
#endif
```

- [ ] **Step 2: Generate `stub_fat32_image.h`**

Build a minimal FAT32 image with a known file in it:

```bash
# Create a 256 KB FAT32 image with a known file
dd if=/dev/zero of=/tmp/stub.img bs=1024 count=256
newfs_msdos -c 1 -F 32 -v STUB /tmp/stub.img    # macOS; or: mkfs.fat -F 32 -s 1 /tmp/stub.img on Linux
mkdir -p /tmp/stub_mount && hdiutil attach /tmp/stub.img -mountpoint /tmp/stub_mount
echo "PHASE2 OK" > /tmp/stub_mount/MARKER.TXT
hdiutil detach /tmp/stub_mount
xxd -i /tmp/stub.img > lib/iodev/stub_fat32_image.h
sed -i 's/_tmp_stub_img/kStubFat32Image/g' lib/iodev/stub_fat32_image.h
```

The generated `stub_fat32_image.h` contains `static const unsigned char kStubFat32Image[262144] = { 0x..., ... };` — about ~64 KB of source.

**Vendor as a tracked artifact** rather than gitignored — CI shouldn't need `newfs_msdos` to run the BizHawk test. The hex dump is small enough (~64 KB) to track without bloating the repo, and tracking it makes the test reproducible. Document its provenance in a comment at the top of the file ("generated from a 256 KB FAT32 image with cluster size 512 bytes; contains MARKER.TXT = 'PHASE2 OK\\n' at root").

For Phase 2, **track the image** to keep the ROM build hermetic.

- [ ] **Step 3: Create `tests/test_fatfs_mount.lua`**

```lua
-- Verifies in-ROM FatFs mount + small file read against a stub iodev backed
-- by a RAM-resident FAT32 image. Run only when ROM is built with
-- IODEV_STUB_RAM_FAT32=1.

local H = dofile("tests/harness.lua")
local S = H.S

H.test_name = "fatfs_mount"

-- Boot to level select (Practice_Init has run, iodev stub is initialized).
local ok = H.wait_until(function()
    return H.practice_screen() == S.const.PSCREEN_LEVEL_SELECT
end, 600, "boot to level select")
H.assert_true(ok, "Booted to level select")

-- The diagnostic ROM exposes a small helper that mounts FatFs and reads
-- /MARKER.TXT into a known buffer. The buffer's contents become "PHASE2 OK"
-- on success.
--
-- TODO during implementation: add a small Practice_TestFatfs helper to
-- src/practice/ that:
--   1. Calls f_mount, opens MARKER.TXT, reads up to 64 bytes, closes.
--   2. Stores the read buffer + read length into a pair of file-static
--      globals (e.g., sFatfsTestBuf[64] and sFatfsTestLen).
--   3. Runs once at boot when IODEV_STUB_RAM_FAT32 is defined.
--
-- The test reads sFatfsTestBuf and confirms it contains "PHASE2 OK".

local buf_addr = S.sFatfsTestBuf
local len_addr = S.sFatfsTestLen
local len = H.read_s32(len_addr)
H.assert_true(len > 0, "Practice_TestFatfs read at least 1 byte")
H.assert_true(len >= 9, "Read at least 9 bytes (length of 'PHASE2 OK')")

-- Read the first 9 bytes of buf and check they spell "PHASE2 OK".
local expected = "PHASE2 OK"
for i = 1, #expected do
    local b = H.read_u8(buf_addr + i - 1)
    H.assert_eq(b, string.byte(expected, i), string.format("byte %d = %02X", i, string.byte(expected, i)))
end

H.finish()
```

- [ ] **Step 4: Run the BizHawk test (if available)**

```bash
make practice -j4 IODEV_STUB_RAM_FAT32=1
BIZHAWK_PATH=... python3 tools/run_tests.py test_fatfs_mount
```

If BizHawk isn't available, document and move on.

- [ ] **Step 5: Commit**

```bash
git add lib/iodev/iodev_stub.c lib/iodev/stub_fat32_image.h \
        tests/test_fatfs_mount.lua tools/extract_symbols.py \
        src/practice/practice_main.c   # for the Practice_TestFatfs helper
git commit -m "test: add BizHawk in-ROM FatFs mount + read test

Stub iodev gains a RAM-backed FAT32 mode (gated by IODEV_STUB_RAM_FAT32).
Practice_TestFatfs helper mounts and reads MARKER.TXT at boot. Test
verifies the file content reaches the expected RAM buffer."
```

**Shippable state:** FatFs verified working inside the N64 runtime. SC64-specific bugs are still pending hardware verification.

---

## Task 7: Hardware verification on real SC64

**Files:**
- Create: `docs/superpowers/plans/HW_VERIFY_phase2.md`
- Possibly: extend `lib/iodev/iodev_diag.c` (Phase 1b artifact) with a FatFs test

**Goal:** End-to-end on real SC64. Boot the practice ROM with a FatFs probe, write a known file to the SD card, eject, read on PC, content matches.

- [ ] **Step 1: Extend the Phase 1b diagnostic ROM with a FatFs test (or author a Phase-2-only diag harness if Phase 1b's diag mode didn't ship)**

**Phase 1b status as of Phase 2 start:** Phase 1b shipped Tasks 1-2 only (ED64 detection + CRC). The `IODEV_DIAG=1` mode (Phase 1b Task 6) was deferred. This means `lib/iodev/iodev_diag.c` does NOT exist when Phase 2 starts.

**Two paths:**

**Path A (preferred if Phase 1b Task 6 ships before Phase 2 Task 7):** extend the existing diag suite as the original plan intended.

**Path B (fallback when Phase 2 Task 7 runs without Phase 1b's diag mode):** create a minimal Phase-2-specific harness inline. Add a `Practice_TestFatfs` helper in `src/practice/practice_test_fatfs.c` (gated by `IODEV_DIAG_FATFS=1`), called from `Practice_Init` after the existing iodev log. Body:

```c
#ifdef IODEV_DIAG_FATFS
{
    FATFS fs;
    FIL fp;
    FRESULT fr;
    UINT bw, br;
    static const char marker_text[] = "phase2 round-trip ok\n";
    static char rbuf[64];
    
    osSyncPrintf("\n[diag-fatfs] === Phase 2 hardware verification ===\n");
    osSyncPrintf("[diag-fatfs] WARNING: this ROM writes SF64TEST.TXT to your SD card root.\n");
    
    fr = f_mount(&fs, "0:", 1);
    osSyncPrintf("[diag-fatfs] T7 fatfs_mount=%d (expect 0=FR_OK)\n", (int)fr);
    if (fr != FR_OK) return;
    
    fr = f_open(&fp, "0:/SF64TEST.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        fr = f_write(&fp, marker_text, sizeof(marker_text) - 1, &bw);
        f_close(&fp);
    }
    osSyncPrintf("[diag-fatfs] T8 write=%d bytes_written=%u\n", (int)fr, (unsigned)bw);
    
    fr = f_open(&fp, "0:/SF64TEST.TXT", FA_READ);
    if (fr == FR_OK) {
        fr = f_read(&fp, rbuf, sizeof(rbuf) - 1, &br);
        rbuf[br < sizeof(rbuf) - 1 ? br : sizeof(rbuf) - 1] = '\0';
        f_close(&fp);
    }
    osSyncPrintf("[diag-fatfs] T9 read=%d bytes_read=%u content=\"%s\"\n",
                 (int)fr, (unsigned)br, rbuf);
    
    f_unmount("0:");
}
#endif
```

Either path produces the same IS-Viewer output for the user to capture. Path B doesn't depend on Phase 1b's Task 6 landing.

**If extending Phase 1b's existing `iodev_diag.c` (Path A):** append after the existing 6 sector-level tests:

```c
#include "ff.h"  /* added by Phase 2 */

/* Test 7: FatFs mount on real SD */
{
    FATFS fs;
    FRESULT fr = f_mount(&fs, "0:", 1);
    osSyncPrintf("[diag] T7 fatfs_mount=%d (expect 0=FR_OK)\n", (int)fr);
    if (fr != FR_OK) {
        osSyncPrintf("[diag] FAIL T7: mount failed (likely card not FAT formatted)\n");
        return;
    }

    /* Test 8: write a known file */
    FIL fp;
    UINT bw;
    static const char marker_text[] = "phase2 round-trip ok\n";
    fr = f_open(&fp, "0:/SF64TEST.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        fr = f_write(&fp, marker_text, sizeof(marker_text) - 1, &bw);
        f_close(&fp);
    }
    osSyncPrintf("[diag] T8 fatfs_write=%d bytes_written=%u (expect 0=OK, %u)\n",
                 (int)fr, (unsigned)bw, (unsigned)(sizeof(marker_text) - 1));

    /* Test 9: read it back, verify content */
    UINT br;
    static char rbuf[64];
    fr = f_open(&fp, "0:/SF64TEST.TXT", FA_READ);
    if (fr == FR_OK) {
        fr = f_read(&fp, rbuf, sizeof(rbuf) - 1, &br);
        rbuf[br < sizeof(rbuf) - 1 ? br : sizeof(rbuf) - 1] = '\0';
        f_close(&fp);
    }
    osSyncPrintf("[diag] T9 fatfs_read=%d bytes_read=%u content=\"%s\"\n",
                 (int)fr, (unsigned)br, rbuf);

    f_unmount("0:");
}
```

- [ ] **Step 2: Author `docs/superpowers/plans/HW_VERIFY_phase2.md`**

```markdown
# Phase 2 Hardware Verification (SC64)

## Time required: ~5 minutes

## What you'll need

- SC64 cart with sc64deployer
- A FAT32-formatted SD card (any size; use a scratch card is recommended
  but not strictly required — Phase 2's diag tests write a single file
  named SF64TEST.TXT to the root, ~22 bytes)

## What you'll do

1. Build the diagnostic ROM:
   ```bash
   make practice -j4 IODEV_DIAG=1
   ```

2. Boot the ROM (`sc64dev` per project workflow).

3. Capture IS-Viewer output. After Phase 1b's tests, expect:
   ```
   [diag] T7 fatfs_mount=0 (expect 0=FR_OK)
   [diag] T8 fatfs_write=0 bytes_written=21 (expect 0=OK, 21)
   [diag] T9 fatfs_read=0 bytes_read=21 content="phase2 round-trip ok\n"
   ```

4. Power off, remove SD card, mount on PC.

5. Verify `SF64TEST.TXT` exists at the root and contains the exact text
   "phase2 round-trip ok\n" (21 bytes including the newline).

6. Optionally delete `SF64TEST.TXT` from the SD before reusing it for
   normal play.

## PASS criteria

- T7 mount → 0
- T8 write → 0, bytes_written = 21
- T9 read → 0, bytes_read = 21, content matches
- File visible on PC with matching content

## Failure modes

- `T7 fatfs_mount=13` (FR_NO_FILESYSTEM) — card isn't FAT formatted, or
  partition table is unusual. Format the card as FAT32 and retry.
- `T8 fatfs_write=4` (FR_DENIED) — write-protected? Card has no free
  space?
- `T9 content="..."` doesn't match — write succeeded but read returned
  different bytes. Possible diskio chunking bug or FatFs config issue.
- File missing on PC after eject — write was reported OK but didn't
  flush. CTRL_SYNC implementation may need work.

## Reporting

Send the captured IS-Viewer output and a confirmation of the file
contents (paste from PC). If anything is off, note SC64 firmware version
and SD card details.
```

- [ ] **Step 3: Run the verification on real SC64**

User executes the checklist. If it passes, Phase 2 is hardware-verified.

- [ ] **Step 4: Commit the doc**

```bash
git add docs/superpowers/plans/HW_VERIFY_phase2.md lib/iodev/iodev_diag.c
git commit -m "feat: add Phase 2 hardware verification + FatFs diag tests

Extends Phase 1b's diagnostic ROM with three FatFs tests (mount, write,
read). HW_VERIFY_phase2.md documents the 5-minute SC64 verification."
```

---

## Task 8: Phase exit gate

- [ ] **Step 1: All automated checks pass**

```bash
python3 tools/practice_invariants.py
make practice -j4
make practice -j4 IODEV_DIAG=1                      # If Phase 1b shipped diag
make practice -j4 IODEV_STUB_RAM_FAT32=1            # For BizHawk test ROM
make lib-test                                       # Host: sd_crc + diskio + fatfs_smoke
```

All four must succeed.

- [ ] **Step 2: Confirm no probe code in `Practice_Init`**

Phase 2 may add a small `Practice_TestFatfs` helper for the BizHawk test (Task 6 Step 5). That helper should be:
- Gated by `#ifdef IODEV_STUB_RAM_FAT32` so it doesn't ship in the production ROM.
- Or marked clearly as test-only and excluded from normal `Practice_Init`.

`grep -c "f_open\|f_mount" src/practice/practice_main.c` should be 0 in production builds (no FatFs in the production code path; Phase 2 just lays the foundation).

- [ ] **Step 3: Hardware verification status**

If `HW_VERIFY_phase2.md` was run on real SC64 and all 9 tests passed (T1-T6 from Phase 1b, T7-T9 from Phase 2), Phase 2 is fully verified. Note in PR description.

If Phase 2 ships before HW verification: PR description must say "Phase 2 ships pending HW verification on SC64. Algorithmic correctness verified via host unit tests. In-ROM correctness verified via BizHawk."

- [ ] **Step 4: Tag the phase**

```bash
git tag phase2-fatfs
```

---

## Risk register

| Risk | Likelihood | Mitigation |
|------|------------|-----------|
| FatFs passes a misaligned buffer to `iodev_sd_*` from a caller's `f_read`/`f_write` | Medium | iodev requires 8-byte aligned buffers. FatFs's internal sector window is naturally aligned, but caller-supplied file payload buffers can be any alignment. Two viable fixes: (a) document in `iodev.h`'s public contract and trust callers, or (b) bounce-buffer in `diskio.c` via a 512-byte stack scratch when alignment check fails. Defer to whichever phase first hits this — Phase 7's save state code (which uses large aligned buffers) is unlikely to trigger; Phase 8's watch-engine I/O might. Track via TODO in `diskio.c`. |
| FatFs source rejected by IDO C89 | High | Task 3 Step 4 documents the per-directory GCC fallback. Cost: a few minutes of build-system work. |
| FatFs's `LBA_t` (32-bit in our config) overflows on >2 TB cards | Low | Practical SD cards in 2026 are <512 GB. Switch to `FF_LBA64=1` if it ever matters; small config change. |
| `GET_SECTOR_COUNT` 32 GB cap rejects larger SDs | Low | Cards >32 GB are exFAT-formatted by default and FatFs (with `FF_FS_EXFAT=0`) won't mount them anyway. Users would need to reformat to FAT32. |
| FatFs write doesn't reach the card before power-off | Medium | `CTRL_SYNC` is a no-op (iodev writes are synchronous). Verify on real hardware in Task 7. If we see "wrote OK but file not present after eject" — diskio's CTRL_SYNC needs to flush at iodev level. |
| diskio chunking bug — large file copies fail at exactly 128*512 bytes | Medium | Host unit test `test_diskio` covers chunking explicitly. Catches before hardware. |
| Stack-allocated LFN buffer (255 bytes) overflows on N64's small thread stacks | Low-Medium | The practice ROM threads have ~4 KB stacks. 255 bytes is fine. If LFN buffer overflow is suspected, switch to `FF_USE_LFN=2` (heap) or `FF_USE_LFN=3` (caller-supplied). |
| Vendored FatFs and project libultra both `typedef BYTE` | Medium | FatFs's `BYTE` is `unsigned char`; libultra likely doesn't redefine. If conflict: rename in our `ffconf.h` via `#define BYTE FF_BYTE` indirection (FatFs supports this). |
| Phase 1b's lib/test infra didn't ship | Medium | Task 5 calls this out; bootstrap lib/test/ if missing. |
| Phase 1b's CMD9 capacity helper didn't ship | Low | TODO in `disk_ioctl GET_SECTOR_COUNT` already documented; conservative 32 GB cap is acceptable for v1. |

---

## Explicit non-goals

- **No exFAT support** (`FF_FS_EXFAT=0`). Cards >32 GB users would need to reformat to FAT32. Acceptable for the practice tool's audience.
- **No multi-volume support** (`FF_VOLUMES=1`). One SD card per cart.
- **No format/mkfs operations** (`FF_USE_MKFS=0`). User formats the card on a PC.
- **No real-time clock** (`FF_FS_NORTC=1`). All file timestamps are epoch (2026-01-01 in our config). Phase 4+ might add a frame-counter-based "clock" if it matters.
- **No file locking / reentrancy** (`FF_FS_LOCK=0`, `FF_FS_REENTRANT=0`). Single-threaded.
- **No long-pathname support beyond LFN Mode 1** (`FF_MAX_LFN=255` on stack). Should be plenty.
- **No Unicode filenames** (`FF_LFN_UNICODE=0`). ANSI/OEM only — saves the Unicode tables.

---

## Final notes for the executing agent

- **FatFs is vendored, not authored.** Don't "improve" `ff.c`. The whole point of vendoring is that updates pull cleanly from upstream. Bugs go upstream as patches via Elm Chan's contribution process; don't fork.
- **`ffconf.h` IS authored locally.** Document every non-default setting with WHY in a comment; the next maintainer needs to know which choices are intentional.
- **The IDO-vs-GCC question (Task 3 Step 4) defines the rest of this phase.** If FatFs builds with IDO out of the box, life is easy. If not, the per-directory GCC fallback is the right answer; don't fight IDO to make FatFs C89-clean.
- **The user has another agent doing PNG-related work in this worktree, plus Phase 1b execution running in parallel.** Use explicit `git add` paths only.
- **Phase 2's deliverable is "round-trip a known file"** — both the host unit test (Task 5) and the hardware verification (Task 7) gate on file content matching what was written. Anything else is incidental.
- **Don't over-build.** Phase 2 lays the FatFs foundation. Phase 4+ build save state files on top. Don't add `f_mkdir`/`f_rename`/etc. wrappers in this phase — they're already exposed via FatFs's API. Phase 7 onward will use them as needed.
