/* lib/sd_host/sd_host.c — generic SD-card protocol engine.
 *
 * Drives an SD card through CMD0/8/41/2/3/7/ACMD6/CMD16 init, then
 * delegates block I/O to the backend's rx_mblk/tx_mblk fast path.
 *
 * Pure logic: no libultra, no MMIO. All hardware contact happens through
 * the function-pointer table in sd_host_t. This keeps the file
 * host-portable for unit tests under lib/test/.
 *
 * IDO C89 compatible: declarations at top of block, no designated
 * initializers, no // comments. */

#include "sd_host.h"
#include "sd_proto.h"
#include "sd_crc.h"

/* Iteration ceiling for poll loops. Backend responsibility: ED64 sets
 * BITLEN appropriately so each cmd_rx_bit / dat_rx_word call corresponds
 * to one SD clock-tick worth of work. ~1 us per call at init speed,
 * so 500 000 ~= 0.5 s. */
#define SDH_POLL_TIMEOUT  500000u

/* ACMD41 init loop. Spec mandates host allow >=1 s. Each iteration is
 * ~1.5 ms at init speed, so 4000 covers a slow SDXC. */
#define SDH_ACMD41_RETRIES  4000u

/* SD block size. Always 512 bytes -- SDHC mandates it; SDSC is
 * configured by CMD16 below. */
#define SDH_BLOCK_BYTES   512u

/* ============================================================ *
 *  CMD frame helpers (host -> card, SDIO mode)                  *
 * ============================================================ */

/* Build a 6-byte SD command frame and shift it out on the CMD line. */
static void sdh_sdio_send_cmd(sd_host_t *h, uint8_t cmd, uint32_t arg)
{
    uint8_t frame[6];

    frame[0] = (uint8_t)(0x40u | (cmd & 0x3Fu));
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >>  8);
    frame[4] = (uint8_t)(arg);
    frame[5] = sd_crc7(frame, 5);   /* sd_crc7 ORs in the trailing 1-bit */

    /* Dummy 0xFF before the frame: gives the card a clock cycle to
     * release the CMD line if it was driving (e.g. tail end of a
     * previous response). The legacy ED64 backend has done this since
     * day one and we keep it. */
    h->sdio_cmd_tx_byte(0xFFu);

    h->sdio_cmd_tx_byte(frame[0]);
    h->sdio_cmd_tx_byte(frame[1]);
    h->sdio_cmd_tx_byte(frame[2]);
    h->sdio_cmd_tx_byte(frame[3]);
    h->sdio_cmd_tx_byte(frame[4]);
    h->sdio_cmd_tx_byte(frame[5]);
}

/* Poll the CMD line one bit at a time for the response start bit (0).
 * Returns SD_HOST_OK once seen, SD_HOST_ERR_TIMEOUT otherwise. */
static sd_host_result_t sdh_sdio_wait_resp_start(sd_host_t *h)
{
    uint32_t i;
    uint8_t  bit;

    for (i = 0; i < SDH_POLL_TIMEOUT; i++) {
        bit = h->sdio_cmd_rx_bit();
        if ((bit & 0x01u) == 0u) {
            return SD_HOST_OK;
        }
    }
    return SD_HOST_ERR_TIMEOUT;
}

/* Receive an R1/R3/R6/R7 response (6 bytes). The start bit (0) was
 * consumed by sdh_sdio_wait_resp_start; it is bit 7 of byte 0 (always
 * 0 in a valid response), so we just clock 6 byte-aligned reads. */
static sd_host_result_t sdh_sdio_recv_r1(sd_host_t *h, uint8_t resp[SDP_RESP_R1_BYTES])
{
    sd_host_result_t r;
    int i;

    r = sdh_sdio_wait_resp_start(h);
    if (r != SD_HOST_OK) return r;

    for (i = 0; i < SDP_RESP_R1_BYTES; i++) {
        resp[i] = h->sdio_cmd_rx_byte();
    }
    return SD_HOST_OK;
}

