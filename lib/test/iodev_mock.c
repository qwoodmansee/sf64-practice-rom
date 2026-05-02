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

void iodev_mock_set_image(const char *path) {
    if (sMockFd >= 0) {
        close(sMockFd);
        sMockFd = -1;
    }
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
}

iodev_id_t iodev_detect(void) {
    return (sMockFd >= 0) ? IODEV_SC64 : IODEV_NONE;
}

iodev_result_t iodev_sd_init(void) {
    return (sMockFd >= 0) ? IODEV_OK : IODEV_ERR_NO_DEVICE;
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

    off = (off_t)lba * 512;
    n = pwrite(sMockFd, buf, (size_t)count * 512, off);
    if (n != (ssize_t)((size_t)count * 512)) {
        return IODEV_ERR_IO;
    }
    return IODEV_OK;
}
