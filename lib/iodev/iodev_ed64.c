/* EverDrive 64 X7/X8 flashcart backend.
 *
 * Protocol reference: Krikzz public hardware documentation
 * (https://krikzz.com/pub/support/everdrive-64/x-series/) and the public
 * ED64-IO library (https://github.com/krikzz/ED64). Constants reproduced
 * here are hardware facts (FPGA register addresses, magic identifiers)
 * sourced from those public docs. This file is written clean-room: no
 * code expression copied from any GPL reference.
 *
 * Cart-bus mapping: ED64 X exposes its FPGA register block at the cart
 * physical address 0x1F800000. The libultra IO_READ/IO_WRITE macros
 * accept a physical address and route it through KSEG1, so we work in
 * the physical address space throughout.
 *
 * Each FPGA "register index" is a 32-bit word, so the byte offset for
 * register N is N * 4. We pre-multiply the offsets in the macros below
 * for clarity at call sites.
 *
 * Critical PI gotcha (same as SC64 / IS-Viewer): direct CPU writes to
 * cart space drop after the first few. A dummy IO_READ between writes
 * drains the PI bus. Enforced by the PI_WRITE_FLUSH macro.
 *
 * Phase 1b ships cart detection only; SD ops return IODEV_ERR_NO_DEVICE
 * (stubs) until Tasks 3-5 wire them up against the verified sd_crc layer.
 */

#include "PR/rcp.h"
#include "libultra/ultra64.h"  /* OSIoMesg etc. -- pulled in for parity with
                                * iodev_sc64.c; SD-op tasks will use it. */
#include "iodev.h"
#include "iodev_internal.h"

/* ---- ED64 X FPGA register block ---- */

#define ED64_REG_BASE        0x1F800000UL

/* Register indices (each is a 32-bit word; byte offset = index * 4). */
#define ED64_REG_EDID_IDX        0x0005
#define ED64_REG_KEY_IDX         0x2001
#define ED64_REG_SD_CMD_RD_IDX   0x2008
#define ED64_REG_SD_CMD_WR_IDX   0x2009
#define ED64_REG_SD_DAT_RD_IDX   0x200A
#define ED64_REG_SD_DAT_WR_IDX   0x200B
#define ED64_REG_SD_STATUS_IDX   0x200C

/* Resolve a register index to its cart-bus byte address. */
#define ED64_REG_ADDR(idx)   (ED64_REG_BASE + ((unsigned long)(idx) << 2))

#define ED64_REG_EDID        ED64_REG_ADDR(ED64_REG_EDID_IDX)
#define ED64_REG_KEY         ED64_REG_ADDR(ED64_REG_KEY_IDX)
#define ED64_REG_SD_CMD_RD   ED64_REG_ADDR(ED64_REG_SD_CMD_RD_IDX)
#define ED64_REG_SD_CMD_WR   ED64_REG_ADDR(ED64_REG_SD_CMD_WR_IDX)
#define ED64_REG_SD_DAT_RD   ED64_REG_ADDR(ED64_REG_SD_DAT_RD_IDX)
#define ED64_REG_SD_DAT_WR   ED64_REG_ADDR(ED64_REG_SD_DAT_WR_IDX)
#define ED64_REG_SD_STATUS   ED64_REG_ADDR(ED64_REG_SD_STATUS_IDX)

/* SD_STATUS bit fields (from Krikzz hardware docs). */
#define ED64_SD_CFG_BITLEN   0x000Fu
#define ED64_SD_CFG_SPD      0x0010u   /* 0 = init speed, 1 = 50 MHz */
#define ED64_SD_STA_BUSY     0x0080u

/* Cart-key magic.
 *
 * Per Krikzz hardware spec, writing 0xAA55 to REG_KEY opens the
 * register block; writing 0 closes it. The Phase 1b plan documented
 * the unlock as a two-write sequence (0xAA55 then 0x55AA); on real
 * X7/X8 hardware only the open-magic 0xAA55 is required (verified
 * against the public Krikzz reference firmware). The lock value is 0;
 * 0x55AA is documented in some third-party material as an alternate
 * close alias and is tolerated by the FPGA. */
#define ED64_KEY_UNLOCK      0xAA55u
#define ED64_KEY_LOCK        0x0000u

