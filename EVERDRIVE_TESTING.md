# EverDrive 64 X7/X8 — SD Savestate Testing Guide

This document is written for an AI assistant helping a tester with an EverDrive 64 X7
or X8. It front-loads the context you need to run a productive session without reading
the full codebase.

---

## What this branch does

The practice ROM saves and loads game state snapshots to the SD card. This already works
on SummerCart64 (SC64). This branch (`everdrive-saving`) extends it to the EverDrive 64
X7/X8 by implementing the SD SPI protocol in `lib/iodev/iodev_ed64.c`.

The implementation was written against Krikzz's public hardware documentation but has
**never been run on real EverDrive hardware**. The goal of this session is to find out
whether it works, and if not, narrow down where it fails.

---

## Build

```bash
make practice -j4
```

Output: `build/starfox64.us.rev1.uncompressed.z64`

Never run `make clean` or `make init` — they delete generated asset headers that take
10+ minutes to rebuild.

---

## Loading onto the EverDrive

The EverDrive 64 X7/X8 has no USB deployer. Workflow:

1. Copy `build/starfox64.us.rev1.uncompressed.z64` to the SD card
2. Rename it something short if desired (e.g. `sf64prac.z64`)
3. Insert SD card into EverDrive, power on, navigate to the file in the ED OS menu
4. Press A to launch

The ROM boots directly to the practice level-select screen.

---

## How to trigger SD save and load

There are three ways once in-game (hold L+R to open the radial menu):

| Action | Method |
|--------|--------|
| SD Save | Radial menu → SD → SAVE, **or** Z while radial is open |
| SD Load | Radial menu → SD → LOAD, **or** Z+B while radial is open |

The hint line at the bottom of the radial menu says:
`L:R B:CLOSE Z:SD SAVE ZB:SD LOAD`

After triggering:
- **SD Save** opens the on-screen keyboard (OSK) to type a filename, then press Start or
  navigate to OK to confirm.
- **SD Load** opens a file browser listing `.SF64ST` files from the SD card. Press A to
  load, B to cancel.

Save files go to: `sageraces/sf64/states/<name>.SF64ST` on the SD card root.

---

## HUD status messages

All outcomes appear as a brief colored banner in the top-left corner of the screen.

| Message | Meaning |
|---------|---------|
| `SD SAVE OK` | Save succeeded |
| `SD LOAD OK` | Load succeeded |
| `XSCENE WAIT` | Load queued for cross-scene (will apply on next level load) |
| `SD XBLD OK` | Load OK but ROM build differs — entity arrays cleared for safety |
| `NO SD CART` | `iodev_sd_was_ok()` returned false — init failed or no card |
| `SD OPEN FAIL` | FatFs `f_open` failed |
| `SD WRITE FAIL` | FatFs `f_write` failed |
| `LOAD OPEN FAIL` | FatFs `f_open` for read failed |
| `LOAD READ FAIL` | FatFs `f_read` failed |
| `LOAD CORRUPT` | State file header checksum bad |
| `LOAD BAD MAGIC` | File is not a valid SF64 state |
| `LOAD VERSION` | State was saved by a different ROM build |
| `NO SD SAVES` | No `.SF64ST` files found in the save directory |
| `SD CANCEL` | User cancelled |
| `SD ERR <n>` | Unexpected slot manager error code n |
| `LOAD ERR <n>` | Unexpected slot manager error code n |

If you see `NO SD CART` on boot and the EverDrive is detected at all, the SD init
sequence failed. This is the most likely first failure.

---

## What to test, in order

### 1. Detection smoke test (no gameplay needed)

Boot the ROM. Open the radial menu (L+R). Try Z (SD Save).

- **`NO SD CART`** banner: The ED64 was not detected OR `iodev_sd_init()` failed.
  This tells us the SPI init sequence is broken. See diagnosis below.
- **OSK appears**: Detection and init succeeded. Continue to test 2.

### 2. SD Save — happy path

With any level running:
1. Open radial (L+R)
2. Press Z
3. Type a name in the OSK (use D-pad + A to select letters, Start or OK to confirm)
4. Watch for `SD SAVE OK`

Check the SD card on a PC: `sageraces/sf64/states/<name>.SF64ST` should exist.

### 3. SD Load — happy path

Still in the same level:
1. Open radial (L+R)
2. Press Z+B
3. Select the file you just saved
4. Watch for `SD LOAD OK`

The game state should restore (position, health, hit count, etc.).

### 4. Cross-scene load

