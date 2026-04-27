# Phase 1b: ED64 X7/X8 iodev Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an EverDrive 64 X7/X8 backend to the existing `lib/iodev/` abstraction so users on Krikzz hardware get the same SD card I/O surface that SC64 users got in Phase 1a.

**Architecture:** Single new file `lib/iodev/iodev_ed64.c` implementing the ED64 X protocol against the FPGA registers documented in Krikzz's public hardware spec. The file plugs into the existing registry pattern from Phase 1a (`iodev_backend_t` descriptor + getter). Unlike SC64's high-level `SD_READ`/`SD_WRITE` commands, ED64 X exposes raw SDIO bus primitives — the host drives the SD card protocol directly (CMD0, CMD8, ACMD41, CMD2/3/7, CMD17/18/24/25, CRC7/CRC16). This is structurally larger than Phase 1a's SC64 backend.

**Tech Stack:** C (IDO C89), libultra PI primitives, ED64 X FPGA register protocol, SD Physical Layer Specification v3.0+.

**Spec reference:** `docs/superpowers/specs/2026-04-27-gz-style-features-design.md` lines 575-580 (Phase 1b deliverable).

**Time-box:** **3-5 days.** If blocked, the project ships SC64-only — Phase 2+ proceed regardless. Phase 1a's iodev abstraction was specifically designed so this fallback is clean.

---

## License & clean-room constraints

The reference implementations available are:

- `~/code/gz/src/gz/ed64_x.c` — **GPL-2**. May be **studied for protocol understanding only**. Do NOT copy code structure, function organization, variable names, or expressions. Read it the way you'd read a paper: extract the *facts* (FPGA register addresses, command sequences, timing requirements), discard the *expression*.
- `~/code/gz/src/gz/ed64_x.h` — **GPL-2** but most of its contents are protocol facts (register addresses, status bit positions) sourced from Krikzz's public hardware documentation. Re-deriving these from the public Krikzz wiki / [ED64-IO library](https://github.com/krikzz/ED64) (which has more permissive terms) is preferred when uncertain. Constants/addresses themselves are not copyrightable; only the file's expression is.
- SD Physical Layer Specification — public standard. CMD0, CMD2, CMD3, CMD7, CMD8, CMD16, CMD17, CMD18, CMD24, CMD25, CMD55, ACMD41, ACMD51, etc. are spec facts. CRC7 and CRC16 polynomial / algorithm are spec facts. Use freely.

**The implementer must NOT have `~/code/gz/src/gz/ed64_x.c` open while writing `iodev_ed64.c`.** Read it once, take protocol notes (register map, init sequence), close it, write fresh code from notes.

