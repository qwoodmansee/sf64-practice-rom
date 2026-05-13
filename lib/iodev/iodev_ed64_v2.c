/* lib/iodev/iodev_ed64_v2.c — EverDrive 64 V2 / V2.5 backend.
 *
 * V2 uses native 4-bit SDIO (NOT SPI), the same protocol as X-series,
 * but through a different FPGA register layout:
 *   - V2 lives in PI Cart Domain 2 (register base 0x08040000).
 *   - X  lives in PI Cart Domain 1 (register base 0x1F800000).
 *   - V2 has a single REG_SPI byte-shift register that can be steered
 *     onto either the CMD line or the DAT[3:0] bus by REG_SPI_CFG.
 *   - X has separate REG_SD_CMD_{RD,WR} and REG_SD_DAT_{RD,WR}.
 *
 * This file is a clean-room implementation; register addresses and
 * bitfields are facts about Krikzz hardware (not copyrightable). No
 * code structure, idioms, or comments copied from gz.
 *
 * Hardware reference:
 *   - REG_BASE              0xA8040000 (KSEG1; physical 0x08040000)
 *   - REG indices (uint32_t-stride):
 *       CFG=0, STATUS=1, DMA_LEN=2, DMA_ADDR=3, MSG=4, DMA_CFG=5,
 *       SPI=6, SPI_CFG=7, KEY=8, SAV_CFG=9, SEC=10, VER=11
 *   - REG_SPI_CFG bitfields (firmware >= 1.16):
 *       [1:0] SPEED: 0=50MHz, 1=25MHz, 2=<400kHz
 *       [2]   SS:    0=on (SPI mode only -- for SDIO it has no effect)
 *       [3]   RD:    0=shift out (host -> card), 1=shift in
 *       [4]   DAT:   0=CMD line  (1-bit), 1=DAT[3:0] (4-bit)
 *       [5]   1CLK:  0=8 clocks (CMD) or 2 clocks (DAT) per byte
 *                    1=1 clock per byte (used for response start polls
 *                                        and DAT0 idle polls)
 *   - REG_STATUS bitfields:
 *       [0] DMA_BUSY,  [1] DMA_TOUT,  [4] SPI (busy)
 *   - DMA: cart staging at 0xB2000000; REG_DMA_ADDR is in 2KB units
 *     (right-shift by 11). REG_DMA_LEN expects (n_blocks - 1).
 *   - PI Domain 2 BSD timings: lat=4, pwd=12 are required for reliable
 *     SPI access (default game timings are too slow).
 *   - Register unlock magic: 0x1234 (different from X-series 0xAA55).
 *   - Detection: REG_VER >= 0x0116 + 4-bit DAT pull-up probe returns 0x0F.
 */

#include "PR/rcp.h"
#include "libultra/ultra64.h"
#include "iodev.h"
#include "iodev_internal.h"
#include "sd_host/sd_host.h"
#include "sd_host/sd_proto.h"
#include "sd_crc.h"

/* Forward decl: __osPiGetAccess / __osPiRelAccess from libultra. */
extern void __osPiGetAccess(void);
extern void __osPiRelAccess(void);

/* ---- FPGA register addresses ---- */

#define V2_REG_BASE             0x08040000UL  /* physical cart-bus address */
#define V2_REG_ADDR(idx)        (V2_REG_BASE + ((unsigned long)(idx) << 2))

#define V2_REG_CFG              V2_REG_ADDR(0)
#define V2_REG_STATUS           V2_REG_ADDR(1)
#define V2_REG_DMA_LEN          V2_REG_ADDR(2)
#define V2_REG_DMA_ADDR         V2_REG_ADDR(3)
#define V2_REG_DMA_CFG          V2_REG_ADDR(5)
#define V2_REG_SPI              V2_REG_ADDR(6)
#define V2_REG_SPI_CFG          V2_REG_ADDR(7)
#define V2_REG_KEY              V2_REG_ADDR(8)
#define V2_REG_VER              V2_REG_ADDR(11)

/* SPI_CFG bits */
#define V2_SPI_SPEED_50         0x0000u
#define V2_SPI_SPEED_25         0x0001u
#define V2_SPI_SPEED_LO         0x0002u
#define V2_SPI_SPEED_MASK       0x0003u
#define V2_SPI_SS               0x0004u
#define V2_SPI_RD               0x0008u
#define V2_SPI_WR               0x0000u
#define V2_SPI_DAT              0x0010u
#define V2_SPI_CMD              0x0000u
#define V2_SPI_1CLK             0x0020u
#define V2_SPI_BYTE             0x0000u