/* R2 is 17 bytes (136 bits). We only issue CMD2 (ALL_SEND_CID) and
 * always discard the CID -- so a single discard helper is enough. */
static sd_host_result_t sdh_sdio_recv_r2_discard(sd_host_t *h)
{
    sd_host_result_t r;
    int i;

    r = sdh_sdio_wait_resp_start(h);
    if (r != SD_HOST_OK) return r;

    for (i = 0; i < SDP_RESP_R2_BYTES; i++) {
        (void)h->sdio_cmd_rx_byte();
    }
    return SD_HOST_OK;
}

/* R6 has the same shape as R1; bytes [1:2] hold the new RCA. */
static sd_host_result_t sdh_sdio_recv_r6(sd_host_t *h, uint32_t *out_rca)
{
    uint8_t resp[SDP_RESP_R6_BYTES];
    sd_host_result_t r;

    r = sdh_sdio_recv_r1(h, resp);
    if (r != SD_HOST_OK) return r;

    if (out_rca != 0) {
        *out_rca = ((uint32_t)resp[1] << 8) | (uint32_t)resp[2];
    }
    return SD_HOST_OK;
}

/* R1b: poll DAT0 until the card releases it (bus high again). */
static sd_host_result_t sdh_sdio_wait_dat0_idle(sd_host_t *h)
{
    uint32_t i;
    uint16_t w;

    for (i = 0; i < SDH_POLL_TIMEOUT; i++) {
        w = h->sdio_dat_rx_word();
        /* DAT0 is the LSB of the most-recently-received nibble. */
        if ((w & 0x0001u) != 0u) {
            return SD_HOST_OK;
        }
    }
    return SD_HOST_ERR_TIMEOUT;
}

/* ============================================================ *
 *  Public command helpers (for backend rx_mblk / tx_mblk impls) *
 * ============================================================ */

sd_host_result_t sd_host_send_cmd_r1(sd_host_t *h, uint8_t cmd, uint32_t arg,
                                     uint8_t resp[6])
{
    if (h == 0 || resp == 0) return SD_HOST_ERR_PARAM;
    if (h->proto != SD_PROTO_SDIO) return SD_HOST_ERR_PARAM;

    sdh_sdio_send_cmd(h, cmd, arg);
    return sdh_sdio_recv_r1(h, resp);
}

sd_host_result_t sd_host_send_cmd_r1b(sd_host_t *h, uint8_t cmd, uint32_t arg,
                                      uint8_t resp[6])
{
    sd_host_result_t r;

    r = sd_host_send_cmd_r1(h, cmd, arg, resp);
    if (r != SD_HOST_OK) return r;
    return sdh_sdio_wait_dat0_idle(h);
}

/* ============================================================ *
 *  ACMD41 init loop                                              *
 * ============================================================ */

static sd_host_result_t sdh_sdio_acmd41_loop(sd_host_t *h, int hcs, uint32_t *out_ocr)
{
    sd_host_result_t r;
    uint8_t resp[SDP_RESP_R1_BYTES];
    uint32_t arg;
    uint32_t ocr;
    uint32_t i;

    arg = hcs ? SDP_ACMD41_ARG_HCS : 0u;
    ocr = 0u;

    for (i = 0; i < SDH_ACMD41_RETRIES; i++) {
        sdh_sdio_send_cmd(h, SDP_CMD55, 0u);
        r = sdh_sdio_recv_r1(h, resp);
        if (r != SD_HOST_OK) return r;

        sdh_sdio_send_cmd(h, SDP_ACMD41, arg);
        /* R3: same shape as R1 in our cmd_rx_byte stream. CRC byte at
         * the tail is undefined per spec but harmless to read. */
        r = sdh_sdio_recv_r1(h, resp);
        if (r != SD_HOST_OK) return r;

        ocr = ((uint32_t)resp[1] << 24) |
              ((uint32_t)resp[2] << 16) |
              ((uint32_t)resp[3] <<  8) |
               (uint32_t)resp[4];

        if ((ocr & SDP_OCR_BUSY_DONE) != 0u) {
            if (out_ocr != 0) *out_ocr = ocr;
            return SD_HOST_OK;
        }
    }
    return SD_HOST_ERR_TIMEOUT;
}

