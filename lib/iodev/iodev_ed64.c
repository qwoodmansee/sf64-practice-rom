/* lib/iodev/iodev_ed64.c — EverDrive 64 X7/X8 backend.
 *
 * After the gz-mirror Wave 2 refactor, this file owns ONLY:
 *   - FPGA register layout / addresses
 *   - PI-bus access helpers and the PI write FIFO drain
 *   - Cart-key unlock and card detection
 *   - Shift-register I/O primitives (cmd / dat) wrapped as sd_host
 *     function-pointer callbacks
 *   - The FPGA-DMA fast read callback (rx_mblk)
 *   - The slow per-block CMD24 write callback (tx_mblk)
 *   - The iodev_backend_t descriptor that the dispatcher looks up
 *
 * The SD protocol state machine (CMD0/8/41/2/3/7/ACMD6/CMD16 init,
 * R1/R2/R6 parsing, ACMD41 retry loop, RCA capture) lives in
 * lib/sd_host/sd_host.c and is shared with the upcoming V2/V1 SPI
 * backends. CMD numbers and protocol constants live in
 * lib/sd_host/sd_proto.h.
 *
 * Hardware reference: Krikzz public docs and gz (glankk/gz)
 * ed64_x.h/ed64_x.c. This file is a clean-room implementation; no
 * expression copied from gz. */

#include "PR/rcp.h"
#include "libultra/ultra64.h"
#include "iodev.h"
#include "iodev_internal.h"
#include "sd_host/sd_host.h"
#include "sd_host/sd_proto.h"
#include "sd_crc.h"

/* ---- FPGA register addresses ---- */

#define ED64_REG_BASE           0x1F800000UL

/* Register indices: byte offset = index * 4 */
#define ED64_REG_EDID_IDX       0x0005
#define ED64_REG_KEY_IDX        0x2001
#define ED64_REG_DMA_STA_IDX    0x2002   /* read = status */
#define ED64_REG_DMA_ADDR_IDX   0x2002   /* write = cart staging address */
#define ED64_REG_DMA_LEN_IDX    0x2003
#define ED64_REG_SD_CMD_RD_IDX  0x2008
#define ED64_REG_SD_CMD_WR_IDX  0x2009
#define ED64_REG_SD_DAT_RD_IDX  0x200A
#define ED64_REG_SD_DAT_WR_IDX  0x200B
#define ED64_REG_SD_STATUS_IDX  0x200C

#define ED64_REG_ADDR(idx)      (ED64_REG_BASE + ((unsigned long)(idx) << 2))

#define ED64_REG_EDID           ED64_REG_ADDR(ED64_REG_EDID_IDX)
#define ED64_REG_KEY            ED64_REG_ADDR(ED64_REG_KEY_IDX)
#define ED64_REG_DMA_STA        ED64_REG_ADDR(ED64_REG_DMA_STA_IDX)
#define ED64_REG_DMA_ADDR       ED64_REG_ADDR(ED64_REG_DMA_ADDR_IDX)
#define ED64_REG_DMA_LEN        ED64_REG_ADDR(ED64_REG_DMA_LEN_IDX)
#define ED64_REG_SD_CMD_RD      ED64_REG_ADDR(ED64_REG_SD_CMD_RD_IDX)
#define ED64_REG_SD_CMD_WR      ED64_REG_ADDR(ED64_REG_SD_CMD_WR_IDX)
#define ED64_REG_SD_DAT_RD      ED64_REG_ADDR(ED64_REG_SD_DAT_RD_IDX)
#define ED64_REG_SD_DAT_WR      ED64_REG_ADDR(ED64_REG_SD_DAT_WR_IDX)
#define ED64_REG_SD_STATUS      ED64_REG_ADDR(ED64_REG_SD_STATUS_IDX)

/* SD_STATUS write fields:  [3:0]=BITLEN (clocks-per-IO, direct), [4]=SPD
 *   (0=init <400kHz, 1=50MHz). Read field [7]=BUSY. */
#define ED64_SD_CFG_BITLEN      0x000Fu
#define ED64_SD_CFG_SPD         0x0010u
#define ED64_SD_STA_BUSY        0x0080u