/* STATUS bits */
#define V2_STATUS_DMA_BUSY      0x0001u
#define V2_STATUS_DMA_TOUT      0x0002u
#define V2_STATUS_SPI           0x0010u

/* DMA_CFG values */
#define V2_DMA_SD_TO_RAM        0x0001u

/* Cart staging area for DMA reads. Same 0xB2000000 the X backend uses;
 * it's a project convention -- any cart-RDRAM slot that the FPGA can
 * write to and the CPU can PI-DMA from would work. */
#define V2_DMA_CART_ADDR        0xB2000000UL

/* Register unlock + version gate. */
#define V2_KEY_UNLOCK           0x1234u
#define V2_KEY_LOCK             0x0000u
#define V2_FW_MIN_FOR_V2        0x0116u

/* Probe pattern: 4-bit DAT pull-up returns 0x0F when the SPI controller
 * works as expected. */
#define V2_PROBE_EXPECT         0x000Fu

/* Sector-count cap matches SC64 / X-series; consistent caller contract. */
#define V2_SD_MAX_SECTORS       128u

/* PI Domain 2 BSD timings to use during V2 access. Restored after each
 * operation so other parts of the runtime see the timings they expect. */
#define V2_PI_DOM2_LAT          4u
#define V2_PI_DOM2_PWD          12u

/* PI BUSY wait ceiling: ~200ns per IO_READ; 500_000 ~= 100ms. */
#define V2_BUSY_TIMEOUT         500000

/* PI write needs a follow-up IO_READ to drain the PI write FIFO -- same
 * gotcha as the X backend / SC64 / IS-Viewer. */
#define V2_PI_WRITE_FLUSH(addr, val) do {  \
    IO_WRITE((addr), (val));               \
    (void) IO_READ(V2_REG_STATUS);         \
} while (0)

/* ---- File-static state ---- */

/* Live SPI_CFG value. Mirrors hardware so spi_mode() can OR/AND just
 * the mode bits without losing the speed / SS bits. */
static uint16_t sV2SpiCfg = V2_SPI_SPEED_LO | V2_SPI_SS;

/* Saved PI Domain 2 BSD timings, captured by lock and restored by unlock. */
static uint32_t sV2SavedDom2Lat = 0;
static uint32_t sV2SavedDom2Pwd = 0;
static int      sV2LockedTiming = 0;

/* Captured FW version from the most-recent detect(). 0 if detect never ran. */
static uint16_t sV2FwVer = 0;

/* The sd_host_t the protocol engine drives. Populated lazily on first
 * v2_sd_init(); the function pointers stay valid for the lifetime of
 * the program. */
static sd_host_t sV2Host;

/* ---- Register helpers ---- */

static uint32_t v2_reg_rd(uint32_t addr) { return IO_READ(addr); }
static void     v2_reg_wr(uint32_t addr, uint32_t val) { V2_PI_WRITE_FLUSH(addr, val); }

/* ---- SPI engine: configure mode, fire transaction, poll busy ---- */

/* Update the mode bits of the cached SPI_CFG and write the result. The
 * speed and SS bits are left untouched so callers don't have to think
 * about them. */
static void v2_spi_mode(uint16_t mode_bits)
{
    sV2SpiCfg &= (uint16_t)~(V2_SPI_RD | V2_SPI_DAT | V2_SPI_1CLK);
    sV2SpiCfg |= mode_bits;
    v2_reg_wr(V2_REG_SPI_CFG, sV2SpiCfg);
}

/* One SPI transaction: load a byte into REG_SPI, wait for the SPI busy
 * bit to clear. The number of clocks and which line(s) shift is decided
 * by the prior v2_spi_mode() call. */
static void v2_spi_tx(uint8_t val)
{
    int n;
    v2_reg_wr(V2_REG_SPI, (uint32_t)val);
    n = V2_BUSY_TIMEOUT;
    while (n > 0 && (v2_reg_rd(V2_REG_STATUS) & V2_STATUS_SPI)) {
        n--;
    }
    /* Timeout is silent: callers detect downstream via response wait. */
}

/* Fire a transaction with a dummy 0xFF and read whatever shifted in. */
static uint8_t v2_spi_rx(void)
{
    v2_spi_tx(0xFFu);
    return (uint8_t)(v2_reg_rd(V2_REG_SPI) & 0xFFu);
}