/* ============================================================ *
 *  SPI mode: protocol helpers                                   *
 * ============================================================ */

/* SPI-mode R1 is a single byte. Bit 7 = 0 indicates a valid response;
 * the host polls spi_io(0xFF) until it sees that. We cap at 16 dummy
 * bytes per the SD spec (Ncr = 0..8 bytes). 16 gives margin for slow
 * cards. Returns 0xFF on timeout (card never responded). */
static uint8_t sdh_spi_wait_r1(sd_host_t *h)
{
    int i;
    uint8_t b;
    for (i = 0; i < 16; i++) {
        b = h->spi_io(SDP_SPI_IDLE_BYTE);
        if ((b & 0x80u) == 0u) return b;
    }
    return 0xFFu;
}

/* Send a 6-byte SPI command frame. The CRC byte is supplied by the
 * caller because CMD0 / CMD8 require their precomputed values
 * (0x95 / 0x87) and other commands can use any byte with bit 0 = 1
 * (we always pass sd_crc7 for cleanliness). */
static void sdh_spi_send_cmd(sd_host_t *h, uint8_t cmd, uint32_t arg,
                              uint8_t crc_byte)
{
    h->spi_io((uint8_t)(0x40u | (cmd & 0x3Fu)));
    h->spi_io((uint8_t)(arg >> 24));
    h->spi_io((uint8_t)(arg >> 16));
    h->spi_io((uint8_t)(arg >>  8));
    h->spi_io((uint8_t)(arg));
    h->spi_io(crc_byte);
}

/* Compute the CRC byte for a (cmd, arg) pair via sd_crc7. The result
 * is bit-aligned the same way the SD spec wants it: 7-bit CRC in bits
 * 7..1, end bit in bit 0. */
static uint8_t sdh_spi_crc_byte(uint8_t cmd, uint32_t arg)
{
    uint8_t frame[5];
    frame[0] = (uint8_t)(0x40u | (cmd & 0x3Fu));
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >>  8);
    frame[4] = (uint8_t)(arg);
    return sd_crc7(frame, 5);
}

/* Send a command and read its R1. Returns 0xFF on time-out. */
static uint8_t sdh_spi_cmd_r1(sd_host_t *h, uint8_t cmd, uint32_t arg)
{
    uint8_t crc;
    if (cmd == SDP_CMD0) crc = SDP_CMD0_CRC_SPI;
    else if (cmd == SDP_CMD8) crc = SDP_CMD8_CRC_SPI;
    else crc = sdh_spi_crc_byte(cmd, arg);

    sdh_spi_send_cmd(h, cmd, arg, crc);
    return sdh_spi_wait_r1(h);
}

/* Issue a CMD55+ACMD pair. Returns the R1 from the ACMD. */
static uint8_t sdh_spi_acmd_r1(sd_host_t *h, uint8_t acmd, uint32_t arg)
{
    (void)sdh_spi_cmd_r1(h, SDP_CMD55, 0u);
    return sdh_spi_cmd_r1(h, acmd, arg);
}

/* Read 4 trailing bytes after R1 (CMD8 echo / CMD58 OCR). */
static uint32_t sdh_spi_read_uint32(sd_host_t *h)
{
    uint32_t v;
    v  = ((uint32_t)h->spi_io(SDP_SPI_IDLE_BYTE)) << 24;
    v |= ((uint32_t)h->spi_io(SDP_SPI_IDLE_BYTE)) << 16;
    v |= ((uint32_t)h->spi_io(SDP_SPI_IDLE_BYTE)) <<  8;
    v |=  (uint32_t)h->spi_io(SDP_SPI_IDLE_BYTE);
    return v;
}

