/* Host unit tests for lib/fatfs/diskio.c.
 *
 * Builds with native gcc. Backs SD I/O via lib/test/iodev_mock.c against a
 * host file pre-formatted as FAT32. Exercises diskio's status, init, read/
 * write paths, chunking, error mapping, and the disk_ioctl surface.
 *
 * The test image is auto-created by lib/test/Makefile if it doesn't exist.
 * Default path: /tmp/test_diskio.img (64 MiB FAT32). */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fatfs/ff.h"
#include "fatfs/diskio.h"

extern void iodev_mock_set_image(const char *path);
extern void iodev_mock_close(void);

static int failures = 0;

#define ASSERT_EQ(actual, expected, label) do {                              \
    long long _a = (long long)(actual);                                      \
    long long _e = (long long)(expected);                                    \
    if (_a != _e) {                                                          \
        printf("FAIL: %s: expected %lld, got %lld\n", (label), _e, _a);      \
        failures++;                                                          \
    } else {                                                                 \
        printf("PASS: %s\n", (label));                                       \
    }                                                                        \
} while (0)

int main(int argc, char **argv) {
    /* DMA-style 8-byte alignment matching iodev contract. */
    static BYTE buf[2048] __attribute__((aligned(8)));
    static BYTE big[200 * 512] __attribute__((aligned(8)));
    WORD ssize;
    DWORD bsize;
    LBA_t scount;

    const char *image = (argc > 1) ? argv[1] : "/tmp/test_diskio.img";
    iodev_mock_set_image(image);

    /* T1: pre-init disk_status returns NOINIT. */
    ASSERT_EQ(disk_status(0), STA_NOINIT, "T1 disk_status pre-init = STA_NOINIT");

    /* T2: bad pdrv before init returns NOINIT|NODISK. */
    ASSERT_EQ(disk_status(1), STA_NOINIT | STA_NODISK, "T2 disk_status bad pdrv = NOINIT|NODISK");

    /* T3: disk_initialize succeeds. */
    ASSERT_EQ(disk_initialize(0), 0, "T3 disk_initialize -> 0");

    /* T4: disk_status post-init = 0. */
    ASSERT_EQ(disk_status(0), 0, "T4 disk_status post-init = 0");

    /* T5: read MBR sector 0; FAT32 image always has the 0x55AA sig at 510-511. */
    ASSERT_EQ(disk_read(0, buf, 0, 1), RES_OK, "T5 disk_read sector 0 -> OK");
    ASSERT_EQ((unsigned)buf[510], 0x55, "T5b MBR sig byte 510 = 0x55");
    ASSERT_EQ((unsigned)buf[511], 0xAA, "T5c MBR sig byte 511 = 0xAA");

    /* T6: chunked read across the 128-sector chunking boundary.
     * Read 200 sectors (forces 128 + 72 split). buffer is on BSS, page-aligned. */
    ASSERT_EQ(disk_read(0, big, 0, 200), RES_OK, "T6 disk_read 200 sectors (chunked) -> OK");

    /* T7: read confirms first sector contents match the single-sector read above. */
    ASSERT_EQ((int)memcmp(big, buf, 512), 0, "T7 chunked first sector matches single-sector read");

    /* T8: disk_ioctl GET_SECTOR_SIZE. */
    ASSERT_EQ(disk_ioctl(0, GET_SECTOR_SIZE, &ssize), RES_OK, "T8 ioctl GET_SECTOR_SIZE -> OK");
    ASSERT_EQ((unsigned)ssize, 512u, "T8b GET_SECTOR_SIZE returns 512");

    /* T9: disk_ioctl GET_BLOCK_SIZE. */
    ASSERT_EQ(disk_ioctl(0, GET_BLOCK_SIZE, &bsize), RES_OK, "T9 ioctl GET_BLOCK_SIZE -> OK");
    ASSERT_EQ((unsigned long)bsize, 1ul, "T9b GET_BLOCK_SIZE returns 1 (no mkfs)");

    /* T10: disk_ioctl GET_SECTOR_COUNT (returns conservative 32 GiB cap). */
    ASSERT_EQ(disk_ioctl(0, GET_SECTOR_COUNT, &scount), RES_OK, "T10 ioctl GET_SECTOR_COUNT -> OK");
    ASSERT_EQ((unsigned long)scount, 67108864ul, "T10b GET_SECTOR_COUNT = 32 GiB cap");

    /* T11: disk_ioctl CTRL_SYNC. */
    ASSERT_EQ(disk_ioctl(0, CTRL_SYNC, NULL), RES_OK, "T11 ioctl CTRL_SYNC -> OK");

    /* T12: disk_ioctl unknown command rejects with PARERR. */
    ASSERT_EQ(disk_ioctl(0, 99, NULL), RES_PARERR, "T12 ioctl unknown -> PARERR");

    /* T13: bad pdrv on init. */
    ASSERT_EQ(disk_initialize(1), STA_NOINIT | STA_NODISK, "T13 disk_init pdrv=1 -> NOINIT|NODISK");

    /* T14: bad pdrv on read. */
    ASSERT_EQ(disk_read(1, buf, 0, 1), RES_PARERR, "T14 disk_read pdrv=1 -> PARERR");

    /* T15: write/read round-trip on a high LBA (well past any FS metadata).
     * 64 MiB image / 512 = 131072 sectors; LBA 100000 is safe. */
    {
        static BYTE wbuf[512] __attribute__((aligned(8)));
        static BYTE rbuf[512] __attribute__((aligned(8)));
        int i;
        for (i = 0; i < 512; i++) wbuf[i] = (BYTE)(i ^ 0xC3);
        memset(rbuf, 0, sizeof rbuf);

        ASSERT_EQ(disk_write(0, wbuf, 100000, 1), RES_OK, "T15a disk_write LBA 100000 -> OK");
        ASSERT_EQ(disk_read(0, rbuf, 100000, 1), RES_OK, "T15b disk_read LBA 100000 -> OK");
        ASSERT_EQ((int)memcmp(wbuf, rbuf, 512), 0, "T15c round-trip content matches");
    }

    /* T16: misaligned buffer round-trip exercises the bounce-buffer path.
     * Simulates a caller (like FATFS.win[]) that's only 4-byte aligned.
     * Note storage[2048]: each segment is 512 bytes, write segment at offset 4
     * (4-aligned), read segment at offset 1028 (also 4-aligned). Both segments
     * fit cleanly within the array. */
    {
        static BYTE storage[2048] __attribute__((aligned(8)));
        BYTE *misaligned   = storage + 4;
        BYTE *misaligned_r = storage + 1028;
        int i;

        memset(storage, 0, sizeof storage);
        for (i = 0; i < 512; i++) misaligned[i] = (BYTE)((i + 7) ^ 0xA5);

        ASSERT_EQ(((uintptr_t)misaligned)   & 7u, 4u, "T16 write buffer is intentionally 4-aligned");
        ASSERT_EQ(((uintptr_t)misaligned_r) & 7u, 4u, "T16 read buffer is intentionally 4-aligned");
        ASSERT_EQ(disk_write(0, misaligned,   101000, 1), RES_OK, "T16a misaligned write -> OK");
        ASSERT_EQ(disk_read (0, misaligned_r, 101000, 1), RES_OK, "T16b misaligned read -> OK");
        ASSERT_EQ((int)memcmp(misaligned, misaligned_r, 512), 0, "T16c misaligned round-trip content matches");
    }

    iodev_mock_close();

    if (failures > 0) {
        printf("\n%d test(s) failed\n", failures);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}
