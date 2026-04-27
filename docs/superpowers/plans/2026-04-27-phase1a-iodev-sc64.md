# Phase 1a: SC64 iodev Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the foundational `lib/iodev/` abstraction and a working SC64 backend that can read/write SD card sectors from the practice ROM.

**Architecture:** Top-level `lib/iodev/` directory with three files: `iodev.h` (public types/API), `iodev.c` (registry + detection), `iodev_sc64.c` (SC64 backend). Plus `iodev_stub.c` for emulator builds. Backends call into libultra PI; registry layer is portable. Static invariants enforce that nothing else under `lib/` includes libultra or game headers.

**Tech Stack:** C99, libultra PI primitives (`IO_READ`/`IO_WRITE`), MIPS GCC for ROM, native gcc for host unit tests. SC64 protocol cribbed from `~/code/SummerCart64/sw/bootloader/src/sc64.c`.

**Spec reference:** `docs/superpowers/specs/2026-04-27-gz-style-features-design.md` §"`lib/iodev/`" (component contracts).

---

## File Structure

**New files:**
- `lib/iodev/iodev.h` — public API: types (`iodev_id_t`, `iodev_result_t`), function decls.
- `lib/iodev/iodev.c` — registry; `iodev_detect()` calls per-backend probes; dispatches `iodev_sd_*` to the active backend.
- `lib/iodev/iodev_sc64.c` — SC64 protocol implementation.
- `lib/iodev/iodev_stub.c` — no-op backend; built when `IODEV_BACKEND=stub` (for emulator/CI).
- `lib/iodev/iodev_internal.h` — internal interface between registry and backends; not part of public API.
- `lib/test/Makefile` — host-side test runner (native gcc, not MIPS).
- `lib/test/test_iodev.c` — host unit test for registry layer.
- `lib/test/iodev_test.c` — mock backend over a host file (used by Phase 2+ tests too).
- `tests/test_iodev_detect.lua` — BizHawk functional test (verifies `iodev_detect` returns `IODEV_NONE` on emulator without flashcart sim).
- `docs/superpowers/plans/HW_VERIFY_phase1a.md` — manual hardware verification checklist.

**Modified files:**
- `Makefile` — add `lib/` to `SRC_DIRS`, add `-Ilib` to includes, add `lib-test` target.
- `tools/patch_linker_script.py` — add `lib/iodev/*` `.o` files to linker injection.
- `tools/practice_invariants.py` — add `check_lib_isolation` and `check_lib_libultra_scope` checks.
- `src/practice/practice_main.c` — call `iodev_detect()` and `iodev_sd_init()` in `Practice_Init`; log result via existing ISViewer.

**Not touched in this phase:** `iodev_ed64.c` (Phase 1b), `iodev_test.c` is created but its consumers come in Phase 2.

---

## Task 1: Public types & headers

**Files:**
- Create: `lib/iodev/iodev.h`

- [ ] **Step 1: Create `lib/iodev/iodev.h` with public types and function decls**

```c
#ifndef LIB_IODEV_H
#define LIB_IODEV_H

#include <stdint.h>

/* Identifies which flashcart (if any) was detected at boot. */
typedef enum {
    IODEV_NONE = 0,
    IODEV_SC64 = 1,
    IODEV_ED64 = 2,  /* Reserved for Phase 1b */
} iodev_id_t;

/* Result codes. IODEV_OK == 0; failures are negative. */
typedef enum {
    IODEV_OK            =  0,
    IODEV_ERR_NO_CARD   = -1,  /* No SD card in slot */
    IODEV_ERR_NO_DEVICE = -2,  /* No flashcart detected (IODEV_NONE) */
    IODEV_ERR_IO        = -3,  /* Hardware I/O error */
    IODEV_ERR_TIMEOUT   = -4,
    IODEV_ERR_PARAM     = -5,  /* Bad arguments */
} iodev_result_t;

/* Run-once hardware probe. Idempotent; result cached after first call. */
iodev_id_t iodev_detect(void);

/* Initializes the SD card on the active flashcart.
 * Must be called once after iodev_detect() returns non-NONE.
 * Returns IODEV_OK on success, negative error code otherwise. */
iodev_result_t iodev_sd_init(void);

/* Read `count` 512-byte sectors starting at `lba` into `buf`.
 * `buf` must be 8-byte aligned (DMA requirement) and `count * 512` bytes long. */
iodev_result_t iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf);

/* Write `count` 512-byte sectors starting at `lba` from `buf`.
 * Same alignment requirement. */
iodev_result_t iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf);

#endif /* LIB_IODEV_H */
```

- [ ] **Step 2: Build to confirm header is syntactically valid**

Run: `gcc -c -Ilib -x c lib/iodev/iodev.h -o /dev/null 2>&1`
Expected: no output (clean parse).

- [ ] **Step 3: Commit**

```bash
git add lib/iodev/iodev.h
git commit -m "feat: add lib/iodev public API header"
```

