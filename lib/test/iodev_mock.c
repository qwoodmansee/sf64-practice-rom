/* Mock iodev backend for host unit tests.
 *
 * Backs SD I/O onto a host file. Tests open a FAT32 image at startup via
 * iodev_mock_set_image(). FatFs (or test code calling iodev directly)
 * then reads/writes through the iodev API and the operations land in
 * the host file via pread/pwrite.
 *
 * The mock enforces the same alignment + 128-sector cap as the real
 * iodev backends so chunking bugs in diskio.c (or callers) surface on
 * the host instead of waiting for hardware. */

#include "iodev/iodev.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int sMockFd = -1;

/* Cached result of the most-recent iodev_sd_init(), mirroring the real
 * lib/iodev/iodev.c semantics so diskio.c's lazy-init path behaves
 * identically on the host. -99 = never attempted. */
static int sMockInitResult = -99;

/* Non-durable-write fault injection. Models the observed SC64 symptom where a
 * single-sector metadata write returns a clean ACK but never becomes readable
 * (see diskio.c's META_WRITE_RETRIES note). When armed, the next sDropWrites
 * writes targeting LBA sDropLba report IODEV_OK *without* actually persisting,
 * so a subsequent read-back returns the stale contents -- exactly what
 * diskio.c's verify-retry loop is built to catch. */
static uint32_t sDropLba = 0;
static int      sDropWrites = 0;

void iodev_mock_drop_writes(uint32_t lba, int count) {
    sDropLba = lba;
    sDropWrites = count;
}

void iodev_mock_set_image(const char *path) {
    if (sMockFd >= 0) {
        close(sMockFd);
        sMockFd = -1;
    }
    sMockInitResult = -99;  /* fresh "card" -> re-run the lazy init path */
    sMockFd = open(path, O_RDWR);
    if (sMockFd < 0) {
        fprintf(stderr, "iodev_mock_set_image('%s'): %s\n", path, strerror(errno));
        exit(1);
    }
}

void iodev_mock_close(void) {
    if (sMockFd >= 0) {
        close(sMockFd);
        sMockFd = -1;
    }
    sMockInitResult = -99;
}

iodev_id_t iodev_detect(void) {
    return (sMockFd >= 0) ? IODEV_SC64 : IODEV_NONE;
}

iodev_result_t iodev_sd_init(void) {
    sMockInitResult = (sMockFd >= 0) ? IODEV_OK : IODEV_ERR_NO_DEVICE;
    return (iodev_result_t)sMockInitResult;
}

int iodev_sd_was_ok(void) {
    return (sMockInitResult == IODEV_OK);
}

int iodev_sd_init_result(void) {
    return sMockInitResult;
}

iodev_result_t iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    ssize_t n;
    off_t off;

    if (sMockFd < 0) return IODEV_ERR_NO_DEVICE;
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u) return IODEV_ERR_PARAM;
    if (count > 128) return IODEV_ERR_PARAM;

    off = (off_t)lba * 512;
    n = pread(sMockFd, buf, (size_t)count * 512, off);
    if (n != (ssize_t)((size_t)count * 512)) {
        return IODEV_ERR_IO;
    }
    return IODEV_OK;
}

iodev_result_t iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    ssize_t n;
    off_t off;

    if (sMockFd < 0) return IODEV_ERR_NO_DEVICE;
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u) return IODEV_ERR_PARAM;
    if (count > 128) return IODEV_ERR_PARAM;

    /* Fault injection: ACK but don't persist (non-durable write). */
    if (sDropWrites > 0 && lba == sDropLba) {
        sDropWrites--;
        return IODEV_OK;
    }

    off = (off_t)lba * 512;
    n = pwrite(sMockFd, buf, (size_t)count * 512, off);
    if (n != (ssize_t)((size_t)count * 512)) {
        return IODEV_ERR_IO;
    }
    return IODEV_OK;
}