/* Wait for a data start token (0xFE for single-block read / write,
 * 0xFC for multi-block write data, 0xFD for stop tran). Returns 0
 * on success, -1 on timeout. The poll budget here matches Nac max. */
static int sdh_spi_wait_token(sd_host_t *h, uint8_t expected)
{
    uint32_t i;
    uint8_t b;
    for (i = 0; i < SDH_POLL_TIMEOUT; i++) {
        b = h->spi_io(SDP_SPI_IDLE_BYTE);
        if (b == expected) return 0;
        /* Any non-0xFF non-token byte that has bit 4 = 0 is an error
         * token (5-bit data error response). The high nibble holds the
         * error info; we just bail. */
        if (b != SDP_SPI_IDLE_BYTE && (b & 0xF0u) == 0u) return -1;
    }
    return -1;
}

/* Wait for the card to release MISO from busy (returns 0xFF). Used
 * after CMD24 data write, after CMD12 stop, etc. */
static sd_host_result_t sdh_spi_wait_busy_clear(sd_host_t *h)
{
    uint32_t i;
    uint8_t b;
    for (i = 0; i < SDH_POLL_TIMEOUT; i++) {
        b = h->spi_io(SDP_SPI_IDLE_BYTE);
        if (b == 0xFFu) return SD_HOST_OK;
    }
    return SD_HOST_ERR_TIMEOUT;
}

/* ============================================================ *
 *  SPI mode: init                                                *
 * ============================================================ */

static sd_host_result_t sdh_spi_init(sd_host_t *h)
{
    int   hcs;
    uint8_t r1;
    uint32_t echo;
    uint32_t ocr;
    uint32_t i;

    /* Required SPI function pointers. */
    if (h->spi_ss == 0 || h->spi_io == 0 || h->spi_tx_clk == 0 ||
        h->set_spd == 0) {
        return SD_HOST_ERR_PARAM;
    }

    h->card_kind = SD_CARD_NONE;
    h->rca       = 0u;
    h->hs_active = 0;

    h->set_spd(0);

    /* Step 1: 80+ idle clocks with CS HIGH so the card enters SPI mode.
     * The byte 0xFF holds MOSI high during all 8 clocks per byte. */
    h->spi_ss(0);                       /* CS off */
    h->spi_tx_clk(SDP_SPI_IDLE_BYTE, 80u);

    /* Step 2: CS LOW, CMD0 with the precomputed 0x95 CRC byte. The
     * card must reply R1 = 0x01 (idle). */
    h->spi_ss(1);                       /* CS on */
    /* A few idle clocks with CS asserted before the first command --
     * some cards want this to settle before responding. */
    h->spi_tx_clk(SDP_SPI_IDLE_BYTE, 8u);

    r1 = sdh_spi_cmd_r1(h, SDP_CMD0, 0u);
    if (r1 != SDP_R1_IDLE) {
        return SD_HOST_ERR_CARD;
    }

    /* Step 3: CMD8. Card replies R7 = R1 + 4 trailing bytes that echo
     * the low 12 bits of arg. Bit 2 (illegal-command) of R1 indicates a
     * v1 / MMC card that doesn't understand CMD8 -- not a fatal error;
     * we set hcs=0 and skip the echo check. */
    r1 = sdh_spi_cmd_r1(h, SDP_CMD8, SDP_CMD8_ARG_VHS_27_36);
    if (r1 == 0xFFu) return SD_HOST_ERR_TIMEOUT;
    if (r1 & SDP_R1_ILLEGAL_CMD) {
        hcs = 0;
    } else {
        hcs  = 1;
        echo = sdh_spi_read_uint32(h);
        if ((echo & SDP_CMD8_ECHO_MASK) != SDP_CMD8_ECHO_EXPECTED) {
            return SD_HOST_ERR_CARD;
        }
    }

    /* Step 4: ACMD41 init loop. Card replies R1 = 0x01 while busy,
     * 0x00 when ready. */
    for (i = 0; i < SDH_ACMD41_RETRIES; i++) {
        r1 = sdh_spi_acmd_r1(h, SDP_ACMD41, hcs ? SDP_ACMD41_ARG_HCS : 0u);
        if (r1 == 0x00u) break;
        if (r1 == 0xFFu) return SD_HOST_ERR_TIMEOUT;
        if (r1 & ~(uint8_t)SDP_R1_IDLE) {
            /* Anything other than "idle" with no other bits set is an
             * error -- card refused our params. */
            return SD_HOST_ERR_CARD;
        }
    }
    if (i >= SDH_ACMD41_RETRIES) return SD_HOST_ERR_TIMEOUT;

    /* Step 5: CMD58 READ_OCR. Bit 30 of OCR is CCS (high-capacity card). */
    r1 = sdh_spi_cmd_r1(h, SDP_CMD58, 0u);
    if (r1 != 0x00u) return SD_HOST_ERR_CARD;
    ocr = sdh_spi_read_uint32(h);
    if (hcs && (ocr & SDP_OCR_CCS_IS_HC)) {
        h->card_kind = SD_CARD_HC;
    } else if (hcs) {
        h->card_kind = SD_CARD_V2;
    } else {
        h->card_kind = SD_CARD_V1;
    }

    /* Step 6: CMD16 SET_BLOCKLEN = 512. SDHC ignores; SDSC needs it. */
    r1 = sdh_spi_cmd_r1(h, SDP_CMD16, SDP_BLOCKLEN_512);
    if (r1 != 0x00u) return SD_HOST_ERR_CARD;

    /* Step 7: bump to high speed. CS stays LOW (single-card bus)
     * for the rest of the session. */
    h->set_spd(1);
    h->hs_active = 1;
    return SD_HOST_OK;
}

