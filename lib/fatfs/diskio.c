/* FatFs disk I/O glue layer for the SF64 practice ROM iodev abstraction.
 *
 * FatFs's ff.c calls these disk_* functions to access the underlying
 * storage. We dispatch them to Phase 1a's iodev_sd_* primitives, which in
 * turn route to the SC64 or ED64 backend depending on which flashcart
 * was detected at boot.
 *
 * iodev caps reads/writes per call at its DMA buffer size (the SC64 backend
 * uses the 8 KiB data buffer == 16 sectors). FatFs may request larger
 * transfers, so we chunk them here at IODEV_MAX_CHUNK_SECTORS. */
#define IODEV_MAX_CHUNK_SECTORS 16u

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

/* Read-back verify buffer for the metadata write path (see disk_write). */
static uint64_t sVerifyWords[64];
#define sVerify ((unsigned char *) sVerifyWords)

/* Metadata write durability: the SC64 SD path has been observed to ACK a
 * single-sector (directory/FAT) write that never becomes readable -- the
 * hardware symptom was f_rename returning FR_NO_FILE for a file whose
 * create+write+close all reported success (diagnostic code "444"). The
 * firmware's SD_WRITE is synchronous (it waits for CPU_BUSY to clear), so a
 * clean ACK is not proof of persistence. For the small, scattered metadata
 * sectors we therefore read each one back and compare; a mismatch means the
 * write did not land and we retry. Bulk file data (the aligned fast path
 * below) is left unverified to keep saves fast. */
#define META_WRITE_RETRIES 5

/* Classify a metadata verify failure: first differing byte offset, the byte we
 * wrote there, the byte that read back, and the byte that read back at the
 * adjacent (byte-swapped) offset. Lets the caller show it on screen and tell
 * apart: R==0 (write didn't persist / erased to 0), R==255 (erased), S==W
 * (16-bit byte-swap on read), else random corruption. -1 offset = none. */
static int sVfyOff = -1, sVfyW = 0, sVfyR = 0, sVfyS = 0;
int disk_last_verify_off(void)   { return sVfyOff; }
int disk_last_verify_wrote(void) { return sVfyW; }
int disk_last_verify_read(void)  { return sVfyR; }
int disk_last_verify_swap(void)  { return sVfyS; }

/* Clear the metadata-verify diagnostics so a caller can attribute any
 * subsequent retry to a specific operation (used by the SD self-test fixture
 * to isolate its rename round-trip from boot-time directory writes). */
void disk_verify_reset(void) { sVfyOff = -1; sVfyW = 0; sVfyR = 0; sVfyS = 0; }

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

    /* Lazy SD init. iodev_sd_init() is no longer called at boot (it wedges
     * the SC64 firmware on cold boot when audio thread's first PI DMAs
     * race for the bus). Instead, we attempt it once here on first f_open.
     * iodev_sd_init_result() == -99 means "never attempted"; after the
     * attempt, the result is cached and we don't re-issue SD_OP_INIT on
     * subsequent f_opens (which would stall up to 6s on a failing card). */
    if (iodev_sd_init_result() == -99) {
        (void)iodev_sd_init();
    }
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

    /* Aligned: chunk at iodev's per-call cap and drive the device direct. */
    while (count > 0) {
        chunk = (count > IODEV_MAX_CHUNK_SECTORS) ? IODEV_MAX_CHUNK_SECTORS : count;
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
        /* Misaligned caller buffer == FATFS.win[]/FIL.buf[] == directory/FAT
         * metadata. Bounce one sector at a time AND verify each landed (see
         * META_WRITE_RETRIES note above). */
        while (count > 0) {
            UINT attempt;
            memcpy(sBounce, buff, 512);
            r = IODEV_ERR_IO;
            for (attempt = 0; attempt < META_WRITE_RETRIES; attempt++) {
                r = iodev_sd_write_sectors((uint32_t)sector, 1, sBounce);
                if (r != IODEV_OK) continue;  /* hard write error -> retry */
                /* Confirm persistence by reading the sector back. */
                r = iodev_sd_read_sectors((uint32_t)sector, 1, sVerify);
                if (r != IODEV_OK) continue;  /* read error -> retry */
                if (memcmp(sVerify, sBounce, 512) == 0) break;  /* durable */
                /* Mismatch: capture the first differing byte for diagnosis,
                 * then retry the write. */
                {
                    int k;
                    for (k = 0; k < 512; k++) {
                        if (sBounce[k] != sVerify[k]) {
                            sVfyOff = k;
                            sVfyW   = sBounce[k];
                            sVfyR   = sVerify[k];
                            sVfyS   = sVerify[k ^ 1];  /* swapped-position byte */
                            break;
                        }
                    }
                }
                r = IODEV_ERR_IO;             /* mismatch -> retry the write */
            }
            if (r != IODEV_OK) return iodev_to_dresult(r);
            sector++;
            buff   += 512;
            count--;
        }
        return RES_OK;
    }

    /* Aligned: chunk at iodev's per-call cap and drive the device direct. */
    while (count > 0) {
        chunk = (count > IODEV_MAX_CHUNK_SECTORS) ? IODEV_MAX_CHUNK_SECTORS : count;
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