/* DMA_STA bits (read) */
#define ED64_DMA_STA_BUSY       0x0001u
#define ED64_DMA_STA_ERROR      0x0002u

/* Cart RDRAM staging area for FPGA DMA reads. The FPGA fills this cart
 * address from the SD bus, then a standard PI DMA copies it to game RDRAM.
 * 0xB2000000 (KSEG1 physical 0x12000000) is within ED64-X cart space. */
#define ED64_DMA_CART_ADDR      0xB2000000UL

/* Register-unlock key */
#define ED64_KEY_UNLOCK         0xAA55u
#define ED64_KEY_LOCK           0x0000u

/* Detection magic: upper 16 bits of REG_EDID */
#define ED64_EDID_MAGIC         0xED64u

/* SD-bus per-operation BITLEN values. These map directly to BITLEN
 * field values in the FPGA SD_STATUS register. */
#define ED64_BITLEN_CMD         8u   /* 8 clocks/byte on CMD line     */
#define ED64_BITLEN_DAT         4u   /* 4 clocks/word on 4-bit DAT bus */
#define ED64_BITLEN_1           1u   /* single-bit poll                */

/* SD_STATUS config words. Combined SPD bit + BITLEN. */
#define ED64_CFG_INIT_CMD       ED64_BITLEN_CMD
#define ED64_CFG_INIT_DAT       ED64_BITLEN_DAT
#define ED64_CFG_INIT_1         ED64_BITLEN_1
#define ED64_CFG_HS_CMD         (ED64_SD_CFG_SPD | ED64_BITLEN_CMD)
#define ED64_CFG_HS_DAT         (ED64_SD_CFG_SPD | ED64_BITLEN_DAT)
#define ED64_CFG_HS_1           (ED64_SD_CFG_SPD | ED64_BITLEN_1)

/* Poll timeout (iterations of IO_READ; ~200 ns each). 500_000 ~= 100 ms. */
#define ED64_SD_TIMEOUT         500000

/* Sector-count cap matches SC64; consistent caller contract. */
#define ED64_SD_MAX_SECTORS     128u

/* ---- PI_WRITE_FLUSH ---- */
/* A follow-up IO_READ drains the PI write FIFO so back-to-back writes
 * don't get silently dropped. */
#define PI_WRITE_FLUSH(addr, val) do {  \
    IO_WRITE((addr), (val));            \
    (void) IO_READ(ED64_REG_EDID);     \
} while (0)

/* ---- File-static state ---- */

/* Raw 32-bit value read from REG_EDID during ed64_detect(). Preserved
 * even when detection fails (IODEV_NONE) so diagnostics can show
 * exactly what the hardware returned. */
static uint32_t sRawEdid = 0;

/* Live SD_STATUS config words. Updated by the sd_host set_spd callback
 * to flip between init speed (<400kHz) and HS50. */
static uint32_t sCfgCmd = ED64_CFG_INIT_CMD;
static uint32_t sCfgDat = ED64_CFG_INIT_DAT;
static uint32_t sCfg1   = ED64_CFG_INIT_1;

/* The sd_host_t we hand to the protocol engine. Populated lazily on
 * the first ed64_sd_init() call. */
static sd_host_t sEd64Host;

/* ---- Register helpers ---- */

static void reg_wr(uint32_t addr, uint32_t val)
{
    PI_WRITE_FLUSH(addr, val);
}

static uint32_t reg_rd(uint32_t addr)
{
    return IO_READ(addr);
}

static void ed64_unlock(void)
{
    reg_wr(ED64_REG_KEY, ED64_KEY_UNLOCK);
}

static void wait_not_busy(void)
{
    int n = ED64_SD_TIMEOUT;
    while (n > 0 && (reg_rd(ED64_REG_SD_STATUS) & ED64_SD_STA_BUSY)) {
        n--;
    }
    /* Timeout is silent: callers detect via the next response wait. */
}

/* ============================================================ *
 *  sd_host SDIO callbacks                                       *
 * ============================================================ */