/* ============================================================ *
 *  SPI mode: slow per-block read / write                        *
 * ============================================================ */

static sd_host_result_t sdh_spi_read_one_block(sd_host_t *h, uint32_t addr,
                                                uint8_t *dst)
{
    uint8_t r1;
    int i;

    r1 = sdh_spi_cmd_r1(h, SDP_CMD17, addr);
    if (r1 != 0x00u) return (r1 == 0xFFu) ? SD_HOST_ERR_TIMEOUT : SD_HOST_ERR_CARD;

    /* Wait for the 0xFE start token. */
    if (sdh_spi_wait_token(h, SDP_TOK_DATA_START_SBR) < 0) {
        return SD_HOST_ERR_IO;
    }

    /* Read 512 bytes. spi_rx_buf is preferred -- fewer per-byte busy
     * polls in the backend -- but spi_io fallback is fine if the
     * backend doesn't supply a bulk variant. */
    if (h->spi_rx_buf != 0) {
        h->spi_rx_buf(dst, 512u);
    } else {
        for (i = 0; i < 512; i++) {
            dst[i] = h->spi_io(SDP_SPI_IDLE_BYTE);
        }
    }

    /* Discard 2 CRC bytes. We trust the card; if it lied about the
     * CRC the next read will surface the error. */
    (void)h->spi_io(SDP_SPI_IDLE_BYTE);
    (void)h->spi_io(SDP_SPI_IDLE_BYTE);

    return SD_HOST_OK;
}