---

## Task 2: Build integration

**Files:**
- Modify: `Makefile` (around line 306, `SRC_DIRS`)
- Modify: `tools/patch_linker_script.py`

- [ ] **Step 1: Extend `SRC_DIRS` in Makefile to include `lib/`**

Locate line 306:
```makefile
SRC_DIRS      := $(shell find src -type d)
```

Replace with:
```makefile
SRC_DIRS      := $(shell find src -type d) $(shell find lib -type d 2>/dev/null)
```

The `2>/dev/null` covers the case where `lib/` doesn't exist yet (older branches).

- [ ] **Step 2: Add `-Ilib` to includes**

Find the `IINC` definition (search Makefile for `IINC`):
```bash
grep -n "^IINC" Makefile
```

Add `-Ilib` after the existing `-Iinclude` entry. If `IINC` is composed across multiple lines, append to the same group.

- [ ] **Step 3: Extend `tools/patch_linker_script.py` with lib objects**

Modify `tools/patch_linker_script.py`. Add a new constant after `PRACTICE_OBJS`:

```python
LIB_IODEV_OBJS = [
    "iodev",
    "iodev_sc64",
    "iodev_stub",
]
```

In the `patch()` function, change the injection loop from:

```python
injection = "\n".join(
    f"        build/src/practice/{obj}.o({section});"
    for obj in PRACTICE_OBJS
)
```

To:

```python
injection_practice = "\n".join(
    f"        build/src/practice/{obj}.o({section});"
    for obj in PRACTICE_OBJS
)
injection_iodev = "\n".join(
    f"        build/lib/iodev/{obj}.o({section});"
    for obj in LIB_IODEV_OBJS
)
injection = injection_practice + "\n" + injection_iodev
```

Update the "already patched" detection (line 31) to also check for `iodev`:

```python
if "practice_main" in content and "iodev.o" in content:
    print("Linker script already patched, skipping.")
    return
```

Also: if a previously-patched script has `practice_main` but lacks `iodev`, we want to re-patch. Replace the early-return logic with:

```python
if "iodev.o" in content:
    print("Linker script already patched (with lib/iodev), skipping.")
    return
if "practice_main" in content:
    # Patched by older version of this script; need to re-inject lib objects
    # Strip prior practice injection so we can re-add cleanly with lib/iodev.
    # (Alternatively: user runs `make extract` to regenerate the linker script.)
    print("ERROR: linker script has stale patch missing lib/iodev/. Run 'make extract' to regenerate.")
    sys.exit(1)
```

Add `import sys` at the top if not already present.

- [ ] **Step 4: Run `make extract` to regenerate the linker script** (one-time setup for this branch)

Per the project's CLAUDE.md, this is normally avoided. But since we're changing the linker script structure, we need a clean regeneration. **Confirm with the user before running** — they may prefer to manage this manually.

```bash
# Only if user approves
make extract
python3 tools/patch_linker_script.py
```

Expected: linker script now contains lines like `build/lib/iodev/iodev.o(.text);`.

If the user prefers manual editing: open `linker_scripts/us/rev1/starfox64.ld`, find the existing `practice_main` injections, and add the three `iodev*` `.o` entries to each of `.text`, `.data`, `.rodata`, `.bss` sections.

- [ ] **Step 5: Commit**

```bash
git add Makefile tools/patch_linker_script.py
git commit -m "build: add lib/ source discovery and iodev linker entries"
```

---

## Task 3: Static invariants

**Files:**
- Modify: `tools/practice_invariants.py`

- [ ] **Step 1: Add `check_lib_isolation` to invariants**

Append to `tools/practice_invariants.py` (before the `__main__` block):

```python
LIB_DIR = "lib"

# Headers/paths that lib/ code must NOT include (it must stay portable).
FORBIDDEN_LIB_INCLUDES = [
    "global.h",
    "practice.h",
    "variables.h",
    # Family patterns checked separately below.
]
FORBIDDEN_LIB_INCLUDE_PATTERNS = [
    r"sf64\w*\.h",   # sf64audio.h, sf64level.h, sf64thread.h, etc.
    r"fox_\w*\.h",   # fox_game.h, fox_play.h, etc.
    r"include/",     # any path-based include into project headers
]

def check_lib_isolation():
    """lib/ code must not include game/decomp headers."""
    if not os.path.isdir(LIB_DIR):
        return  # lib/ doesn't exist yet — nothing to check
    for root, _dirs, files in os.walk(LIB_DIR):
        for fname in files:
            if not fname.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fname)
            src = read(path)
            for inc in FORBIDDEN_LIB_INCLUDES:
                if re.search(rf'#include\s*[<"]{re.escape(inc)}[>"]', src):
                    error(f"{path}: lib/ may not include game header '{inc}'")
            for pat in FORBIDDEN_LIB_INCLUDE_PATTERNS:
                if re.search(rf'#include\s*[<"]{pat}[>"]', src):
                    error(f"{path}: lib/ may not include game header matching /{pat}/")
```