If this constraint feels arbitrary: it isn't. GPL contamination at the lib/ layer would force the entire practice ROM project to be GPL'd (it isn't currently). The whole point of `lib/`'s portability story is that it can lift into any project regardless of license.

---

## Hardware constraint

**The user does NOT have an EverDrive 64 X7/X8.** All hardware verification in this phase is **deferred** — written into a checklist for a future contributor (or a future user purchase) to run.

This means automated tests catch:
- Build/link cleanliness
- Static invariants (lib isolation, libultra scope, ED64-specific invariants)
- BizHawk stub mode (verifies the registry's polymorphism — calling `iodev_detect()` doesn't crash with the ED64 backend wired in, and returns `IODEV_NONE` since no flashcart is simulated)

The actual SDIO traffic on real hardware is unproven until someone runs `HW_VERIFY_phase1b.md` on an ED64. The plan should produce code that compiles and is *structurally* correct; on-the-wire correctness is a future deliverable.

If during execution this constraint becomes a real blocker (e.g., the ED64 protocol has a subtle init-timing requirement that's only obvious from gz's code), STOP and report — don't shape the design around assumptions you can't verify.

---

## File Structure

**New files:**
- `lib/iodev/iodev_ed64.c` — ED64 X7/X8 protocol implementation (single file, realistically ~500-650 LoC: register macros + PI_WRITE_FLUSH + cart unlock + detect + ~8 SDIO primitives + CRC7+CRC16 + 6-step init + read_block + write_block + descriptor table).
- `docs/superpowers/plans/HW_VERIFY_phase1b.md` — manual hardware verification checklist (run by future ED64-equipped contributor).

**Modified files:**
- `lib/iodev/iodev.c` — uncomment / enable `iodev_backend_ed64()` in the `candidates[]` list (one-line change).
- `tools/patch_linker_script.py` — add `iodev_ed64` to `LIB_IODEV_OBJS`.
- `tools/practice_invariants.py` — add `check_iodev_ed64()` analogous to `check_iodev_sc64()`. Update `LIBULTRA_ALLOWED` list to include `lib/iodev/iodev_ed64.c` (already listed in the existing `LIBULTRA_ALLOWED` per Phase 1a).

**Not touched:**
- `lib/iodev/iodev.h` — public API unchanged.
- `lib/iodev/iodev_internal.h` — `iodev_backend_ed64()` was declared during Phase 1a as a placeholder.
- `lib/iodev/iodev_sc64.c`, `iodev_stub.c`, `iodev.c` (registry logic), `iodev_internal.h`.
- `lib/lib_types.h`.
- `tests/test_iodev_detect.lua` — unchanged; on emulator both backends report no cart, the stub still wins.

---

## Shippable checkpoints

The plan is structured so each chunk produces a buildable, committable state. **If the time-box expires, the user can stop at any of these points:**

- **After Task 1:** ED64 cart detection works (no SD ops). On real ED64 hardware, `iodev_detect()` would return `IODEV_ED64`; SD ops return `IODEV_ERR_NO_DEVICE`. SC64 users entirely unaffected.
- **After Task 2:** ED64 SD initialization works. Card reaches "transfer state" but no read/write yet.
- **After Task 3:** ED64 single-block SD reads work.
- **After Task 4:** ED64 single-block SD writes work.
- **After Task 5:** ED64 multi-block reads/writes (≤128 sectors per call, matching SC64 cap) work.
- **After Task 6:** Static invariants and hardware verification doc landed. Phase 1b complete.

Tasks 1-6 don't all need to ship in this phase. Stopping early at Task 1 is a legitimate outcome if SD-protocol implementation proves too time-expensive — ED64 users still benefit from cart detection (e.g., the practice ROM's IS-Viewer log can show `cart=2` so the user knows the abstraction *sees* their cart).

---

## Task 1: ED64 X register access + cart detection

**Files:**
- Create: `lib/iodev/iodev_ed64.c`
- Modify: `tools/patch_linker_script.py` (add `iodev_ed64` to `LIB_IODEV_OBJS`)
- Modify: `lib/iodev/iodev.c` (uncomment `iodev_backend_ed64()` in `candidates[]`)

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
- Register address `#define`s using the names you extracted (e.g., `ED64_REG_BASE`, `ED64_REG_KEY`, `ED64_REG_EDID`, etc.). Use a `ED64_` prefix to namespace them clearly (avoid collisions with future ED64 v1/v2 backends if added).
- A `PI_WRITE_FLUSH(addr, val)` macro analogous to SC64's, with a dummy follow-up `IO_READ` to drain the PI bus (same gotcha applies — direct cart-bus writes drop without it).
- Cart unlock helper: writes the magic key sequence and confirms unlock by reading `REG_SYS_CFG` or similar.
- `ed64_detect()` function: tries to unlock, reads `REG_EDID`, returns `IODEV_ED64` if the upper byte matches X7/X8 magic, else `IODEV_NONE`. Idempotent.
- Stub bodies for `ed64_sd_init`, `ed64_sd_read_sectors`, `ed64_sd_write_sectors` that return `IODEV_ERR_NO_DEVICE`.
- `ED64_BACKEND` const struct (positional initializer for IDO C89) with the function pointers.
- `iodev_backend_ed64()` getter returning `&ED64_BACKEND`.

