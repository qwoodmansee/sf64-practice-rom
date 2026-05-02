/* lib/test/diskio_ram.c - RAM-backed FatFs diskio for host unit tests.
 * Provides a 512 KB virtual disk. Link this INSTEAD OF lib/fatfs/diskio.c
 * in test binaries that need working FatFs I/O.
 *
 * diskio_ram_init() zeroes the RAM disk.
 * diskio_ram_format() writes a minimal FAT16 BPB + FAT so that FatFs's
 * f_mount() succeeds immediately — no f_mkfs() call needed (FF_USE_MKFS=0). */

#include "../fatfs/ff.h"
#include "../fatfs/diskio.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Geometry: 512 KB disk, 512-byte sectors, FAT16.
 *
 *   Total sectors  : 1024
 *   Reserved sects : 4    (sectors 0-3; sector 0 = BPB/boot sector)
 *   FAT copies     : 1
 *   Sectors per FAT: 2    (2 * 256 entries * 2 bytes = 1024 bytes = 2 sectors)
 *   Root dir sects : 4    (4 * 512 / 32 = 64 entries)
 *   Sectors/cluster: 4    → 32 clusters of 2KB for data
 *   Data area start: sector 10 (4 reserved + 2 FAT + 4 root)
 * ------------------------------------------------------------------------- */

#define RAM_DISK_SECTORS   1024u
#define SECTOR_SIZE        512u

#define BPB_SECTOR_SIZE    SECTOR_SIZE
#define BPB_SECTORS_TOTAL  RAM_DISK_SECTORS
#define BPB_RESERVED_SECTS 4u
#define BPB_NUM_FATS       1u
#define BPB_SECTS_PER_FAT  2u
#define BPB_ROOT_ENTRIES   64u
#define BPB_ROOT_SECTS     ((BPB_ROOT_ENTRIES * 32u) / BPB_SECTOR_SIZE)  /* 4 */
#define BPB_SECTS_PER_CLUS 4u

static uint8_t *sDisk = NULL;
static int      sDiskReady = 0;

/* Helper: write a little-endian 16-bit value. */
static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/* Helper: write a little-endian 32-bit value. */
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* diskio_ram_init: allocate (once) and zero the entire disk. */
int diskio_ram_init(void) {
    if (!sDisk) {
        sDisk = (uint8_t *)malloc(RAM_DISK_SECTORS * SECTOR_SIZE);
        if (!sDisk) return -1;
    }
    memset(sDisk, 0, RAM_DISK_SECTORS * SECTOR_SIZE);
    sDiskReady = 0;
    return 0;
}

/* diskio_ram_format: write a minimal FAT16 volume into the zeroed RAM disk
 * so that FatFs can mount it without calling f_mkfs (FF_USE_MKFS=0 is fine).
 *
 * Call this AFTER diskio_ram_init() (which zeroes the disk) and BEFORE
 * disk_initialize() + f_mount(). */
int diskio_ram_format(void) {
    uint8_t *boot;   /* sector 0 */
    uint8_t *fat;    /* sector BPB_RESERVED_SECTS */

    if (!sDisk) return -1;

    /* ---- Boot sector / BPB ---- */
    boot = sDisk; /* sector 0 */
    memset(boot, 0, SECTOR_SIZE);

    /* Jump instruction (x86 tradition, ignored by FatFs but required) */
    boot[0] = 0xEB; boot[1] = 0x58; boot[2] = 0x90;
    /* OEM name */
    memcpy(&boot[3], "MSDOS5.0", 8);

    /* BPB */
    put_le16(&boot[0x0B], (uint16_t)BPB_SECTOR_SIZE);       /* bytes/sector */
    boot[0x0D] = (uint8_t)BPB_SECTS_PER_CLUS;               /* sectors/cluster */
    put_le16(&boot[0x0E], (uint16_t)BPB_RESERVED_SECTS);    /* reserved sectors */
    boot[0x10] = (uint8_t)BPB_NUM_FATS;                     /* number of FATs */
    put_le16(&boot[0x11], (uint16_t)BPB_ROOT_ENTRIES);      /* root dir entries */
    put_le16(&boot[0x13], (uint16_t)BPB_SECTORS_TOTAL);     /* total sectors (16) */
    boot[0x15] = 0xF8;                                       /* media descriptor */
    put_le16(&boot[0x16], (uint16_t)BPB_SECTS_PER_FAT);     /* sectors/FAT */
    put_le16(&boot[0x18], 1);                                /* sectors/track (dummy) */
    put_le16(&boot[0x1A], 1);                                /* number of heads (dummy) */
    put_le32(&boot[0x1C], 0);                                /* hidden sectors */
    put_le32(&boot[0x20], 0);                                /* total sectors (32) = 0 when 16-bit is used */

    /* Extended BPB */
    boot[0x24] = 0x80;   /* drive number */
    boot[0x25] = 0x00;   /* reserved */
    boot[0x26] = 0x29;   /* boot signature */
    put_le32(&boot[0x27], 0x12345678u); /* volume serial */
    memcpy(&boot[0x2B], "RAMTEST    ", 11); /* volume label */
    memcpy(&boot[0x36], "FAT16   ", 8);    /* FS type */

    /* Boot sector signature */
    boot[0x1FE] = 0x55;
    boot[0x1FF] = 0xAA;

    /* ---- FAT16 table ---- */
    /* FAT starts at sector BPB_RESERVED_SECTS. */
    fat = sDisk + BPB_RESERVED_SECTS * SECTOR_SIZE;
    memset(fat, 0, BPB_SECTS_PER_FAT * SECTOR_SIZE);

    /* FAT[0] = media descriptor byte + 0xFF; FAT[1] = end-of-chain marker */
    fat[0] = 0xF8; fat[1] = 0xFF;   /* cluster 0: media type */
    fat[2] = 0xFF; fat[3] = 0xFF;   /* cluster 1: end-of-chain */

    sDiskReady = 0;   /* caller must still call disk_initialize() / f_mount() */
    return 0;
}

/* -------------------------------------------------------------------------
 * FatFs diskio interface
 * ------------------------------------------------------------------------- */

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    sDiskReady = (sDisk != NULL);
    return sDiskReady ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    return sDiskReady ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buf, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!sDiskReady || sector + count > RAM_DISK_SECTORS) return RES_ERROR;
    memcpy(buf, sDisk + sector * SECTOR_SIZE, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buf, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!sDiskReady || sector + count > RAM_DISK_SECTORS) return RES_ERROR;
    memcpy(sDisk + sector * SECTOR_SIZE, buf, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buf) {
    (void)pdrv;
    if (!sDiskReady) return RES_NOTRDY;
    switch (cmd) {
        case CTRL_SYNC:        return RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t *)buf = RAM_DISK_SECTORS; return RES_OK;
        case GET_SECTOR_SIZE:  *(WORD *)buf  = (WORD)SECTOR_SIZE; return RES_OK;
        case GET_BLOCK_SIZE:   *(DWORD *)buf = 1;                return RES_OK;
        default: return RES_PARERR;
    }
}

DWORD get_fattime(void) { return 0; }
