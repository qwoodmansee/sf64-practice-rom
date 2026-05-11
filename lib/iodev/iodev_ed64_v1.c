/* lib/iodev/iodev_ed64_v1.c — EverDrive 64 V1 backend.
 *
 * V1 (Krikzz EverDrive 64 firmware versions 1.00..1.15) drives the SD
 * card via a 1-bit SPI bus on the CMD line, with a software-driven
 * chip-select. Same FPGA register file as V2 / V2.5 (REG_BASE
 * 0x08040000), but the SPI controller is the older byte-oriented
 * variant -- no native 4-bit DAT mode.
 *
 * This file is a clean-room implementation. Register addresses, CFG
 * bitfields, and detection patterns are facts about Krikzz hardware
 * (not copyrightable). No code structure, idioms, or comments copied
 * from gz.
 *
 * Hardware reference (firmware < 1.16):
 *   - Same REG_BASE 0xA8040000 / register indices as V2.
 *   - REG_SPI_CFG bitfields:
 *       [1:0] SPEED: 0=50MHz, 1=25MHz, 2=<400kHz
 *       [2]   SS:    0=on (CS asserted, low), 1=off (CS released, high)
 *     (No SPI_RD / SPI_DAT / SPI_1CLK on FW < 1.16: REG_SPI is always
 *      a byte-oriented full-duplex 8-clock SPI exchange on the CMD line.)
 *   - REG_STATUS bit [4] = SPI busy.
 *   - DMA path: same as V2 (REG_DMA_LEN = n_blk - 1, REG_DMA_ADDR in
 *     2KB units, REG_DMA_CFG = SD_TO_RAM=1). FPGA-DMA reads work on V1
 *     the same way they do on V2.
 *   - Detection: REG_KEY=0x1234 unlock, REG_VER in [0x0100, 0x0115],
 *     SPI probe writing 0x00 returns 0xFF (MISO line idle high).
 */

#include "PR/rcp.h"
#include "libultra/ultra64.h"
#include "iodev.h"
#include "iodev_internal.h"
#include "sd_host/sd_host.h"
#include "sd_host/sd_proto.h"
#include "sd_crc.h"

extern void __osPiGetAccess(void);
extern void __osPiRelAccess(void);

/* ---- FPGA register addresses (same layout as V2) ---- */

#define V1_REG_BASE             0x08040000UL
#define V1_REG_ADDR(idx)        (V1_REG_BASE + ((unsigned long)(idx) << 2))

#define V1_REG_STATUS           V1_REG_ADDR(1)
#define V1_REG_DMA_LEN          V1_REG_ADDR(2)
#define V1_REG_DMA_ADDR         V1_REG_ADDR(3)
#define V1_REG_DMA_CFG          V1_REG_ADDR(5)
#define V1_REG_SPI              V1_REG_ADDR(6)
#define V1_REG_SPI_CFG          V1_REG_ADDR(7)
#define V1_REG_KEY              V1_REG_ADDR(8)
#define V1_REG_VER              V1_REG_ADDR(11)

/* SPI_CFG bits valid on FW < 1.16. */
#define V1_SPI_SPEED_50         0x0000u
#define V1_SPI_SPEED_25         0x0001u
#define V1_SPI_SPEED_LO         0x0002u
#define V1_SPI_SPEED_MASK       0x0003u
#define V1_SPI_SS               0x0004u    /* set = CS off */

/* STATUS bits */
#define V1_STATUS_DMA_BUSY      0x0001u
#define V1_STATUS_DMA_TOUT      0x0002u
#define V1_STATUS_SPI           0x0010u

#define V1_DMA_SD_TO_RAM        0x0001u

#define V1_DMA_CART_ADDR        0xB2000000UL

/* Version range for V1: 1.00 inclusive .. 1.16 exclusive. */
#define V1_KEY_UNLOCK           0x1234u
#define V1_KEY_LOCK             0x0000u
#define V1_FW_MIN               0x0100u
#define V1_FW_MAX_EXCL          0x0116u