**Implementation notes for the agent:**
- IDO C89: declarations at top of block, positional struct initializers, no em-dashes, no `<stdint.h>` (use `lib_types.h` transitively via `iodev.h`).
- The cart-lock unlock sequence has a specific timing requirement — there's typically a small delay after each key write. Use the same `PI_WRITE_FLUSH` macro that includes the IO_READ drain; that gives you ~1 µs between operations, which is sufficient.
- **DO NOT FALSE-POSITIVE ON SC64**: the registry probes SC64 first (per `lib/iodev/iodev.c` candidates list), but if Phase 1b's `ed64_detect()` somehow returns `IODEV_ED64` on an SC64 cart, the registry's first-match-wins logic would still pick SC64 (good). Reverse case: on a real ED64, sc64_detect's `SC64_REG_IDENT` read goes to a region that doesn't exist on ED64; the cart-bus returns open-bus values which won't match `0x53437632`. Should be fine — but verify with the implementer's added invariant.

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

Both lines matter — leaving the array size at `[1]` while writing to `candidates[1]` is undefined behavior, and IDO won't necessarily warn. Verify by post-edit grep:

```bash
grep -A2 "candidates\[" lib/iodev/iodev.c
```

Expected output shows `candidates[2]`, both `candidates[0] =` and `candidates[1] =` lines, no commented-out remnant.

- [ ] **Step 4: Add `iodev_ed64` to the linker patcher**

In `tools/patch_linker_script.py`, extend `LIB_IODEV_OBJS`. **Order matters — `iodev_ed64` must come BEFORE `iodev_stub` so that incremental injection (next paragraph) anchors correctly:**

```python
LIB_IODEV_OBJS = [
    "iodev",
    "iodev_sc64",
    "iodev_ed64",  # Phase 1b — must precede iodev_stub
    "iodev_stub",
]
```

**The harder part: extending the patcher's three-state logic to handle the "Phase 1a injected, Phase 1b not yet" intermediate state.**

The current patcher (after Phase 1a) handles three states:

| State | Detection | Action |
|-------|-----------|--------|
| Fully unpatched | no `practice_main` | Inject practice + lib lines after `fox_save.o` anchor |
| Practice patched, lib unpatched | `practice_main` present, `iodev.o` missing | Inject lib lines after `practice_freecam.o` anchor |
| Fully patched | both present | No-op |

After Phase 1b, we need a fourth state:

| State | Detection | Action |
|-------|-----------|--------|
| Practice + Phase-1a-iodev patched, ed64 missing | `practice_main` + `iodev.o` present, `iodev_ed64.o` missing | Inject `build/lib/iodev/iodev_ed64.o(...)` lines anchored on `build/lib/iodev/iodev_sc64.o(...)` (insert between sc64 and stub, NOT after stub or after practice) |

**Why anchor on `iodev_sc64.o` and not `iodev_stub.o`:** the linker resolves symbols by section order, and the registry's `iodev_backend_*()` getters are called in `LIB_IODEV_OBJS` order. Keeping `iodev_stub` last preserves the "stub fallback" semantic. Inserting `iodev_ed64` after `iodev_sc64` (i.e., between sc64 and stub) keeps the order consistent with the `LIB_IODEV_OBJS` list and matches the registry's probe order in `iodev.c`.

**Patcher revision sketch:**

```python
def patch():
    with open(LINKER_SCRIPT, "r") as f:
        content = f.read()

    has_practice = "practice_main" in content
    has_iodev = "iodev.o" in content
    has_ed64 = "iodev_ed64.o" in content

    if has_practice and has_iodev and has_ed64:
        print("Linker script already patched (practice + full lib/iodev), skipping.")
        return

    for section in [".text", ".data", ".rodata", ".bss"]:
        if not has_practice:
            # Existing "fully unpatched" branch — inject practice + all lib lines.
            # (No change from Phase 1a.)
            ...
        elif not has_iodev:
            # Existing "practice patched, lib unpatched" branch — inject all lib lines
            # anchored on practice_freecam.
            # (No change from Phase 1a.)
            ...
        else:
            # NEW: Phase 1a iodev injected, Phase 1b ed64 missing.
            # Inject ONLY iodev_ed64.o lines, anchored on iodev_sc64.o.
            anchor_line = f"build/lib/iodev/iodev_sc64.o({section});"
            injection = f"        build/lib/iodev/iodev_ed64.o({section});"
            content = _replace_after_anchor(content, anchor_line, injection)
    ...
```

