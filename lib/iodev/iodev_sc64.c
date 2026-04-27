/* SC64 flashcart backend.
 *
 * Protocol reference: ~/code/SummerCart64/sw/bootloader/src/sc64.c
 *
 * SC64 register block lives at cart-bus 0x1FFF0000. Commands are sent by
 * writing arguments to DATA[0..1], then writing the command byte to SCR.
 * The CPU_BUSY flag in SCR clears when the command completes; the response
 * (if any) is read back from DATA[0..1].
 *
 * Critical PI gotcha (same as isviewer.c): direct CPU writes to cart space
 * drop after the first few. A dummy IO_READ between writes drains the PI bus.
 *
 * SD I/O works via a cart-bus scratch buffer (SC64_SD_DMA_SCRATCH): for reads,
 * the SC64 firmware fetches sectors from the SD card into the scratch buffer,
 * and the N64 then DMAs them into RDRAM. Writes go the other way.
 */

#include "PR/rcp.h"
#include "libultra/ultra64.h"  /* OSIoMesg, OSMesgQueue, osPiStartDma, osCreateMesgQueue,
                                * osRecvMesg, osInvalDCache, osWritebackDCache.
                                * Path matches the project convention (see src/sys/sys.h). */
#include "iodev.h"
#include "iodev_internal.h"

#define SC64_REGS_BASE    0x1FFF0000UL
#define SC64_REG_SCR      (SC64_REGS_BASE + 0x00)
#define SC64_REG_DATA0    (SC64_REGS_BASE + 0x04)
#define SC64_REG_DATA1    (SC64_REGS_BASE + 0x08)
#define SC64_REG_IDENT    (SC64_REGS_BASE + 0x0C)
#define SC64_REG_KEY      (SC64_REGS_BASE + 0x10)

#define SC64_SCR_CPU_BUSY    (1u << 31)
#define SC64_SCR_CMD_ERROR   (1u << 30)

#define SC64_V2_IDENTIFIER   0x53437632u  /* "SCv2" */

#define SC64_KEY_RESET       0x00000000u
#define SC64_KEY_UNLOCK_1    0x5F554E4Cu
#define SC64_KEY_UNLOCK_2    0x4F434B5Fu

#define PI_WRITE_FLUSH(addr, val) do {            \
    IO_WRITE((addr), (val));                      \
    (void) IO_READ(SC64_REG_IDENT);               \
} while (0)

/* SC64 command IDs (from ~/code/SummerCart64/docs/02_n64_commands.md). */
#define SC64_CMD_SD_CARD_OP     'i'
#define SC64_CMD_SD_SECTOR_SET  'I'
#define SC64_CMD_SD_READ        's'
#define SC64_CMD_SD_WRITE       'S'

/* SD_CARD_OP sub-operations. */
#define SD_OP_DEINIT          0
#define SD_OP_INIT            1
#define SD_OP_GET_STATUS      2
#define SD_OP_GET_INFO        3

/* PI cart-space target for SD DMA buffers.
 *
 * Must point at SC64 SDRAM (the cart-bus ROM region 0x10000000..0x13FE0000),
 * NOT at the SC64 flash-shadow region (0x13FE0000..0x13FFFFFF, where the
 * IS-Viewer at 0x13FF0000 also lives -- overlapping that region is a hardware
 * conflict that would either silently fail or corrupt IS-Viewer logging).
 *
 * SF64's ROM is approximately 10 MiB. We reserve 64 KiB at offset 15 MiB
 * (0x10F00000..0x10F0FFFF), well past the ROM's tail and well below the
 * flash-shadow boundary. Gives us 128 sectors (64 KiB / 512) of buffer
 * space, which caps a single SD R/W call at 128 sectors. */
#define SC64_SD_DMA_SCRATCH   0x10F00000u

/* SC64 command-completion timeout. PI-bus IO_READ is roughly 1 us at
 * worst-case wait states; this gives a multi-second wall-clock upper
 * bound, well past CMD_INIT's ~50ms worst case. */
#define SC64_CMD_TIMEOUT_RETRIES  6000000

/* Execute one command. Args go in arg[0..1], response (if any) lands in rsp[0..1].
 * Returns IODEV_OK on success, IODEV_ERR_IO on CMD_ERROR, IODEV_ERR_TIMEOUT if
 * CPU_BUSY never clears.
 *
 * NOTE: this uses the polling path (no IRQ), matching the bootloader's
 * non-IRQ branch in sc64.c:108-113. */
static iodev_result_t sc64_execute_cmd(uint8_t cmd_id,
                                       uint32_t arg0, uint32_t arg1,
                                       uint32_t *rsp0_out, uint32_t *rsp1_out) {
    int retries;
    uint32_t sr;

    PI_WRITE_FLUSH(SC64_REG_DATA0, arg0);
    PI_WRITE_FLUSH(SC64_REG_DATA1, arg1);
    PI_WRITE_FLUSH(SC64_REG_SCR, (uint32_t)cmd_id);

    /* Spin until CPU_BUSY clears. */
    retries = SC64_CMD_TIMEOUT_RETRIES;
    do {
        sr = IO_READ(SC64_REG_SCR);
        if (--retries <= 0) {
            return IODEV_ERR_TIMEOUT;
        }
    } while (sr & SC64_SCR_CPU_BUSY);

    if (sr & SC64_SCR_CMD_ERROR) {
        /* The error code is in DATA0; we don't translate it for now --
         * caller just gets IODEV_ERR_IO. Per-error logging via osSyncPrintf
         * can be added later if needed. */
        return IODEV_ERR_IO;
    }

    if (rsp0_out) *rsp0_out = IO_READ(SC64_REG_DATA0);
    if (rsp1_out) *rsp1_out = IO_READ(SC64_REG_DATA1);
    return IODEV_OK;
}

