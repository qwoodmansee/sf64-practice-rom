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
#include "PR/os_internal.h"     /* __osDisableInt / __osRestoreInt */
#include "libultra/ultra64.h"  /* OSIoMesg, OSMesgQueue, osPiStartDma, osCreateMesgQueue,
                                * osRecvMesg, osInvalDCache, osWritebackDCache.
                                * Path matches the project convention (see src/sys/sys.h). */
#include "piint.h"             /* __osPiGetAccess / __osPiRelAccess / __osPiAccessQueueEnabled */
#include "iodev.h"
#include "iodev_internal.h"

/* Diagnostic breadcrumb: fire the app-registered hook (if any) with a step
 * code. No-op in production (hook NULL). See gIodevBreadcrumb in iodev.h. */
#define BC(code) do { if (gIodevBreadcrumb) gIodevBreadcrumb(code); } while (0)

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

/* Wait for any in-flight PI DMA/IO to finish before raw access.
 * Even with __osPiGetAccess held, the PI hardware can still be servicing
 * a DMA that was queued before we acquired the semaphore. Raw IO_READ/
 * IO_WRITE without this wait can collide with in-flight transfers and
 * wedge the SC64 firmware. Mirrors gz's __pi_wait() in src/gz/pi.h. */
#define PI_WAIT() do {                                                  \
    while (IO_READ(PI_STATUS_REG) & (PI_STATUS_IO_BUSY | PI_STATUS_DMA_BUSY)) \
        ;                                                                \
} while (0)

#define PI_WRITE_FLUSH(addr, val) do {                  \
    PI_WAIT(); IO_WRITE((addr), (val));                 \
    PI_WAIT(); (void) IO_READ(SC64_REG_IDENT);          \
} while (0)

/* SC64 command IDs (from ~/code/SummerCart64/docs/02_n64_commands.md). */
#define SC64_CMD_SD_CARD_OP     'i'
#define SC64_CMD_SD_SECTOR_SET  'I'
#define SC64_CMD_SD_READ        's'
#define SC64_CMD_SD_WRITE       'S'
#define SC64_CMD_CONFIG_SET     'C'   /* arg0=config_id, arg1=new_value */

/* SC64 config IDs (docs/04_config_options.md). */
#define SC64_CFG_ROM_WRITE_ENABLE  1  /* bool, default 0 (ROM/SDRAM read-only) */

/* SD_CARD_OP sub-operations. */
#define SD_OP_DEINIT          0
#define SD_OP_INIT            1
#define SD_OP_GET_STATUS      2
#define SD_OP_GET_INFO        3

/* PI cart-space target for SD DMA buffers.
 *
 * Use the SC64's DEDICATED 8 KiB Data buffer at PI 0x1FFE0000 (BlockRAM,
 * RW, see docs/01_memory_map.md). This is exactly what gz uses for SC64 SD
 * (src/gz/sc64.h: BUFFER_BASE 0xBFFE0000 == PI 0x1FFE0000), and it is the
 * correct target for three reasons the old 0x10F00000 got wrong:
 *
 *  1. It is genuinely READ-WRITE from the N64 side. The previous scratch
 *     0x10F00000 lives in the ROM/SDRAM region, which is READ-ONLY unless the
 *     ROM_WRITE_ENABLE config is set -- so every SD *write* DMA (N64 PI-writes
 *     a sector into scratch for SD_WRITE) was silently dropped and the firmware
 *     flushed stale bytes to the card. Writes never persisted; reads (which
 *     only read scratch) worked. That is the whole "SAVE OK but no file" / 444
 *     / WRITE FAIL story. The data buffer needs no config to be writable.
 *  2. It does not overlap and clobber the running ROM image in SDRAM.
 *  3. The earlier 0x12000000 attempt wedged because it, too, was an arbitrary
 *     ROM-region address; the data buffer is the firmware's intended target.
 *
 * Tradeoff: 8 KiB = 16 sectors per transfer (vs the old 64 KiB/128). diskio.c
 * and the count checks below chunk to SC64_SD_DMA_MAX_SECTORS. */