/* Probe: writing 0x00 in plain SPI mode shifts in 8 bits from the
 * MISO pull-up, which idle-reads 0xFF. */
#define V1_PROBE_EXPECT         0x00FFu

#define V1_SD_MAX_SECTORS       128u

#define V1_PI_DOM2_LAT          4u
#define V1_PI_DOM2_PWD          12u

#define V1_BUSY_TIMEOUT         500000

#define V1_PI_WRITE_FLUSH(addr, val) do {  \
    IO_WRITE((addr), (val));               \
    (void) IO_READ(V1_REG_STATUS);         \
} while (0)

/* ---- File-static state ---- */

static uint16_t sV1SpiCfg = V1_SPI_SPEED_LO | V1_SPI_SS;
static uint32_t sV1SavedDom2Lat = 0;
static uint32_t sV1SavedDom2Pwd = 0;
static int      sV1LockedTiming = 0;
static uint16_t sV1FwVer = 0;
static sd_host_t sV1Host;

/* ---- Register helpers ---- */

static uint32_t v1_reg_rd(uint32_t addr) { return IO_READ(addr); }
static void     v1_reg_wr(uint32_t addr, uint32_t val) { V1_PI_WRITE_FLUSH(addr, val); }

/* ---- SPI engine ---- */

/* One SPI byte exchange: write the byte, wait for SPI_BUSY clear, read
 * back whatever shifted in on MISO. */
static uint8_t v1_spi_io(uint8_t out)
{
    int n;
    v1_reg_wr(V1_REG_SPI, (uint32_t)out);
    n = V1_BUSY_TIMEOUT;
    while (n > 0 && (v1_reg_rd(V1_REG_STATUS) & V1_STATUS_SPI)) {
        n--;
    }
    return (uint8_t)(v1_reg_rd(V1_REG_SPI) & 0xFFu);
}

/* ---- PI Domain 2 lock / unlock ---- */

static void v1_lock(void)
{
    __osPiGetAccess();
    sV1SavedDom2Lat = IO_READ(PI_BSD_DOM2_LAT_REG);
    sV1SavedDom2Pwd = IO_READ(PI_BSD_DOM2_PWD_REG);
    V1_PI_WRITE_FLUSH(PI_BSD_DOM2_LAT_REG, V1_PI_DOM2_LAT);
    V1_PI_WRITE_FLUSH(PI_BSD_DOM2_PWD_REG, V1_PI_DOM2_PWD);
    sV1LockedTiming = 1;
}

static void v1_unlock(void)
{
    if (sV1LockedTiming) {
        V1_PI_WRITE_FLUSH(PI_BSD_DOM2_LAT_REG, sV1SavedDom2Lat);
        V1_PI_WRITE_FLUSH(PI_BSD_DOM2_PWD_REG, sV1SavedDom2Pwd);
        sV1LockedTiming = 0;
    }
    __osPiRelAccess();
}

static void v1_lock_safe(void)
{
    __osPiGetAccess();
    sV1LockedTiming = 0;
}

static void v1_unlock_safe(void)
{
    __osPiRelAccess();
}

/* ---- sd_host SPI callbacks ---- */

static void v1_sdh_set_spd(int hs)
{
    sV1SpiCfg &= (uint16_t)~V1_SPI_SPEED_MASK;
    if (hs) {
        sV1SpiCfg |= V1_SPI_SPEED_25;   /* V1 hardware tops out lower than X / V2 */
    } else {
        sV1SpiCfg |= V1_SPI_SPEED_LO;
    }
    v1_reg_wr(V1_REG_SPI_CFG, sV1SpiCfg);
}

static void v1_sdh_spi_ss(int select)
{
    if (select) {
        sV1SpiCfg &= (uint16_t)~V1_SPI_SS;     /* CS asserted (low) */
    } else {
        sV1SpiCfg |= V1_SPI_SS;                /* CS released (high) */
    }
    v1_reg_wr(V1_REG_SPI_CFG, sV1SpiCfg);
}