/* ---- PI Domain 2 lock / unlock ---- */

/* The default PI Domain 2 BSD timings the rest of the runtime uses are
 * too slow for reliable V2 SPI register access. We patch dom2_lat=4
 * and dom2_pwd=12 around each protocol operation, restoring afterward
 * so other consumers (e.g. SRAM access) see what they expect.
 *
 * __osPiGetAccess / __osPiRelAccess serialize against the libultra PI
 * manager; we still own the cart for the duration of lock(). */
static void v2_lock(void)
{
    __osPiGetAccess();
    sV2SavedDom2Lat = IO_READ(PI_BSD_DOM2_LAT_REG);
    sV2SavedDom2Pwd = IO_READ(PI_BSD_DOM2_PWD_REG);
    V2_PI_WRITE_FLUSH(PI_BSD_DOM2_LAT_REG, V2_PI_DOM2_LAT);
    V2_PI_WRITE_FLUSH(PI_BSD_DOM2_PWD_REG, V2_PI_DOM2_PWD);
    sV2LockedTiming = 1;
}

static void v2_unlock(void)
{
    if (sV2LockedTiming) {
        V2_PI_WRITE_FLUSH(PI_BSD_DOM2_LAT_REG, sV2SavedDom2Lat);
        V2_PI_WRITE_FLUSH(PI_BSD_DOM2_PWD_REG, sV2SavedDom2Pwd);
        sV2LockedTiming = 0;
    }
    __osPiRelAccess();
}

/* "Safe" lock: take the PI access mutex but don't reprogram timings.
 * Used during detect() so a probe failure on a non-V2 cart doesn't
 * leave Domain 2 timings altered. */
static void v2_lock_safe(void)
{
    __osPiGetAccess();
    sV2LockedTiming = 0;
}

static void v2_unlock_safe(void)
{
    __osPiRelAccess();
}

/* ---- sd_host SDIO callbacks ---- */

static void v2_sdh_set_spd(int hs)
{
    sV2SpiCfg &= (uint16_t)~V2_SPI_SPEED_MASK;
    if (hs) {
        /* card may still ignore the host clock change unless CMD6 was
         * issued, but that's the same trade-off the legacy iodev_ed64
         * (X) makes -- "bump and hope" works on every modern SDHC. */
        sV2SpiCfg |= V2_SPI_SPEED_50;
    } else {
        sV2SpiCfg |= V2_SPI_SPEED_LO;
    }
    v2_reg_wr(V2_REG_SPI_CFG, sV2SpiCfg);
}

static void v2_sdh_cmd_tx_byte(uint8_t b)
{
    v2_spi_mode(V2_SPI_CMD | V2_SPI_WR | V2_SPI_BYTE);
    v2_spi_tx(b);
}

static uint8_t v2_sdh_cmd_rx_byte(void)
{
    v2_spi_mode(V2_SPI_CMD | V2_SPI_RD | V2_SPI_BYTE);
    return v2_spi_rx();
}

static uint8_t v2_sdh_cmd_rx_bit(void)
{
    /* 1CLK on CMD line: returns one bit in the LSB. */
    v2_spi_mode(V2_SPI_CMD | V2_SPI_RD | V2_SPI_1CLK);
    return (uint8_t)(v2_spi_rx() & 0x01u);
}

static void v2_sdh_dat_tx_word(uint16_t w)
{
    /* In V2's BYTE mode the DAT bus emits 8 bits per spi_tx -- two
     * 4-bit nibbles per call. sd_host's dat_tx_word semantics are 16
     * bits = 4 nibbles, so we transmit two bytes here. The MSB nibble
     * of the high byte hits the bus first, matching the SD wire order. */
    v2_spi_mode(V2_SPI_DAT | V2_SPI_WR | V2_SPI_BYTE);
    v2_spi_tx((uint8_t)((w >> 8) & 0xFFu));
    v2_spi_tx((uint8_t)(w & 0xFFu));
}

static uint16_t v2_sdh_dat_rx_word(void)
{
    /* sd_host calls this only for single-nibble polls (DAT0 idle
     * checks during R1b waits, write-busy waits, the data response
     * token). 1CLK on DAT bus returns one nibble in the low 4 bits. */
    v2_spi_mode(V2_SPI_DAT | V2_SPI_RD | V2_SPI_1CLK);
    return (uint16_t)(v2_spi_rx() & 0x0Fu);
}