- [ ] **Step 2: Add `check_lib_libultra_scope` to invariants**

Append:

```python
# Files allowed to include libultra headers (PI/cart-bus access).
LIBULTRA_ALLOWED = [
    "lib/iodev/iodev_sc64.c",
    "lib/iodev/iodev_ed64.c",  # Phase 1b
]
LIBULTRA_INCLUDE_PATTERNS = [
    r"PR/[\w/]+\.h",
    r"ultra64\.h",
    r"libultra\.h",
]

def check_lib_libultra_scope():
    """lib/ files outside the iodev backends must build host-portable.

    Forbid libultra includes everywhere except the explicit allowlist,
    so unit tests can build with native gcc.
    """
    if not os.path.isdir(LIB_DIR):
        return
    for root, _dirs, files in os.walk(LIB_DIR):
        for fname in files:
            if not fname.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fname)
            if path in LIBULTRA_ALLOWED:
                continue
            src = read(path)
            for pat in LIBULTRA_INCLUDE_PATTERNS:
                if re.search(rf'#include\s*[<"]{pat}[>"]', src):
                    error(f"{path}: only iodev backends may include libultra (matched /{pat}/)")
```

- [ ] **Step 3: Wire the new checks into the `__main__` block**

Find the function-call list near the bottom of `practice_invariants.py` (where `check_config_inits()`, `check_function_definitions()` etc. are called). Add:

```python
    check_lib_isolation()
    check_lib_libultra_scope()
```

- [ ] **Step 4: Run invariants — should pass with no `lib/` files yet**

Run: `python3 tools/practice_invariants.py`
Expected: `Practice ROM invariant checks passed.` (lib/ checks no-op when dir missing or empty)

- [ ] **Step 5: Commit**

```bash
git add tools/practice_invariants.py
git commit -m "build: add lib/ isolation and libultra-scope invariants"
```

---

## Task 4: Stub backend (smallest possible build target)

**Files:**
- Create: `lib/iodev/iodev_internal.h`
- Create: `lib/iodev/iodev_stub.c`
- Create: `lib/iodev/iodev.c`

- [ ] **Step 1: Create `lib/iodev/iodev_internal.h` (registry↔backend interface)**

```c
#ifndef LIB_IODEV_INTERNAL_H
#define LIB_IODEV_INTERNAL_H

#include "iodev.h"

/* Per-backend function table. Backends supply one of these to the registry. */
typedef struct {
    iodev_id_t      id;
    iodev_id_t    (*detect)(void);
    iodev_result_t (*sd_init)(void);
    iodev_result_t (*sd_read_sectors)(uint32_t lba, uint32_t count, void *buf);
    iodev_result_t (*sd_write_sectors)(uint32_t lba, uint32_t count, const void *buf);
} iodev_backend_t;

/* Each backend exposes a single getter for its descriptor. */
const iodev_backend_t *iodev_backend_sc64(void);
const iodev_backend_t *iodev_backend_ed64(void);  /* Phase 1b */
const iodev_backend_t *iodev_backend_stub(void);

#endif /* LIB_IODEV_INTERNAL_H */
```

- [ ] **Step 2: Create `lib/iodev/iodev_stub.c`**

```c
#include "iodev.h"
#include "iodev_internal.h"

static iodev_id_t      stub_detect(void)                           { return IODEV_NONE; }
static iodev_result_t  stub_sd_init(void)                          { return IODEV_ERR_NO_DEVICE; }
static iodev_result_t  stub_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    (void)lba; (void)count; (void)buf;
    return IODEV_ERR_NO_DEVICE;
}
static iodev_result_t  stub_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    (void)lba; (void)count; (void)buf;
    return IODEV_ERR_NO_DEVICE;
}

static const iodev_backend_t STUB_BACKEND = {
    .id               = IODEV_NONE,
    .detect           = stub_detect,
    .sd_init          = stub_sd_init,
    .sd_read_sectors  = stub_sd_read_sectors,
    .sd_write_sectors = stub_sd_write_sectors,
};

const iodev_backend_t *iodev_backend_stub(void) { return &STUB_BACKEND; }
```

- [ ] **Step 3: Create `lib/iodev/iodev.c` (registry, SC64-only initial)**

```c
#include "iodev.h"
#include "iodev_internal.h"

/* Cached after first detection. NULL until iodev_detect() runs. */
static const iodev_backend_t *active = 0;

iodev_id_t iodev_detect(void) {
    if (active) {
        return active->id;
    }

    /* Probe order: SC64 first, then ED64 (Phase 1b), fallback to stub. */
    const iodev_backend_t *candidates[] = {
        iodev_backend_sc64(),
        /* iodev_backend_ed64(), */  /* Phase 1b */
    };
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (candidates[i]->detect() == candidates[i]->id) {
            active = candidates[i];
            return active->id;
        }
    }

    active = iodev_backend_stub();
    return IODEV_NONE;
}

iodev_result_t iodev_sd_init(void) {
    if (!active) iodev_detect();
    return active->sd_init();
}

iodev_result_t iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    if (!active) iodev_detect();
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    return active->sd_read_sectors(lba, count, buf);
}

iodev_result_t iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    if (!active) iodev_detect();
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    return active->sd_write_sectors(lba, count, buf);
}
```

