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
 * SD I/O is implemented as SPI bit-bang via the FPGA registers. Unlike SC64,
 * there is no firmware-side DMA: the N64 CPU transfers each byte directly
 * through the SD_CMD_WR / SD_DAT_WR registers and polls SD_STATUS.BUSY.
 * This is slower than SC64's DMA path, but is the only mechanism exposed
 * by the EverDrive X FPGA for CPU-initiated SD access.
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

/* SD-bus cap: 128-sector max matches SC64's caller-contract. */
#define ED64_SD_MAX_SECTORS  128u

/* ---- SD SPI configuration ---- */

/* SD_STATUS config word written before each byte transfer.
 * BITLEN = 7 means 8-bit transfer (7 + 1 = 8). */
#define ED64_SD_INIT_CFG   7u                       /* SPD=0: ~400 kHz init clock */
#define ED64_SD_HS_CFG     (7u | ED64_SD_CFG_SPD)  /* SPD=1: 50 MHz after init */

/* Polling retry budget for the BUSY bit and data-token waits.
 * IO_READ at worst-case wait states is ~200 ns; 500000 retries gives a
 * generous ~100 ms ceiling for a single byte transfer that should complete
 * in ~20 us at init speed. */
#define ED64_SD_TIMEOUT     500000

/* ---- SD SPI protocol constants ---- */

#define SD_R1_IDLE          0x01u  /* in-idle-state: normal after CMD0 */
#define SD_R1_OK            0x00u  /* ready: ACMD41 complete */

#define SD_DATA_TOKEN       0xFEu  /* start of a 512-byte data block */
#define SD_RESP_MASK        0x1Fu  /* data response token mask */
#define SD_RESP_ACCEPTED    0x05u  /* data accepted */

#define SD_CMD0             0u
#define SD_CMD8             8u
#define SD_CMD16            16u
#define SD_CMD17            17u
#define SD_CMD24            24u
#define SD_CMD55            55u
#define SD_ACMD41           41u

/* CMD8 SEND_IF_COND argument: VHS=0001 (2.7-3.6V) | check pattern 0xAA. */
#define SD_CMD8_ARG         0x000001AAu

/* CRC7 values. Only CMD0 and CMD8 require a valid CRC in SPI mode;
 * all subsequent commands accept a dummy 0xFF. */
#define SD_CMD0_CRC         0x95u
#define SD_CMD8_CRC         0x87u

/* ---- File-static SD state ---- */

/* SD_STATUS config word; updated to HS after successful init. */
static uint32_t sSdCfg  = ED64_SD_INIT_CFG;

/* Non-zero if the card identified itself as SDHC/SDXC during CMD8. */
static int      sSdIsHC = 0;

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

/* ---- SPI byte transfer helpers ---- */

/* Send one byte on the CMD line and optionally receive one back.
 * Sequence: write SD_STATUS config, write CMD_WR (triggers SPI), poll BUSY,
 * read CMD_RD. */
static iodev_result_t ed64_cmd_byte(uint8_t out, uint8_t *in) {
    int n;
    PI_WRITE_FLUSH(ED64_REG_SD_STATUS, sSdCfg);
    PI_WRITE_FLUSH(ED64_REG_SD_CMD_WR, (uint32_t)out);
    n = ED64_SD_TIMEOUT;
    while (n > 0 && (IO_READ(ED64_REG_SD_STATUS) & ED64_SD_STA_BUSY)) {
        n--;
    }
    if (n <= 0) return IODEV_ERR_TIMEOUT;
    if (in) *in = (uint8_t)(IO_READ(ED64_REG_SD_CMD_RD) & 0xFFu);
    return IODEV_OK;
}

/* Send one byte on the DAT0 line and optionally receive one back.
 * Same protocol as ed64_cmd_byte but routes through DAT_WR/DAT_RD. */
static iodev_result_t ed64_dat_byte(uint8_t out, uint8_t *in) {
    int n;
    PI_WRITE_FLUSH(ED64_REG_SD_STATUS, sSdCfg);
    PI_WRITE_FLUSH(ED64_REG_SD_DAT_WR, (uint32_t)out);
    n = ED64_SD_TIMEOUT;
    while (n > 0 && (IO_READ(ED64_REG_SD_STATUS) & ED64_SD_STA_BUSY)) {
        n--;
    }
    if (n <= 0) return IODEV_ERR_TIMEOUT;
    if (in) *in = (uint8_t)(IO_READ(ED64_REG_SD_DAT_RD) & 0xFFu);
    return IODEV_OK;
}

