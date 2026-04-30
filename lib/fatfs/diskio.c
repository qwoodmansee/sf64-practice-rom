/* FatFs disk I/O glue layer for the SF64 practice ROM iodev abstraction.
 *
 * FatFs's ff.c calls these disk_* functions to access the underlying
 * storage. We dispatch them to Phase 1a's iodev_sd_* primitives, which in
 * turn route to the SC64 or ED64 backend depending on which flashcart
 * was detected at boot.
 *
 * iodev caps reads/writes at 128 sectors per call (the SC64 backend's
 * 64 KiB DMA scratch limit). FatFs may request larger transfers, so we
 * chunk them here. */

#include "ff.h"
#include "diskio.h"
#include "string.h"
#include "iodev/iodev.h"

/* FatFs has only one volume in our config (FF_VOLUMES=1); pdrv is always 0. */
#define VOL_SD 0

/* Track init state so disk_status returns sane values pre-init. */
static int sFatfsDiskInited = 0;

/* 512-byte 8-byte-aligned bounce buffer for misaligned caller buffers.
 *
 * iodev_sd_*'s API requires 8-byte alignment (PI DMA constraint). FatFs's
 * internal sector window FATFS.win[] lands at offset 0x44 inside FATFS
 * struct -- 4-byte aligned but NOT 8-byte aligned. Same for FIL.buf[].
 * Any caller passing those into disk_read/disk_write would be rejected
 * with IODEV_ERR_PARAM, mapped here to RES_PARERR, mapped by FatFs to
 * FR_DISK_ERR -- a confusing failure mode for a real config issue.
 *
 * When buf is misaligned we copy through this bounce buffer one sector
 * at a time. Cost: an extra 512-byte memcpy per misaligned sector. The
 * practice ROM is single-threaded (FF_FS_REENTRANT=0) so a file-static
 * bounce is safe; a multi-threaded port would need to put this on the
 * caller's stack or per-volume.
 *
 * The Phase 2 plan's risk register documented this and offered two fixes:
 * (a) document alignment in iodev's contract and trust callers, or
 * (b) bounce-buffer in diskio.c when alignment check fails. We chose (b)
 * because (a) would require FatFs callers to know about iodev's internal
 * DMA constraint -- a leaky abstraction.
 *
 * Type chosen to guarantee 8-byte alignment on both IDO and host: uint64_t
 * has natural 8-byte alignment under MIPS3 ABI and any modern host ABI,
 * so no compiler-specific __attribute__((aligned)) is needed (which IDO
 * would silently no-op via the project's macros.h __attribute__ stub). */
static uint64_t sBounceWords[64];   /* 64 * 8 = 512 bytes */
#define sBounce ((unsigned char *) sBounceWords)

#define IS_ALIGNED8(p) ((((uintptr_t)(const void *)(p)) & 7u) == 0)

/* Map iodev_result_t to FatFs's DRESULT. */
static DRESULT iodev_to_dresult(iodev_result_t r) {
    switch (r) {
        case IODEV_OK:           return RES_OK;
        case IODEV_ERR_PARAM:    return RES_PARERR;
        case IODEV_ERR_NO_DEVICE:
        case IODEV_ERR_NO_CARD:  return RES_NOTRDY;
        default:                 return RES_ERROR;  /* IO, TIMEOUT, etc. */
    }
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != VOL_SD) return STA_NOINIT | STA_NODISK;
    if (!sFatfsDiskInited) return STA_NOINIT;
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != VOL_SD) return STA_NOINIT | STA_NODISK;
    if (sFatfsDiskInited) return 0;

    /* Use the cached result from Practice_Init's iodev_sd_init() call.
     * Avoids re-issuing SC64_CMD_SD_CARD_OP / SD_OP_INIT on every f_open,
     * which can stall the game thread for up to 6 seconds on a failing card. */
    if (!iodev_sd_was_ok()) {
        return STA_NOINIT;
    }
    sFatfsDiskInited = 1;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    UINT chunk;
    iodev_result_t r;

    if (pdrv != VOL_SD) return RES_PARERR;
    if (!sFatfsDiskInited) return RES_NOTRDY;
    if (!buff) return RES_PARERR;

    if (!IS_ALIGNED8(buff)) {
        /* Misaligned caller buffer (typically FATFS.win[] or FIL.buf[]).
         * Bounce one sector at a time through our 8-byte-aligned scratch. */
        while (count > 0) {
            r = iodev_sd_read_sectors((uint32_t)sector, 1, sBounce);
            if (r != IODEV_OK) return iodev_to_dresult(r);
            memcpy(buff, sBounce, 512);
            sector++;
            buff   += 512;
            count--;
        }
        return RES_OK;
    }

    /* Aligned: chunk at iodev's 128-sector cap and drive the device direct. */
    while (count > 0) {
        chunk = (count > 128u) ? 128u : count;
        r = iodev_sd_read_sectors((uint32_t)sector, (uint32_t)chunk, buff);
        if (r != IODEV_OK) return iodev_to_dresult(r);
        sector += chunk;
        buff   += chunk * 512u;
        count  -= chunk;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    UINT chunk;
    iodev_result_t r;

    if (pdrv != VOL_SD) return RES_PARERR;
    if (!sFatfsDiskInited) return RES_NOTRDY;
    if (!buff) return RES_PARERR;

    if (!IS_ALIGNED8(buff)) {
        /* Misaligned caller buffer; bounce one sector at a time. */
        while (count > 0) {
            memcpy(sBounce, buff, 512);
            r = iodev_sd_write_sectors((uint32_t)sector, 1, sBounce);
            if (r != IODEV_OK) return iodev_to_dresult(r);
            sector++;
            buff   += 512;
            count--;
        }
        return RES_OK;
    }

    /* Aligned: chunk at iodev's 128-sector cap and drive the device direct. */
    while (count > 0) {
        chunk = (count > 128u) ? 128u : count;
        r = iodev_sd_write_sectors((uint32_t)sector, (uint32_t)chunk, buff);
        if (r != IODEV_OK) return iodev_to_dresult(r);
        sector += chunk;
        buff   += chunk * 512u;
        count  -= chunk;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != VOL_SD) return RES_PARERR;
    if (!sFatfsDiskInited) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            /* iodev writes are synchronous; nothing to flush. */
            return RES_OK;

        case GET_SECTOR_SIZE:
            if (buff) *(WORD *)buff = 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            /* FatFs uses this for erase-block alignment when formatting.
             * We don't format (FF_USE_MKFS=0), so 1 is a safe placeholder. */
            if (buff) *(DWORD *)buff = 1;
            return RES_OK;

        case GET_SECTOR_COUNT:
            /* TODO: replace with iodev's CMD9/CSD-derived capacity helper
             * when Phase 1c (deferred ED64 work) ships its diag mode, OR
             * add a minimal capacity probe to lib/iodev. For now, return
             * a conservative 32 GiB cap that works for any FAT32-formatted
             * card up to 32 GiB. FatFs uses this field for write-bounds
             * checking and partition validation; an over-estimate causes
             * read/write past end of card to return IODEV_ERR_IO from
             * iodev_sd_*, which propagates to FatFs as RES_ERROR.
             * Acceptable for v1; tighten if it bites.
             *
             * 32 GiB / 512 = 67108864 sectors. */
            if (buff) *(LBA_t *)buff = 67108864UL;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