- [ ] **Step 4: Build `make practice -j4` to confirm everything compiles**

Run: `make practice -j4`
Expected: clean build. The `iodev_backend_sc64()` symbol will be unresolved at link time — fine, we add it in Task 6.

If link fails on `iodev_backend_sc64`, that's expected at this stage. To confirm just the iodev objects compile, run:

```bash
mips-linux-gnu-gcc -c -Ilib -Iinclude -mabi=32 -DPRACTICE_ROM=1 lib/iodev/iodev.c -o /tmp/iodev.o
mips-linux-gnu-gcc -c -Ilib -Iinclude -mabi=32 -DPRACTICE_ROM=1 lib/iodev/iodev_stub.c -o /tmp/iodev_stub.o
```

Expected: clean object files.

- [ ] **Step 5: Run static invariants**

Run: `python3 tools/practice_invariants.py`
Expected: passes (`lib/iodev/*` includes only `<stdint.h>` and our own headers).

- [ ] **Step 6: Commit**

```bash
git add lib/iodev/iodev.h lib/iodev/iodev.c lib/iodev/iodev_stub.c lib/iodev/iodev_internal.h
git commit -m "feat: add iodev registry skeleton with stub backend"
```

---

## Task 5: SC64 backend — detection only

**Files:**
- Create: `lib/iodev/iodev_sc64.c`

- [ ] **Step 1: Create `lib/iodev/iodev_sc64.c` with detection**

Reference: `~/code/SummerCart64/sw/bootloader/src/sc64.c` lines 6-32 (register layout, identifier).

```c
/* SC64 flashcart backend.
 *
 * Protocol reference: ~/code/SummerCart64/sw/bootloader/src/sc64.c
 *
 * SC64 register block lives at cart-bus 0x1FFF0000. Commands are sent by
 * writing arguments to DATA[0..1], then writing the command byte to SCR.
 * The CPU_BUSY flag in SCR clears when the command completes; the response
 * (if any) is read back from DATA[0..1].
 *
 * Critical PI gotcha (same as isviewer.c): direct CPU writes to cart space
 * drop after the first few. A dummy IO_READ between writes drains the PI bus.
 */

#include "PR/rcp.h"
#include <stdint.h>
#include "iodev.h"
#include "iodev_internal.h"

#define SC64_REGS_BASE    0x1FFF0000UL
#define SC64_REG_SCR      (SC64_REGS_BASE + 0x00)
#define SC64_REG_DATA0    (SC64_REGS_BASE + 0x04)
#define SC64_REG_DATA1    (SC64_REGS_BASE + 0x08)
#define SC64_REG_IDENT    (SC64_REGS_BASE + 0x0C)
#define SC64_REG_KEY      (SC64_REGS_BASE + 0x10)

#define SC64_SCR_CPU_BUSY    (1u << 31)
#define SC64_SCR_CMD_ERROR   (1u << 30)

#define SC64_V2_IDENTIFIER   0x53437632u  /* "SCv2" */

#define SC64_KEY_RESET       0x00000000u
#define SC64_KEY_UNLOCK_1    0x5F554E4Cu
#define SC64_KEY_UNLOCK_2    0x4F434B5Fu

#define PI_WRITE_FLUSH(addr, val) do {            \
    IO_WRITE((addr), (val));                      \
    (void) IO_READ(SC64_REG_IDENT);               \
} while (0)

static iodev_id_t sc64_detect(void) {
    /* The SC64 unlocks register access after a magic key sequence. We probe
     * by reading IDENT — even before unlock, IDENT is readable. */
    uint32_t ident = IO_READ(SC64_REG_IDENT);
    if (ident == SC64_V2_IDENTIFIER) {
        /* Found SC64; unlock command interface. */
        PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_RESET);
        PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_UNLOCK_1);
        PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_UNLOCK_2);
        return IODEV_SC64;
    }
    return IODEV_NONE;
}

/* Stubs for now — implemented in Task 6. */
static iodev_result_t sc64_sd_init(void)                                                 { return IODEV_ERR_NO_DEVICE; }
static iodev_result_t sc64_sd_read_sectors(uint32_t lba, uint32_t count, void *buf)      { (void)lba;(void)count;(void)buf;return IODEV_ERR_NO_DEVICE; }
static iodev_result_t sc64_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf){(void)lba;(void)count;(void)buf;return IODEV_ERR_NO_DEVICE; }

static const iodev_backend_t SC64_BACKEND = {
    .id               = IODEV_SC64,
    .detect           = sc64_detect,
    .sd_init          = sc64_sd_init,
    .sd_read_sectors  = sc64_sd_read_sectors,
    .sd_write_sectors = sc64_sd_write_sectors,
};

const iodev_backend_t *iodev_backend_sc64(void) { return &SC64_BACKEND; }
```