#define SC64_SD_DMA_SCRATCH    0x1FFE0000u
#define SC64_SD_DMA_MAX_SECTORS 16u   /* 8 KiB data buffer / 512 */

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
/* Cart-lock state for save/restore. Pattern lifted from gz src/gz/sc64.c
 * cart_lock/cart_unlock. The SC64 firmware needs the PI domain 1
 * latency/pulse-width registers in a specific configuration to respond
 * to register reads/writes. The audio thread's PI manager configures
 * dom1 timing differently for ROM data DMAs; if our SC64 register
 * access runs under audio's timing values, the firmware wedges (red LED
 * stuck on). Save audio's timing on lock, swap to SC64-safe values
 * (the libultra defaults baked into pi_regs at boot), do our work,
 * restore audio's timing on unlock. */
static int sc64_lock_irqf;
static uint32_t sc64_lock_lat;
static uint32_t sc64_lock_pwd;
static int sc64_lock_pi_acquired;

static void sc64_cart_lock(void) {
    if (__osPiAccessQueueEnabled) {
        __osPiGetAccess();
        sc64_lock_pi_acquired = 1;
    } else {
        sc64_lock_pi_acquired = 0;
    }
    sc64_lock_irqf = __osDisableInt();
    sc64_lock_lat = IO_READ(PI_BSD_DOM1_LAT_REG);
    sc64_lock_pwd = IO_READ(PI_BSD_DOM1_PWD_REG);
}

static void sc64_cart_unlock(void) {
    IO_WRITE(PI_BSD_DOM1_LAT_REG, sc64_lock_lat);
    IO_WRITE(PI_BSD_DOM1_PWD_REG, sc64_lock_pwd);
    __osRestoreInt(sc64_lock_irqf);
    if (sc64_lock_pi_acquired) {
        __osPiRelAccess();
        sc64_lock_pi_acquired = 0;
    }
}

/* Bus-settle pacing after every SC64 command (host-independent).
 *
 * On SF64 the cart-bus race between SC64 register I/O and the audio subsystem
 * is only fully tamed when the IS-Viewer debug prints happen to serialize the
 * PI bus between FatFs steps -- each osSyncPrintf does __osPiGetAccess plus a
 * timed PI rp/wp dance. With no host draining the IS-Viewer, those prints
 * self-disable into no-ops after 5 timeouts and the race reappears: SD ops
 * wedge/corrupt only when NOT attached to `sc64deployer debug --isv`.
 *
 * Replicate that serialization unconditionally. Reacquire PI access (forcing
 * the PI manager to drain any queued audio DMA), wait for the PI hardware to go
 * idle, then hold a quiet bus for a short tunable interval so the SC64 firmware
 * has breathing room before the next command. SC64_SETTLE_US is the single knob
 * -- start generous, then dial down to the minimum that still holds reliably
 * without `--isv`. Set to 0 to disable.
 *
 * Hardware finding (2026-06-07): the failure point is settle-INDEPENDENT.
 * At 200µs, 500µs, and 2000µs the save reaches the SAME place -- f_rename --
 * and fails there; larger settle only makes the bulk write take longer (2ms
 * pushed a full save into minutes). So settle is NOT the rename fix. Held at
 * 500µs: large enough to let the bulk write through reliably, small enough to
 * keep a full save to tens of seconds while the real rename cause is chased
 * (the caller now surfaces the exact f_rename FRESULT on screen). */
#ifndef SC64_SETTLE_US
#define SC64_SETTLE_US 500
#endif

static void sc64_settle(void) {
    u32 target;
    int acquired = 0;

    if (SC64_SETTLE_US == 0) {
        return;
    }
    if (__osPiAccessQueueEnabled) {
        __osPiGetAccess();
        acquired = 1;
    }
    PI_WAIT();
    target = osGetCount() + (u32)OS_USEC_TO_CYCLES(SC64_SETTLE_US);
    while ((s32)(osGetCount() - target) < 0) {
        ;
    }
    if (acquired) {
        __osPiRelAccess();
    }
}

