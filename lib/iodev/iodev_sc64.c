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
 * NOTE: this file currently implements detection only. Task 6 of the Phase 1a
 * plan replaces the sd_init / sd_read_sectors / sd_write_sectors stubs below
 * with real implementations using the SC64 SD command protocol.
 */

#include "PR/rcp.h"
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

/* SD operations are stubs in this commit. Task 6 of the Phase 1a plan
 * implements them using the SC64 SD command protocol (CMD_SD_CARD_OP,
 * CMD_SD_SECTOR_SET, CMD_SD_READ, CMD_SD_WRITE). For now they return
 * IODEV_ERR_NO_DEVICE so the build links cleanly and the behavior is
 * predictable: detection works, SD operations report unavailable. */
static iodev_result_t sc64_sd_init(void) {
    return IODEV_ERR_NO_DEVICE;
}
static iodev_result_t sc64_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    (void)lba; (void)count; (void)buf;
    return IODEV_ERR_NO_DEVICE;
}
static iodev_result_t sc64_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    (void)lba; (void)count; (void)buf;
    return IODEV_ERR_NO_DEVICE;
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