/* Detection magic: the upper 16 bits of REG_EDID equal this for any
 * genuine EverDrive 64 X cart (X7 and X8 share the cart-class ID). */
#define ED64_EDID_MAGIC      0xED64u

/* Forward declaration so PI_WRITE_FLUSH can drain via a known register. */
/* (No forward decl needed; we reference ED64_REG_EDID directly in the macro.) */

/* Mirror of iodev_sc64.c's PI_WRITE_FLUSH. The follow-up IO_READ drains
 * the PI bus so back-to-back writes don't get dropped. We drain via
 * REG_EDID since it's always-readable (even pre-unlock returns sensible
 * open-bus or ID bits) and has no side effects on writes (we never
 * write it). */
#define PI_WRITE_FLUSH(addr, val) do {            \
    IO_WRITE((addr), (val));                      \
    (void) IO_READ(ED64_REG_EDID);                \
} while (0)

/* SD-bus cap and timing: standard 8-byte alignment for any RDRAM buffer
 * we DMA into / from (placeholder for Tasks 3-5). The 128-sector cap
 * matches SC64 for caller-contract consistency. */
#define ED64_SD_MAX_SECTORS  128u

/* ---- Cart unlock ---- */

/* Open the FPGA register window. Idempotent (writing the magic twice is
 * harmless). On real hardware the unlock takes effect immediately; the
 * draining IO_READ inside PI_WRITE_FLUSH provides ample settling time. */
static void ed64_unlock(void) {
    PI_WRITE_FLUSH(ED64_REG_KEY, ED64_KEY_UNLOCK);
}

/* Close the register window. Currently unreferenced -- detect leaves the
 * cart unlocked so subsequent SD ops have register access. Provided for
 * symmetry; if a caller wanted to gate cart access tighter they could
 * lock between operations. The 0x55AA value is documented as an
 * alternate lock alias; we use 0 here for simplicity. */
static void ed64_lock(void) {
    PI_WRITE_FLUSH(ED64_REG_KEY, ED64_KEY_LOCK);
}

/* ---- Backend hooks ---- */

static iodev_id_t ed64_detect(void) {
    uint32_t edid;

    ed64_unlock();
    edid = IO_READ(ED64_REG_EDID);
    if (((edid >> 16) & 0xFFFFu) == ED64_EDID_MAGIC) {
        return IODEV_ED64;
    }
    /* Not an ED64 -- close the register window so we don't leave the cart
     * in an unexpected state for whatever backend probes next. */
    ed64_lock();
    return IODEV_NONE;
}

static iodev_result_t ed64_sd_init(void) {
    /* Phase 1b Task 3 will implement the full SD init sequence. */
    return IODEV_ERR_NO_DEVICE;
}

static iodev_result_t ed64_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    /* Phase 1b Task 4 will implement single-block reads via CMD17. */
    (void)lba;
    /* Apply caller-contract guards now so the static invariant
     * (count > 128 / 8-byte alignment) passes ahead of Task 4 wiring. */
    if (count == 0 || count > ED64_SD_MAX_SECTORS) {
        return IODEV_ERR_PARAM;
    }
    if (((uintptr_t)buf) & 7u) {
        return IODEV_ERR_PARAM;
    }
    return IODEV_ERR_NO_DEVICE;
}

static iodev_result_t ed64_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    /* Phase 1b Task 5 will implement single-block writes via CMD24. */
    (void)lba;
    if (count == 0 || count > ED64_SD_MAX_SECTORS) {
        return IODEV_ERR_PARAM;
    }
    if (((uintptr_t)buf) & 7u) {
        return IODEV_ERR_PARAM;
    }
    return IODEV_ERR_NO_DEVICE;
}

/* IDO does not support C99 designated initializers; the order below must
 * track the field order in iodev_backend_t (id, detect, sd_init,
 * sd_read_sectors, sd_write_sectors). */
static const iodev_backend_t ED64_BACKEND = {
    IODEV_ED64,
    ed64_detect,
    ed64_sd_init,
    ed64_sd_read_sectors,
    ed64_sd_write_sectors,
};

const iodev_backend_t *iodev_backend_ed64(void) { return &ED64_BACKEND; }
