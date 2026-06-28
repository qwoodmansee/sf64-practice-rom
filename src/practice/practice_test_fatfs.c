/* Phase 2 hardware verification probe: FatFs round-trip on real SC64.
 *
 * Build with: make practice IODEV_DIAG_FATFS=1
 *
 * On boot, after iodev_detect/sd_init have run, this helper:
 *   T7:  f_mount the SD card
 *   T8:  write SF64TEST.TXT to the root with a known string
 *   T9:  read it back, verify content matches
 *   T10: write a temp file, close it
 *   T11: f_rename the temp file over the destination
 *   T12: re-open the destination, read it back, verify content matches
 *   T13: report the metadata write-verify-retry diagnostics
 *
 * T10-T13 reproduce the exact temp-file + rename pattern slot_manager uses
 * for real saves -- the path where the SC64 metadata-durability bug surfaced
 * as f_rename -> FR_NO_FILE (diagnostic "444"). They double as the HIL SD
 * save/load regression fixture (see tests/hil/test_sd_save_load.py): a green
 * "[diag-fatfs] RENAME-ROUNDTRIP PASS" line proves the diskio.c metadata
 * write-verify-retry fix keeps directory writes durable on real hardware.
 *
 * Output goes to IS-Viewer (Terminal A running `sc64deployer debug --isv`).
 * IMPORTANT: each result line is emitted via a SINGLE osSyncPrintf call.
 * Phase 1a hardware verification proved that rapid-fire small osSyncPrintf
 * calls after SD command flow can race the SC64 firmware's IS-Viewer poller
 * and trigger the "wp<rp wrap interpreted as 64 KiB of garbage to USB"
 * failure mode documented in CLAUDE.md.
 *
 * REMOVE this file (or simply build without IODEV_DIAG_FATFS=1) once
 * verification passes -- it writes a real file to the user's SD card. */

#include "practice.h"

#ifdef PRACTICE_ROM
#ifdef IODEV_DIAG_FATFS

#include "fatfs/ff.h"
#include "fatfs/diskio.h"

#define SD_SELF_TMP  "0:/SF64SELF.TMP"
#define SD_SELF_DAT  "0:/SF64SELF.DAT"

/* Static buffers so we don't pressure stack. FATFS is ~576 bytes; FIL is
 * larger. Single-threaded use means file-static is safe. */
static FATFS sFatfs;
static FIL   sFile;
static char  sReadBuf[64];