Save on one level, go to level select, enter a different level, then load from SD.
Expect `XSCENE WAIT` banner. The state applies when the next level finishes loading.

---

## Likely failure modes and diagnosis

The ED64 SPI implementation is unverified. These are the most probable failure points
in order of likelihood:

### `NO SD CART` on first boot

`iodev_sd_was_ok()` is false. Possible causes:

**A. SD init timeout during ACMD41 loop**
The ACMD41 busy-poll runs up to 4000 iterations. If the card is slow to initialize,
this could time out. Fix: increase `ED64_SD_TIMEOUT` or the loop limit in
`ed64_sd_init()` (`lib/iodev/iodev_ed64.c`).

**B. CMD0 ignored — card not in SPI mode**
The 80-clock preamble (10 × 0xFF) must be sent before CMD0. If the FPGA pre-drives CS
low, the card may have already left native mode. Fix is hardware-specific.

**C. Wrong FPGA register addresses**
The register indices (`ED64_REG_SD_CMD_WR_IDX = 0x2009` etc.) were taken from the
Krikzz public docs. If the hardware revision uses different indices, every write lands
in the wrong place. Compare against `github.com/krikzz/ED64` source for the actual
register map for your cart revision.

**D. STATUS config write triggers transfer on some revisions**
Currently we write STATUS before each CMD_WR/DAT_WR. If the cart instead expects a
STATUS write to trigger the transfer itself (not the data write), byte ordering breaks.

### `SD SAVE OK` but file is corrupt/empty on PC

The FatFs layer is working but the data bytes written to SD are wrong. Likely a DAT line
protocol issue — check whether CMD and DAT lines are wired to the same FPGA interface or
need separate enable logic.

### `SD OPEN FAIL` or `LOAD OPEN FAIL`

FatFs mounted but file operations fail. Possible causes:
- Directory `sageraces/sf64/states` was not created (the ROM tries to create it at boot,
  but that path also uses FatFs, so if SD init failed partially this could be empty).
- The SD card is formatted exFAT — FatFs on this ROM supports FAT32 only.
- Card needs FAT32 formatting.

### Game hangs (no banner, no response to input)

A BUSY poll loop is spinning forever. One of the `ED64_SD_TIMEOUT = 500000` loops hit
its limit but the caller didn't check the return value, OR there is an infinite retry
somewhere. If this happens, note exactly which action triggered it (save/load/boot) and
report it.

---

## Key source files

| File | What it does |
|------|-------------|
| `lib/iodev/iodev_ed64.c` | ED64 SPI backend — this is what's being tested |
| `lib/iodev/iodev.c` | Backend dispatch; `iodev_sd_was_ok()` lives here |
| `src/practice/practice_sd.c` | FatFs glue, OSK/file-browser wiring, HUD messages |
| `lib/iodev/iodev_sc64.c` | SC64 backend for reference (working baseline) |

---

## SPI protocol cheat sheet (for AI reasoning about failures)

The ED64 X FPGA exposes an SPI controller through PI-bus registers. Each byte transfer:

1. Write `SD_STATUS` config (`0x07` = init speed, `0x17` = 50 MHz)
2. Write byte to `SD_CMD_WR` (CMD line) or `SD_DAT_WR` (data line)
3. Poll `SD_STATUS` bit 7 (`BUSY`) until clear
4. Read `SD_CMD_RD` or `SD_DAT_RD` for the received byte

Init sequence: 80 clocks → CMD0 (CRC 0x95) → CMD8 (CRC 0x87, expect echo 0x1AA) →
ACMD41 loop → switch to 50 MHz → CMD16 (blocklen=512).

Read: CMD17 (LBA) → poll for token 0xFE → 512 bytes via DAT → 2 CRC bytes discarded.

Write: CMD24 (LBA) → 0xFF dummy → token 0xFE → 512 bytes via DAT → 2 CRC bytes →
read response (lower 5 bits must be 0x05) → poll until DAT != 0x00.

SDHC cards use LBA directly; SD v1 multiplies LBA by 512 for byte addressing.

---

## Reporting results

Please capture:
- EverDrive model (X7 or X8) and OS version shown in the EverDrive menu
- SD card brand, capacity, and format (FAT32 vs exFAT)
- Which test step passed/failed and what banner appeared
- If the game hung, what action triggered it
- Any patterns (e.g. "save works but load hangs", "fails only on cross-scene")

With this information the failing path in `iodev_ed64.c` can be identified and fixed.