The `_replace_after_anchor` helper (added in Phase 1a's patcher hardening commit `ee5bb05`) raises `RuntimeError` if the anchor isn't found — that's the desired behavior here too.

**Verify the change:**

```bash
python3 tools/patch_linker_script.py
grep "iodev" linker_scripts/us/rev1/starfox64.ld
```

Expected output: in each section, `iodev.o` → `iodev_sc64.o` → **`iodev_ed64.o`** → `iodev_stub.o`. 16 total lines (4 objects × 4 sections), with the ed64 entries between sc64 and stub.

If running the patcher reports `RuntimeError` ("anchor not found"), the linker script's `iodev_sc64.o` line probably doesn't exist in that section yet — investigate before continuing. Don't force `make extract` (forbidden by CLAUDE.md); manual edit is acceptable as a last resort.

- [ ] **Step 5: Build & verify**

Run: `make practice -j4`
Expected: clean build, no link errors.

Run: `python3 tools/practice_invariants.py`
Expected: pass (the existing libultra allowlist already includes `lib/iodev/iodev_ed64.c`).

- [ ] **Step 6: BizHawk smoke test**

The existing `tests/test_iodev_detect.lua` should still pass — on emulator, neither SC64 nor ED64 is simulated, both detect functions return `IODEV_NONE`, and the registry parks the stub backend. No new test file needed for Task 1.

If BizHawk is available locally:
```bash
BIZHAWK_PATH=... python3 tools/run_tests.py test_iodev_detect
```

If not, document and move on (per Phase 1a precedent).

- [ ] **Step 7: Commit**

```bash
git add lib/iodev/iodev_ed64.c lib/iodev/iodev.c tools/patch_linker_script.py
git commit -m "feat: add ED64 X7/X8 cart detection (SD ops are stubs)"
```

**Shippable state:** at this point Phase 1b can be paused. Real ED64 users get cart detection (visible via the IS-Viewer log: `[iodev] cart=2`); SD ops correctly report `IODEV_ERR_NO_DEVICE`. Phase 2+ proceed unaffected.

---

## Task 2: SD card initialization (CMD0/CMD8/ACMD41/CMD2/CMD3/CMD7)

**Files:**
- Modify: `lib/iodev/iodev_ed64.c`

**Goal:** `ed64_sd_init` brings the SD card from power-on to "transfer state" (ready to issue CMD17/24 read/write). After this task, `iodev_sd_init()` returns `IODEV_OK` on a real ED64 with an SD card inserted.

- [ ] **Step 1: Add SD-bus shift-register primitives**

The ED64 X FPGA exposes:
- `REG_SD_CMD_RD/WR` — 8-bit shift register on the CMD line.
- `REG_SD_DAT_RD/WR` — 16-bit shift register on the DAT0-DAT3 lines (4 bits per clock).
- `REG_SD_STATUS` — busy flag + bit-length config + speed config (low / 50 MHz).

Implement the basic primitives (write fresh, do not copy gz):
- `ed64_sd_set_speed(int slow)` — write `REG_SD_STATUS` to set `SD_CFG_SPD` bit + bit-length appropriate for slow (init phase) vs fast (post-init).
- `ed64_sd_busy_wait()` — spin on `SD_STA_BUSY` bit clearing, with a timeout (~50ms equivalent — same `SC64_CMD_TIMEOUT_RETRIES`-style upper bound).
- `ed64_sd_cmd_tx(uint8_t byte)` — write to `REG_SD_CMD_WR`, wait for not-busy.
- `ed64_sd_cmd_rx(uint8_t *byte)` — write to `REG_SD_CMD_RD` to clock in 8 bits, read result, wait not-busy.
- `ed64_sd_dat_tx(uint16_t)` and `ed64_sd_dat_rx(uint16_t *)` — analogous for DAT.

Each primitive should return `iodev_result_t` so timeout failures bubble up cleanly.

- [ ] **Step 2: Add CRC7 and CRC16 helpers**

CRC7 polynomial: x⁷ + x³ + 1 = `0x89`, used on every SD command transmission.
CRC16-CCITT polynomial: `0x1021`, used on every data block.

Public algorithms (write fresh — these are spec facts):

```c
/* SD CRC7. The accumulator processes the input byte-by-byte. After 8 shifts
 * per input byte, the 7-bit CRC sits in bits 7..1 of `crc` (bit 0 was always
 * shifted in as 0). The SD spec mandates a trailing 1 bit in bit 0 of the
 * output byte, so we OR 0x01 into the final value. NO right-shift. */
static uint8_t ed64_crc7(const uint8_t *buf, size_t len) {
    uint8_t crc = 0;
    size_t i;
    int j;
    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x12);  /* poly 0x89 shifted to land in bit 7 */
            else            crc = (uint8_t)(crc << 1);
        }
    }
    return (uint8_t)(crc | 0x01);  /* CRC in bits 7..1, trailing 1 bit in bit 0 */
}

/* CRC16-CCITT for 4-bit DAT lines: each of the 4 lines has its own CRC,
 * so this returns 4 16-bit CRCs in a 64-bit packed word. Implementation
 * detail — see SD spec section "CRC for 4-bit Wide Bus" */
```

**Sanity-check `ed64_crc7` against known SD spec test vectors before integrating** (do this BEFORE running on hardware):

| Command | Frame (5 bytes input) | Expected CRC7 byte |
|---------|----------------------|-------------------|
| CMD0 GO_IDLE_STATE, arg=0 | `0x40 0x00 0x00 0x00 0x00` | `0x95` |
| CMD8 SEND_IF_COND, arg=0x000001AA | `0x48 0x00 0x00 0x01 0xAA` | `0x87` |
| CMD17 READ_SINGLE_BLOCK, arg=0 | `0x51 0x00 0x00 0x00 0x00` | `0x55` |

If your `ed64_crc7` returns different values, the algorithm is wrong — debug before continuing. Common mistakes: using `(crc >> 1) | 0x01` (off by one bit position), wrong polynomial constant, swapped bit-order.

- [ ] **Step 3: Implement CMD send / response receive**

`ed64_sd_send_cmd(uint8_t cmd, uint32_t arg, void *resp_buf, size_t resp_len)`:
1. Build 6-byte command frame: `[0x40 | cmd] [arg_be_4_bytes] [crc7]`.
2. Set CMD line to push-pull mode (open-drain pre-init, push-pull post-init).
3. Shift out 6 bytes via `ed64_sd_cmd_tx`.
4. Set CMD line to input mode.
5. Shift in `resp_len` bytes via `ed64_sd_cmd_rx`.
6. Validate response start bit and CRC.
7. Return `IODEV_OK` or appropriate error.

- [ ] **Step 4: Implement init sequence**

```
ed64_sd_init():
  1. Power up: send 74+ dummy clocks with CMD high (some hosts skip).
  2. CMD0 (GO_IDLE_STATE): expect R1 with idle bit set.
  3. CMD8 (SEND_IF_COND, voltage = 0x1AA): expect echo. SDv2 detection.
  4. ACMD41 loop (SD_SEND_OP_COND, HCS bit): retry until card_busy clears.
     Read OCR to determine if SDHC vs SDSC.
  5. CMD2 (ALL_SEND_CID): get CID (16 bytes).
  6. CMD3 (SEND_RELATIVE_ADDR): receive RCA.
  7. CMD7 (SELECT_CARD with RCA): card transitions to transfer state.
  8. CMD16 (SET_BLOCKLEN, 512): only needed for SDSC (SDHC ignores).
  9. ACMD6 (SET_BUS_WIDTH = 4-bit): switch DAT to 4-line mode.
  10. Switch FPGA to 50 MHz speed.

  Return IODEV_OK if all steps succeed, IODEV_ERR_NO_CARD if step 4
  doesn't see card_busy clear, IODEV_ERR_IO for any other failure.
```

Each step is its own helper function (`ed64_sd_cmd0`, `ed64_sd_cmd8`, etc.) so debugging on hardware is incremental.

- [ ] **Step 5: Build, invariants pass**

Run: `make practice -j4 && python3 tools/practice_invariants.py`

The build must remain clean. If it links but `ed64_sd_init` segfaults in some edge case at runtime, that's a future-hardware problem — out of scope for build-time checks.

- [ ] **Step 6: Commit**

```bash
git add lib/iodev/iodev_ed64.c
git commit -m "feat: add ED64 SD card init sequence (CMD0/CMD8/ACMD41/CMD2/CMD3/CMD7)"
```

**Shippable state:** ED64 cart detection + SD init. SD reads/writes still return `IODEV_ERR_NO_DEVICE`. ED64 users could probe init success via IS-Viewer log.

---

## Task 3: Single-block read (CMD17)

**Files:**
- Modify: `lib/iodev/iodev_ed64.c`

- [ ] **Step 1: Implement `ed64_sd_read_block`**

```
ed64_sd_read_block(uint32_t lba, void *buf):
  1. Adjust lba for SDSC vs SDHC (SDSC takes byte address, SDHC takes block address).
  2. Send CMD17 (READ_SINGLE_BLOCK, arg = lba).
  3. Wait for data start token (0xFE or DAT-line equivalent).
  4. Shift in 512 bytes via REG_SD_DAT_RD.
  5. Read 16-byte CRC, verify (or trust FPGA — many ED64 setups skip).
  6. Return IODEV_OK.
```

- [ ] **Step 2: Update `ed64_sd_read_sectors` to use the block primitive**

Replace the stub with a loop over `count` calling `ed64_sd_read_block(lba+i, buf+i*512)`.

The 8-byte alignment check (`(uintptr_t)buf & 7u → IODEV_ERR_PARAM`) must be present here too (matches Phase 1a Issue 4 fix).

The `count == 0 || count > 128` check must also be present (matches SC64's cap).

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

## Task 4: Single-block write (CMD24)

**Files:**
- Modify: `lib/iodev/iodev_ed64.c`

- [ ] **Step 1: Implement `ed64_sd_write_block`**

```
ed64_sd_write_block(uint32_t lba, const void *buf):
  1. Adjust lba for SDSC/SDHC.
  2. Send CMD24 (WRITE_BLOCK, arg = lba).
  3. Send data start token.
  4. Shift out 512 bytes via REG_SD_DAT_WR.
  5. Send CRC16.
  6. Wait for card response token (data accepted / not).
  7. Wait for card not-busy (DAT0 high).
  8. Return IODEV_OK or IODEV_ERR_IO.
```

- [ ] **Step 2: Update `ed64_sd_write_sectors`**

Loop over `count` calling `ed64_sd_write_block`. Same alignment + count-cap checks.

- [ ] **Step 3: Build, invariants, commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add lib/iodev/iodev_ed64.c
git commit -m "feat: add ED64 single-block SD write (CMD24)"
```

---

## Task 5: Multi-block transfers (CMD18/CMD25 — optional optimization)

**Files:**
- Modify: `lib/iodev/iodev_ed64.c`

**Goal:** Performance optimization. Replace the loop-over-single-blocks with proper multi-block transfers (CMD18 READ_MULTIPLE_BLOCK, CMD25 WRITE_MULTIPLE_BLOCK). Same caller-visible behavior, ~10-20× faster on real hardware.

**This task is optional.** If the time-box is running short, skip it — single-block reads/writes work and Phase 2+ can move forward.

- [ ] **Step 1: Implement multi-block read with CMD18 + CMD12 stop**

CMD18 starts a stream of blocks; CMD12 (STOP_TRANSMISSION) terminates. The FPGA may also support `sd_rx_mblk` style hardware-accelerated multi-block — check the ED64 X register set for `REG_SDIO_ARD` or similar batching primitives.

- [ ] **Step 2: Implement multi-block write with CMD25 + CMD12 stop**

Same shape, write direction.

- [ ] **Step 3: Build, invariants, commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add lib/iodev/iodev_ed64.c
git commit -m "perf: ED64 multi-block SD transfers (CMD18/CMD25)"
```

---

## Task 6: Static invariants + hardware verification doc

**Files:**
- Modify: `tools/practice_invariants.py`
- Create: `docs/superpowers/plans/HW_VERIFY_phase1b.md`

- [ ] **Step 1: Add `check_iodev_ed64()` to invariants**

Mirror the structure of `check_iodev_sc64()` from Phase 1a. Specific checks:

```python
def check_iodev_ed64():
    """ED64 X iodev backend must preserve protocol invariants.

    Without these, the channel silently fails on real ED64 hardware.
    """
    path = "lib/iodev/iodev_ed64.c"
    if not os.path.isfile(path):
        return
    src = read(path)

    # Cart-bus writes must use PI_WRITE_FLUSH (same gotcha as SC64/IS-Viewer).
    if "PI_WRITE_FLUSH" not in src:
        error(f"{path}: must use PI_WRITE_FLUSH macro for cart-bus writes")

    # Cart unlock magic — these are hardware facts; if removed, cart is inaccessible.
    # (Use whatever names you chose for the unlock keys; common gz names are 0xAA55/0x55AA.)
    if "ED64_KEY" not in src:
        error(f"{path}: must define ED64_KEY constants (cart unlock sequence)")

    # 128-sector cap matches SC64's DMA scratch cap; consistent caller contract.
    if src.count("count > 128") < 2:
        error(f"{path}: both sd_read_sectors and sd_write_sectors must enforce count > 128 → ERR_PARAM")

    # 8-byte buffer alignment check (matches iodev.h public contract).
    if src.count("& 7u") < 2:
        error(f"{path}: both sd_read_sectors and sd_write_sectors must enforce 8-byte buffer alignment")

    # CRC7 and CRC16 must be implemented — these are SD spec requirements.
    if "crc7" not in src.lower() or "crc16" not in src.lower():
        error(f"{path}: must implement CRC7 (commands) and CRC16 (data blocks) per SD spec")
```

Wire into `main()` alongside the existing `check_iodev_sc64()` call.

- [ ] **Step 2: Verify positive + negative tests**

Positive: `python3 tools/practice_invariants.py` passes on the current code.

Negative tests (each: edit, run, confirm fails, revert):
- Comment out `PI_WRITE_FLUSH` macro definition → must fail
- Rename `ED64_KEY_1` constants to `KEY_1` → must fail
- Change `count > 128` to `count > 256` in one read → must fail

- [ ] **Step 3: Create `docs/superpowers/plans/HW_VERIFY_phase1b.md`**

Mirror the structure of `HW_VERIFY_phase1a.md`. Key differences:

- No SC64-specific deployer commands; user runs ED64 toolchain (typically `unfloader` or the ED64 SD-mounted approach).
- Test 1: cart detection — confirm IS-Viewer log shows `cart=2 sd_init=0` on real ED64 hardware.
- Test 2: sector 0 read round-trip vs `dd` (same shape as Phase 1a).
- Test 3: sector 0x100000 write/read round-trip (same shape as Phase 1a).
- Add a callout: **"This phase has not been hardware-verified by the original implementer (no ED64 cart available). The first ED64-equipped contributor running this checklist should expect to find at least one issue."** This sets honest expectations.
- Reporting section: ED64 firmware version (cart label or via `unfloader`), SD card details, any anomalies.

- [ ] **Step 4: Build, commit**

```bash
make practice -j4
python3 tools/practice_invariants.py
git add tools/practice_invariants.py docs/superpowers/plans/HW_VERIFY_phase1b.md
git commit -m "feat: ED64 invariants + Phase 1b hardware verification checklist"
```

---

## Task 7: Phase exit gate

- [ ] **Step 1: Verify all automated checks pass**

```bash
python3 tools/practice_invariants.py
make practice -j4
```

Both must succeed.

- [ ] **Step 2: Confirm no probe code in `Practice_Init`**

`grep -c "iodev_sd_read_sectors\|iodev_sd_write_sectors" src/practice/practice_main.c` must return 0.

- [ ] **Step 3: Hardware verification status**

`docs/superpowers/plans/HW_VERIFY_phase1b.md` exists. **Hardware testing has NOT been run** (user lacks ED64). Document this clearly in any PR description: *"Phase 1b ships without hardware verification on real ED64. Code is structurally sound (compiles, links, passes invariants, registry polymorphism verified in BizHawk). Wire-level correctness pending ED64-equipped contributor."*

- [ ] **Step 4: Optional — tag the phase**

```bash
git tag phase1b-iodev-ed64
```

If the time-box was hit and Phase 1b shipped at Task 1 (detection only), tag as `phase1b-iodev-ed64-detection-only` instead. Subsequent ED64 SD work would then be a separately-tracked follow-up.

---

## Risk register

| Risk | Likelihood | Mitigation |
|------|------------|-----------|
| ED64 SDIO protocol has a subtle init-timing requirement only obvious from gz code | High | Time-box bites first; ship Task 1 + Task 2 (detection + init), defer reads/writes if unclear. |
| Clean-room concerns: implementer reflexively reproduces gz's variable names or function structure | Medium | Plan explicitly forbids having gz files open during writing. Notes-only approach. Code review checks for suspicious phrase reuse. |
| `tools/patch_linker_script.py` incremental mode breaks on the new entry | Medium | Plan acknowledges this; Task 1 Step 4 has explicit guidance on the patcher fix. |
| Without hardware to verify, the "passes invariants" green light is misleading | High | Hardware verification doc explicitly notes the unproven status. PR description must say so. Future ED64 users expect to find issues. |
| ED64 detect false-positives on SC64 cart (or vice versa) | Low | First-match-wins in registry mitigates one direction. Verify with both backends enabled in BizHawk stub mode (no false positive since neither cart is simulated). |
| Multi-block writes have a CMD12 race condition that's only visible at high speed | Low | Task 5 is optional. Falling back to single-block is acceptable. |
| Audio bank corruption from ED64 register access during SDIO transfers | Low-Medium | ED64 X's audio is a separate FPGA region; SDIO doesn't touch it. Same logic as SC64. Verify on hardware. |

---

## Explicit non-goals

- **No EverDrive 64 v1/v2 support.** The user's spec said X7/X8 only. Older ED64 revs use a substantially different protocol; supporting them would be a separate phase.
- **No ED64 cart-USB support.** ED64's USB pass-through is unrelated to SD I/O; out of scope.
- **No SDXC support beyond what SDHC handles.** SDXC > 32 GB cards may work via the SDHC code path; if they don't, that's a follow-up.
- **No write-protect detection.** Not exposed by the FPGA in a useful way.
- **No card-removal detection at runtime.** Boot-time only via `iodev_detect`.
- **No multi-cart support.** ED64 X has only one SD slot; this is a hardware constraint, not a software one.

---

## Final notes for the executing agent

- **Time-box discipline.** If you find yourself reading gz code repeatedly to figure out a detail, STOP. Either the public Krikzz docs cover it or you need hardware to verify it — neither is a good reason to lift gz code structure. Report BLOCKED and let the user decide.
- **Clean-room hygiene.** When in doubt, check whether your code has the same variable names as gz's. `cart_lock` / `cart_unlock` / `reg_rd` / `reg_wr` / `cmd_tx` / `dat_rx` are gz's names. Your names should differ (e.g., `ed64_unlock`, `ed64_reg_read`, `ed64_cmd_send`, `ed64_dat_recv`).
- **The user has another agent doing PNG-related work in this worktree.** Use explicit `git add` paths only.
- **Spec compliance over completeness.** If your `ed64_sd_init` returns `IODEV_OK` but the actual SD card hasn't reached transfer state, hardware testing will catch it later. Static invariants and build-time checks can't see that. Don't over-promise in the commit message.