static void ed64_sdh_set_spd(int hs)
{
    if (hs) {
        sCfgCmd = ED64_CFG_HS_CMD;
        sCfgDat = ED64_CFG_HS_DAT;
        sCfg1   = ED64_CFG_HS_1;
    } else {
        sCfgCmd = ED64_CFG_INIT_CMD;
        sCfgDat = ED64_CFG_INIT_DAT;
        sCfg1   = ED64_CFG_INIT_1;
    }
}

static void ed64_sdh_cmd_tx_byte(uint8_t b)
{
    reg_wr(ED64_REG_SD_STATUS, sCfgCmd);     /* BITLEN=8 on CMD line */
    reg_wr(ED64_REG_SD_CMD_WR, (uint32_t)b);
    wait_not_busy();
}

static uint8_t ed64_sdh_cmd_rx_byte(void)
{
    reg_wr(ED64_REG_SD_STATUS, sCfgCmd);     /* BITLEN=8 */
    reg_wr(ED64_REG_SD_CMD_RD, 0xFFu);       /* trigger receive */
    wait_not_busy();
    return (uint8_t)(reg_rd(ED64_REG_SD_CMD_RD) & 0xFFu);
}

static uint8_t ed64_sdh_cmd_rx_bit(void)
{
    reg_wr(ED64_REG_SD_STATUS, sCfg1);       /* BITLEN=1 */
    reg_wr(ED64_REG_SD_CMD_RD, 0xFFu);
    wait_not_busy();
    return (uint8_t)(reg_rd(ED64_REG_SD_CMD_RD) & 0x01u);
}

static void ed64_sdh_dat_tx_word(uint16_t w)
{
    reg_wr(ED64_REG_SD_STATUS, sCfgDat);     /* BITLEN=4 (4 clocks/word) */
    reg_wr(ED64_REG_SD_DAT_WR, (uint32_t)w);
    wait_not_busy();
}

static uint16_t ed64_sdh_dat_rx_word(void)
{
    /* sd_host.c invokes this only for single-nibble polls (DAT0 idle
     * checks during R1b waits, write-busy checks, etc.). Use BITLEN=1
     * so the result is one clock's worth of nibble in the LSB.
     *
     * The slow-write callback below also drives single-nibble reads
     * for the data-response token; it shares this BITLEN=1 setup. */
    reg_wr(ED64_REG_SD_STATUS, sCfg1);       /* BITLEN=1 */
    reg_wr(ED64_REG_SD_DAT_RD, 0xFFFFu);
    wait_not_busy();
    return (uint16_t)(reg_rd(ED64_REG_SD_DAT_RD) & 0xFFFFu);
}

static void ed64_sdh_dat_idle_clks(uint32_t n_clk)
{
    uint32_t i;
    /* BITLEN=4 -> each dat_tx delivers 4 SD clocks of 4 idle bits each. */
    reg_wr(ED64_REG_SD_STATUS, sCfgDat);
    for (i = 0; i < n_clk / 4u; i++) {
        reg_wr(ED64_REG_SD_DAT_WR, 0xFFFFu);
        wait_not_busy();
    }
}

/* ============================================================ *
 *  rx_mblk: FPGA DMA fast read                                  *
 * ============================================================ */

/* The FPGA can DMA multiple sectors directly from the SD bus into a
 * cart-bus staging area. After the burst completes we PI-DMA the
 * staging area into RDRAM. This is ~256x faster per sector than
 * shoving each byte through CPU-driven dat_rx_word reads. */
