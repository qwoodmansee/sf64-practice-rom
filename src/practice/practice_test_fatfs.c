/* Phase 2 hardware verification probe: FatFs round-trip on real SC64.
 *
 * Build with: make practice IODEV_DIAG_FATFS=1
 *
 * On boot, after iodev_detect/sd_init have run, this helper:
 *   T7: f_mount the SD card
 *   T8: write SF64TEST.TXT to the root with a known string
 *   T9: read it back, verify content matches
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

/* Static buffers so we don't pressure stack. FATFS is ~576 bytes; FIL is
 * larger. Single-threaded use means file-static is safe. */
static FATFS sFatfs;
static FIL   sFile;
static char  sReadBuf[64];

void Practice_TestFatfs(void) {
    FRESULT fr;
    UINT bw, br;
    static const char marker_text[] = "phase2 round-trip ok\n";
    UINT marker_len = (UINT)(sizeof(marker_text) - 1);  /* exclude trailing NUL */
    s32 i;
    int content_match;

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

    f_unmount("0:");

    osSyncPrintf("[diag-fatfs] === DONE ===\n");
}

#endif /* IODEV_DIAG_FATFS */
#endif /* PRACTICE_ROM */