static sd_host_result_t sdh_spi_write_one_block(sd_host_t *h, uint32_t addr,
                                                 const uint8_t *src)
{
    uint8_t r1;
    uint8_t resp;
    int i;
    uint16_t crc16;
    sd_host_result_t r;

    r1 = sdh_spi_cmd_r1(h, SDP_CMD24, addr);
    if (r1 != 0x00u) return (r1 == 0xFFu) ? SD_HOST_ERR_TIMEOUT : SD_HOST_ERR_CARD;

    /* Send a brief idle byte before the start token -- some cards are
     * picky about the gap between R1 and 0xFE. */
    (void)h->spi_io(SDP_SPI_IDLE_BYTE);

    /* Start token for single-block write. */
    h->spi_io(SDP_TOK_DATA_START_SBR);

    /* 512 data bytes. */
    if (h->spi_tx_buf != 0) {
        h->spi_tx_buf(src, 512u);
    } else {
        for (i = 0; i < 512; i++) {
            h->spi_io(src[i]);
        }
    }

    /* Two-byte CRC16-CCITT (big-endian). The card validates and rejects
     * via the data-response token if wrong. */
    crc16 = sd_crc16_ccitt(src, 512u);
    h->spi_io((uint8_t)((crc16 >> 8) & 0xFFu));
    h->spi_io((uint8_t)(crc16 & 0xFFu));

    /* Read the data-response token. Bits [4:0]: start(0), 3 status bits,
     * end(1). Mask 0x1F; 0x05 = data accepted. */
    resp = h->spi_io(SDP_SPI_IDLE_BYTE);
    if ((resp & SDP_DATA_RESP_MASK) != SDP_DATA_RESP_ACCEPTED) {
        /* Drain any remaining busy bytes so the bus is in a known state. */
        (void)sdh_spi_wait_busy_clear(h);
        return SD_HOST_ERR_IO;
    }

    /* Card-busy: MISO held low until programming completes. */
    r = sdh_spi_wait_busy_clear(h);
    if (r != SD_HOST_OK) return r;

    return SD_HOST_OK;
}

/* ============================================================ *
 *  SDIO mode: init flow                                          *
 * ============================================================ */