static sd_host_result_t ed64_sdh_rx_mblk(struct sd_host *h,
                                          uint32_t addr,
                                          void *dst,
                                          uint32_t n_blk)
{
    sd_host_result_t r;
    uint8_t resp[6];
    int n;
    OSIoMesg dma_msg;
    OSMesgQueue dma_queue;
    OSMesg dma_mesg;

    /* Step 1: CMD18 READ_MULTIPLE_BLOCK. sd_host already translated
     * LBA -> byte/LBA address based on card_kind. */
    r = sd_host_send_cmd_r1(h, SDP_CMD18, addr, resp);
    if (r != SD_HOST_OK) return r;

    /* Step 2: trigger FPGA DMA. The FPGA waits for the data-start
     * nibble on DAT, then reads `n_blk` * 512 bytes into the staging
     * address. */
    reg_wr(ED64_REG_DMA_ADDR, ED64_DMA_CART_ADDR);
    PI_WRITE_FLUSH(ED64_REG_DMA_LEN, n_blk);

    /* Step 3: poll DMA_STA.BUSY until clear. */
    n = ED64_SD_TIMEOUT;
    while (n > 0 && (reg_rd(ED64_REG_DMA_STA) & ED64_DMA_STA_BUSY)) {
        n--;
    }
    if (n <= 0) {
        (void)sd_host_send_cmd_r1(h, SDP_CMD12, 0u, resp);
        return SD_HOST_ERR_TIMEOUT;
    }
    if (reg_rd(ED64_REG_DMA_STA) & ED64_DMA_STA_ERROR) {
        (void)sd_host_send_cmd_r1(h, SDP_CMD12, 0u, resp);
        return SD_HOST_ERR_IO;
    }

    /* Step 4: CMD12 STOP_TRANSMISSION (R1b). The R1b wait absorbs the
     * card's busy-while-finishing DAT0 hold. */
    r = sd_host_send_cmd_r1b(h, SDP_CMD12, 0u, resp);
    if (r != SD_HOST_OK) return r;

    /* Step 5: PI DMA from cart staging area to game RDRAM. The CPU
     * writeback of `dst` happens before the read so any stale dirty
     * cache lines are flushed; the invalidate happens after so the
     * CPU sees the freshly-DMA'd data on the next access. */
    osWritebackDCache(dst, (s32)(n_blk * 512u));
    osCreateMesgQueue(&dma_queue, &dma_mesg, 1);
    osPiStartDma(&dma_msg, OS_MESG_PRI_NORMAL, OS_READ,
                 ED64_DMA_CART_ADDR, dst, n_blk * 512u, &dma_queue);
    (void)osRecvMesg(&dma_queue, NULL, OS_MESG_BLOCK);
    osInvalDCache(dst, (s32)(n_blk * 512u));

    return SD_HOST_OK;
}

/* ============================================================ *
 *  tx_mblk: per-block CMD24 + 4-bit data path                   *
 * ============================================================ */

/* ED64-X has no FPGA DMA write helper, so writes go block-by-block
 * through the CPU. The card-side wire format follows the SD spec for
 * 4-bit native-mode writes:
 *   1. CMD24 WRITE_BLOCK + R1
 *   2. wait for DAT bus idle (all four lines high)
 *   3. emit at least 2 idle nibbles, then a start nibble (0x0)
 *   4. 1024 data nibbles  (512 bytes -- high nibble of byte first)
 *   5. 16 nibbles of CRC16 (4 lanes, MSB-first, nibble-interleaved)
 *   6. one end nibble (0xF) so the bus returns to idle
 *   7. data-response token on DAT0 (5 bits)
 *   8. busy on DAT0 until card finishes programming the block
 *
 * The 4-lane CRC packing is implemented in lib/sd_crc.c. */