static uint8_t v1_sdh_spi_io(uint8_t out)
{
    return v1_spi_io(out);
}

static void v1_sdh_spi_tx_buf(const void *buf, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t i;
    for (i = 0; i < n; i++) {
        (void)v1_spi_io(p[i]);
    }
}

static void v1_sdh_spi_rx_buf(void *buf, uint32_t n)
{
    uint8_t *p = (uint8_t *)buf;
    uint32_t i;
    for (i = 0; i < n; i++) {
        p[i] = v1_spi_io(SDP_SPI_IDLE_BYTE);
    }
}

static void v1_sdh_spi_tx_clk(uint8_t mosi_byte, uint32_t n_clk)
{
    /* Each spi_io is 8 SD clocks. */
    uint32_t n_bytes = n_clk / 8u;
    uint32_t i;
    for (i = 0; i < n_bytes; i++) {
        (void)v1_spi_io(mosi_byte);
    }
}

/* ---- rx_mblk: FPGA DMA fast read ---- */

/* V1 has the same FPGA DMA fast-read path as V2. The DMA reads the
 * card's data block stream (after the host issued CMD18 + R1 + the
 * 0xFE start token has appeared on MISO) directly into the cart
 * staging area. We let the FPGA eat the start token by leaving the
 * SPI controller in read mode while the DMA runs. */
static sd_host_result_t v1_sdh_rx_mblk(struct sd_host *h,
                                        uint32_t addr,
                                        void *dst,
                                        uint32_t n_blk)
{
    uint32_t i;
    uint8_t r1;
    int n;
    OSIoMesg dma_msg;
    OSMesgQueue dma_queue;
    OSMesg dma_mesg;

    /* Issue CMD18 manually (not via sd_host_send_cmd_r1, which is SDIO
     * only). The address the protocol engine handed us is already in
     * wire form (LBA for SDHC, byte for SDSC). IDO is C89 -- no
     * compound literals; declare the frame buffer up front. */
    {
        uint8_t frame[5];
        uint8_t crc;
        frame[0] = (uint8_t)(0x40u | SDP_CMD18);
        frame[1] = (uint8_t)(addr >> 24);
        frame[2] = (uint8_t)(addr >> 16);
        frame[3] = (uint8_t)(addr >>  8);
        frame[4] = (uint8_t)(addr);
        crc = sd_crc7(frame, 5);
        h->spi_io(frame[0]);
        h->spi_io(frame[1]);
        h->spi_io(frame[2]);
        h->spi_io(frame[3]);
        h->spi_io(frame[4]);
        h->spi_io(crc);
    }

    /* Wait for R1 from the card. */
    r1 = 0xFFu;
    for (i = 0; i < 16u; i++) {
        uint8_t b = h->spi_io(SDP_SPI_IDLE_BYTE);
        if ((b & 0x80u) == 0u) { r1 = b; break; }
    }
    if (r1 != 0x00u) {
        /* Send CMD12 to abort, just in case the card started streaming. */
        h->spi_io((uint8_t)(0x40u | SDP_CMD12));
        h->spi_io(0); h->spi_io(0); h->spi_io(0); h->spi_io(0);
        h->spi_io(SDP_SPI_IDLE_BYTE);
        return (r1 == 0xFFu) ? SD_HOST_ERR_TIMEOUT : SD_HOST_ERR_CARD;
    }

    /* Trigger the FPGA DMA. The FPGA waits for the 0xFE start token
     * itself, then streams n_blk * 512 bytes into staging. */
    v1_reg_wr(V1_REG_DMA_LEN,  n_blk - 1u);
    v1_reg_wr(V1_REG_DMA_ADDR, V1_DMA_CART_ADDR >> 11);
    V1_PI_WRITE_FLUSH(V1_REG_DMA_CFG, V1_DMA_SD_TO_RAM);

    n = V1_BUSY_TIMEOUT;
    while (n > 0 && (v1_reg_rd(V1_REG_STATUS) & V1_STATUS_DMA_BUSY)) {
        n--;
    }
    if (n <= 0 || (v1_reg_rd(V1_REG_STATUS) & V1_STATUS_DMA_TOUT)) {
        /* Send CMD12 to recover. */
        h->spi_io((uint8_t)(0x40u | SDP_CMD12));
        h->spi_io(0); h->spi_io(0); h->spi_io(0); h->spi_io(0);
        h->spi_io(SDP_SPI_IDLE_BYTE);
        return SD_HOST_ERR_IO;
    }

    /* CMD12 STOP_TRANSMISSION. R1b in SPI = R1 + busy on MISO. */
    h->spi_io((uint8_t)(0x40u | SDP_CMD12));
    h->spi_io(0); h->spi_io(0); h->spi_io(0); h->spi_io(0);
    h->spi_io(SDP_SPI_IDLE_BYTE);
    /* Wait for one stuff byte (CMD12-specific Nec) then for busy clear. */
    (void)h->spi_io(SDP_SPI_IDLE_BYTE);
    {
        uint32_t k;
        for (k = 0; k < V1_BUSY_TIMEOUT; k++) {
            if (h->spi_io(SDP_SPI_IDLE_BYTE) == 0xFFu) break;
        }
        if (k >= V1_BUSY_TIMEOUT) return SD_HOST_ERR_TIMEOUT;
    }

    /* PI DMA from cart staging to game RDRAM. */
    osWritebackDCache(dst, (s32)(n_blk * 512u));
    osCreateMesgQueue(&dma_queue, &dma_mesg, 1);
    osPiStartDma(&dma_msg, OS_MESG_PRI_NORMAL, OS_READ,
                 V1_DMA_CART_ADDR, dst, n_blk * 512u, &dma_queue);
    (void)osRecvMesg(&dma_queue, NULL, OS_MESG_BLOCK);
    osInvalDCache(dst, (s32)(n_blk * 512u));

    return SD_HOST_OK;
}

