/* lib/sd_host/sd_host.h — generic SD-card protocol engine.
 *
 * Drives the SD Physical-Layer protocol against a function-pointer table
 * supplied by the iodev backend. Knows nothing about ED64 or SC64
 * registers; only knows CMD numbers, response shapes, and data tokens.
 *
 * One backend (e.g. ED64-X) populates an sd_host_t with shift-register
 * primitives and an FPGA-DMA fast path, then calls sd_host_init /
 * sd_host_read_blocks / sd_host_write_blocks.
 *
 * Wave 1 design notes:
 *   - SDIO mode only. SPI mode (V1/V2) added in Wave 4; the function
 *     pointer slots for SPI are reserved here so the struct is stable.
 *   - For SDIO, the rx_mblk and tx_mblk DMA hooks are REQUIRED. ED64-X
 *     has FPGA DMA so this is not a limitation in practice. A future
 *     wave can add a slow per-block fallback if a non-DMA SDIO backend
 *     ever appears. */

#ifndef LIB_SD_HOST_H
#define LIB_SD_HOST_H

#include "lib_types.h"

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(_LANGUAGE_C_NO_STDINT)
  #include <stddef.h>   /* size_t on host */
#endif
/* IDO build pulls size_t from PR/ultratypes.h via lib_types.h. */

typedef enum {
    SD_PROTO_SDIO = 0,
    SD_PROTO_SPI  = 1
} sd_proto_t;

typedef enum {
    SD_HOST_OK          =  0,
    SD_HOST_ERR_PARAM   = -1,
    SD_HOST_ERR_TIMEOUT = -2,
    SD_HOST_ERR_IO      = -3,
    SD_HOST_ERR_CARD    = -4
} sd_host_result_t;

typedef enum {
    SD_CARD_NONE = 0,
    SD_CARD_V1   = 1,    /* SDSC v1 / MMC -- byte-addressed */
    SD_CARD_V2   = 2,    /* SDSC v2 (CMD8 echoed)         -- byte-addressed */
    SD_CARD_HC   = 3     /* SDHC / SDXC                   -- LBA-addressed  */
} sd_card_kind_t;

/* Forward declaration so the function-pointer typedefs below can take a
 * sd_host_t* parameter. */
struct sd_host;

/* ---- Function-pointer types (one typedef each, IDO-friendly) ---- */

typedef void     (*sd_host_void_fn)(void);
typedef void     (*sd_host_set_spd_fn)(int hs);

typedef void     (*sd_host_sdio_cmd_tx_byte_fn)(uint8_t b);
typedef uint8_t  (*sd_host_sdio_cmd_rx_byte_fn)(void);
typedef uint8_t  (*sd_host_sdio_cmd_rx_bit_fn)(void);
typedef void     (*sd_host_sdio_dat_tx_word_fn)(uint16_t w);
typedef uint16_t (*sd_host_sdio_dat_rx_word_fn)(void);
typedef void     (*sd_host_sdio_dat_idle_clks_fn)(uint32_t n_clk);

typedef void     (*sd_host_spi_ss_fn)(int select);  /* 1=assert (CS low), 0=release */
typedef uint8_t  (*sd_host_spi_io_fn)(uint8_t out);
typedef void     (*sd_host_spi_tx_buf_fn)(const void *buf, uint32_t n);
typedef void     (*sd_host_spi_rx_buf_fn)(void *buf, uint32_t n);
typedef void     (*sd_host_spi_tx_clk_fn)(uint8_t mosi_byte, uint32_t n_clk);

typedef sd_host_result_t (*sd_host_mblk_rx_fn)(struct sd_host *h,
                                                uint32_t lba_or_addr,
                                                void *dst,
                                                uint32_t n_blk);
typedef sd_host_result_t (*sd_host_mblk_tx_fn)(struct sd_host *h,
                                                uint32_t lba_or_addr,
                                                const void *src,
                                                uint32_t n_blk);