static void v2_sdh_dat_idle_clks(uint32_t n_clk)
{
    uint32_t n_bytes;
    uint32_t i;
    /* In BYTE mode on DAT bus, each spi_tx emits 2 SD clocks. To deliver
     * n_clk SD clocks we issue n_clk/2 byte transactions. The data byte
     * 0xFF puts all four DAT lines high for both clocks. */
    v2_spi_mode(V2_SPI_DAT | V2_SPI_WR | V2_SPI_BYTE);
    n_bytes = n_clk / 2u;
    for (i = 0; i < n_bytes; i++) {
        v2_spi_tx(0xFFu);
    }
}

/* ---- rx_mblk: FPGA DMA fast read ---- */

/* The V2 FPGA, like the X-series, can DMA multiple sectors directly
 * from the SD bus into a cart-bus staging area. Differences from X:
 *   - The DMA address register expects the staging address in 2KB
 *     units (right-shift by 11) -- not a full 32-bit cart address.
 *   - The DMA length register expects (n_blk - 1), not n_blk.
 *   - There's a single REG_DMA_CFG register; selecting SD_TO_RAM (1)
 *     starts the transfer. */
static sd_host_result_t v2_sdh_rx_mblk(struct sd_host *h,
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

    /* CMD18 READ_MULTIPLE_BLOCK. */
    r = sd_host_send_cmd_r1(h, SDP_CMD18, addr, resp);
    if (r != SD_HOST_OK) return r;

    /* Set DAT-bus shift mode for the FPGA's DMA reader. The mode bits
     * stay applied for the duration of the DMA. */
    v2_spi_mode(V2_SPI_DAT | V2_SPI_RD | V2_SPI_1CLK);

    /* Trigger DMA. The address register expects 2KB units. */
    v2_reg_wr(V2_REG_DMA_LEN,  n_blk - 1u);
    v2_reg_wr(V2_REG_DMA_ADDR, V2_DMA_CART_ADDR >> 11);
    V2_PI_WRITE_FLUSH(V2_REG_DMA_CFG, V2_DMA_SD_TO_RAM);

    /* Wait for DMA to finish. */
    n = V2_BUSY_TIMEOUT;
    while (n > 0 && (v2_reg_rd(V2_REG_STATUS) & V2_STATUS_DMA_BUSY)) {
        n--;
    }
    if (n <= 0 || (v2_reg_rd(V2_REG_STATUS) & V2_STATUS_DMA_TOUT)) {
        (void)sd_host_send_cmd_r1(h, SDP_CMD12, 0u, resp);
        return SD_HOST_ERR_IO;
    }

    /* CMD12 STOP_TRANSMISSION (R1b). Absorbs the card's busy hold. */
    r = sd_host_send_cmd_r1b(h, SDP_CMD12, 0u, resp);
    if (r != SD_HOST_OK) return r;

    /* PI DMA from cart staging to game RDRAM. */
    osWritebackDCache(dst, (s32)(n_blk * 512u));
    osCreateMesgQueue(&dma_queue, &dma_mesg, 1);
    osPiStartDma(&dma_msg, OS_MESG_PRI_NORMAL, OS_READ,
                 V2_DMA_CART_ADDR, dst, n_blk * 512u, &dma_queue);
    (void)osRecvMesg(&dma_queue, NULL, OS_MESG_BLOCK);
    osInvalDCache(dst, (s32)(n_blk * 512u));

    return SD_HOST_OK;
}

/* ---- tx_mblk: per-block CMD24 + 4-bit data path (no DMA write on V2) ---- */

/* V2 seems to have no FPGA DMA writer.
 * Writes go block-by-block via CPU-driven DAT bus emission. The wire
 * format is identical to the X backend's slow write path -- only the
 * byte transport differs (BYTE-mode spi_tx instead of dat_tx_word). */