static iodev_id_t sc64_detect(void) {
    /* The SC64 unlocks register access after a magic key sequence. We probe
     * by reading IDENT -- even before unlock, IDENT is readable. */
    uint32_t ident = IO_READ(SC64_REG_IDENT);
    if (ident == SC64_V2_IDENTIFIER) {
        /* Found SC64; unlock command interface. */
        PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_RESET);
        PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_UNLOCK_1);
        PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_UNLOCK_2);
        return IODEV_SC64;
    }
    return IODEV_NONE;
}

static iodev_result_t sc64_sd_init(void) {
    return sc64_execute_cmd(SC64_CMD_SD_CARD_OP,
                            0,         /* arg0: pi_address (unused for INIT) */
                            SD_OP_INIT,
                            0, 0);
}

/* SD DMA bookkeeping. File-static because the queue must persist across
 * calls; first call lazily creates it. NOT thread-safe -- callers must
 * not invoke iodev_sd_*_sectors concurrently. (The practice ROM is
 * single-threaded for these calls; revisit if that ever changes.) */
static OSMesgQueue sSc64DmaMq;
static OSMesg      sSc64DmaMsgBuf[1];
static int         sSc64DmaMqInited = 0;

static void sc64_dma_setup(void) {
    if (!sSc64DmaMqInited) {
        osCreateMesgQueue(&sSc64DmaMq, sSc64DmaMsgBuf, 1);
        sSc64DmaMqInited = 1;
    }
}

static iodev_result_t sc64_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    iodev_result_t res;
    OSIoMesg mb;  /* Stack-local OK: __osDevMgrMain finishes touching mb
                   * before osRecvMesg unblocks. */

    if (count == 0 || count > 128) {
        return IODEV_ERR_PARAM;  /* > 128 exceeds our 64 KiB DMA scratch */
    }
    if (((uintptr_t)buf) & 7u) {
        return IODEV_ERR_PARAM;  /* PI DMA needs 8-byte alignment (iodev.h) */
    }

    sc64_dma_setup();

    /* Tell SC64 firmware: read `count` sectors starting at `lba` into our
     * cart-bus scratch buffer. */
    res = sc64_execute_cmd(SC64_CMD_SD_SECTOR_SET, lba, 0, 0, 0);
    if (res != IODEV_OK) return res;
    res = sc64_execute_cmd(SC64_CMD_SD_READ, SC64_SD_DMA_SCRATCH, count, 0, 0);
    if (res != IODEV_OK) return res;

    /* Now DMA cart-bus scratch -> caller's RDRAM buffer.
     * Pattern follows src/sys/sys_lib.c:104-118 (Lib_DmaRead). */
    osInvalDCache(buf, (s32)(count * 512));
    if (osPiStartDma(&mb, 0, OS_READ,
                     SC64_SD_DMA_SCRATCH, buf, count * 512,
                     &sSc64DmaMq) != 0) {
        return IODEV_ERR_IO;
    }
    osRecvMesg(&sSc64DmaMq, NULL, OS_MESG_BLOCK);

    return IODEV_OK;
}

static iodev_result_t sc64_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    iodev_result_t res;
    OSIoMesg mb;  /* Stack-local OK: __osDevMgrMain finishes touching mb
                   * before osRecvMesg unblocks. */

    if (count == 0 || count > 128) {
        return IODEV_ERR_PARAM;
    }
    if (((uintptr_t)buf) & 7u) {
        return IODEV_ERR_PARAM;  /* PI DMA needs 8-byte alignment (iodev.h) */
    }

    sc64_dma_setup();

    /* DMA caller's RDRAM buffer into cart-bus scratch.
     * Pattern: writeback dcache, then osPiStartDma with OS_WRITE direction. */
    osWritebackDCache((void *)buf, (s32)(count * 512));
    if (osPiStartDma(&mb, 0, OS_WRITE,
                     SC64_SD_DMA_SCRATCH, (void *)buf, count * 512,
                     &sSc64DmaMq) != 0) {
        return IODEV_ERR_IO;
    }
    osRecvMesg(&sSc64DmaMq, NULL, OS_MESG_BLOCK);

    /* Tell SC64 firmware to flush scratch -> SD card at `lba`. */
    res = sc64_execute_cmd(SC64_CMD_SD_SECTOR_SET, lba, 0, 0, 0);
    if (res != IODEV_OK) return res;
    res = sc64_execute_cmd(SC64_CMD_SD_WRITE, SC64_SD_DMA_SCRATCH, count, 0, 0);
    return res;
}

/* IDO does not support C99 designated initializers; the order below must
 * track the field order in iodev_backend_t (id, detect, sd_init,
 * sd_read_sectors, sd_write_sectors). */
static const iodev_backend_t SC64_BACKEND = {
    IODEV_SC64,
    sc64_detect,
    sc64_sd_init,
    sc64_sd_read_sectors,
    sc64_sd_write_sectors,
};

const iodev_backend_t *iodev_backend_sc64(void) { return &SC64_BACKEND; }