static sd_host_result_t ed64_write_one_block(struct sd_host *h,
                                              uint32_t lba_or_byte_addr,
                                              const uint8_t *src)
{
    sd_host_result_t r;
    uint8_t resp[6];
    uint64_t crc;
    uint16_t c0, c1, c2, c3;
    uint16_t w;
    uint32_t poll;
    uint8_t  resp_token;
    uint32_t i;
    int k;
    uint8_t crc_bytes[8];

    /* CMD24 + R1. */
    r = sd_host_send_cmd_r1(h, SDP_CMD24, lba_or_byte_addr, resp);
    if (r != SD_HOST_OK) return r;

    /* Wait for DAT bus idle. dat_rx_word here uses BITLEN=1 so the
     * result is a single nibble in the low 4 bits; idle == 0xF. */
    for (poll = 0; poll < ED64_SD_TIMEOUT; poll++) {
        w = h->sdio_dat_rx_word();
        if ((w & 0x000Fu) == 0x000Fu) break;
    }
    if (poll >= ED64_SD_TIMEOUT) return SD_HOST_ERR_TIMEOUT;

    /* Compute the 4-lane CRC16 over the data block. */
    crc = sd_crc16_4bit(src, 512u);
    c0 = (uint16_t)(crc & 0xFFFFu);
    c1 = (uint16_t)((crc >> 16) & 0xFFFFu);
    c2 = (uint16_t)((crc >> 32) & 0xFFFFu);
    c3 = (uint16_t)((crc >> 48) & 0xFFFFu);

    /* Pack the 4 CRC16s into 8 bytes nibble-interleaved. Clock k
     * (0 = first transmitted) carries
     *   nibble = (c3>>(15-k))<<3 | (c2>>(15-k))<<2
     *          | (c1>>(15-k))<<1 |  (c0>>(15-k))
     * with the high nibble of out[k/2] = clock (k*2), low = clock (k*2+1). */
    for (k = 0; k < 8; k++) {
        int hi_bit = 15 - (k * 2);
        int lo_bit = hi_bit - 1;
        uint8_t n_hi, n_lo;

        n_hi = (uint8_t)(
            (((c3 >> hi_bit) & 1u) << 3) |
            (((c2 >> hi_bit) & 1u) << 2) |
            (((c1 >> hi_bit) & 1u) << 1) |
             ((c0 >> hi_bit) & 1u)
        );
        n_lo = (uint8_t)(
            (((c3 >> lo_bit) & 1u) << 3) |
            (((c2 >> lo_bit) & 1u) << 2) |
            (((c1 >> lo_bit) & 1u) << 1) |
             ((c0 >> lo_bit) & 1u)
        );
        crc_bytes[k] = (uint8_t)((n_hi << 4) | n_lo);
    }

    /* Preamble: at least 2 idle clocks (8 nibbles), then start nibble
     * (0x0). dat_tx_word transmits 4 nibbles per call. */
    h->sdio_dat_tx_word(0xFFFFu);    /* 4 idle nibbles */
    h->sdio_dat_tx_word(0xFFF0u);    /* 3 idle nibbles + start (0x0) */

    /* Data: 256 dat_words covering 512 bytes. Each word carries two
     * bytes; per SD spec the high nibble of byte b[i] hits the bus
     * first, so the word is (b[i] << 8) | b[i+1]. */
    for (i = 0; i < 512u; i += 2u) {
        w = (uint16_t)(((uint16_t)src[i] << 8) | (uint16_t)src[i + 1]);
        h->sdio_dat_tx_word(w);
    }

    /* CRC: 8 packed bytes -> 4 dat_words. */
    for (i = 0; i < 8u; i += 2u) {
        w = (uint16_t)(((uint16_t)crc_bytes[i] << 8) | (uint16_t)crc_bytes[i + 1]);
        h->sdio_dat_tx_word(w);
    }

    /* End: hold all DAT lines high one more clock so the controller's
     * state machine returns to idle. */
    h->sdio_dat_tx_word(0xFFFFu);

    /* Receive the data-response token. Token is 5 bits on DAT0:
     *   start (0), 3 status bits, end (1). Poll for the start bit
     *   (DAT0 low), then read the next 4 bits. */
    resp_token = 0xFFu;
    for (poll = 0; poll < 64u; poll++) {
        w = h->sdio_dat_rx_word();   /* BITLEN=1 -> single nibble in LSB */
        if ((w & 0x0001u) == 0u) {
            uint8_t tok = 0;
            int b;
            for (b = 0; b < 4; b++) {
                w = h->sdio_dat_rx_word();
                tok = (uint8_t)((tok << 1) | (uint8_t)(w & 0x0001u));
            }
            resp_token = tok;
            break;
        }
    }
    if ((resp_token & SDP_DATA_RESP_MASK) != SDP_DATA_RESP_ACCEPTED) {
        return SD_HOST_ERR_IO;
    }

    /* Card-busy: DAT0 stays low while the card programs the block.
     * Released high when done. */
    for (poll = 0; poll < ED64_SD_TIMEOUT; poll++) {
        w = h->sdio_dat_rx_word();
        if ((w & 0x0001u) != 0u) {
            return SD_HOST_OK;
        }
    }
    return SD_HOST_ERR_TIMEOUT;
}