/* Send a 6-byte SD command frame: 0x40|cmd, arg[31:24..7:0], crc. */
static iodev_result_t ed64_send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    iodev_result_t r;
    r = ed64_cmd_byte(0x40u | cmd,              NULL); if (r != IODEV_OK) return r;
    r = ed64_cmd_byte((uint8_t)(arg >> 24),     NULL); if (r != IODEV_OK) return r;
    r = ed64_cmd_byte((uint8_t)(arg >> 16),     NULL); if (r != IODEV_OK) return r;
    r = ed64_cmd_byte((uint8_t)(arg >>  8),     NULL); if (r != IODEV_OK) return r;
    r = ed64_cmd_byte((uint8_t)(arg),           NULL); if (r != IODEV_OK) return r;
    return    ed64_cmd_byte(crc,                NULL);
}

/* Receive R1 response. SD holds MISO high until ready; clock 0xFF bytes
 * until bit 7 clears. Returns IODEV_ERR_TIMEOUT if no valid byte in 8 tries. */
static iodev_result_t ed64_recv_r1(uint8_t *r1_out) {
    uint8_t b;
    int i;
    iodev_result_t r;
    b = 0xFFu;
    for (i = 0; i < 8; i++) {
        r = ed64_cmd_byte(0xFFu, &b);
        if (r != IODEV_OK) return r;
        if (!(b & 0x80u)) {
            if (r1_out) *r1_out = b;
            return IODEV_OK;
        }
    }
    return IODEV_ERR_TIMEOUT;
}

/* ---- SD init sequence ---- */

static iodev_result_t ed64_sd_init(void) {
    /* IDO C89: all declarations before any statements. */
    iodev_result_t r;
    uint8_t r1;
    uint8_t echo[4];
    uint32_t v8;
    int i;

    sSdCfg  = ED64_SD_INIT_CFG;
    sSdIsHC = 0;

    /* >= 74 clock pulses with MOSI=1 before CMD0 to enter SPI mode.
     * 10 bytes = 80 clocks. */
    for (i = 0; i < 10; i++) {
        r = ed64_cmd_byte(0xFFu, NULL);
        if (r != IODEV_OK) return r;
    }

    /* CMD0: GO_IDLE_STATE. CRC is required at this point. */
    r = ed64_send_cmd(SD_CMD0, 0u, SD_CMD0_CRC);
    if (r != IODEV_OK) return r;
    r = ed64_recv_r1(&r1);
    if (r != IODEV_OK) return r;
    if (r1 != SD_R1_IDLE) return IODEV_ERR_IO;

    /* CMD8: SEND_IF_COND. Distinguishes SDHC/SDXC (v2+) from SD v1/MMC.
     * CRC is required here too. */
    r = ed64_send_cmd(SD_CMD8, SD_CMD8_ARG, SD_CMD8_CRC);
    if (r != IODEV_OK) return r;
    r = ed64_recv_r1(&r1);
    if (r != IODEV_OK) return r;
    if (r1 == SD_R1_IDLE) {
        /* SD v2+: trailing 4 bytes echo the argument. */
        for (i = 0; i < 4; i++) {
            r = ed64_cmd_byte(0xFFu, &echo[i]);
            if (r != IODEV_OK) return r;
        }
        /* Lower 12 bits must match 0x1AA (VHS=1, pattern=0xAA). */
        v8 = (((uint32_t)(echo[2] & 0x0Fu)) << 8) | (uint32_t)echo[3];
        if (v8 != 0x01AAu) return IODEV_ERR_IO;
        sSdIsHC = 1;
    }
    /* r1 == 0x05 (illegal command): SD v1 or MMC; sSdIsHC stays 0. */

    /* ACMD41: send CMD55 + CMD41 until R1 == 0x00 (card initialized). */
    r1 = SD_R1_IDLE;
    for (i = 0; i < 4000; i++) {
        r = ed64_send_cmd(SD_CMD55, 0u, 0xFFu);
        if (r != IODEV_OK) return r;
        r = ed64_recv_r1(NULL);
        if (r != IODEV_OK) return r;

        r = ed64_send_cmd(SD_ACMD41, sSdIsHC ? 0x40000000u : 0u, 0xFFu);
        if (r != IODEV_OK) return r;
        r = ed64_recv_r1(&r1);
        if (r != IODEV_OK) return r;
        if (r1 == SD_R1_OK) break;
    }
    if (r1 != SD_R1_OK) return IODEV_ERR_IO;

    /* Card initialized: switch to 50 MHz. */
    sSdCfg = ED64_SD_HS_CFG;

    /* CMD16: SET_BLOCKLEN = 512 bytes. Required for SD v1; SDHC ignores it. */
    r = ed64_send_cmd(SD_CMD16, 512u, 0xFFu);
    if (r != IODEV_OK) return r;
    r = ed64_recv_r1(&r1);
    if (r != IODEV_OK) return r;
    if (r1 != SD_R1_OK) return IODEV_ERR_IO;

    return IODEV_OK;
}

/* ---- Single-sector read (CMD17) ---- */

