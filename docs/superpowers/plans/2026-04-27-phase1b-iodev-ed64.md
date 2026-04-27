# Phase 1b: ED64 X7/X8 iodev Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an EverDrive 64 X7/X8 backend to the existing `lib/iodev/` abstraction so users on Krikzz hardware get the same SD card I/O surface that SC64 users got in Phase 1a — with enough confidence in the result that a hardware-equipped contributor can verify it in 10 minutes.

**Architecture:** Single new file `lib/iodev/iodev_ed64.c` implementing the ED64 X protocol against the FPGA registers documented in Krikzz's public hardware spec. The file plugs into the existing registry pattern from Phase 1a (`iodev_backend_t` descriptor + getter). Unlike SC64's high-level `SD_READ`/`SD_WRITE` commands, ED64 X exposes raw SDIO bus primitives — the host drives the SD card protocol directly (CMD0, CMD8, ACMD41, CMD2/3/7, CMD17/24, CRC7/CRC16). A new `lib/sd_crc.{c,h}` extracts the SD-spec CRC code so it's host-portable and unit-testable. A new `lib/iodev/iodev_diag.c` (gated by `IODEV_DIAG=1`) ships a turnkey hardware-verification ROM.

**Tech Stack:** C (IDO C89), libultra PI primitives, ED64 X FPGA register protocol, SD Physical Layer Specification v3.0+. Host gcc for unit tests on `sd_crc`.

**Spec reference:** `docs/superpowers/specs/2026-04-27-gz-style-features-design.md` lines 575-580 (Phase 1b deliverable).

**Time-box:** **3-5 days.** If blocked, the project ships SC64-only — Phase 2+ proceed regardless. Phase 1a's iodev abstraction was specifically designed so this fallback is clean.

---

## License & clean-room constraints

The reference implementations available are:

- `~/code/gz/src/gz/ed64_x.c` — **GPL-2**. May be **studied for protocol understanding only**. Do NOT copy code structure, function organization, variable names, or expressions. Read it the way you'd read a paper: extract the *facts* (FPGA register addresses, command sequences, timing requirements), discard the *expression*.
- `~/code/gz/src/gz/ed64_x.h` — **GPL-2** but most of its contents are protocol facts (register addresses, status bit positions) sourced from Krikzz's public hardware documentation. Re-deriving these from the public Krikzz wiki / [ED64-IO library](https://github.com/krikzz/ED64) (which has more permissive terms) is preferred when uncertain. Constants/addresses themselves are not copyrightable; only the file's expression is.
- SD Physical Layer Specification — public standard. CMD0, CMD2, CMD3, CMD7, CMD8, CMD16, CMD17, CMD24, CMD55, ACMD41, ACMD51, etc. are spec facts. CRC7 and CRC16 polynomial / algorithm are spec facts. Use freely.

**The implementer must NOT have `~/code/gz/src/gz/ed64_x.c` open while writing `iodev_ed64.c`.** Read it once, take protocol notes (register map, init sequence), close it, write fresh code from notes.