static sd_host_result_t v2_write_one_block(struct sd_host *h,
                                            uint32_t lba_or_byte_addr,
                                            const uint8_t *src)
{
    sd_host_result_t r;
    uint8_t resp[6];
    uint64_t crc;
    uint16_t c0, c1, c2, c3;
    uint32_t poll;
    uint32_t i;
    int k;
    uint8_t crc_bytes[8];
    uint8_t resp_token;
    uint16_t w;

    /* CMD24 + R1. */
    r = sd_host_send_cmd_r1(h, SDP_CMD24, lba_or_byte_addr, resp);
    if (r != SD_HOST_OK) return r;

    /* Wait for DAT bus idle (low nibble of single-clock read == 0xF). */
    for (poll = 0; poll < V2_BUSY_TIMEOUT; poll++) {
        w = h->sdio_dat_rx_word();
        if ((w & 0x000Fu) == 0x000Fu) break;
    }
    if (poll >= V2_BUSY_TIMEOUT) return SD_HOST_ERR_TIMEOUT;

    /* Pre-compute the 4-lane CRC16 for the data block. */
    crc = sd_crc16_4bit(src, 512u);
    c0 = (uint16_t)(crc & 0xFFFFu);
    c1 = (uint16_t)((crc >> 16) & 0xFFFFu);
    c2 = (uint16_t)((crc >> 32) & 0xFFFFu);
    c3 = (uint16_t)((crc >> 48) & 0xFFFFu);
    /* Pack the 4 CRC16s into 8 bytes nibble-interleaved (clock k carries
     * one bit from each lane, MSB first). Same packing as the X backend. */
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

    /* Preamble: at least 2 idle clocks (8 nibbles). dat_tx_word
     * transmits 4 nibbles per call; second call starts the start
     * nibble (0x0) on the trailing clock. */
    h->sdio_dat_tx_word(0xFFFFu);
    h->sdio_dat_tx_word(0xFFF0u);

    /* Data: 256 dat_words covering 512 bytes. (b[i] << 8) | b[i+1] so
     * the high nibble of byte b[i] hits the bus first. */
    for (i = 0; i < 512u; i += 2u) {
        w = (uint16_t)(((uint16_t)src[i] << 8) | (uint16_t)src[i + 1]);
        h->sdio_dat_tx_word(w);
    }

    /* CRC. */
    for (i = 0; i < 8u; i += 2u) {
        w = (uint16_t)(((uint16_t)crc_bytes[i] << 8) | (uint16_t)crc_bytes[i + 1]);
        h->sdio_dat_tx_word(w);
    }

    /* End nibble. */
    h->sdio_dat_tx_word(0xFFFFu);

    /* Receive 5-bit data response token on DAT0. */
    resp_token = 0xFFu;
    for (poll = 0; poll < 64u; poll++) {
        w = h->sdio_dat_rx_word();
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

    /* Card-busy: DAT0 stays low until programming completes. */
    for (poll = 0; poll < V2_BUSY_TIMEOUT; poll++) {
        w = h->sdio_dat_rx_word();
        if ((w & 0x0001u) != 0u) {
            return SD_HOST_OK;
        }
    }
    return SD_HOST_ERR_TIMEOUT;
}

static sd_host_result_t v2_sdh_tx_mblk(struct sd_host *h,
                                        uint32_t addr,
                                        const void *src,
                                        uint32_t n_blk)
{
    /* sd_host hands us LBA (SDHC) or byte address (SDSC) in `addr`.
     * Per-block stride is 1 (LBA) or 512 (bytes). */
    uint32_t stride = (h->card_kind == SD_CARD_HC) ? 1u : 512u;
    const uint8_t *p = (const uint8_t *)src;
    uint32_t i;
    sd_host_result_t r;

    for (i = 0; i < n_blk; i++) {
        r = v2_write_one_block(h, addr + i * stride, p + i * 512u);
        if (r != SD_HOST_OK) return r;
    }
    return SD_HOST_OK;
}

/* ---- Backend hooks ---- */

static iodev_id_t v2_detect(void)
{
    uint16_t fw;
    int n;
    uint16_t probe;

    v2_lock_safe();

    /* Open registers. */
    V2_PI_WRITE_FLUSH(V2_REG_KEY, V2_KEY_UNLOCK);

    /* Firmware version gate: V2/V2.5 are 0x0116+. V1 is 0x0100..0x0115;
     * pre-1.00 is none-of-the-above. */
    fw = (uint16_t)(IO_READ(V2_REG_VER) & 0xFFFFu);
    sV2FwVer = fw;
    if (fw < V2_FW_MIN_FOR_V2) {
        V2_PI_WRITE_FLUSH(V2_REG_KEY, V2_KEY_LOCK);
        v2_unlock_safe();
        return IODEV_NONE;
    }

    /* Switch to PI Domain 2 timings the V2 SPI controller actually wants.
     * Without this the SPI probe below will read garbage on real hardware. */
    sV2SavedDom2Lat = IO_READ(PI_BSD_DOM2_LAT_REG);
    sV2SavedDom2Pwd = IO_READ(PI_BSD_DOM2_PWD_REG);
    V2_PI_WRITE_FLUSH(PI_BSD_DOM2_LAT_REG, V2_PI_DOM2_LAT);
    V2_PI_WRITE_FLUSH(PI_BSD_DOM2_PWD_REG, V2_PI_DOM2_PWD);

    /* Probe: in DAT-bus single-clock read mode the card-side pull-ups
     * read all four lines high (0x0F). Any other value means either
     * (a) we're not on a V2 cart, or (b) the SPI controller isn't wired
     * up the way the firmware expects. */
    V2_PI_WRITE_FLUSH(V2_REG_SPI_CFG,
                      V2_SPI_SPEED_LO | V2_SPI_SS | V2_SPI_RD | V2_SPI_DAT | V2_SPI_1CLK);
    V2_PI_WRITE_FLUSH(V2_REG_SPI, 0x00u);
    n = 32;
    while (n > 0 && (IO_READ(V2_REG_STATUS) & V2_STATUS_SPI)) {
        n--;
    }

    probe = (uint16_t)(IO_READ(V2_REG_SPI) & 0xFFFFu);

    /* Restore Domain 2 timings before returning -- detect must leave PI
     * in the state we found it. */
    V2_PI_WRITE_FLUSH(PI_BSD_DOM2_LAT_REG, sV2SavedDom2Lat);
    V2_PI_WRITE_FLUSH(PI_BSD_DOM2_PWD_REG, sV2SavedDom2Pwd);

    if (n <= 0 || probe != V2_PROBE_EXPECT) {
        V2_PI_WRITE_FLUSH(V2_REG_KEY, V2_KEY_LOCK);
        v2_unlock_safe();
        return IODEV_NONE;
    }

    v2_unlock_safe();
    return IODEV_ED64;   /* same id as X-series; backend-specific behavior is bounded inside this file */
}

uint16_t iodev_ed64_v2_fw_version(void) { return sV2FwVer; }

static void v2_populate_host(void)
{
    sV2Host.proto              = SD_PROTO_SDIO;
    sV2Host.user               = 0;
    sV2Host.lock               = v2_lock;
    sV2Host.unlock             = v2_unlock;
    sV2Host.set_spd            = v2_sdh_set_spd;
    sV2Host.sdio_cmd_tx_byte   = v2_sdh_cmd_tx_byte;
    sV2Host.sdio_cmd_rx_byte   = v2_sdh_cmd_rx_byte;
    sV2Host.sdio_cmd_rx_bit    = v2_sdh_cmd_rx_bit;
    sV2Host.sdio_dat_tx_word   = v2_sdh_dat_tx_word;
    sV2Host.sdio_dat_rx_word   = v2_sdh_dat_rx_word;
    sV2Host.sdio_dat_idle_clks = v2_sdh_dat_idle_clks;
    sV2Host.spi_io             = 0;
    sV2Host.spi_tx_buf         = 0;
    sV2Host.spi_rx_buf         = 0;
    sV2Host.rx_mblk            = v2_sdh_rx_mblk;
    sV2Host.tx_mblk            = v2_sdh_tx_mblk;
    sV2Host.card_kind          = SD_CARD_NONE;
    sV2Host.rca                = 0u;
    sV2Host.hs_active          = 0;

    /* Reset live SPI_CFG to the safe init value. */
    sV2SpiCfg = V2_SPI_SPEED_LO | V2_SPI_SS;
}

static iodev_result_t v2_sd_init(void)
{
    sd_host_result_t r;
    v2_populate_host();
    r = sd_host_init(&sV2Host);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t v2_sd_read_sectors(uint32_t lba, uint32_t count, void *buf)
{
    sd_host_result_t r;

    if (count == 0u || count > V2_SD_MAX_SECTORS) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u)                    return IODEV_ERR_PARAM;

    r = sd_host_read_blocks(&sV2Host, lba, buf, count);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t v2_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf)
{
    sd_host_result_t r;

    if (count == 0u || count > V2_SD_MAX_SECTORS) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u)                    return IODEV_ERR_PARAM;

    r = sd_host_write_blocks(&sV2Host, lba, buf, count);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t v2_sd_release(void)
{
    return IODEV_OK;
}

static const iodev_backend_t V2_BACKEND = {
    IODEV_ED64,
    v2_detect,
    v2_sd_init,
    v2_sd_read_sectors,
    v2_sd_write_sectors,
    v2_sd_release,
};

const iodev_backend_t *iodev_backend_ed64_v2(void) { return &V2_BACKEND; }