- [ ] **Step 2: Wire SC64 backend into the registry's candidate list**

Edit `lib/iodev/iodev.c`. Uncomment / activate the SC64 entry in `candidates[]`:

```c
    const iodev_backend_t *candidates[] = {
        iodev_backend_sc64(),
        /* iodev_backend_ed64(), */  /* Phase 1b */
    };
```

(It's already there — confirm it's not commented out.)

- [ ] **Step 3: Build practice ROM**

Run: `make practice -j4`
Expected: clean build, no link errors.

- [ ] **Step 4: Run invariants**

Run: `python3 tools/practice_invariants.py`
Expected: passes. `iodev_sc64.c` is on the libultra-scope allowlist.

- [ ] **Step 5: Commit**

```bash
git add lib/iodev/iodev_sc64.c lib/iodev/iodev.c
git commit -m "feat: add SC64 iodev backend with detect()"
```

---

## Task 6: SC64 backend — command primitive + SD init/read/write

**Files:**
- Modify: `lib/iodev/iodev_sc64.c`

- [ ] **Step 1: Add command primitive (`sc64_execute_cmd`)**

Insert into `iodev_sc64.c` between the register defines and `sc64_detect`:

```c
/* SC64 command IDs (from ~/code/SummerCart64/docs/02_n64_commands.md). */
#define SC64_CMD_SD_CARD_OP     'i'
#define SC64_CMD_SD_SECTOR_SET  'I'
#define SC64_CMD_SD_READ        's'
#define SC64_CMD_SD_WRITE       'S'

/* SD_CARD_OP sub-operations. */
#define SD_OP_DEINIT          0
#define SD_OP_INIT            1
#define SD_OP_GET_STATUS      2
#define SD_OP_GET_INFO        3

/* PI cart-space targets for SD DMA buffers (FPGA SDRAM region). */
#define SC64_SDRAM_BUF_BASE   0x10000000u  /* Start of cart ROM space */
/* Use a 16 MB-aligned offset that doesn't overlap the running ROM. The
 * upper 64 KB of cart space (around the IS-Viewer at 0x13FF0000) is the
 * traditional scratch zone. We use 0x13FE0000 for SD DMA scratch — 64 KB
 * gives us 128 sectors of buffer space. */
#define SC64_SD_DMA_SCRATCH   0x13FE0000u

/* Execute one command. Args go in arg[0..1], response (if any) lands in rsp[0..1].
 * Returns IODEV_OK on success, IODEV_ERR_IO on CMD_ERROR, IODEV_ERR_TIMEOUT if
 * CPU_BUSY never clears.
 *
 * NOTE: this uses the polling path (no IRQ), matching the bootloader's
 * non-IRQ branch in sc64.c:108-113. */
static iodev_result_t sc64_execute_cmd(uint8_t cmd_id,
                                       uint32_t arg0, uint32_t arg1,
                                       uint32_t *rsp0_out, uint32_t *rsp1_out) {
    PI_WRITE_FLUSH(SC64_REG_DATA0, arg0);
    PI_WRITE_FLUSH(SC64_REG_DATA1, arg1);
    PI_WRITE_FLUSH(SC64_REG_SCR, (uint32_t)cmd_id);

    /* Spin until CPU_BUSY clears. ~100ms timeout at 60 MHz CPU is generous
     * (SD ops are typically <10ms but CMD_INIT can take longer). */
    int retries = 6000000;
    uint32_t sr;
    do {
        sr = IO_READ(SC64_REG_SCR);
        if (--retries <= 0) {
            return IODEV_ERR_TIMEOUT;
        }
    } while (sr & SC64_SCR_CPU_BUSY);

    if (sr & SC64_SCR_CMD_ERROR) {
        /* The error code is in DATA0; we don't translate it for now —
         * caller just gets IODEV_ERR_IO. Per-error logging via osSyncPrintf
         * can be added later if needed. */
        return IODEV_ERR_IO;
    }

    if (rsp0_out) *rsp0_out = IO_READ(SC64_REG_DATA0);
    if (rsp1_out) *rsp1_out = IO_READ(SC64_REG_DATA1);
    return IODEV_OK;
}
```

- [ ] **Step 2: Implement `sc64_sd_init`**

Replace the stub in `iodev_sc64.c`:

```c
static iodev_result_t sc64_sd_init(void) {
    return sc64_execute_cmd(SC64_CMD_SD_CARD_OP,
                            0,         /* arg0: pi_address (unused for INIT) */
                            SD_OP_INIT,
                            0, 0);
}
```