static iodev_result_t ed64_read_sector(uint32_t lba, uint8_t *buf) {
    iodev_result_t r;
    uint8_t r1;
    uint8_t tok;
    uint32_t addr;
    int n;
    int i;

    /* SDHC uses LBA directly; SD v1 uses byte address. */
    addr = sSdIsHC ? lba : (lba * 512u);

    r = ed64_send_cmd(SD_CMD17, addr, 0xFFu);
    if (r != IODEV_OK) return r;
    r = ed64_recv_r1(&r1);
    if (r != IODEV_OK) return r;
    if (r1 != SD_R1_OK) return IODEV_ERR_IO;

    /* Poll for the data token 0xFE; any other non-0xFF byte is an error token. */
    tok = 0xFFu;
    n = ED64_SD_TIMEOUT;
    while (n > 0) {
        r = ed64_dat_byte(0xFFu, &tok);
        if (r != IODEV_OK) return r;
        if (tok == SD_DATA_TOKEN) break;
        if (tok != 0xFFu) return IODEV_ERR_IO;
        n--;
    }
    if (tok != SD_DATA_TOKEN) return IODEV_ERR_TIMEOUT;

    /* Read 512 data bytes. */
    for (i = 0; i < 512; i++) {
        r = ed64_dat_byte(0xFFu, &buf[i]);
        if (r != IODEV_OK) return r;
    }

    /* Discard 2 CRC bytes. */
    r = ed64_dat_byte(0xFFu, NULL); if (r != IODEV_OK) return r;
    return  ed64_dat_byte(0xFFu, NULL);
}

static iodev_result_t ed64_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    uint32_t i;
    iodev_result_t r;
    if (count == 0 || count > ED64_SD_MAX_SECTORS) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u) return IODEV_ERR_PARAM;
    for (i = 0; i < count; i++) {
        r = ed64_read_sector(lba + i, (uint8_t *)buf + i * 512u);
        if (r != IODEV_OK) return r;
    }
    return IODEV_OK;
}

/* ---- Single-sector write (CMD24) ---- */

static iodev_result_t ed64_write_sector(uint32_t lba, const uint8_t *buf) {
    iodev_result_t r;
    uint8_t r1;
    uint8_t resp;
    uint32_t addr;
    int n;
    int i;

    addr = sSdIsHC ? lba : (lba * 512u);

    r = ed64_send_cmd(SD_CMD24, addr, 0xFFu);
    if (r != IODEV_OK) return r;
    r = ed64_recv_r1(&r1);
    if (r != IODEV_OK) return r;
    if (r1 != SD_R1_OK) return IODEV_ERR_IO;

    /* One dummy byte before the data token (SD spec requirement). */
    r = ed64_dat_byte(0xFFu, NULL); if (r != IODEV_OK) return r;

    /* Send data token. */
    r = ed64_dat_byte(SD_DATA_TOKEN, NULL); if (r != IODEV_OK) return r;

    /* Write 512 data bytes. */
    for (i = 0; i < 512; i++) {
        r = ed64_dat_byte(buf[i], NULL);
        if (r != IODEV_OK) return r;
    }

    /* Two dummy CRC bytes. */
    r = ed64_dat_byte(0xFFu, NULL); if (r != IODEV_OK) return r;
    r = ed64_dat_byte(0xFFu, NULL); if (r != IODEV_OK) return r;

    /* Read data response token; lower 5 bits must be 0b00101. */
    r = ed64_dat_byte(0xFFu, &resp);
    if (r != IODEV_OK) return r;
    if ((resp & SD_RESP_MASK) != SD_RESP_ACCEPTED) return IODEV_ERR_IO;

    /* Wait for card to finish programming (busy = 0x00 on DAT0). */
    resp = 0x00u;
    n = ED64_SD_TIMEOUT;
    while (n > 0) {
        r = ed64_dat_byte(0xFFu, &resp);
        if (r != IODEV_OK) return r;
        if (resp != 0x00u) break;
        n--;
    }
    if (resp == 0x00u) return IODEV_ERR_TIMEOUT;

    return IODEV_OK;
}

static iodev_result_t ed64_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    uint32_t i;
    iodev_result_t r;
    if (count == 0 || count > ED64_SD_MAX_SECTORS) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u) return IODEV_ERR_PARAM;
    for (i = 0; i < count; i++) {
        r = ed64_write_sector(lba + i, (const uint8_t *)buf + i * 512u);
        if (r != IODEV_OK) return r;
    }
    return IODEV_OK;
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

static iodev_result_t ed64_sd_release(void) {
    /* ED64 has no firmware-level SD lock protocol equivalent to SC64. */
    return IODEV_OK;
}

/* IDO does not support C99 designated initializers; the order below must
 * track the field order in iodev_backend_t (id, detect, sd_init,
 * sd_read_sectors, sd_write_sectors, sd_release). */
static const iodev_backend_t ED64_BACKEND = {
    IODEV_ED64,
    ed64_detect,
    ed64_sd_init,
    ed64_sd_read_sectors,
    ed64_sd_write_sectors,
    ed64_sd_release,
};

const iodev_backend_t *iodev_backend_ed64(void) { return &ED64_BACKEND; }