/* tx_mblk left NULL: writes go through sd_host's slow per-block CMD24
 * path (sdh_spi_write_one_block in lib/sd_host/sd_host.c). */

/* ---- Backend hooks ---- */

static iodev_id_t v1_detect(void)
{
    uint16_t fw;
    int n;
    uint16_t probe;

    v1_lock_safe();

    V1_PI_WRITE_FLUSH(V1_REG_KEY, V1_KEY_UNLOCK);

    fw = (uint16_t)(IO_READ(V1_REG_VER) & 0xFFFFu);
    sV1FwVer = fw;
    if (fw < V1_FW_MIN || fw >= V1_FW_MAX_EXCL) {
        V1_PI_WRITE_FLUSH(V1_REG_KEY, V1_KEY_LOCK);
        v1_unlock_safe();
        return IODEV_NONE;
    }

    sV1SavedDom2Lat = IO_READ(PI_BSD_DOM2_LAT_REG);
    sV1SavedDom2Pwd = IO_READ(PI_BSD_DOM2_PWD_REG);
    V1_PI_WRITE_FLUSH(PI_BSD_DOM2_LAT_REG, V1_PI_DOM2_LAT);
    V1_PI_WRITE_FLUSH(PI_BSD_DOM2_PWD_REG, V1_PI_DOM2_PWD);

    /* Probe: with CS released and clock at init speed, writing 0x00 to
     * REG_SPI shifts in 8 bits of MISO. The pull-up reads as 0xFF on a
     * working V1 cart. Anything else (including 0x00 or 0xFFFF) means
     * either no V1 cart or the SPI controller isn't responding. */
    V1_PI_WRITE_FLUSH(V1_REG_SPI_CFG, V1_SPI_SPEED_LO | V1_SPI_SS);
    V1_PI_WRITE_FLUSH(V1_REG_SPI, 0x00u);
    n = 32;
    while (n > 0 && (IO_READ(V1_REG_STATUS) & V1_STATUS_SPI)) {
        n--;
    }
    probe = (uint16_t)(IO_READ(V1_REG_SPI) & 0xFFFFu);

    V1_PI_WRITE_FLUSH(PI_BSD_DOM2_LAT_REG, sV1SavedDom2Lat);
    V1_PI_WRITE_FLUSH(PI_BSD_DOM2_PWD_REG, sV1SavedDom2Pwd);

    if (n <= 0 || probe != V1_PROBE_EXPECT) {
        V1_PI_WRITE_FLUSH(V1_REG_KEY, V1_KEY_LOCK);
        v1_unlock_safe();
        return IODEV_NONE;
    }

    v1_unlock_safe();
    return IODEV_ED64;
}