- [ ] **Step 3: Implement `sc64_sd_read_sectors`**

Replace the stub in `iodev_sc64.c`:

```c
/* DMA from cart-bus into RDRAM. We use the existing osPi machinery; for
 * Phase 1a we use the simpler blocking variant (osPiStartDma + waiting on
 * the message queue is overkill here — direct synchronous DMA is fine). */
extern void osWritebackDCache(void *vaddr, s32 nbytes);
extern void osInvalDCache(void *vaddr, s32 nbytes);
extern s32 osPiStartDma(OSIoMesg *mb, s32 priority, s32 direction,
                        u32 devAddr, void *vAddr, u32 nbytes,
                        OSMesgQueue *mq);

#include <ultra64.h>  /* OSIoMesg, OSMesgQueue, etc. */

static OSMesgQueue sc64_dma_mq;
static OSMesg      sc64_dma_msg_buf[1];
static int         sc64_dma_mq_inited = 0;

static void sc64_dma_setup(void) {
    if (!sc64_dma_mq_inited) {
        osCreateMesgQueue(&sc64_dma_mq, sc64_dma_msg_buf, 1);
        sc64_dma_mq_inited = 1;
    }
}

static iodev_result_t sc64_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    iodev_result_t res;
    OSIoMesg mb;
    OSMesg dummy;

    if (count > 128) {
        return IODEV_ERR_PARAM;  /* exceeds our DMA scratch buffer */
    }

    sc64_dma_setup();

    /* Tell SC64 where to start, then read sectors into FPGA SDRAM scratch. */
    res = sc64_execute_cmd(SC64_CMD_SD_SECTOR_SET, lba, 0, 0, 0);
    if (res != IODEV_OK) return res;

    res = sc64_execute_cmd(SC64_CMD_SD_READ, SC64_SD_DMA_SCRATCH, count, 0, 0);
    if (res != IODEV_OK) return res;

    /* DMA from cart-bus scratch into caller's RDRAM buffer. */
    osInvalDCache(buf, (s32)(count * 512));
    mb.hdr.pri = OS_MESG_PRI_NORMAL;
    mb.hdr.retQueue = &sc64_dma_mq;
    mb.dramAddr = buf;
    mb.devAddr = SC64_SD_DMA_SCRATCH;
    mb.size = count * 512;
    if (osPiStartDma(&mb, OS_MESG_PRI_NORMAL, OS_READ,
                     SC64_SD_DMA_SCRATCH, buf, count * 512,
                     &sc64_dma_mq) != 0) {
        return IODEV_ERR_IO;
    }
    osRecvMesg(&sc64_dma_mq, &dummy, OS_MESG_BLOCK);

    return IODEV_OK;
}
```

- [ ] **Step 4: Implement `sc64_sd_write_sectors`** (mirror of read)

```c
static iodev_result_t sc64_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    iodev_result_t res;
    OSIoMesg mb;
    OSMesg dummy;

    if (count > 128) {
        return IODEV_ERR_PARAM;
    }

    sc64_dma_setup();

    /* DMA caller's RDRAM buffer into FPGA SDRAM scratch. */
    osWritebackDCache((void *)buf, (s32)(count * 512));
    mb.hdr.pri = OS_MESG_PRI_NORMAL;
    mb.hdr.retQueue = &sc64_dma_mq;
    mb.dramAddr = (void *)buf;
    mb.devAddr = SC64_SD_DMA_SCRATCH;
    mb.size = count * 512;
    if (osPiStartDma(&mb, OS_MESG_PRI_NORMAL, OS_WRITE,
                     SC64_SD_DMA_SCRATCH, (void *)buf, count * 512,
                     &sc64_dma_mq) != 0) {
        return IODEV_ERR_IO;
    }
    osRecvMesg(&sc64_dma_mq, &dummy, OS_MESG_BLOCK);

    /* Now tell SC64 to flush scratch into the SD card. */
    res = sc64_execute_cmd(SC64_CMD_SD_SECTOR_SET, lba, 0, 0, 0);
    if (res != IODEV_OK) return res;

    res = sc64_execute_cmd(SC64_CMD_SD_WRITE, SC64_SD_DMA_SCRATCH, count, 0, 0);
    return res;
}
```

- [ ] **Step 5: Build practice ROM**

Run: `make practice -j4`
Expected: clean build.

- [ ] **Step 6: Run invariants**