If this constraint feels arbitrary: it isn't. GPL contamination at the lib/ layer would force the entire practice ROM project to be GPL'd (it isn't currently). The whole point of `lib/`'s portability story is that it can lift into any project regardless of license.

---

## Hardware constraint

**The user does NOT have an EverDrive 64 X7/X8.** They have an EverDrive-equipped contact whose time is valuable, so the goal of this phase is to ship code that's been **algorithmically verified on the developer's machine**, with a **turnkey diagnostic ROM** the contact runs once to validate the wire-level path.

This shapes the plan in two ways:

1. **Pure-logic code (CRC7, CRC16) gets host unit tests.** No N64 needed; runs in seconds with `make lib-test`. Catches every CRC bug before the ROM ever touches hardware. Phase 2's host-unit-test infrastructure is pulled forward to Phase 1b for this purpose.
2. **A dedicated diagnostic build mode (`IODEV_DIAG=1`)** auto-runs the entire HW verification suite on boot and dumps a structured pass/fail log via IS-Viewer (or UNFLoader for ED64). Contact's workflow: flash diag ROM, run capture tool, paste output. ~10 minutes total.

What automated tests catch (without hardware):
- Build/link cleanliness
- Static invariants (lib isolation, libultra scope, ED64-specific invariants)
- BizHawk stub mode (registry polymorphism — calling `iodev_detect()` doesn't crash with the ED64 backend wired in, returns `IODEV_NONE` since no flashcart simulated)
- Host unit tests on `sd_crc.c` (CRC7 + CRC16-CCITT against SD spec test vectors)

What's still unproven without hardware: ED64 register-access timing, FPGA shift-register quirks, SDIO line state machines, real card initialization timings. The diagnostic ROM closes those gaps in a single 10-minute test session.

---

## File Structure

**New files:**
- `lib/sd_crc.h`, `lib/sd_crc.c` — host-portable SD-spec CRC implementations (~80 LoC). CRC7 (commands) and CRC16-CCITT (data blocks). No libultra dependency. Reusable from Phase 2's FatFs glue if needed.
- `lib/iodev/iodev_ed64.c` — ED64 X7/X8 protocol implementation (single file, realistically ~450-550 LoC: register macros + PI_WRITE_FLUSH + cart unlock + detect + ~8 SDIO primitives + 6-step init + read_block + write_block + descriptor table; CRC code lives in `sd_crc.c` not here).
- `lib/iodev/iodev_diag.c` — diagnostic-mode entry point (~150 LoC). Empty translation unit unless `IODEV_DIAG` is defined. When defined: hooks into `Practice_Init` and runs the full verification suite, logging pass/fail per step.
- `lib/test/Makefile` — host gcc test runner. Single target `lib-test` builds and runs all host unit tests.
- `lib/test/test_sd_crc.c` — unit tests for CRC7 and CRC16 against SD spec vectors.
- `docs/superpowers/plans/HW_VERIFY_phase1b.md` — manual hardware verification checklist (run by the EverDrive-equipped contributor; mostly automated via the diagnostic ROM).

**Modified files:**
- `lib/iodev/iodev.c` — bump `candidates[]` array from `[1]` to `[2]`, add `candidates[1] = iodev_backend_ed64();`.
- `tools/patch_linker_script.py` — replace the per-state `if/elif` ladder with a single "compute missing entries from the expected `LIB_IODEV_OBJS` list, inject each anchored on its predecessor" pass. Adds `iodev_ed64` and `iodev_diag` to the list. See Task 1 Step 4 for the refactor.
- `tools/practice_invariants.py` — add `check_iodev_ed64()` analogous to `check_iodev_sc64()`. `lib/iodev/iodev_ed64.c` is already in `LIBULTRA_ALLOWED` (Phase 1a). `lib/iodev/iodev_diag.c` joins the allowlist (it's the first lib/ file to log via `osSyncPrintf`). `lib/sd_crc.{c,h}` stay off the libultra allowlist (they're host-portable).
- `Makefile` — add `lib-test` target. **Crucial:** also exclude `lib/test/` from the practice ROM's `SRC_DIRS` so host-only test files don't get compiled with IDO. See Task 2 Step 4.

**Not touched:**
- `lib/iodev/iodev.h` — public API unchanged.
- `lib/iodev/iodev_internal.h` — `iodev_backend_ed64()` was declared during Phase 1a as a placeholder.
- `lib/iodev/iodev_sc64.c`, `iodev_stub.c`, `iodev_internal.h`.
- `lib/lib_types.h`.
- `tests/test_iodev_detect.lua` — unchanged; on emulator both backends return `IODEV_NONE`, stub still wins.

---

## Shippable checkpoints

Each task produces a buildable, committable state. **If the time-box expires, the user can stop at any of these points:**

- **After Task 1:** ED64 cart detection works (no SD ops). On real ED64 hardware, `iodev_detect()` would return `IODEV_ED64`; SD ops return `IODEV_ERR_NO_DEVICE`. SC64 users entirely unaffected.
- **After Task 2:** CRC7 and CRC16 are implemented and unit-tested on host. Pure logic correctness proven without hardware.
- **After Task 3:** ED64 SD initialization works. Card reaches "transfer state" but no read/write yet.
- **After Task 4:** ED64 single-block SD reads work.
- **After Task 5:** ED64 single-block SD writes work.
- **After Task 6:** Diagnostic ROM (`make practice IODEV_DIAG=1`) auto-runs the verification suite. **This is the milestone we ship to the EverDrive contact.**
- **After Task 7:** Static invariants and HW verification checklist landed.
- **After Task 8:** Phase exit gate. Phase 1b complete pending HW verification report.

The minimum to send to the EverDrive contact is **after Task 6**. Tasks 1-3 alone would only let them confirm "cart detected" — not useful enough to justify their time. Tasks 1-6 give them a real verification run.

Multi-block transfers (CMD18/CMD25) — the previous Task 5 — are **dropped from this phase**. Single-block R/W works; the perf delta isn't worth the additional CRC-streaming and timing complexity that's exactly what fails subtly on real hardware.

---

## Task 1: ED64 X register access + cart detection

**Files:**
- Create: `lib/iodev/iodev_ed64.c`
- Modify: `tools/patch_linker_script.py` (add `iodev_ed64` to `LIB_IODEV_OBJS`)
- Modify: `lib/iodev/iodev.c` (bump candidates array size and add `iodev_backend_ed64()`)

**Goal:** ED64 X carts are detected; SD ops are stubs returning `IODEV_ERR_NO_DEVICE`. Buildable and committable on its own.

- [ ] **Step 1: Take ED64 protocol notes (read for understanding only)**

Spend 30-60 minutes reading `~/code/gz/src/gz/ed64_x.c` and `~/code/gz/src/gz/ed64_x.h` to extract the protocol facts. **Take notes in a scratch file** (NOT in this repo) covering:

- `REG_BASE = 0xBF800000` and the offsets to each register (`REG_SYS_CFG`, `REG_KEY`, `REG_EDID`, `REG_SD_CMD_RD/WR`, `REG_SD_DAT_RD/WR`, `REG_SD_STATUS`).
- Cart-lock dance: ED64 X registers are inaccessible until the cart is unlocked via `REG_KEY` writes (sequence: `0xAA55`, `0x55AA`).
- **Detection (hardware fact):** after unlock, read `REG_EDID` (a 32-bit register). The upper 16 bits of the result equal the literal value `0xED64` for any genuine EverDrive 64 X cart (X7 and X8 share this magic). The lower 16 bits encode model/firmware revision and can be ignored for detection. Pseudocode: `if (((reg_edid_value >> 16) & 0xFFFF) == 0xED64u) return IODEV_ED64;` else return `IODEV_NONE`. **Do not invent an "upper byte" check or look for separate X7/X8 magic** — they share the cart-class identifier.
- Cart-bus access pattern: PI register reads/writes via `IO_READ`/`IO_WRITE` (same as SC64).

After taking notes, **close the gz files**. Write the implementation from notes only. The notes are facts; reproduce facts. Don't reproduce code structure or variable names from gz.

- [ ] **Step 2: Create `lib/iodev/iodev_ed64.c` skeleton with detection only**

Match the structure of `lib/iodev/iodev_sc64.c` (Phase 1a). Key elements:

- File header comment block referencing the ED64 protocol source (Krikzz hardware spec) and noting the clean-room provenance ("not derived from gz; written from public protocol notes").
- Includes: `"PR/rcp.h"`, `"libultra/ultra64.h"` (libultra-allowlisted), `"iodev.h"`, `"iodev_internal.h"`.
- Register address `#define`s using the names you extracted (use a `ED64_` prefix to namespace them clearly).
- A `PI_WRITE_FLUSH(addr, val)` macro analogous to SC64's, with a dummy follow-up `IO_READ` to drain the PI bus.
- Cart unlock helper.
- `ed64_detect()`: tries to unlock, reads `REG_EDID`, returns `IODEV_ED64` if upper 16 bits == `0xED64`, else `IODEV_NONE`. Idempotent.
- Stub bodies for `ed64_sd_init`, `ed64_sd_read_sectors`, `ed64_sd_write_sectors` returning `IODEV_ERR_NO_DEVICE`.
- `ED64_BACKEND` const struct (positional initializer for IDO C89) with the function pointers.
- `iodev_backend_ed64()` getter.

**Implementation notes for the agent:**
- IDO C89: declarations at top of block, positional struct initializers, no em-dashes, no `<stdint.h>` (use `lib_types.h` transitively via `iodev.h`).
- The cart-lock unlock sequence has a specific timing requirement — a small delay after each key write. The `PI_WRITE_FLUSH` macro's `IO_READ` drain provides ~1 µs between operations, which is sufficient.
- DO NOT FALSE-POSITIVE ON SC64: the registry probes SC64 first, so even if `ed64_detect()` returned `IODEV_ED64` on an SC64 cart (it shouldn't), SC64 wins. Reverse case: on a real ED64, sc64_detect's `SC64_REG_IDENT` read returns open-bus values which won't match `0x53437632`. Should be fine — verify with the static invariant in Task 7.

- [ ] **Step 3: Wire ED64 into the registry candidates list**

In `lib/iodev/iodev.c`, the candidates array currently looks like:

```c
const iodev_backend_t *candidates[1];
candidates[0] = iodev_backend_sc64();
/* candidates[1] = iodev_backend_ed64();  Phase 1b */
```

Two changes required (the array size declaration AND the assignment):

```c
const iodev_backend_t *candidates[2];
candidates[0] = iodev_backend_sc64();
candidates[1] = iodev_backend_ed64();
```

Both lines matter — leaving the array size at `[1]` while writing to `candidates[1]` is undefined behavior.

- [ ] **Step 4: Refactor the linker patcher to compute missing entries dynamically**

The current patcher has a hand-rolled per-state if/elif ladder. Adding `iodev_ed64` (this task) and `iodev_diag` (Task 6) on top of that ladder would push it to 5 states — and Phase 2+ will keep adding more lib files. Refactor now to a single algorithm: walk through the expected `LIB_IODEV_OBJS` in order; for each entry, if its `.o` line is missing from a section, inject it after whatever predecessor entry IS present in that section.

Update `LIB_IODEV_OBJS` (order matters — predecessor entries must precede in the list):

```python
LIB_IODEV_OBJS = [
    "iodev",
    "iodev_sc64",
    "iodev_ed64",  # Phase 1b — must precede iodev_stub
    "iodev_stub",
]
```

Replace the `patch()` function body with a single dynamic algorithm. Sketch:

```python
def patch():
    with open(LINKER_SCRIPT, "r") as f:
        content = f.read()

    has_practice = "practice_main" in content

    if not has_practice:
        # Fully unpatched: inject the full practice block + lib block after fox_save.o.
        # (Existing logic; preserve.)
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{ANCHOR}({section});"
            practice_block = "\n".join(
                f"        build/src/practice/{obj}.o({section});" for obj in PRACTICE_OBJS)
            lib_block = "\n".join(
                f"        build/lib/iodev/{obj}.o({section});" for obj in LIB_IODEV_OBJS)
            injection = practice_block + "\n" + lib_block
            content = _replace_after_anchor(content, anchor_line, injection)
        with open(LINKER_SCRIPT, "w") as f:
            f.write(content)
        print(f"Patched {LINKER_SCRIPT}: practice + lib/iodev (full).")
        return

    # Practice already patched. Walk LIB_IODEV_OBJS and inject any missing entries.
    # First entry's predecessor is the last practice obj; subsequent entries anchor
    # on the previous entry in LIB_IODEV_OBJS.
    last_practice_obj = PRACTICE_OBJS[-1]
    inject_count = 0
    for i, obj in enumerate(LIB_IODEV_OBJS):
        if f"build/lib/iodev/{obj}.o" in content:
            continue  # Already present
        if i == 0:
            predecessor = f"build/src/practice/{last_practice_obj}"
        else:
            predecessor = f"build/lib/iodev/{LIB_IODEV_OBJS[i-1]}"
        for section in [".text", ".data", ".rodata", ".bss"]:
            anchor_line = f"{predecessor}.o({section});"
            injection = f"        build/lib/iodev/{obj}.o({section});"
            content = _replace_after_anchor(content, anchor_line, injection)
        inject_count += 1

    if inject_count == 0:
        print("Linker script already fully patched, skipping.")
        return

    with open(LINKER_SCRIPT, "w") as f:
        f.write(content)
    print(f"Patched {LINKER_SCRIPT}: injected {inject_count} lib/iodev entries.")
```

This algorithm is N-state agnostic — adding more entries to `LIB_IODEV_OBJS` (Phase 1b's `iodev_diag`, Phase 2's FatFs files, etc.) requires zero patcher changes. Just append to the list.

`_replace_after_anchor` (from Phase 1a's `ee5bb05`) raises `RuntimeError` if the anchor isn't found, which becomes the early-warning when an entry's predecessor doesn't yet exist in the script (e.g., someone bypassed the patcher).

Verify:

```bash
python3 tools/patch_linker_script.py
grep "iodev" linker_scripts/us/rev1/starfox64.ld
```

Expected: in each section, `iodev.o` → `iodev_sc64.o` → **`iodev_ed64.o`** → `iodev_stub.o`. 16 lines total. Running the patcher again is a no-op ("already fully patched").

- [ ] **Step 5: Build & verify**

```bash
make practice -j4
python3 tools/practice_invariants.py
```

Both must pass.

- [ ] **Step 6: Commit**

```bash
git add lib/iodev/iodev_ed64.c lib/iodev/iodev.c tools/patch_linker_script.py
git commit -m "feat: add ED64 X7/X8 cart detection (SD ops are stubs)"
```

**Shippable state:** ED64 cart detection works. SD ops return `IODEV_ERR_NO_DEVICE`.

---

## Task 2: SD CRC layer + host unit tests

**Files:**
- Create: `lib/sd_crc.h`, `lib/sd_crc.c`
- Create: `lib/test/Makefile`
- Create: `lib/test/test_sd_crc.c`
- Modify: `Makefile` (add `lib-test` target)

**Goal:** CRC7 and CRC16-CCITT are implemented in a host-portable file and unit-tested against SD spec test vectors. After this task, `make lib-test` runs in <1 second on the developer machine and proves the CRC code is correct without needing the N64 toolchain.

- [ ] **Step 1: Create `lib/sd_crc.h`**

```c
#ifndef LIB_SD_CRC_H
#define LIB_SD_CRC_H

#include "lib_types.h"
#include <stddef.h>

/* SD spec CRC7 (used on every command frame).
 * Polynomial: x^7 + x^3 + 1.
 * Returns the 8-bit CRC byte: 7-bit CRC in bits 7..1, trailing 1 bit in bit 0. */
uint8_t sd_crc7(const uint8_t *buf, size_t len);

/* SD spec CRC16-CCITT for a single 4-bit DAT line.
 * Polynomial: x^16 + x^12 + x^5 + 1 (0x1021).
 * Used on every data block. */
uint16_t sd_crc16_ccitt(const uint8_t *buf, size_t len);

/* SD spec CRC16-CCITT for 4-line wide bus.
 * Each of the 4 DAT lines has its own independent CRC.
 * Returns 4 16-bit CRCs packed: [DAT3][DAT2][DAT1][DAT0] (MSB-first). */
uint64_t sd_crc16_4bit(const uint8_t *buf, size_t len);

#endif /* LIB_SD_CRC_H */
```

- [ ] **Step 2: Create `lib/sd_crc.c`**

Implement the three functions. Write fresh — these are SD spec algorithms, public. Reference: SD Physical Layer Specification §4.5 (CRC).

```c
#include "sd_crc.h"

/* CRC7 polynomial 0x89 (x^7 + x^3 + 1). After 8 left-shifts per input byte,
 * the 7-bit CRC sits in bits 7..1 of the accumulator (bit 0 always shifted in
 * as 0). Per SD spec, append a trailing 1 bit in bit 0 of the output byte. */
uint8_t sd_crc7(const uint8_t *buf, size_t len) {
    uint8_t crc = 0;
    size_t i;
    int j;
    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x12);  /* poly shifted */
            else            crc = (uint8_t)(crc << 1);
        }
    }
    return (uint8_t)(crc | 0x01);
}

/* CRC16-CCITT polynomial 0x1021. Standard byte-wise implementation. */
uint16_t sd_crc16_ccitt(const uint8_t *buf, size_t len) {
    uint16_t crc = 0;
    size_t i;
    int j;
    for (i = 0; i < len; i++) {
        crc ^= ((uint16_t)buf[i]) << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* 4-bit wide bus: each DAT line gets bits [n, n-4, n-8, ...] of the byte stream.
 * The SD spec defines the CRC as if each line's bit stream is a separate byte
 * stream concatenated. We accumulate 4 CRCs in parallel.
 *
 * Bit ordering on each line: DAT3 gets bit 7 of byte 0, then bit 3 of byte 0,
 * then bit 7 of byte 1, etc. (alternating high-nibble / low-nibble per byte). */
uint64_t sd_crc16_4bit(const uint8_t *buf, size_t len) {
    uint16_t crc[4] = {0, 0, 0, 0};
    size_t i;
    int j;
    int bit;
    int nibble;
    for (i = 0; i < len; i++) {
        for (nibble = 0; nibble < 2; nibble++) {
            uint8_t nib = (nibble == 0) ? (buf[i] >> 4) : (buf[i] & 0x0F);
            /* DAT3 = bit 3, DAT2 = bit 2, DAT1 = bit 1, DAT0 = bit 0. */
            for (j = 0; j < 4; j++) {
                bit = (nib >> (3 - j)) & 1;
                if (((crc[j] >> 15) & 1) ^ bit) crc[j] = (uint16_t)((crc[j] << 1) ^ 0x1021);
                else                            crc[j] = (uint16_t)(crc[j] << 1);
            }
        }
    }
    return ((uint64_t)crc[3] << 48) | ((uint64_t)crc[2] << 32)
         | ((uint64_t)crc[1] << 16) |  (uint64_t)crc[0];
}
```

The 4-bit CRC implementation may need adjustment based on the actual SD spec wire ordering — verify against the test vectors in Step 3 before integrating.

- [ ] **Step 3: Create `lib/test/test_sd_crc.c`**

```c
/* Host unit tests for sd_crc.c. Builds with native gcc; no libultra. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../sd_crc.h"

static int failures = 0;

#define ASSERT_EQ(actual, expected, label) do { \
    if ((actual) != (expected)) {               \
        printf("FAIL: %s: expected 0x%llx, got 0x%llx\n", \
               (label), (unsigned long long)(expected), (unsigned long long)(actual)); \
        failures++;                             \
    } else {                                    \
        printf("PASS: %s\n", (label));          \
    }                                           \
} while (0)

int main(void) {
    /* SD CRC7 test vectors (SD spec §4.5 + common reference impls). */
    {
        uint8_t cmd0[]  = {0x40, 0x00, 0x00, 0x00, 0x00};
        uint8_t cmd8[]  = {0x48, 0x00, 0x00, 0x01, 0xAA};
        uint8_t cmd17[] = {0x51, 0x00, 0x00, 0x00, 0x00};
        ASSERT_EQ(sd_crc7(cmd0,  sizeof(cmd0)),  0x95, "CRC7 CMD0  -> 0x95");
        ASSERT_EQ(sd_crc7(cmd8,  sizeof(cmd8)),  0x87, "CRC7 CMD8  -> 0x87");
        ASSERT_EQ(sd_crc7(cmd17, sizeof(cmd17)), 0x55, "CRC7 CMD17 -> 0x55");
    }

    /* CRC16-CCITT test vectors (CCITT-FALSE / SD spec). */
    {
        /* All-zero block: 512 bytes of 0x00 -> CRC 0x0000 */
        uint8_t zeros[512];
        memset(zeros, 0, sizeof(zeros));
        ASSERT_EQ(sd_crc16_ccitt(zeros, sizeof(zeros)), 0x0000, "CRC16 zeros -> 0x0000");

        /* All-0xFF block: 512 bytes of 0xFF.
         * Reference value computed via independent SD-spec implementation. */
        uint8_t ones[512];
        memset(ones, 0xFF, sizeof(ones));
        ASSERT_EQ(sd_crc16_ccitt(ones, sizeof(ones)), 0x7FA1, "CRC16 ones -> 0x7FA1");

        /* "123456789" classic CCITT test vector */
        uint8_t classic[] = "123456789";
        ASSERT_EQ(sd_crc16_ccitt(classic, 9), 0x31C3, "CRC16 \"123456789\" -> 0x31C3");
    }

    /* CRC16 4-bit wide bus: zeros is trivially 0 for any bit ordering, so
     * exercise an asymmetric pattern too — it's the only way to actually
     * catch a wrong bit-distribution to DAT lines. */
    {
        uint8_t zeros[512];
        memset(zeros, 0, sizeof(zeros));
        ASSERT_EQ(sd_crc16_4bit(zeros, sizeof(zeros)), 0ULL, "CRC16 4-bit zeros -> 0");

        /* Asymmetric pattern: every byte is 0xF0 (high nibble all-1, low all-0).
         * After SD's wide-bus serialization (high nibble first; bit n -> DAT n):
         *   - DAT3 line sees: "1010101..." (alternating from high nibble bit 3 = 1, low = 0)
         *   - DAT2 line: "1010..."
         *   - DAT1 line: "1010..."
         *   - DAT0 line: "1010..."
         * All four lines see the same bit stream, so all four CRCs equal.
         * Any misordering of high/low nibble or DAT-line indexing breaks this
         * symmetry and produces a different packed result.
         *
         * Compute the expected value with an external reference (Python's
         * crcmod, Linux kernel lib/crc-ccitt.c, online calculator) and place
         * it here. If you don't have one handy at implementation time:
         *   - Run the implementation, capture the output as the "actual" value.
         *   - Independently compute via Python:
         *       import crcmod
         *       fn = crcmod.predefined.mkCrcFun('xmodem')   # CRC16-CCITT init=0
         *       bits = '10' * (256*4)  # 256 bytes * 4 nibbles/byte * 2 bits/line per nibble pair... wait, derive carefully
         *   - If the values don't match, the implementation has a bit-order bug.
         *
         * For now, the test asserts that the four lines are equal (a weaker
         * but useful property). Replace this with a known-good vector before
         * shipping the diagnostic ROM. */
        uint8_t pat[512];
        for (size_t i = 0; i < 512; i++) pat[i] = 0xF0;
        uint64_t crc = sd_crc16_4bit(pat, sizeof(pat));
        uint16_t l0 = (uint16_t)(crc >>  0);
        uint16_t l1 = (uint16_t)(crc >> 16);
        uint16_t l2 = (uint16_t)(crc >> 32);
        uint16_t l3 = (uint16_t)(crc >> 48);
        if (!(l0 == l1 && l1 == l2 && l2 == l3)) {
            printf("FAIL: CRC16 4-bit pattern 0xF0: lines not equal "
                   "(l0=%04X l1=%04X l2=%04X l3=%04X) — bit-order bug\n",
                   l0, l1, l2, l3);
            failures++;
        } else {
            printf("PASS: CRC16 4-bit pattern 0xF0: all 4 lines equal (=%04X)\n", l0);
        }
        /* TODO: replace the above with a known-good external-reference value. */
    }

    if (failures > 0) {
        printf("\n%d test(s) failed\n", failures);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}
```

NOTE on the `0x7FA1` and `0x31C3` test vectors: these are independent reference values. If the implementer's reference has different known-good outputs, use those instead — what matters is that **the test catches a regression in the CRC code** by checking against an external reference.

- [ ] **Step 4: Create `lib/test/Makefile` AND exclude `lib/test/` from the practice ROM build**

**Critical setup issue:** the top-level Makefile at line 306 currently does:

```makefile
SRC_DIRS := $(shell find src -type d) $(shell find lib -type d 2>/dev/null)
```

This means `lib/test/` (created by this task) gets picked up as a SRC_DIR for the practice ROM build, and `lib/test/test_sd_crc.c` gets compiled with IDO into a MIPS object — which fails because the test file uses `<stdio.h>` and `printf` (host C library, not available under `-nostdinc`). **This breaks `make practice` the moment Task 2 lands.**

Fix: change line 306 of the top-level Makefile to exclude the test directory:

```makefile
SRC_DIRS := $(shell find src -type d) $(shell find lib -type d -not -path 'lib/test*' 2>/dev/null)
```

Then create `lib/test/Makefile`:

```makefile
# Host gcc test runner for lib/ unit tests.
# Builds with native compiler (NOT MIPS) so tests run on developer machine.
#
# Usage from repo root: make lib-test

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Werror -std=c99 -O0 -g
INCLUDES = -I.. -I.

TESTS = test_sd_crc

all: run-all

%: %.c ../sd_crc.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

run-all: $(TESTS)
	@for t in $(TESTS); do \
		echo "===> $$t"; \
		./$$t || exit 1; \
	done

clean:
	rm -f $(TESTS)

.PHONY: all run-all clean
```

- [ ] **Step 5: Add `lib-test` target to top-level `Makefile`**

Add near the existing `practice` target:

```makefile
lib-test:
	@$(MAKE) -C lib/test run-all
```

And add `lib-test` to the `.PHONY` line (around Makefile line 586) alongside the existing phony targets.

Verify the SRC_DIRS exclusion works:

```bash
make practice -j4    # Must build clean — lib/test/test_sd_crc.c must NOT be compiled
```

If you see compile errors mentioning `lib/test/test_sd_crc.c` in the practice build, the SRC_DIRS exclusion didn't take effect. The `-not -path 'lib/test*'` predicate must be in the right place inside the `find` invocation.

- [ ] **Step 6: Run unit tests**

```bash
make lib-test
```

Expected output:
```
===> test_sd_crc
PASS: CRC7 CMD0  -> 0x95
PASS: CRC7 CMD8  -> 0x87
PASS: CRC7 CMD17 -> 0x55
PASS: CRC16 zeros -> 0x0000
PASS: CRC16 ones -> 0x7FA1
PASS: CRC16 "123456789" -> 0x31C3
PASS: CRC16 4-bit zeros -> 0

All tests passed.
```

If any test fails, fix the implementation and re-run before moving on. **Don't proceed to Task 3 with a broken CRC.**

- [ ] **Step 7: Build practice ROM (lib/sd_crc.c is now in the SRC_DIRS for lib/)**

```bash
make practice -j4
python3 tools/practice_invariants.py
```

Both must pass. If `lib/sd_crc.c` fails IDO compile, common causes: stray em-dashes in comments, C99-style declarations mid-block, designated initializers — all forbidden by IDO C89. Fix and rebuild.

- [ ] **Step 8: Add `sd_crc` to the linker patcher**

The new files `lib/sd_crc.{c,h}` need to land in the linker script. They live in `lib/` (top-level), not `lib/iodev/`, so they need a separate constant or to extend `LIB_IODEV_OBJS` is not appropriate.

Add to `tools/patch_linker_script.py`:

```python
LIB_TOP_OBJS = [
    "sd_crc",  # in lib/sd_crc.c
]
```

Adjust the patcher's per-section injection to also include `build/lib/{obj}.o(...)` lines for `LIB_TOP_OBJS`. Place them after `iodev_stub.o` in each section (or before — order between `lib/iodev/*` and `lib/sd_crc.o` doesn't matter since they don't reference each other directly until Task 3).

Re-run: `python3 tools/patch_linker_script.py`. Expected: linker script grows by 4 more lines (one per section) for `sd_crc.o`.

Build again: `make practice -j4`. Must remain clean.

- [ ] **Step 9: Commit**

```bash
git add lib/sd_crc.h lib/sd_crc.c lib/test/Makefile lib/test/test_sd_crc.c \
        Makefile tools/patch_linker_script.py
git commit -m "feat: add SD CRC layer with host unit tests

CRC7 and CRC16-CCITT extracted to lib/sd_crc.{c,h} as host-portable
SD-spec utilities. Independent reference test vectors verify correctness
without needing N64 hardware. Phase 2's FatFs glue can reuse this code.

Test runner at lib/test/Makefile. Run with 'make lib-test' (~1 second).

Three CRC7 test vectors (CMD0/CMD8/CMD17), three CRC16 test vectors
(zeros / 0xFF / classic '123456789'), one CRC16-4bit smoke test."
```

**Shippable state:** SD CRC code is implemented and proven correct on host. ED64 doesn't yet use it (Task 3 wires it in).

---

## Task 3: SD card initialization (CMD0/CMD8/ACMD41/CMD2/CMD3/CMD7)

**Files:**
- Modify: `lib/iodev/iodev_ed64.c`

**Goal:** `ed64_sd_init` brings the SD card from power-on to "transfer state" using the verified CRC code from Task 2. After this task, `iodev_sd_init()` returns `IODEV_OK` on a real ED64 with an SD card inserted.

- [ ] **Step 1: Add SD-bus shift-register primitives**

The ED64 X FPGA exposes:
- `REG_SD_CMD_RD/WR` — 8-bit shift register on the CMD line.
- `REG_SD_DAT_RD/WR` — 16-bit shift register on the DAT0-DAT3 lines (4 bits per clock).
- `REG_SD_STATUS` — busy flag + bit-length config + speed config (low / 50 MHz).

Implement the basic primitives (write fresh, do not copy gz):
- `ed64_sd_set_speed(int slow)` — set `SD_CFG_SPD` bit + bit-length appropriate for slow (init phase) vs fast (post-init).
- `ed64_sd_busy_wait()` — spin on `SD_STA_BUSY` bit clearing, with a timeout (~50ms equivalent).
- `ed64_sd_cmd_tx(uint8_t byte)` — write to `REG_SD_CMD_WR`, wait for not-busy.
- `ed64_sd_cmd_rx(uint8_t *byte)` — write to `REG_SD_CMD_RD` to clock in 8 bits, read result, wait not-busy.
- `ed64_sd_dat_tx(uint16_t)` and `ed64_sd_dat_rx(uint16_t *)` — analogous for DAT.

Each primitive returns `iodev_result_t`.

- [ ] **Step 2: Include the verified CRC layer**

At the top of `iodev_ed64.c`, add:

```c
#include "sd_crc.h"  /* CRC7/CRC16 from lib/sd_crc.c (unit-tested in Task 2) */
```

Use `sd_crc7(...)` for command CRC and `sd_crc16_4bit(...)` for data block CRC. Do NOT reimplement these inline.

- [ ] **Step 3: Implement CMD send / response receive**

`ed64_sd_send_cmd(uint8_t cmd, uint32_t arg, void *resp_buf, size_t resp_len)`:
1. Build 6-byte command frame: `[0x40 | cmd] [arg_be_4_bytes] [crc7]` using `sd_crc7`.
2. Set CMD line to push-pull mode.
3. Shift out 6 bytes via `ed64_sd_cmd_tx`.
4. Set CMD line to input mode.
5. Shift in `resp_len` bytes via `ed64_sd_cmd_rx`.
6. Validate response start bit and CRC.
7. Return `IODEV_OK` or appropriate error.

- [ ] **Step 4: Implement init sequence**

```
ed64_sd_init():
  1. Power up: send 74+ dummy clocks with CMD high.
  2. CMD0 (GO_IDLE_STATE): expect R1 with idle bit set.
  3. CMD8 (SEND_IF_COND, voltage = 0x1AA): expect echo. SDv2 detection.
  4. ACMD41 loop (SD_SEND_OP_COND, HCS bit): retry until card_busy clears.
     Read OCR to determine if SDHC vs SDSC.
  5. CMD2 (ALL_SEND_CID): get CID (16 bytes).
  6. CMD3 (SEND_RELATIVE_ADDR): receive RCA.
  7. CMD7 (SELECT_CARD with RCA): card transitions to transfer state.
  8. CMD16 (SET_BLOCKLEN, 512): only needed for SDSC.
  9. ACMD6 (SET_BUS_WIDTH = 4-bit): switch DAT to 4-line mode.
  10. Switch FPGA to 50 MHz speed.
```

Each step is its own helper function for incremental hardware debugging. Track `ed64_card_is_sdhc` as a file-static bool — Tasks 4 and 5 need it for LBA conversion.

- [ ] **Step 5: Build, invariants pass**

```bash
make practice -j4 && python3 tools/practice_invariants.py
```

- [ ] **Step 6: Commit**

```bash
git add lib/iodev/iodev_ed64.c
git commit -m "feat: add ED64 SD card init sequence (CMD0/CMD8/ACMD41/CMD2/CMD3/CMD7)"
```

**Shippable state:** ED64 cart detection + SD init using verified CRC code. SD R/W still return `IODEV_ERR_NO_DEVICE`.

---

## Task 4: Single-block read (CMD17)

**Files:**
- Modify: `lib/iodev/iodev_ed64.c`

- [ ] **Step 1: Implement `ed64_sd_read_block`**

```
ed64_sd_read_block(uint32_t lba, void *buf):
  1. Adjust lba for SDSC vs SDHC (SDSC takes byte address, SDHC takes block address).
  2. Send CMD17 (READ_SINGLE_BLOCK, arg = lba).
  3. Wait for data start token.
  4. Shift in 512 bytes via REG_SD_DAT_RD.
  5. Read 8-byte CRC (4 lines × 16 bits), verify with sd_crc16_4bit.
  6. Return IODEV_OK.
```

- [ ] **Step 2: Update `ed64_sd_read_sectors`**

Replace the stub with a loop over `count` calling `ed64_sd_read_block(lba+i, buf+i*512)`.

Required guards (matching SC64):
- `(uintptr_t)buf & 7u → IODEV_ERR_PARAM` (8-byte alignment)
- `count == 0 || count > 128 → IODEV_ERR_PARAM` (sector cap)

- [ ] **Step 3: Build & test**

```bash
make practice -j4 && python3 tools/practice_invariants.py
```

- [ ] **Step 4: Commit**

```bash
git add lib/iodev/iodev_ed64.c
git commit -m "feat: add ED64 single-block SD read (CMD17)"
```

---

## Task 5: Single-block write (CMD24)

**Files:**
- Modify: `lib/iodev/iodev_ed64.c`

- [ ] **Step 1: Implement `ed64_sd_write_block`**

```
ed64_sd_write_block(uint32_t lba, const void *buf):
  1. Adjust lba for SDSC/SDHC.
  2. Send CMD24 (WRITE_BLOCK, arg = lba).
  3. Send data start token.
  4. Shift out 512 bytes via REG_SD_DAT_WR.
  5. Compute and send CRC16 via sd_crc16_4bit.
  6. Wait for card response token (data accepted / not).
  7. Wait for card not-busy (DAT0 high).
  8. Return IODEV_OK or IODEV_ERR_IO.
```

- [ ] **Step 2: Update `ed64_sd_write_sectors`**

Loop over `count` calling `ed64_sd_write_block`. Same alignment + count-cap checks as Task 4.

- [ ] **Step 3: Build, invariants, commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add lib/iodev/iodev_ed64.c
git commit -m "feat: add ED64 single-block SD write (CMD24)"
```

---

## Task 6: Diagnostic build mode (`IODEV_DIAG=1`)

**Files:**
- Create: `lib/iodev/iodev_diag.c`
- Create: `lib/iodev/iodev_diag.h`
- Modify: `Makefile` (pass `-DIODEV_DIAG=1` when building with `IODEV_DIAG=1`)
- Modify: `src/practice/practice_main.c` (call `iodev_diag_run()` after the existing iodev log, gated by `#ifdef IODEV_DIAG`)
- Modify: `tools/patch_linker_script.py` (add `iodev_diag` to `LIB_IODEV_OBJS`)

**Goal:** `make practice IODEV_DIAG=1` produces a ROM that auto-runs the full HW verification suite on boot. The EverDrive contact flashes this once, captures output, sends it back. No manual probe-code editing required.

- [ ] **Step 1: Create `lib/iodev/iodev_diag.h`**

```c
#ifndef LIB_IODEV_DIAG_H
#define LIB_IODEV_DIAG_H

/* Run the full hardware verification suite. Logs structured pass/fail per
 * step via osSyncPrintf. Safe to call from Practice_Init.
 *
 * No-op when IODEV_DIAG is not defined at compile time. */
void iodev_diag_run(void);

#endif
```

- [ ] **Step 2: Create `lib/iodev/iodev_diag.c`**

Empty translation unit unless `IODEV_DIAG` is defined.

**Critical safety note:** the diagnostic ROM writes to the SD card. Sector 0 (MBR) is read-only in the diag suite, but the round-trip test (T4) writes a test sector. The plan's previous draft used LBA `0x100000` (~512 MB into the card) and called it "well past any filesystem use" — that claim is **wrong**. On any non-empty FAT-formatted card, sector 0x100000 is in the data region and likely allocated to a real user file. The diagnostic ROM must NOT write there silently.

The mitigation is two-layered:

1. **Use the SD card's last sector as the write target.** Last sector is rarely allocated in normal use (FAT32's data region typically ends well before the physical end of the card). The diag code queries the card's capacity via CMD9 (`SEND_CSD`) at init time and computes `last_lba = capacity_blocks - 1`.
2. **Document scratch-card requirement loudly in `HW_VERIFY_phase1b.md`.** The contact uses a freshly-formatted blank card for testing.

Both. Belt and suspenders.

The diag source includes the right libultra header (`PR/os_libc.h`, NOT `PR/xstdio.h` — `osSyncPrintf` is declared in `os_libc.h:91`).

```c
#include "iodev_diag.h"

#ifdef IODEV_DIAG

#include "PR/os_libc.h"  /* osSyncPrintf */
#include "iodev.h"

/* Card capacity in 512-byte blocks. Computed in iodev_diag_run after sd_init.
 * Set to 0 if CMD9 / capacity probe fails — diag aborts in that case. */
static uint32_t sDiagCapacityBlocks = 0;

/* TODO during Task 6 implementation: implement diag_query_capacity() that
 * issues CMD9 (SEND_CSD), parses the CSD response (v1 or v2), and returns
 * total block count. The CSD parse is non-trivial but well-documented in
 * the SD spec §5.3. Sets sDiagCapacityBlocks on success, 0 on failure.
 *
 * If implementing capacity probe is too time-expensive, fall back to a
 * hardcoded "test_lba" defined at compile time, with a HUGE warning in
 * HW_VERIFY_phase1b.md that the user MUST use a scratch card. */
static iodev_result_t diag_query_capacity(void);

/* The diagnostic suite. Called once from Practice_Init when IODEV_DIAG=1. */
void iodev_diag_run(void) {
    iodev_id_t cart;
    iodev_result_t res;
    static unsigned char sec0[512] __attribute__((aligned(8)));
    static unsigned char wbuf[512] __attribute__((aligned(8)));
    static unsigned char rbuf[512] __attribute__((aligned(8)));
    uint32_t test_lba;
    int i;
    int match;

    osSyncPrintf("\n[diag] === Phase 1b iodev hardware verification ===\n");
    osSyncPrintf("[diag] WARNING: this ROM writes to the SD card. Use a SCRATCH CARD.\n");

    /* Test 1: cart detection */
    cart = iodev_detect();
    osSyncPrintf("[diag] T1 cart_id=%d  (expect 1=SC64, 2=ED64; 0=NONE means no flashcart detected)\n",
                 (int)cart);
    if (cart == IODEV_NONE) {
        osSyncPrintf("[diag] FAIL T1: no flashcart detected, aborting\n");
        return;
    }

    /* Test 2: SD init */
    res = iodev_sd_init();
    osSyncPrintf("[diag] T2 sd_init=%d  (expect 0=OK)\n", (int)res);
    if (res != IODEV_OK) {
        osSyncPrintf("[diag] FAIL T2: SD init failed (-1=NO_CARD, -3=IO, -4=TIMEOUT)\n");
        return;
    }

    /* Capacity probe — required to compute a safe write target. */
    res = diag_query_capacity();
    osSyncPrintf("[diag] capacity_probe=%d capacity_blocks=%u\n",
                 (int)res, (unsigned)sDiagCapacityBlocks);
    if (res != IODEV_OK || sDiagCapacityBlocks < 2) {
        osSyncPrintf("[diag] FAIL: cannot determine card capacity, refusing to write\n");
        osSyncPrintf("[diag] (T3 read tests will run; T4-T6 write tests skipped)\n");
        test_lba = 0;
    } else {
        /* Use the very last sector. Almost never allocated in FAT32. */
        test_lba = sDiagCapacityBlocks - 1;
    }
    osSyncPrintf("[diag] write_test_lba=0x%X\n", (unsigned)test_lba);

    /* Test 3: read sector 0 (MBR), check 0x55AA signature at offset 0x1FE */
    res = iodev_sd_read_sectors(0, 1, sec0);
    osSyncPrintf("[diag] T3 read_sec0=%d  signature=%02X%02X (expect 55AA)\n",
                 (int)res, (unsigned)sec0[510], (unsigned)sec0[511]);
    osSyncPrintf("[diag] T3 sec0 bytes 0..15: ");
    for (i = 0; i < 16; i++) osSyncPrintf("%02X ", (unsigned)sec0[i]);
    osSyncPrintf("\n");
    if (res != IODEV_OK) {
        osSyncPrintf("[diag] FAIL T3: read failed\n");
        return;
    }
    /* MBR signature check is a soft fail — some cards aren't FAT-formatted. */
    if (sec0[510] != 0x55 || sec0[511] != 0xAA) {
        osSyncPrintf("[diag] WARN T3: MBR signature missing (card may not be FAT-formatted)\n");
    }

    if (test_lba == 0) {
        osSyncPrintf("[diag] === SKIPPED T4-T6 (no capacity) ===\n");
        return;
    }

    /* Test 4: write/read round-trip on the LAST sector of the card. */
    for (i = 0; i < 512; i++) wbuf[i] = (unsigned char)(i ^ 0x5A);
    res = iodev_sd_write_sectors(test_lba, 1, wbuf);
    osSyncPrintf("[diag] T4 write_sec=%d\n", (int)res);
    if (res != IODEV_OK) {
        osSyncPrintf("[diag] FAIL T4: write failed\n");
        return;
    }
    for (i = 0; i < 512; i++) rbuf[i] = 0;
    res = iodev_sd_read_sectors(test_lba, 1, rbuf);
    osSyncPrintf("[diag] T4 read_back=%d\n", (int)res);
    if (res != IODEV_OK) {
        osSyncPrintf("[diag] FAIL T4: read-back failed\n");
        return;
    }
    match = 1;
    for (i = 0; i < 512; i++) if (wbuf[i] != rbuf[i]) { match = 0; break; }
    osSyncPrintf("[diag] T4 round_trip_match=%d (expect 1)\n", match);
    if (!match) {
        osSyncPrintf("[diag] FAIL T4: round-trip mismatch at byte %d (wrote %02X, read %02X)\n",
                     i, (unsigned)wbuf[i], (unsigned)rbuf[i]);
        return;
    }

    /* Test 5: count > 128 must reject (cap guard) */
    res = iodev_sd_read_sectors(test_lba, 200, rbuf);
    osSyncPrintf("[diag] T5 cap_check=%d (expect -5=ERR_PARAM)\n", (int)res);

    /* Test 6: misaligned buffer must reject (alignment guard) */
    res = iodev_sd_read_sectors(test_lba, 1, (void *)((unsigned char *)rbuf + 1));
    osSyncPrintf("[diag] T6 align_check=%d (expect -5=ERR_PARAM)\n", (int)res);

    osSyncPrintf("[diag] === ALL TESTS PASS ===\n");
}

#else  /* !IODEV_DIAG */

void iodev_diag_run(void) { /* no-op */ }

#endif
```

The `diag_query_capacity()` helper is a real implementation task. CMD9 (`SEND_CSD`) returns the Card-Specific Data structure; SD spec §5.3 describes the fields. CSD v1 (legacy SDSC) and v2 (SDHC) have different layouts — both encode capacity as `(C_SIZE + 1) * 2^C_SIZE_MULT * 2^READ_BL_LEN` bytes (v1) or `(C_SIZE + 1) * 512 KB` (v2). Implementer should add the helper alongside `ed64_sd_init` in `iodev_ed64.c` (since CMD9 is an ED64-specific operation) and expose it via a small accessor `iodev_get_capacity_blocks(uint32_t *out)` that the diag code calls. Or (simpler): just add CMD9 + capacity computation as a private helper in `iodev_diag.c` that issues raw SD commands via the iodev backend's primitives. Whichever is cleaner.

NOTE: `iodev_diag.c` includes `PR/os_libc.h` (where `osSyncPrintf` is declared per `include/PR/os_libc.h:91`). It's a libultra include and `iodev_diag.c` is NOT on the libultra-allowlist by default. Add it to `LIBULTRA_ALLOWED` in `tools/practice_invariants.py` as part of this task. Document in a comment that diag is a hardware-test artifact, not normal lib/ code.

- [ ] **Step 3: Wire `iodev_diag_run()` into `Practice_Init`**

Edit `src/practice/practice_main.c`. After the existing iodev log block, add:

```c
#ifdef IODEV_DIAG
    iodev_diag_run();
#endif
```

Add `#include "iodev/iodev_diag.h"` near the existing `#include "iodev/iodev.h"`. The header is safe to include even when `IODEV_DIAG` is not defined (it just declares the no-op).

- [ ] **Step 4: Wire `IODEV_DIAG` into the Makefile**

Find the existing `BUILD_DEFINES += -DPRACTICE_ROM=1 -DAVOID_UB` block (around Makefile line 155). Add:

```makefile
ifeq ($(IODEV_DIAG),1)
    BUILD_DEFINES   += -DIODEV_DIAG=1
endif
```

- [ ] **Step 5: Add `iodev_diag` to the linker patcher**

Extend `LIB_IODEV_OBJS`:

```python
LIB_IODEV_OBJS = [
    "iodev",
    "iodev_sc64",
    "iodev_ed64",
    "iodev_diag",  # Phase 1b diagnostic mode
    "iodev_stub",
]
```

The patcher's incremental injection logic added in Task 1 needs another iteration: the new `iodev_diag.o` slots between `iodev_ed64.o` and `iodev_stub.o`. Extend the four-state machine to a five-state one:

| State | Detection | Action |
|-------|-----------|--------|
| ... existing 4 states ... | | |
| Phase-1b ed64 patched, diag missing (NEW) | `iodev_ed64.o` present, `iodev_diag.o` missing | Inject `iodev_diag.o` anchored on `iodev_ed64.o` |

If maintaining N states becomes painful, refactor to: "compute the difference between expected `LIB_IODEV_OBJS` and what's in the script; insert the missing entries each anchored on the entry that should immediately precede it." That's cleaner long-term.

- [ ] **Step 6: Build and verify both modes**

```bash
make practice -j4                    # Normal build
make practice -j4 IODEV_DIAG=1       # Diagnostic build
python3 tools/practice_invariants.py
```

Both must pass. The diagnostic build's ROM checksum will differ from the normal build (extra symbols).

- [ ] **Step 7: Commit**

```bash
git add lib/iodev/iodev_diag.c lib/iodev/iodev_diag.h \
        src/practice/practice_main.c \
        Makefile tools/patch_linker_script.py tools/practice_invariants.py
git commit -m "feat: add IODEV_DIAG=1 diagnostic build mode

Auto-runs the Phase 1b hardware verification suite on boot, dumping
structured per-step pass/fail via osSyncPrintf. EverDrive-equipped
contributors flash 'make practice IODEV_DIAG=1' once, capture output,
report findings — no manual probe-code editing required.

Six tests: cart detection, SD init, MBR read + signature check,
write/read round-trip on safe sector 0x100000, count-cap guard,
misalignment guard."
```

**Shippable state:** Diagnostic ROM ready to send to the EverDrive contact. **This is the milestone for shipping to them.**

---

## Task 7: Static invariants + hardware verification doc

**Files:**
- Modify: `tools/practice_invariants.py`
- Create: `docs/superpowers/plans/HW_VERIFY_phase1b.md`

- [ ] **Step 1: Add `check_iodev_ed64()` to invariants**

```python
def check_iodev_ed64():
    """ED64 X iodev backend must preserve protocol invariants."""
    path = "lib/iodev/iodev_ed64.c"
    if not os.path.isfile(path):
        return
    src = read(path)

    # Cart-bus writes must use PI_WRITE_FLUSH (same gotcha as SC64/IS-Viewer).
    if "PI_WRITE_FLUSH" not in src:
        error(f"{path}: must use PI_WRITE_FLUSH macro for cart-bus writes")

    # Cart unlock sequence — hardware fact; if removed, cart is inaccessible.
    # Match the literal magic values rather than any specific constant name,
    # so the implementer is free to name them ED64_UNLOCK_KEY_1 / KEY_A / etc.
    if "0xAA55" not in src or "0x55AA" not in src:
        error(f"{path}: must use the cart-unlock magic sequence (0xAA55 + 0x55AA)")

    # 128-sector cap matches SC64's; consistent caller contract.
    if src.count("count > 128") < 2:
        error(f"{path}: both sd_read_sectors and sd_write_sectors must enforce count > 128 → ERR_PARAM")

    # 8-byte buffer alignment check (matches iodev.h public contract).
    if src.count("& 7u") < 2:
        error(f"{path}: both sd_read_sectors and sd_write_sectors must enforce 8-byte buffer alignment")

    # Must use the unit-tested CRC layer, not reimplement inline.
    if 'sd_crc.h' not in src:
        error(f"{path}: must include sd_crc.h (use unit-tested CRC, don't reimplement)")
    if "sd_crc7" not in src:
        error(f"{path}: must call sd_crc7() for command CRCs")
```

Wire into `main()`.

- [ ] **Step 2: Verify positive + negative tests**

Positive: invariants pass on current code.

Negative tests (each: edit, run, confirm fails, revert):
- Comment out `PI_WRITE_FLUSH` macro definition → must fail
- Remove `#include "sd_crc.h"` → must fail
- Change `count > 128` to `count > 256` in one path → must fail
- Change one `0xAA55` literal to `0xBA55` → must fail (cart-unlock magic check)

- [ ] **Step 3: Create `docs/superpowers/plans/HW_VERIFY_phase1b.md`**

Mirror the structure of `HW_VERIFY_phase1a.md` but center on the diagnostic ROM. The contact's experience should be:

```markdown
# Phase 1b Hardware Verification (EverDrive 64 X7/X8)

## Time required: ~10 minutes

## ⚠ READ FIRST: SD card requirement

**This ROM writes to the SD card. Use a SCRATCH CARD that you don't care about.**

The diagnostic ROM writes one sector to the card during the round-trip test (T4). It targets the LAST sector of the card (computed from CMD9/CSD), which is rarely allocated in normal use, AND it logs the target LBA before writing — so if anything looks wrong, you can power off before the write happens. But it's still a real write to a real SD card. **Use a card with no important data.** A spare freshly-formatted FAT32 card is ideal.

If you only have your normal card, **stop here** and respond that you need a scratch card. Don't run the ROM.

## What you'll need

- EverDrive 64 X7 or X8 cart
- A SCRATCH SD card (see warning above — any size, class doesn't matter, but the data on it will be destroyed in one sector)
- A way to capture serial / IS-Viewer output from the cart
  (UNFLoader, ED64-specific debug tooling, or analogous)

## What you'll do

1. **Build the diagnostic ROM** (or get it from the project maintainer):
   ```bash
   make practice -j4 IODEV_DIAG=1
   ```
   The output is `build/starfox64.us.rev1.uncompressed.z64`.

2. **Insert SD card into ED64**, **flash the ROM**, **boot the N64**.

3. **Capture the IS-Viewer output**. Should look like:
   ```
   [iodev] cart=2 sd_init=0
   [diag] === Phase 1b iodev hardware verification ===
   [diag] T1 cart_id=2  (expect 1=SC64, 2=ED64; ...)
   [diag] T2 sd_init=0  (expect 0=OK)
   [diag] T3 read_sec0=0  signature=55AA (expect 55AA)
   [diag] T3 sec0 bytes 0..15: <16 hex bytes>
   [diag] T4 write_sec=0
   [diag] T4 read_back=0
   [diag] T4 round_trip_match=1 (expect 1)
   [diag] T5 cap_check=-5 (expect -5=ERR_PARAM)
   [diag] T6 align_check=-5 (expect -5=ERR_PARAM)
   [diag] === ALL TESTS PASS ===
   ```

4. **Send the captured output back to the maintainer.**

## Failure modes you might see

- `cart_id=0` — ED64 not detected. Cart unlock sequence is wrong.
- `cart_id=1` — SC64 detected on an ED64 cart. Detection logic conflict.
- `sd_init=-1` — no SD card in slot. Insert one.
- `sd_init=-3` — SD card init failed. Could be card incompatibility (try a different one), CRC bug, or timing issue.
- `signature=????` not `55AA` — read transferred wrong bytes. Likely 4-bit DAT line ordering or block-size mismatch.
- `round_trip_match=0` — write or read corrupting data. CRC16 likely wrong, or write-acceptance handshake broken.
- `cap_check=0` (instead of -5) — guard missing. Bug.

## Reporting

Note in your report:
- ED64 firmware version (cart label or via UNFLoader's info command)
- SD card brand / size / class
- Full captured output (paste verbatim)
- Anything that didn't match expectations
```

The diagnostic ROM does the work; the human just runs it and pastes output.

- [ ] **Step 4: Build, commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add tools/practice_invariants.py docs/superpowers/plans/HW_VERIFY_phase1b.md
git commit -m "feat: ED64 invariants + Phase 1b hardware verification checklist"
```

---

## Task 8: Phase exit gate

- [ ] **Step 1: Verify all automated checks pass**

```bash
python3 tools/practice_invariants.py
make practice -j4
make practice -j4 IODEV_DIAG=1
make lib-test
```

All four must succeed. The first three exercise the production ROM and diagnostic ROM build paths; `lib-test` proves the CRC layer is correct.

- [ ] **Step 2: Confirm no probe code in `Practice_Init`**

`grep -c "iodev_sd_read_sectors\|iodev_sd_write_sectors" src/practice/practice_main.c` must return 0. The diagnostic mode lives in `lib/iodev/iodev_diag.c`, NOT in `Practice_Init`.

- [ ] **Step 3: Build the diagnostic ROM artifact for the EverDrive contact**

```bash
make practice -j4 IODEV_DIAG=1
cp build/starfox64.us.rev1.uncompressed.z64 phase1b-diag.z64
```

Send `phase1b-diag.z64` and `docs/superpowers/plans/HW_VERIFY_phase1b.md` to the EverDrive contact.

- [ ] **Step 4: Hardware verification status (in PR description)**

```
Phase 1b ships with:
- Algorithmic correctness verified (CRC unit tests pass)
- Static invariants for ED64 protocol guards
- Diagnostic ROM ready for ED64-equipped contributor
- Wire-level correctness pending HW verification report

When HW verification report comes back, file follow-up issues for any
failures and treat the phase as fully complete only after report shows
"ALL TESTS PASS".
```

- [ ] **Step 5: Optional — tag the phase**

```bash
git tag phase1b-iodev-ed64-pre-hw-verify
```

After HW verification report comes back clean, retag:

```bash
git tag -d phase1b-iodev-ed64-pre-hw-verify
git tag phase1b-iodev-ed64
```

If the time-box was hit and Phase 1b shipped at Task 1 (detection only) or Task 2 (CRC only), tag accordingly with a more specific suffix.

---

## Risk register

| Risk | Likelihood | Mitigation |
|------|------------|-----------|
| ED64 SDIO protocol has a subtle init-timing requirement only obvious from gz code | Medium | Diagnostic ROM exposes it on real hardware in 10 minutes. Time-boxed retry budget if first HW report fails. |
| Clean-room concerns: implementer reflexively reproduces gz's variable names | Medium | Plan explicitly forbids gz files open during writing. Notes-only approach. Code review checks for suspicious phrase reuse. |
| `tools/patch_linker_script.py` incremental mode breaks across multiple Phase 1b additions (sd_crc, iodev_ed64, iodev_diag) | Medium | Plan suggests refactoring to "compute missing entries from expected list" if the four-state machine becomes painful. |
| Without hardware to verify mid-implementation, bugs only surface at Task 8 | High | Host unit tests on CRC catch most algorithmic bugs early. Diagnostic ROM gives single-shot HW verification rather than iterative shipping. |
| ED64 detect false-positives on SC64 cart (or vice versa) | Low | First-match-wins in registry. Static invariant for both backends. |
| 4-bit CRC16 ordering: SD spec is subtle on which bit goes to which DAT line | Medium | Test vector in `test_sd_crc.c` for all-zeros (must give 0); on hardware, write/read round-trip catches incorrect ordering. If T4 fails on the diagnostic ROM, this is the first thing to suspect. |
| Multi-block speed loss (~10-20× slower than CMD18/CMD25) annoys users | Low | Acceptable for v1. Phase 4 (state save/load) makes ~250 KB writes — at single-block speed, ~5-10 sec on real hardware. Users tolerate that for save state. Multi-block can ship later. |
| Audio bank corruption from ED64 register access during SDIO transfers | Low-Medium | ED64 X's audio is a separate FPGA region. Same logic as SC64. Verify on hardware — diagnostic ROM doesn't exercise this directly but the practice ROM's normal use will. |

---

## Explicit non-goals

- **No EverDrive 64 v1/v2 support.** Spec says X7/X8 only.
- **No ED64 cart-USB support.** Out of scope.
- **No SDXC support beyond what SDHC handles.** SDXC > 32 GB cards may work via the SDHC code path; if they don't, that's a follow-up.
- **No write-protect detection.** Not exposed by the FPGA in a useful way.
- **No card-removal detection at runtime.** Boot-time only via `iodev_detect`.
- **No multi-cart support.** ED64 X has only one SD slot; hardware constraint.
- **No multi-block CMD18/CMD25 transfers.** Dropped from Phase 1b. Single-block R/W is sufficient. Multi-block is a future perf optimization.

---

## Final notes for the executing agent

- **Time-box discipline.** If you find yourself reading gz code repeatedly to figure out a detail, STOP. Either the public Krikzz docs cover it or you need hardware to verify it — neither is a good reason to lift gz code structure. Report BLOCKED and let the user decide.
- **Clean-room hygiene.** When in doubt, check whether your code has the same variable names as gz's. `cart_lock` / `cart_unlock` / `reg_rd` / `reg_wr` / `cmd_tx` / `dat_rx` are gz's names. Your names should differ (`ed64_unlock`, `ed64_reg_read`, `ed64_cmd_send`, `ed64_dat_recv`).
- **Run host unit tests early and often.** Every time you touch CRC code, `make lib-test` first.
- **The diagnostic ROM is the contract with the EverDrive contact.** Their time is finite; the diag suite must produce actionable output (named test, expected value, actual value) so they can paste it back without further investigation.
- **The user has another agent doing PNG-related work in this worktree.** Use explicit `git add` paths only.
- **Spec compliance over completeness.** If your `ed64_sd_init` returns `IODEV_OK` but the actual SD card hasn't reached transfer state, the diagnostic ROM will catch it on real hardware. Static invariants and build-time checks can't see that. Don't over-promise in the commit message.