static sd_host_result_t sdh_sdio_init(sd_host_t *h)
{
    sd_host_result_t r;
    uint8_t resp[SDP_RESP_R1_BYTES];
    uint32_t cmd8_echo;
    uint32_t ocr;
    int hcs;

    /* Required SDIO function pointers. */
    if (h->sdio_cmd_tx_byte == 0 || h->sdio_cmd_rx_byte == 0 ||
        h->sdio_cmd_rx_bit == 0  || h->sdio_dat_tx_word == 0 ||
        h->sdio_dat_rx_word == 0 || h->sdio_dat_idle_clks == 0 ||
        h->set_spd == 0) {
        return SD_HOST_ERR_PARAM;
    }

    if (h->lock != 0) h->lock();

    h->card_kind = SD_CARD_NONE;
    h->rca       = 0u;
    h->hs_active = 0;

    /* Force init speed (<400kHz) before clocking the card. */
    h->set_spd(0);

    /* Step 1: >= 80 idle clocks on DAT to wake the card. */
    h->sdio_dat_idle_clks(80u);

    /* Step 2: CMD0 GO_IDLE_STATE. No response in native mode. */
    sdh_sdio_send_cmd(h, SDP_CMD0, 0u);

    /* Step 3: CMD8 SEND_IF_COND. SD v2+ echoes 0x01AA in low 12 bits.
     * Older cards (v1, MMC) may NACK or time out -- we tolerate.
     * `hcs` records whether the card claimed v2+; we use it as the HCS
     * argument for ACMD41. */
    sdh_sdio_send_cmd(h, SDP_CMD8, SDP_CMD8_ARG_VHS_27_36);
    r = sdh_sdio_recv_r1(h, resp);
    hcs = 0;
    if (r == SD_HOST_OK) {
        cmd8_echo = ((uint32_t)resp[3] << 8) | (uint32_t)resp[4];
        if ((cmd8_echo & SDP_CMD8_ECHO_MASK) != SDP_CMD8_ECHO_EXPECTED) {
            r = SD_HOST_ERR_CARD;
            goto fail;
        }
        hcs = 1;
    } else if (r != SD_HOST_ERR_TIMEOUT) {
        goto fail;
    }
    /* TIMEOUT here is non-fatal -- v1/MMC card. Continue with hcs=0. */

    /* Step 4: ACMD41 init loop. */
    r = sdh_sdio_acmd41_loop(h, hcs, &ocr);
    if (r != SD_HOST_OK) goto fail;

    /* Latch card type. CCS is meaningful only when the host requested HCS. */
    if (hcs && (ocr & SDP_OCR_CCS_IS_HC) != 0u) {
        h->card_kind = SD_CARD_HC;
    } else if (hcs) {
        h->card_kind = SD_CARD_V2;
    } else {
        h->card_kind = SD_CARD_V1;
    }

    /* Step 5: CMD2 ALL_SEND_CID -> R2; CID discarded. */
    sdh_sdio_send_cmd(h, SDP_CMD2, 0u);
    r = sdh_sdio_recv_r2_discard(h);
    if (r != SD_HOST_OK) goto fail;

    /* Step 6: CMD3 SEND_RELATIVE_ADDR -> R6 holds the new RCA. */
    sdh_sdio_send_cmd(h, SDP_CMD3, 0u);
    r = sdh_sdio_recv_r6(h, &h->rca);
    if (r != SD_HOST_OK) goto fail;

    /* Step 7: CMD7 SELECT_CARD with the captured RCA. R1b -> wait for
     * DAT0 to release. */
    sdh_sdio_send_cmd(h, SDP_CMD7, h->rca << 16);
    r = sdh_sdio_recv_r1(h, resp);
    if (r != SD_HOST_OK) goto fail;
    r = sdh_sdio_wait_dat0_idle(h);
    if (r != SD_HOST_OK) goto fail;

    /* Step 8: ACMD6 SET_BUS_WIDTH = 4-bit. */
    sdh_sdio_send_cmd(h, SDP_CMD55, h->rca << 16);
    r = sdh_sdio_recv_r1(h, resp);
    if (r != SD_HOST_OK) goto fail;
    sdh_sdio_send_cmd(h, SDP_ACMD6, SDP_ACMD6_ARG_4BIT);
    r = sdh_sdio_recv_r1(h, resp);
    if (r != SD_HOST_OK) goto fail;

    /* Step 9: CMD16 SET_BLOCKLEN = 512 (SDHC ignores; SDSC needs it). */
    sdh_sdio_send_cmd(h, SDP_CMD16, SDP_BLOCKLEN_512);
    r = sdh_sdio_recv_r1(h, resp);
    if (r != SD_HOST_OK) goto fail;

    /* Step 10: bump clock to HS50.
     *
     * Mirrors the legacy ED64 backend: we skip the spec's CMD6
     * SWITCH_FUNC negotiation and rely on the fact that all SD cards
     * tolerate up to 50 MHz default mode in practice. CMD6 + slow data
     * path can be added in a later wave; doing so requires single-
     * nibble DAT primitives that aren't part of the Wave 1 contract. */
    h->set_spd(1);
    h->hs_active = 1;

    if (h->unlock != 0) h->unlock();
    return SD_HOST_OK;

fail:
    if (h->unlock != 0) h->unlock();
    return r;
}

/* ============================================================ *
 *  Top-level dispatch (init + block I/O)                        *
 * ============================================================ */

sd_host_result_t sd_host_init(sd_host_t *h)
{
    sd_host_result_t r;

    if (h == 0) return SD_HOST_ERR_PARAM;

    if (h->proto == SD_PROTO_SDIO) {
        return sdh_sdio_init(h);
    }
    if (h->proto == SD_PROTO_SPI) {
        if (h->lock != 0) h->lock();
        r = sdh_spi_init(h);
        if (h->unlock != 0) h->unlock();
        return r;
    }
    return SD_HOST_ERR_PARAM;
}

/* Translate LBA to wire address: SDHC uses LBA directly; older cards
 * use byte address. */
