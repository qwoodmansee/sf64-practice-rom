# Phase 2 Hardware Verification (SC64)

## Time required: ~5 minutes

## What you'll need

- SC64 cart with `sc64deployer` configured (per Phase 1a workflow)
- A FAT32-formatted SD card. **A scratch card is recommended** — Phase 2's
  test writes a real file (`SF64TEST.TXT`, ~22 bytes) to the card root.
  Any size up to 32 GB works (Phase 2 doesn't enable exFAT).

## Background

Phase 2 layered FatFs R0.15 on top of Phase 1a's `iodev_sd_*` SD primitives.
Algorithmic correctness is verified by host unit tests (`make lib-test`):
22 cases for the diskio glue + 50-file FatFs round-trip + bounce-buffer
coverage. This hardware verification step proves the same path works on
real SC64 hardware: mount, write, read, content-match, unmount.

If this passes, Phase 2 is fully verified. If it fails, see "Failure
modes" below for triage hints.

## Verification procedure

1. **Build the diagnostic ROM:**
   ```bash
   make practice -j4 IODEV_DIAG_FATFS=1
   ```
   Confirm output ends with `Generating Rom Header Checksum...` and no errors.
   The ROM lives at `build/starfox64.us.rev1.uncompressed.z64`.

2. **Start the IS-Viewer listener (Terminal A):**
   ```bash
   sc64deployer debug --isv 0x03FF0000
   ```
   Wait for `[IS-Viewer 64]: Listening on ROM offset [0x03FF0000]`.

3. **Upload the ROM (Terminal B), then press the physical reset button on
   the N64:**
   ```bash
   ./tools/sc64dev   # repo-root discovery; see ./tools/sc64dev help
   ```

4. **Capture Terminal A output.** Expected on a healthy run:
   ```
   === PRACTICE ROM boot @ <date> <time> ===
   [iodev] cart=1 sd_init=0

   [diag-fatfs] === Phase 2 hardware verification ===
   [diag-fatfs] WARNING: writes SF64TEST.TXT to your SD card root.
   [diag-fatfs] T7 fatfs_mount=0 (expect 0=FR_OK)
   [diag-fatfs] T8 fatfs_write=0 bytes_written=21 (expect 0, 21)
   [diag-fatfs] T9 fatfs_read=0 bytes_read=21 match=1 (expect 0, 21, 1)
   [diag-fatfs] === DONE ===
   ```

5. **Power off the N64.** Remove the SD card and mount it on a PC.

6. **Verify on PC:** `SF64TEST.TXT` exists at the card root and contains the
   exact 21 bytes:
   ```
   phase2 round-trip ok
   ```
   (followed by a single newline; trailing NUL is not written).

   On macOS:
   ```bash
   cat /Volumes/<your-sd-volume>/SF64TEST.TXT
   wc -c /Volumes/<your-sd-volume>/SF64TEST.TXT   # should print 21
   ```

7. **Optional cleanup:** delete `SF64TEST.TXT` from the SD before reusing
   the card for normal play.

## PASS criteria

All four conditions must hold:

- [iodev] line: `cart=1 sd_init=0`
- T7 mount: `fatfs_mount=0`
- T8 write: `fatfs_write=0 bytes_written=21`
- T9 read: `fatfs_read=0 bytes_read=21 match=1`
- File visible on PC at `/SF64TEST.TXT` with exact content `phase2 round-trip ok\n` (21 bytes)

## Failure modes

| Symptom | Likely cause | Fix |
|---|---|---|
| `cart=0 sd_init=-2` | SC64 not detected / register interface locked | Confirm Phase 1a verification ran clean; if regressed, see commit ad9c59a |
| `T7 fatfs_mount=13` (FR_NO_FILESYSTEM) | Card isn't FAT formatted, or has unusual partition layout | Reformat SD as FAT32 on PC (the test image-creation in `lib/test/Makefile` is a known-good reference) |
| `T7 fatfs_mount=1` (FR_DISK_ERR) | Underlying disk_read failed | Suspect SD I/O regression; first check sd_init worked. Diskio bounce-buffer for misaligned FATFS.win[] is in place (lib/fatfs/diskio.c) -- if missing, regressed |
| `T8 fatfs_write=4` (FR_DENIED) | Write-protect lock on SD, or card is full | Check the physical lock tab; check `df` on PC before reformatting |
| `T9 match=0` | Write went through but read returned different bytes | Diskio chunking bug or FAT cluster allocation issue. Run `make lib-test` -- the host smoke test is the authoritative repro |
| File missing on PC after eject | Write reported OK but didn't flush | iodev's writes are synchronous so this shouldn't happen on SC64. If it does, suspect iodev_sc64 cache writeback path |

## Reporting back

Paste the captured Terminal A output and confirm whether `SF64TEST.TXT` is
present + correctly sized + correct content on the PC. If anything is off,
note SC64 firmware version (`sc64deployer info`) and SD card details (size,
filesystem, format date).

## After verification passes

The probe gating (`#ifdef IODEV_DIAG_FATFS`) means the production ROM
(`make practice`) does NOT include the FatFs probe call. The probe code
lives in `src/practice/practice_test_fatfs.c` and stays in the tree
permanently as documentation + future regression coverage; future hardware
verification runs can re-enable it just by setting the build flag.

The `Practice_TestFatfs` declaration in `include/practice.h` is similarly
gated, so the production ROM has zero overhead from this work.