static iodev_result_t sc64_execute_cmd(uint8_t cmd_id,
                                       uint32_t arg0, uint32_t arg1,
                                       uint32_t *rsp0_out, uint32_t *rsp1_out) {
    int retries;
    uint32_t sr;
    iodev_result_t result;

    BC(1);                  /* entry: about to take PI access (cart_lock) */
    sc64_cart_lock();
    BC(2);                  /* PI access acquired -- cart_lock returned.
                             * If 1 shows but not 2, __osPiGetAccess() in
                             * sc64_cart_lock is deadlocked (audio thread was
                             * stopped while holding the PI access token). */

    PI_WRITE_FLUSH(SC64_REG_DATA0, arg0);
    BC(3);                  /* wrote DATA0 (first PI_WAIT survived) */
    PI_WRITE_FLUSH(SC64_REG_DATA1, arg1);
    BC(4);                  /* wrote DATA1 */
    PI_WRITE_FLUSH(SC64_REG_SCR, (uint32_t)cmd_id);
    BC(5);                  /* command byte issued; CPU_BUSY poll begins */

    /* Spin until CPU_BUSY clears. PI access + IRQs disabled means no
     * audio DMA or interrupt can interleave with the SCR poll. PI_WAIT
     * before each read ensures PI hardware is idle before we touch the
     * register (gz pi.h __pi_read_raw pattern). */
    retries = SC64_CMD_TIMEOUT_RETRIES;
    do {
        PI_WAIT();
        sr = IO_READ(SC64_REG_SCR);
        if (--retries <= 0) {
            result = IODEV_ERR_TIMEOUT;
            goto unlock;
        }
    } while (sr & SC64_SCR_CPU_BUSY);
    BC(6);                  /* CPU_BUSY cleared -- firmware finished the cmd */

    if (sr & SC64_SCR_CMD_ERROR) {
        result = IODEV_ERR_IO;
        goto unlock;
    }

    if (rsp0_out) { PI_WAIT(); *rsp0_out = IO_READ(SC64_REG_DATA0); }
    if (rsp1_out) { PI_WAIT(); *rsp1_out = IO_READ(SC64_REG_DATA1); }
    result = IODEV_OK;

unlock:
    sc64_cart_unlock();
    BC(7);                  /* cart_unlock done; entering settle */
    sc64_settle();
    BC(8);                  /* settle done; returning. If 7 shows but not 8,
                             * sc64_settle's __osPiGetAccess/PI_WAIT wedged. */
    return result;
}

static iodev_id_t sc64_detect(void) {
    uint32_t ident;

    sc64_cart_lock();
    PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_RESET);
    PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_UNLOCK_1);
    PI_WRITE_FLUSH(SC64_REG_KEY, SC64_KEY_UNLOCK_2);
    PI_WAIT();
    ident = IO_READ(SC64_REG_IDENT);
    sc64_cart_unlock();

    if (ident == SC64_V2_IDENTIFIER) {
        return IODEV_SC64;
    }
    return IODEV_NONE;
}

static iodev_result_t sc64_sd_init(void) {
    /* The SD DMA scratch is now the dedicated RW data buffer (0x1FFE0000), so
     * no ROM_WRITE_ENABLE is needed -- writes land without touching ROM. */
    return sc64_execute_cmd(SC64_CMD_SD_CARD_OP,
                            0,    /* arg0: pi_address (unused for INIT) */
                            SD_OP_INIT,
                            0, 0);
}

static iodev_result_t sc64_sd_release(void) {
    /* SD_OP_DEINIT releases SD_LOCK_N64 in the SC64 firmware, allowing the
     * host (sc64deployer sd) to acquire SD_LOCK_USB. Must be followed by
     * sc64_sd_init (re-acquire) before any further FatFs operations. */
    return sc64_execute_cmd(SC64_CMD_SD_CARD_OP, 0, SD_OP_DEINIT, 0, 0);
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

    if (count == 0 || count > SC64_SD_DMA_MAX_SECTORS) {
        return IODEV_ERR_PARAM;  /* exceeds the 8 KiB SD DMA data buffer */
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

    if (count == 0 || count > SC64_SD_DMA_MAX_SECTORS) {
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
 * sd_read_sectors, sd_write_sectors, sd_release). */
static const iodev_backend_t SC64_BACKEND = {
    IODEV_SC64,
    sc64_detect,
    sc64_sd_init,
    sc64_sd_read_sectors,
    sc64_sd_write_sectors,
    sc64_sd_release,
};

const iodev_backend_t *iodev_backend_sc64(void) { return &SC64_BACKEND; }
