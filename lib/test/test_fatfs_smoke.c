/* Host integration test for FatFs.
 *
 * Mounts a FAT32 image on the mock iodev, writes 50 small files, reads them
 * back, lists the directory, unmounts. Exercises diskio + ff.c end-to-end.
 *
 * Filenames: TEST_NN.BIN (8.3 SFN clean to avoid LFN edge cases in this
 * smoke test). Contents: 256 bytes of (NN ^ 0x5A). */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fatfs/ff.h"

extern void iodev_mock_set_image(const char *path);
extern void iodev_mock_close(void);

static int failures = 0;

static void check(int cond, const char *label) {
    if (cond) {
        printf("PASS: %s\n", label);
    } else {
        printf("FAIL: %s\n", label);
        failures++;
    }
}

int main(int argc, char **argv) {
    static FATFS fs;
    static FIL fp;
    FRESULT fr;
    UINT bw, br;
    char path[64];
    char wbuf[256];
    char rbuf[256];
    int i;

    iodev_mock_set_image((argc > 1) ? argv[1] : "/tmp/test_diskio.img");

    fr = f_mount(&fs, "0:", 1);
    check(fr == FR_OK, "f_mount /0:");
    if (fr != FR_OK) {
        printf("f_mount returned FRESULT=%d; aborting\n", (int)fr);
        return 1;
    }

    /* Write 50 files. */
    for (i = 0; i < 50; i++) {
        snprintf(path, sizeof(path), "0:/TEST_%02d.BIN", i);
        fr = f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr != FR_OK) {
            printf("FAIL: f_open write %s -> %d\n", path, (int)fr);
            failures++;
            continue;
        }
        memset(wbuf, (i ^ 0x5A) & 0xFF, sizeof(wbuf));
        fr = f_write(&fp, wbuf, sizeof(wbuf), &bw);
        if (fr != FR_OK || bw != sizeof(wbuf)) {
            printf("FAIL: f_write %s -> fr=%d bw=%u\n", path, (int)fr, (unsigned)bw);
            failures++;
        }
        f_close(&fp);
    }
    if (failures == 0) printf("PASS: 50 files written\n");

    /* Read 50 files back, verify content. */
    {
        int read_failures = 0;
        for (i = 0; i < 50; i++) {
            snprintf(path, sizeof(path), "0:/TEST_%02d.BIN", i);
            fr = f_open(&fp, path, FA_READ);
            if (fr != FR_OK) {
                printf("FAIL: f_open read %s -> %d\n", path, (int)fr);
                read_failures++;
                continue;
            }
            fr = f_read(&fp, rbuf, sizeof(rbuf), &br);
            f_close(&fp);
            if (fr != FR_OK || br != sizeof(rbuf)) {
                printf("FAIL: f_read %s -> fr=%d br=%u\n", path, (int)fr, (unsigned)br);
                read_failures++;
                continue;
            }
            memset(wbuf, (i ^ 0x5A) & 0xFF, sizeof(wbuf));
            if (memcmp(rbuf, wbuf, sizeof(rbuf)) != 0) {
                printf("FAIL: %s content mismatch\n", path);
                read_failures++;
            }
        }
        if (read_failures == 0) {
            printf("PASS: 50-file round-trip (content matches)\n");
        } else {
            failures += read_failures;
        }
    }

    /* List directory; expect at least 50 entries (test files). */
    {
        DIR dp;
        FILINFO fi;
        int dir_count = 0;
        fr = f_opendir(&dp, "0:/");
        check(fr == FR_OK, "f_opendir /");
        if (fr == FR_OK) {
            while (f_readdir(&dp, &fi) == FR_OK && fi.fname[0] != '\0') {
                dir_count++;
            }
            f_closedir(&dp);
        }
        check(dir_count >= 50, "dir_count >= 50");
    }

    /* Re-open one file and check size. */
    {
        FILINFO fi;
        fr = f_stat("0:/TEST_25.BIN", &fi);
        check(fr == FR_OK, "f_stat TEST_25.BIN");
        check((unsigned long)fi.fsize == 256ul, "TEST_25.BIN size = 256");
    }

    /* Unmount. */
    fr = f_unmount("0:");
    check(fr == FR_OK, "f_unmount /0:");

    iodev_mock_close();

    if (failures > 0) {
        printf("\n%d test(s) failed\n", failures);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}