static uint32_t sdh_lba_to_addr(const sd_host_t *h, uint32_t lba)
{
    if (h->card_kind == SD_CARD_HC) return lba;
    return lba * SDH_BLOCK_BYTES;
}

sd_host_result_t sd_host_read_blocks(sd_host_t *h, uint32_t lba,
                                     void *dst, uint32_t n_blk)
{
    sd_host_result_t r;
    uint32_t addr;
    uint32_t i;

    if (h == 0 || dst == 0 || n_blk == 0u) return SD_HOST_ERR_PARAM;
    if (((uintptr_t)dst & 7u) != 0u)        return SD_HOST_ERR_PARAM;

    addr = sdh_lba_to_addr(h, lba);

    if (h->proto == SD_PROTO_SDIO) {
        /* SDIO requires the FPGA-DMA fast read. */
        if (h->rx_mblk == 0) return SD_HOST_ERR_PARAM;
        if (h->lock != 0) h->lock();
        r = h->rx_mblk(h, addr, dst, n_blk);
        if (h->unlock != 0) h->unlock();
        return r;
    }

    if (h->proto == SD_PROTO_SPI) {
        /* SPI may have an FPGA-DMA reader (V1 does). Otherwise use
         * the per-block CMD17 slow path. */
        if (h->rx_mblk != 0) {
            if (h->lock != 0) h->lock();
            r = h->rx_mblk(h, addr, dst, n_blk);
            if (h->unlock != 0) h->unlock();
            return r;
        }
        if (h->lock != 0) h->lock();
        for (i = 0; i < n_blk; i++) {
            uint32_t per_block = (h->card_kind == SD_CARD_HC)
                ? (addr + i)
                : (addr + i * SDH_BLOCK_BYTES);
            r = sdh_spi_read_one_block(h, per_block,
                                        (uint8_t *)dst + i * SDH_BLOCK_BYTES);
            if (r != SD_HOST_OK) {
                if (h->unlock != 0) h->unlock();
                return r;
            }
        }
        if (h->unlock != 0) h->unlock();
        return SD_HOST_OK;
    }

    return SD_HOST_ERR_PARAM;
}

sd_host_result_t sd_host_write_blocks(sd_host_t *h, uint32_t lba,
                                      const void *src, uint32_t n_blk)
{
    sd_host_result_t r;
    uint32_t addr;
    uint32_t i;

    if (h == 0 || src == 0 || n_blk == 0u) return SD_HOST_ERR_PARAM;
    if (((uintptr_t)src & 7u) != 0u)        return SD_HOST_ERR_PARAM;

    addr = sdh_lba_to_addr(h, lba);

    if (h->proto == SD_PROTO_SDIO) {
        if (h->tx_mblk == 0) return SD_HOST_ERR_PARAM;
        if (h->lock != 0) h->lock();
        r = h->tx_mblk(h, addr, src, n_blk);
        if (h->unlock != 0) h->unlock();
        return r;
    }

    if (h->proto == SD_PROTO_SPI) {
        if (h->tx_mblk != 0) {
            if (h->lock != 0) h->lock();
            r = h->tx_mblk(h, addr, src, n_blk);
            if (h->unlock != 0) h->unlock();
            return r;
        }
        if (h->lock != 0) h->lock();
        for (i = 0; i < n_blk; i++) {
            uint32_t per_block = (h->card_kind == SD_CARD_HC)
                ? (addr + i)
                : (addr + i * SDH_BLOCK_BYTES);
            r = sdh_spi_write_one_block(h, per_block,
                                         (const uint8_t *)src + i * SDH_BLOCK_BYTES);
            if (r != SD_HOST_OK) {
                if (h->unlock != 0) h->unlock();
                return r;
            }
        }
        if (h->unlock != 0) h->unlock();
        return SD_HOST_OK;
    }

    return SD_HOST_ERR_PARAM;
}