uint16_t iodev_ed64_v1_fw_version(void) { return sV1FwVer; }

static void v1_populate_host(void)
{
    sV1Host.proto              = SD_PROTO_SPI;
    sV1Host.user               = 0;
    sV1Host.lock               = v1_lock;
    sV1Host.unlock             = v1_unlock;
    sV1Host.set_spd            = v1_sdh_set_spd;
    sV1Host.sdio_cmd_tx_byte   = 0;
    sV1Host.sdio_cmd_rx_byte   = 0;
    sV1Host.sdio_cmd_rx_bit    = 0;
    sV1Host.sdio_dat_tx_word   = 0;
    sV1Host.sdio_dat_rx_word   = 0;
    sV1Host.sdio_dat_idle_clks = 0;
    sV1Host.spi_ss             = v1_sdh_spi_ss;
    sV1Host.spi_io             = v1_sdh_spi_io;
    sV1Host.spi_tx_buf         = v1_sdh_spi_tx_buf;
    sV1Host.spi_rx_buf         = v1_sdh_spi_rx_buf;
    sV1Host.spi_tx_clk         = v1_sdh_spi_tx_clk;
    sV1Host.rx_mblk            = v1_sdh_rx_mblk;
    sV1Host.tx_mblk            = 0;        /* slow per-block writes */
    sV1Host.card_kind          = SD_CARD_NONE;
    sV1Host.rca                = 0u;
    sV1Host.hs_active          = 0;

    sV1SpiCfg = V1_SPI_SPEED_LO | V1_SPI_SS;
}

static iodev_result_t v1_sd_init(void)
{
    sd_host_result_t r;
    v1_populate_host();
    r = sd_host_init(&sV1Host);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t v1_sd_read_sectors(uint32_t lba, uint32_t count, void *buf)
{
    sd_host_result_t r;

    if (count == 0u || count > V1_SD_MAX_SECTORS) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u)                    return IODEV_ERR_PARAM;

    r = sd_host_read_blocks(&sV1Host, lba, buf, count);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t v1_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf)
{
    sd_host_result_t r;

    if (count == 0u || count > V1_SD_MAX_SECTORS) return IODEV_ERR_PARAM;
    if (((uintptr_t)buf) & 7u)                    return IODEV_ERR_PARAM;

    r = sd_host_write_blocks(&sV1Host, lba, buf, count);

    if (r == SD_HOST_OK)             return IODEV_OK;
    if (r == SD_HOST_ERR_TIMEOUT)    return IODEV_ERR_TIMEOUT;
    if (r == SD_HOST_ERR_PARAM)      return IODEV_ERR_PARAM;
    return IODEV_ERR_IO;
}

static iodev_result_t v1_sd_release(void)
{
    return IODEV_OK;
}

static const iodev_backend_t V1_BACKEND = {
    IODEV_ED64,
    v1_detect,
    v1_sd_init,
    v1_sd_read_sectors,
    v1_sd_write_sectors,
    v1_sd_release,
};

const iodev_backend_t *iodev_backend_ed64_v1(void) { return &V1_BACKEND; }