static sd_host_result_t ed64_sdh_tx_mblk(struct sd_host *h,
                                          uint32_t addr,
                                          const void *src,
                                          uint32_t n_blk)
{
    /* sd_host hands us either an LBA (SDHC) or a byte address (SDSC) in
     * `addr`. Per-block stride is 1 (LBA) or 512 (bytes). */
    uint32_t stride = (h->card_kind == SD_CARD_HC) ? 1u : 512u;
    const uint8_t *p = (const uint8_t *)src;
    uint32_t i;
    sd_host_result_t r;

    for (i = 0; i < n_blk; i++) {
        r = ed64_write_one_block(h, addr + i * stride, p + i * 512u);
        if (r != SD_HOST_OK) return r;
    }
    return SD_HOST_OK;
}

/* ============================================================ *
 *  Backend hooks                                                *
 * ============================================================ */

static iodev_id_t ed64_detect(void)
{
    uint32_t edid;

    ed64_unlock();
    edid = IO_READ(ED64_REG_EDID);
    sRawEdid = edid;
    if (((edid >> 16) & 0xFFFFu) == ED64_EDID_MAGIC) {
        return IODEV_ED64;
    }
    PI_WRITE_FLUSH(ED64_REG_KEY, ED64_KEY_LOCK);
    return IODEV_NONE;
}

uint32_t iodev_ed64_raw_edid(void) { return sRawEdid; }

static void ed64_populate_host(void)
{
    sEd64Host.proto              = SD_PROTO_SDIO;
    sEd64Host.user               = 0;
    sEd64Host.lock               = 0;        /* PI critical section TBD */
    sEd64Host.unlock             = 0;
    sEd64Host.set_spd            = ed64_sdh_set_spd;
    sEd64Host.sdio_cmd_tx_byte   = ed64_sdh_cmd_tx_byte;
    sEd64Host.sdio_cmd_rx_byte   = ed64_sdh_cmd_rx_byte;
    sEd64Host.sdio_cmd_rx_bit    = ed64_sdh_cmd_rx_bit;
    sEd64Host.sdio_dat_tx_word   = ed64_sdh_dat_tx_word;
    sEd64Host.sdio_dat_rx_word   = ed64_sdh_dat_rx_word;
    sEd64Host.sdio_dat_idle_clks = ed64_sdh_dat_idle_clks;
    sEd64Host.spi_io             = 0;
    sEd64Host.spi_tx_buf         = 0;
    sEd64Host.spi_rx_buf         = 0;
    sEd64Host.rx_mblk            = ed64_sdh_rx_mblk;
    sEd64Host.tx_mblk            = ed64_sdh_tx_mblk;
    sEd64Host.card_kind          = SD_CARD_NONE;
    sEd64Host.rca                = 0u;
    sEd64Host.hs_active          = 0;
}

static iodev_result_t ed64_sd_init(void)
{
    sd_host_result_t r;

    ed64_populate_host();
    r = sd_host_init(&sEd64Host);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t ed64_sd_read_sectors(uint32_t lba, uint32_t count, void *buf)
{
    sd_host_result_t r;

    if (count == 0u || count > ED64_SD_MAX_SECTORS) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u)                       return IODEV_ERR_PARAM;

    r = sd_host_read_blocks(&sEd64Host, lba, buf, count);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t ed64_sd_write_sectors(uint32_t lba, uint32_t count,
                                             const void *buf)
{
    sd_host_result_t r;

    if (count == 0u || count > ED64_SD_MAX_SECTORS) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u)                       return IODEV_ERR_PARAM;

    r = sd_host_write_blocks(&sEd64Host, lba, buf, count);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t ed64_sd_release(void)
{
    return IODEV_OK;
}

/* Field order must match iodev_backend_t: id, detect, sd_init,
 * sd_read_sectors, sd_write_sectors, sd_release. */
static const iodev_backend_t ED64_BACKEND = {
    IODEV_ED64,
    ed64_detect,
    ed64_sd_init,
    ed64_sd_read_sectors,
    ed64_sd_write_sectors,
    ed64_sd_release,
};

const iodev_backend_t *iodev_backend_ed64(void) { return &ED64_BACKEND; }