Run: `python3 tools/practice_invariants.py`
Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add lib/iodev/iodev_sc64.c
git commit -m "feat: add SC64 SD init / read / write via cart-bus DMA"
```

---

## Task 7: Wire iodev into Practice_Init for IS-Viewer logging

**Files:**
- Modify: `src/practice/practice_main.c`
- Modify: `include/practice.h` (only if a new function decl is needed)

- [ ] **Step 1: Add `Practice_Iodev_Init` to log results**

Edit `src/practice/practice_main.c`. At the top with the other includes, add:

```c
#include "iodev/iodev.h"
```

(The `-Ilib` flag from Task 2 makes this work.)

In `Practice_Init` (function body), after the existing init code, add:

```c
{
    iodev_id_t cart = iodev_detect();
    iodev_result_t sd = iodev_sd_init();
    osSyncPrintf("[iodev] cart=%d sd_init=%d\n", (int)cart, (int)sd);
}
```

(`osSyncPrintf` works via the existing IS-Viewer module per `CLAUDE.md`. Use lowercase `[iodev]` to match the project's logging style.)

- [ ] **Step 2: Build practice ROM**

Run: `make practice -j4`
Expected: clean build.

- [ ] **Step 3: Run static invariants**

Run: `python3 tools/practice_invariants.py`
Expected: pass.

- [ ] **Step 4: Commit**

```bash
git add src/practice/practice_main.c
git commit -m "feat: log iodev detection and SD init in Practice_Init"
```

---

## Task 8: BizHawk functional test

**Files:**
- Create: `tests/test_iodev_detect.lua`

- [ ] **Step 1: Create `tests/test_iodev_detect.lua`**

```lua
-- Verifies iodev_detect() returns IODEV_NONE on the emulator (no cart sim).
-- Run with: python3 tools/run_tests.py test_iodev_detect

dofile("tests/harness.lua")
H.test_name = "iodev_detect_emulator"

-- Boot to a state where Practice_Init has run.
H.boot_to_practice()

-- Search RAM for the IS-Viewer log buffer; the line "[iodev] cart=0 sd_init=-2"
-- should be present (IODEV_NONE = 0, IODEV_ERR_NO_DEVICE = -2).
--
-- IS-Viewer buffer is at cart-bus 0x13FF0000 + 0x20 (ISV_BUFFER_OFF) but
-- on emulator without an IS-Viewer device the writes go nowhere. Instead,
-- check the iodev internal state by reading the iodev `active` symbol:

local active_addr = H.symbol("active")  -- in lib/iodev/iodev.c
if active_addr == nil then
    H.fail("symbol 'active' not in symbol table — extract_symbols.py needs lib/iodev/iodev.c")
end

-- 'active' is a pointer; if iodev_detect ran, it points to the stub backend.
-- The stub backend's `id` field is at offset 0 of the struct.
local backend_ptr = H.read_u32(active_addr)
H.assert_neq(backend_ptr, 0, "iodev_detect did not run — active pointer is NULL")

local backend_id = H.read_u32(backend_ptr)
H.assert_eq(backend_id, 0, "expected IODEV_NONE (0) on emulator")

H.finish()
```

- [ ] **Step 2: Add iodev symbols to `tools/extract_symbols.py`**

Find the symbol list in `tools/extract_symbols.py` (search for an existing entry like `gPlayer`). Add:

```python
SYMBOLS = [
    # ... existing entries ...
    "active",  # in lib/iodev/iodev.c — points to active backend descriptor
]
```

(Adapt to the actual structure of `extract_symbols.py` — it may use a different format.)

- [ ] **Step 3: Run the BizHawk test (if BizHawk available)**

Run: `BIZHAWK_PATH=... python3 tools/run_tests.py test_iodev_detect`
Expected: PASS.

If BizHawk is not available locally, document the test for CI and move on. The pre-commit hook gracefully skips functional tests when BizHawk is absent.

- [ ] **Step 4: Commit**

```bash
git add tests/test_iodev_detect.lua tools/extract_symbols.py
git commit -m "test: add BizHawk functional test for iodev detect"
```

---

## Task 9: Hardware verification checklist

**Files:**
- Create: `docs/superpowers/plans/HW_VERIFY_phase1a.md`

- [ ] **Step 1: Create the hardware verification document**

```markdown
# Phase 1a Hardware Verification

Manual checklist for verifying the SC64 iodev backend on real hardware.
Run AFTER the automated tests pass.

## Setup

Per `CLAUDE.md`'s `Debug printf over SC64 IS-Viewer 64` workflow:

1. Terminal A: `sc64deployer debug --isv 0x03FF0000`
2. Terminal B: `sc64dev` (alias for build + upload — see `~/.zshrc`)
3. Press the physical N64 reset button after upload.

## Test 1: Cart detection

**Expected output in Terminal A:** `[iodev] cart=1 sd_init=...`

- `cart=1` confirms `IODEV_SC64` was detected.
- `sd_init=0` (IODEV_OK) confirms the SD card initialized.
- `sd_init=-1` (IODEV_ERR_NO_CARD) means card slot is empty — insert a card and reboot.
- `sd_init=-3` (IODEV_ERR_IO) means SC64 firmware reported an SD error — check card formatting.

PASS: `cart=1` and `sd_init=0`.

## Test 2: Sector 0 read

This requires a one-off probe routine added to `Practice_Init` temporarily.
After the existing iodev log, append:

```c
{
    static u8 sec0[512] __attribute__((aligned(8)));
    iodev_result_t r = iodev_sd_read_sectors(0, 1, sec0);
    osSyncPrintf("[iodev] read sec0 res=%d  bytes 0..15: ", (int)r);
    for (int i = 0; i < 16; i++) {
        osSyncPrintf("%02X ", sec0[i]);
    }
    osSyncPrintf("\n");
}
```

Build, upload, reset.

**Expected output in Terminal A:** `[iodev] read sec0 res=0  bytes 0..15: <16 hex bytes>`

Compare against host:
```bash
sudo dd if=/dev/diskN bs=512 count=1 status=none | xxd -l 16
```

PASS: `res=0` and the 16-byte prefix matches the host's `dd` output.

## Test 3: Sector write round-trip

Use a dedicated test sector (NOT sector 0 — that's the MBR; corrupting it
makes the SD unbootable until reformatted). Pick a sector well past any
filesystem use, e.g., LBA 0x100000 (~512 MB into the card).

Append to the probe:

```c
{
    static u8 wbuf[512] __attribute__((aligned(8)));
    static u8 rbuf[512] __attribute__((aligned(8)));
    for (int i = 0; i < 512; i++) wbuf[i] = (u8)(i ^ 0x5A);
    iodev_result_t wr = iodev_sd_write_sectors(0x100000, 1, wbuf);
    iodev_result_t rd = iodev_sd_read_sectors(0x100000, 1, rbuf);
    int match = 1;
    for (int i = 0; i < 512; i++) if (wbuf[i] != rbuf[i]) { match = 0; break; }
    osSyncPrintf("[iodev] roundtrip wr=%d rd=%d match=%d\n", (int)wr, (int)rd, match);
}
```

Build, upload, reset.

**Expected output:** `[iodev] roundtrip wr=0 rd=0 match=1`

PASS: all three values are correct.

## Cleanup

After verification, **remove the probe code from `Practice_Init`** before committing further work.

## Reporting

Note in the PR or follow-up commit:
- SC64 firmware version (run `sc64deployer info`).
- SD card size + class (e.g., `SanDisk 64GB Class 10`).
- Any anomalies — first-time SD init delays, intermittent CMD_ERROR, etc.
```

- [ ] **Step 2: Commit the verification doc**

```bash
git add docs/superpowers/plans/HW_VERIFY_phase1a.md
git commit -m "docs: add Phase 1a hardware verification checklist"
```

- [ ] **Step 3: Run the checklist on real SC64 hardware**

Follow the doc. If any test fails, debug before proceeding to Phase 1b. Document findings.

---

## Task 10: Phase exit gate

- [ ] **Step 1: All automated checks pass**

```bash
python3 tools/practice_invariants.py
make practice -j4
```

Both must succeed.

- [ ] **Step 2: Hardware verification complete**

`HW_VERIFY_phase1a.md` checklist run on real SC64. All three tests pass.

- [ ] **Step 3: Probe code removed from `Practice_Init`**

Verify `git diff` shows no stray test/probe code in `practice_main.c`. Only the persistent `[iodev] cart=... sd_init=...` log line should remain.

- [ ] **Step 4: Tag the phase**

```bash
git tag phase1a-iodev-sc64
```

(Optional — for marking completion in git history.)

- [ ] **Step 5: Phase 1a is done; ready to start Phase 1b plan**

The Phase 1b plan adds the EverDrive 64 backend. Phase 1b is **time-boxed at 3-5 days** per the spec; if blocked, Phase 1a's deliverable (SC64-only) is still shippable.

---

## Risk notes for the executing agent

- **DO NOT run `make clean` or `make init`** — see `CLAUDE.md`'s top warning. Asset regeneration takes 10+ minutes.
- **`make extract` regenerates the linker script.** If you need to re-extract (Task 2 Step 4), confirm with the user first.
- **The SC64 protocol gotchas in `CLAUDE.md`** (IS-Viewer section, "Hard-won SC64 protocol gotchas") apply to iodev too: every cart-bus write needs a follow-up `IO_READ` to drain the PI bus. The `PI_WRITE_FLUSH` macro in `iodev_sc64.c` enforces this.
- **DMA buffer alignment**: `iodev_sd_read_sectors` and `iodev_sd_write_sectors` require 8-byte aligned buffers. Document this in the public API and add an assertion in debug builds if useful.
- **The 64 KB DMA scratch at 0x13FE0000** is below the IS-Viewer's 0x13FF0000. We're using both — confirm no overlap. If they conflict, move the scratch to 0x13F00000.
- **Save state slot count** is not yet a concern in Phase 1a. Heap audit is Phase 4.
- **GPL-2 caveat**: gz's iodev backends are at `~/code/gz/src/gz/ed64_*.c`. **Read for understanding; do not copy code.** SC64 backend is original work guided by the SC64 docs at `~/code/SummerCart64/docs/`.