void Practice_TestFatfs(void) {
    FRESULT fr;
    FRESULT fr_close, fr_rename;
    UINT bw, br;
    static const char marker_text[] = "phase2 round-trip ok\n";
    static const char self_text[]   = "sd selftest rename roundtrip\n";
    UINT marker_len = (UINT)(sizeof(marker_text) - 1);  /* exclude trailing NUL */
    UINT self_len   = (UINT)(sizeof(self_text) - 1);
    s32 i;
    int content_match;
    int self_match;

    osSyncPrintf("\n[diag-fatfs] === Phase 2 hardware verification ===\n");
    osSyncPrintf("[diag-fatfs] WARNING: writes SF64TEST.TXT to your SD card root.\n");

    fr = f_mount(&sFatfs, "0:", 1);
    osSyncPrintf("[diag-fatfs] T7 fatfs_mount=%d (expect 0=FR_OK)\n", (int)fr);
    if (fr != FR_OK) {
        osSyncPrintf("[diag-fatfs] FAIL T7: stop. Card not FAT32-formatted, or SD I/O broken.\n");
        return;
    }

    bw = 0;
    fr = f_open(&sFile, "0:/SF64TEST.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        fr = f_write(&sFile, marker_text, marker_len, &bw);
        f_close(&sFile);
    }
    osSyncPrintf("[diag-fatfs] T8 fatfs_write=%d bytes_written=%u (expect 0, %u)\n",
                 (int)fr, (unsigned)bw, (unsigned)marker_len);
    if (fr != FR_OK || bw != marker_len) {
        osSyncPrintf("[diag-fatfs] FAIL T8: stop. Write incomplete or errored.\n");
        f_unmount("0:");
        return;
    }

    /* Zero the read buffer so partial reads are visible. */
    for (i = 0; i < (s32)sizeof(sReadBuf); i++) {
        sReadBuf[i] = 0;
    }
    br = 0;

    fr = f_open(&sFile, "0:/SF64TEST.TXT", FA_READ);
    if (fr == FR_OK) {
        fr = f_read(&sFile, sReadBuf, sizeof(sReadBuf) - 1, &br);
        f_close(&sFile);
        /* Force NUL terminator inside the buffer for the %s print below. */
        if (br < sizeof(sReadBuf)) {
            sReadBuf[br] = '\0';
        } else {
            sReadBuf[sizeof(sReadBuf) - 1] = '\0';
        }
    }

    /* Compare bytes manually (no memcmp in our libc) */
    content_match = (br == marker_len) ? 1 : 0;
    if (content_match) {
        for (i = 0; i < (s32)marker_len; i++) {
            if (sReadBuf[i] != marker_text[i]) {
                content_match = 0;
                break;
            }
        }
    }

    osSyncPrintf("[diag-fatfs] T9 fatfs_read=%d bytes_read=%u match=%d (expect 0, %u, 1)\n",
                 (int)fr, (unsigned)br, content_match, (unsigned)marker_len);

    /* --- T10-T13: temp-file + rename durability round-trip ---------------
     * This is the path that reproduced the SC64 metadata-durability bug.
     * Reset the verify diagnostics first so any retry we report below is
     * attributable to THIS round-trip, not the boot-time writes above. */
    disk_verify_reset();

    /* Clean slate: ignore FR_NO_FILE (nothing to remove on first run). */
    (void)f_unlink(SD_SELF_DAT);
    (void)f_unlink(SD_SELF_TMP);

    bw = 0;
    fr_close = FR_OK;
    fr = f_open(&sFile, SD_SELF_TMP, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        fr = f_write(&sFile, self_text, self_len, &bw);
        fr_close = f_close(&sFile);  /* capture: a dropped close hid the bug before */
    }
    osSyncPrintf("[diag-fatfs] T10 tmp_write=%d close=%d bytes=%u (expect 0, 0, %u)\n",
                 (int)fr, (int)fr_close, (unsigned)bw, (unsigned)self_len);
    if (fr != FR_OK || fr_close != FR_OK || bw != self_len) {
        osSyncPrintf("[diag-fatfs] RENAME-ROUNDTRIP FAIL T10: temp write failed.\n");
        f_unmount("0:");
        osSyncPrintf("[diag-fatfs] === DONE ===\n");
        return;
    }

    /* T11: the rename that used to fail FR_NO_FILE when the tmp's directory
     * entry never became durable. With the verify-retry fix it must be FR_OK. */
    fr_rename = f_rename(SD_SELF_TMP, SD_SELF_DAT);
    osSyncPrintf("[diag-fatfs] T11 rename=%d (expect 0=FR_OK)\n", (int)fr_rename);
    if (fr_rename != FR_OK) {
        osSyncPrintf("[diag-fatfs] RENAME-ROUNDTRIP FAIL T11: rename=%d "
                     "(verify off=%d wrote=%d read=%d).\n",
                     (int)fr_rename, disk_last_verify_off(),
                     disk_last_verify_wrote(), disk_last_verify_read());
        f_unmount("0:");
        osSyncPrintf("[diag-fatfs] === DONE ===\n");
        return;
    }

    /* T12: read the renamed file back and verify its contents survived. */
    for (i = 0; i < (s32)sizeof(sReadBuf); i++) {
        sReadBuf[i] = 0;
    }
    br = 0;
    fr = f_open(&sFile, SD_SELF_DAT, FA_READ);
    if (fr == FR_OK) {
        fr = f_read(&sFile, sReadBuf, sizeof(sReadBuf) - 1, &br);
        f_close(&sFile);
    }
    self_match = (br == self_len) ? 1 : 0;
    if (self_match) {
        for (i = 0; i < (s32)self_len; i++) {
            if (sReadBuf[i] != self_text[i]) {
                self_match = 0;
                break;
            }
        }
    }
    osSyncPrintf("[diag-fatfs] T12 read_back=%d bytes=%u match=%d (expect 0, %u, 1)\n",
                 (int)fr, (unsigned)br, self_match, (unsigned)self_len);

    /* T13: report whether the metadata write-verify-retry path engaged.
     * off == -1 means every directory/FAT write landed first try; off >= 0
     * means a transient drop was caught and retried (still a PASS as long as
     * T11/T12 succeeded -- the fix recovered it). */
    osSyncPrintf("[diag-fatfs] T13 verify_retry off=%d wrote=%d read=%d "
                 "(off=-1 means clean first-try)\n",
                 disk_last_verify_off(), disk_last_verify_wrote(),
                 disk_last_verify_read());

    /* Clean up the test artifact so repeated boots start fresh. */
    (void)f_unlink(SD_SELF_DAT);

    if (fr == FR_OK && self_match) {
        osSyncPrintf("[diag-fatfs] RENAME-ROUNDTRIP PASS\n");
    } else {
        osSyncPrintf("[diag-fatfs] RENAME-ROUNDTRIP FAIL T12: read-back mismatch.\n");
    }

    f_unmount("0:");

    osSyncPrintf("[diag-fatfs] === DONE ===\n");
}

#endif /* IODEV_DIAG_FATFS */
#endif /* PRACTICE_ROM */