typedef struct sd_host {
    /* ---- Backend identification (set before sd_host_init) ---- */
    sd_proto_t                 proto;       /* SDIO or SPI */
    void                      *user;        /* opaque backend context */

    /* ---- Concurrency / power ---- */
    sd_host_void_fn            lock;        /* enter PI critical section */
    sd_host_void_fn            unlock;      /* leave PI critical section  */

    /* ---- Speed control (called by sd_host_init at end of init) ---- */
    sd_host_set_spd_fn         set_spd;     /* hs=0: <400kHz; hs=1: 50MHz */

    /* ---- SDIO mode primitives (proto == SD_PROTO_SDIO) ---- */
    sd_host_sdio_cmd_tx_byte_fn   sdio_cmd_tx_byte;   /* 8 clocks, MSB-first */
    sd_host_sdio_cmd_rx_byte_fn   sdio_cmd_rx_byte;   /* 8 clocks               */
    sd_host_sdio_cmd_rx_bit_fn    sdio_cmd_rx_bit;    /* 1 clock, returns 0/1 */
    sd_host_sdio_dat_tx_word_fn   sdio_dat_tx_word;   /* 16 bits = 4 SD clocks */
    sd_host_sdio_dat_rx_word_fn   sdio_dat_rx_word;   /* 16 bits */
    sd_host_sdio_dat_idle_clks_fn sdio_dat_idle_clks; /* hold DAT high N clocks */

    /* ---- SPI mode primitives (proto == SD_PROTO_SPI) ----
     * spi_ss: assert (1) or release (0) the SD card's chip-select line.
     *         Required because the SD spec mandates the first 80 init
     *         clocks happen with CS HIGH (then CS can stay low forever
     *         on a single-card bus).
     * spi_io: single-byte exchange. Sends `out` on MOSI and returns
     *         whatever the card put on MISO during the same 8 clocks.
     * spi_tx_buf / spi_rx_buf: bulk variants. Implementations should
     *         skip the per-byte busy poll where possible.
     * spi_tx_clk: emit n_clk clocks holding MOSI at the byte pattern
     *         `mosi_byte` (0xFF or 0x00 typical). Used for the init
     *         wake-up sequence and for waiting between commands. */
    sd_host_spi_ss_fn          spi_ss;
    sd_host_spi_io_fn          spi_io;
    sd_host_spi_tx_buf_fn      spi_tx_buf;
    sd_host_spi_rx_buf_fn      spi_rx_buf;
    sd_host_spi_tx_clk_fn      spi_tx_clk;

    /* ---- Block-transfer fast path (optional in SPI; required in SDIO) ----
     * SDIO (ED64-X / V2): rx_mblk + tx_mblk MUST both be set.
     * SPI  (ED64-V1):     rx_mblk MAY be set (V1 has FPGA DMA reads).
     *                     tx_mblk MAY be NULL -- the SPI slow write path
     *                     in sd_host.c handles per-block CMD24 + 0xFE
     *                     token + CRC16 + busy-on-MISO automatically. */
    sd_host_mblk_rx_fn         rx_mblk;
    sd_host_mblk_tx_fn         tx_mblk;

    /* ---- State filled by sd_host_init (read by sd_host_*_blocks) ---- */
    sd_card_kind_t             card_kind;   /* HC -> use LBA directly */
    uint32_t                   rca;         /* relative card address    */
    int                        hs_active;   /* 1 if running at high speed */
} sd_host_t;

/* ---- Command helpers exposed for backend rx_mblk / tx_mblk impls ----
 *
 * The DMA fast path on ED64-X (and similar carts) needs to issue CMD18 /
 * CMD12 (multi-block read) and CMD24 / CMD25 (multi-block write) around
 * the FPGA-orchestrated data phase. Rather than have every backend
 * reimplement the CMD-frame format and R1 parser, we expose them here.
 *
 * The card-state side effects (h->card_kind / h->rca) are NOT updated
 * by these helpers -- they are pure command transports. Use sd_host_init
 * for the bring-up sequence. */

/* Issue a command and receive a 6-byte R1/R3/R6/R7-style response. */
sd_host_result_t sd_host_send_cmd_r1(sd_host_t *h, uint8_t cmd, uint32_t arg,
                                     uint8_t resp[6]);

/* Issue a command, receive R1, then wait for DAT0 to release (R1b). */
sd_host_result_t sd_host_send_cmd_r1b(sd_host_t *h, uint8_t cmd, uint32_t arg,
                                      uint8_t resp[6]);

/* Bring the card from cold-boot to ready-for-block-IO.
 *
 * Sequence (mirroring the proven legacy ED64 backend):
 *   1. >=80 idle clocks on DAT
 *   2. CMD0  GO_IDLE_STATE
 *   3. CMD8  SEND_IF_COND  (probe SD v2 / SDHC capability)
 *   4. ACMD41 init loop until OCR.busy = done
 *   5. CMD2  ALL_SEND_CID  (CID discarded)
 *   6. CMD3  SEND_RELATIVE_ADDR -> stash RCA
 *   7. CMD7  SELECT_CARD
 *   8. SDIO only: ACMD6 = 4-bit bus
 *   9. set_spd(1)            (HS50 -- legacy behavior is to bump blindly)
 *
 * Caller must populate proto + the protocol primitives before calling.
 * Return value latches into h->card_kind / h->rca / h->hs_active. */
sd_host_result_t sd_host_init(sd_host_t *h);

/* Read n_blk * 512 bytes starting at LBA `lba` into `dst`.
 * SDIO mode requires h->rx_mblk to be set. `dst` must be 8-byte aligned. */
sd_host_result_t sd_host_read_blocks(sd_host_t *h, uint32_t lba,
                                     void *dst, uint32_t n_blk);

/* Write n_blk * 512 bytes starting at LBA `lba` from `src`.
 * SDIO mode requires h->tx_mblk to be set. `src` must be 8-byte aligned. */
sd_host_result_t sd_host_write_blocks(sd_host_t *h, uint32_t lba,
                                      const void *src, uint32_t n_blk);

#endif /* LIB_SD_HOST_H */
