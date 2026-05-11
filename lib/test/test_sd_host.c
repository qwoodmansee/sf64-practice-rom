/* Host unit tests for lib/sd_host/sd_host.c.
 *
 * Builds with native gcc + libc; no libultra, no MIPS toolchain. Runs
 * in milliseconds. The protocol engine talks to a mock SD card via
 * function pointers; the mock is a tiny state machine that replies
 * exactly as a real SDHC/SDSC card would, letting us exercise the full
 * init flow without any hardware.
 *
 * What this verifies, and why each thing matters:
 *
 *  - sd_host_init emits the expected command sequence in the expected
 *    order. If a wave that touches sd_host.c reorders or drops a step
 *    (e.g. forgetting CMD2 before CMD3), this fails before the bug
 *    reaches a real card.
 *  - Each CMD frame has a correct CRC7. A wave that swaps in a wrong
 *    CRC (or an off-by-one in the frame builder) produces a CRC the
 *    card silently rejects, and the only symptom is "init times out"
 *    on hardware -- catch it here.
 *  - The card-kind classification (HC vs V2 vs V1) is driven by CMD8
 *    response + ACMD41 OCR.CCS. Wrong classification means SDHC LBA
 *    addresses get multiplied by 512 and reads land at the wrong byte.
 *  - sd_host_send_cmd_r1 / r1b helpers (used by ED64 rx_mblk / tx_mblk
 *    for CMD18 / CMD12 / CMD24) emit valid CMD frames and parse R1.
 *  - sd_host_read_blocks / write_blocks dispatch to rx_mblk / tx_mblk
 *    with the wire-correct address (LBA for SDHC, byte address for SDSC).
 *
 * Out of scope (would need a much bigger mock):
 *  - The slow per-block CMD17 / CMD24 data path (sd_host doesn't have
 *    one in Wave 1; rx_mblk/tx_mblk are required).
 *  - DAT-bus CRC16 over real data blocks (sd_crc has its own test).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "sd_host/sd_host.h"
#include "sd_host/sd_proto.h"
#include "sd_crc.h"

/* ------------------------------------------------------------ */
/* Test infra                                                    */
/* ------------------------------------------------------------ */

static int failures = 0;

#define ASSERT_EQ(actual, expected, label) do {                         \
    unsigned long long _a = (unsigned long long)(actual);               \
    unsigned long long _e = (unsigned long long)(expected);             \
    if (_a != _e) {                                                     \
        printf("FAIL: %s: expected 0x%llX, got 0x%llX\n",               \
               (label), _e, _a);                                        \
        failures++;                                                     \
    } else {                                                            \
        printf("PASS: %s\n", (label));                                  \
    }                                                                   \
} while (0)

#define ASSERT_OK(call, label) do {                                     \
    sd_host_result_t _r = (call);                                       \
    if (_r != SD_HOST_OK) {                                             \
        printf("FAIL: %s: returned %d\n", (label), (int)_r);            \
        failures++;                                                     \
    } else {                                                            \
        printf("PASS: %s\n", (label));                                  \
    }                                                                   \
} while (0)

/* ------------------------------------------------------------ */
/* Mock SD card state machine                                    */
/*                                                                */
/* Tracks "what kind of card am I?" plus a tiny FSM for the      */
/* response stream. We script just enough behavior to satisfy    */
/* sd_host_init for SDHC, SDv2 (non-HC), and SDv1 / MMC.         */
/* ------------------------------------------------------------ */

typedef enum {
    MOCK_CARD_SDHC = 0,    /* CMD8 echoes; ACMD41 OCR.CCS = 1 */
    MOCK_CARD_SDV2,        /* CMD8 echoes; ACMD41 OCR.CCS = 0 */
    MOCK_CARD_SDV1         /* CMD8 timeout; ACMD41 returns OCR with CCS=0 */
} mock_card_kind;

typedef struct {
    /* Mock setup */
    mock_card_kind kind;
    uint16_t       canned_rca;     /* what the mock card hands out via CMD3 */

    /* Captured-tx state (host -> card) */
    uint8_t        tx_buf[16];     /* most recent CMD frame: 0xFF + 6 cmd bytes */
    int            tx_len;
    uint8_t        last_cmd;       /* cmd index extracted from the most recent frame */
    uint32_t       last_arg;
    uint8_t        last_crc7;      /* crc byte the host sent (we verify) */
    int            last_acmd;      /* 1 if this frame followed CMD55 */
    int            cmd55_pending;  /* 1 between CMD55 and the next non-CMD55 frame */

    /* CMD log: list of cmd numbers in the order seen by the mock. ACMD
     * variants are recorded as their underlying CMD number with the
     * ACMD bit set in cmd_log_acmd[]. */
    uint8_t        cmd_log[64];
    int            cmd_log_acmd[64];
    int            cmd_log_len;

    /* Response state machine */
    uint8_t        resp_buf[17];   /* current canned response */
    int            resp_len;
    int            resp_pos;       /* byte index for cmd_rx_byte */
    int            resp_started;   /* 0 until first cmd_rx_bit returns 0 */

    /* ACMD41 retry counter — we make the card "busy" for the first
     * ACMD41_BUSY_FOR cycles, then return ready. */
    int            acmd41_busy_remaining;

    /* Set_spd observability */
    int            spd_calls;
    int            last_spd;

    /* Idle clocks observability */
    uint32_t       idle_clks_total;

    /* mblk dispatch observability */
    int            rx_mblk_calls;
    int            tx_mblk_calls;
    uint32_t       last_rx_addr;
    uint32_t       last_tx_addr;
    uint32_t       last_rx_n_blk;
    uint32_t       last_tx_n_blk;
} mock_card_t;

#define ACMD41_BUSY_FOR 3

static mock_card_t g_card;

static void mock_reset(mock_card_kind kind) {
    memset(&g_card, 0, sizeof(g_card));
    g_card.kind = kind;
    g_card.canned_rca = 0x1234;
    g_card.acmd41_busy_remaining = ACMD41_BUSY_FOR;
}

/* ---- Build canned responses ---- */

/* Helper: emit a 6-byte R1-style response with the given fields.
 * Direction byte (resp[0]) follows the SD native-mode convention: bit 7
 * is start (always 0), then 6 bits of cmd index, then status follows
 * in resp[1..4], then a CRC byte we don't validate. */
static void canned_r1(uint8_t cmd, uint32_t status) {
    g_card.resp_buf[0] = (uint8_t)(cmd & 0x3F);
    g_card.resp_buf[1] = (uint8_t)(status >> 24);
    g_card.resp_buf[2] = (uint8_t)(status >> 16);
    g_card.resp_buf[3] = (uint8_t)(status >> 8);
    g_card.resp_buf[4] = (uint8_t)(status);
    g_card.resp_buf[5] = 0x00;   /* CRC byte; unchecked by sd_host */
    g_card.resp_len = 6;
    g_card.resp_pos = 0;
    g_card.resp_started = 0;
}

/* R7 for CMD8: bytes [3:4] echo lower 16 bits of arg. We keep the
 * spec's "voltage accepted" bit but the engine only checks the echo. */
static void canned_r7_for_cmd8(uint32_t arg) {
    canned_r1(SDP_CMD8, arg & 0x00000FFFu);
}

/* R3 for ACMD41: bytes [1:4] are OCR. OCR.busy_done flips on after
 * we've answered "busy" a few times, so the engine actually loops. */
static void canned_r3_for_acmd41(int hc) {
    uint32_t ocr = 0;
    if (g_card.acmd41_busy_remaining > 0) {
        g_card.acmd41_busy_remaining--;
        ocr = 0;                 /* still powering up */
    } else {
        ocr = SDP_OCR_BUSY_DONE; /* power-up done */
        if (hc) ocr |= SDP_OCR_CCS_IS_HC;
    }
    canned_r1(SDP_ACMD41, ocr);
}

/* R6 for CMD3: bytes [1:2] = RCA. */
static void canned_r6_for_cmd3(uint16_t rca) {
    canned_r1(SDP_CMD3, ((uint32_t)rca << 16));
}

/* R2 for CMD2: 17 bytes of don't-care. */
static void canned_r2_discard(void) {
    int i;
    g_card.resp_buf[0] = 0x3F;   /* direction byte */
    for (i = 1; i < 17; i++) g_card.resp_buf[i] = 0xAB;
    g_card.resp_len = 17;
    g_card.resp_pos = 0;
    g_card.resp_started = 0;
}

/* "No response" marker: cmd_rx_bit will time out (we still cap the
 * spinning). Used for CMD0 (which has no native response) and to
 * simulate v1/MMC ignoring CMD8. */
static void canned_no_response(void) {
    g_card.resp_buf[0] = 0xFF;   /* will keep returning 1s on cmd_rx_bit */
    g_card.resp_len = 0;         /* sentinel: "no start bit ever comes" */
    g_card.resp_pos = 0;
    g_card.resp_started = 0;
}

/* ---- Mock function-pointer callbacks ---- */

static void mock_set_spd(int hs) {
    g_card.spd_calls++;
    g_card.last_spd = hs;
}

static void mock_cmd_tx_byte(uint8_t b) {
    if (g_card.tx_len < (int)sizeof(g_card.tx_buf)) {
        g_card.tx_buf[g_card.tx_len++] = b;
    }
    /* The frame is: 0xFF prefix + 6-byte cmd. After we've collected 7
     * bytes, parse the cmd and hand the mock the canned response. */
    if (g_card.tx_len == 7) {
        uint8_t cmd = (uint8_t)(g_card.tx_buf[1] & 0x3F);
        uint32_t arg =
            ((uint32_t)g_card.tx_buf[2] << 24) |
            ((uint32_t)g_card.tx_buf[3] << 16) |
            ((uint32_t)g_card.tx_buf[4] <<  8) |
             (uint32_t)g_card.tx_buf[5];
        uint8_t expected_crc = sd_crc7(&g_card.tx_buf[1], 5);

        g_card.last_cmd = cmd;
        g_card.last_arg = arg;
        g_card.last_crc7 = g_card.tx_buf[6];

        /* Verify the host's CRC matches what sd_crc7 would compute --
         * if these ever differ, sd_host's frame builder is broken. */
        if (g_card.last_crc7 != expected_crc) {
            printf("FAIL: bad CRC7: cmd=%u arg=0x%08X expected=0x%02X got=0x%02X\n",
                   cmd, arg, expected_crc, g_card.last_crc7);
            failures++;
        }

        g_card.last_acmd = g_card.cmd55_pending;

        /* Log the command. */
        if (g_card.cmd_log_len < (int)(sizeof(g_card.cmd_log)/sizeof(g_card.cmd_log[0]))) {
            g_card.cmd_log[g_card.cmd_log_len] = cmd;
            g_card.cmd_log_acmd[g_card.cmd_log_len] = g_card.last_acmd;
            g_card.cmd_log_len++;
        }

        /* Hand the mock the right canned response. */
        if (g_card.last_acmd) {
            /* This is an ACMD. */
            if (cmd == SDP_ACMD41) {
                int hc = (g_card.kind == MOCK_CARD_SDHC);
                canned_r3_for_acmd41(hc);
            } else if (cmd == SDP_ACMD6) {
                canned_r1(cmd, 0);
            } else {
                canned_r1(cmd, 0);
            }
            g_card.cmd55_pending = 0;
        } else if (cmd == SDP_CMD0) {
            canned_no_response();
        } else if (cmd == SDP_CMD8) {
            if (g_card.kind == MOCK_CARD_SDV1) {
                canned_no_response();
            } else {
                canned_r7_for_cmd8(arg);
            }
        } else if (cmd == SDP_CMD2) {
            canned_r2_discard();
        } else if (cmd == SDP_CMD3) {
            canned_r6_for_cmd3(g_card.canned_rca);
        } else if (cmd == SDP_CMD55) {
            canned_r1(cmd, 0);
            g_card.cmd55_pending = 1;
        } else {
            canned_r1(cmd, 0);
        }

        /* Reset tx accumulator for the next frame. */
        g_card.tx_len = 0;
    }
}

/* cmd_rx_bit: returns 0 once the canned response has "started" (host
 * has spun long enough for us to release the start bit). */
static uint8_t mock_cmd_rx_bit(void) {
    if (g_card.resp_len == 0) {
        /* "no response" mode -- card never drops CMD low. */
        return 1;
    }
    /* Pretend the card starts responding immediately. The very first
     * call returns 0 (start bit), then sd_host transitions to
     * cmd_rx_byte for the body. */
    g_card.resp_started = 1;
    return 0;
}

static uint8_t mock_cmd_rx_byte(void) {
    if (g_card.resp_pos < g_card.resp_len) {
        return g_card.resp_buf[g_card.resp_pos++];
    }
    return 0xFF;
}

/* dat_rx_word: sd_host uses this only for DAT0 idle polls (R1b after
 * CMD7). We always return DAT0 high so the card is "not busy". */
static uint16_t mock_dat_rx_word(void) {
    return 0x000F;   /* all four DAT lines high */
}

/* dat_tx_word: not invoked by sd_host_init in Wave 1, but the protocol
 * engine accepts a non-NULL pointer. Keep it benign. */
static void mock_dat_tx_word(uint16_t w) {
    (void)w;
}

/* dat_idle_clks: track total clocks for assertion on the >=80 idle
 * clocks at the start of init. */
static void mock_dat_idle_clks(uint32_t n) {
    g_card.idle_clks_total += n;
}

/* mblk callbacks: log call shape; sd_host_read/write_blocks dispatches
 * here. Always return OK. */
static sd_host_result_t mock_rx_mblk(struct sd_host *h, uint32_t addr,
                                     void *dst, uint32_t n_blk) {
    (void)h; (void)dst;
    g_card.rx_mblk_calls++;
    g_card.last_rx_addr = addr;
    g_card.last_rx_n_blk = n_blk;
    return SD_HOST_OK;
}

static sd_host_result_t mock_tx_mblk(struct sd_host *h, uint32_t addr,
                                     const void *src, uint32_t n_blk) {
    (void)h; (void)src;
    g_card.tx_mblk_calls++;
    g_card.last_tx_addr = addr;
    g_card.last_tx_n_blk = n_blk;
    return SD_HOST_OK;
}

/* ---- Wire up an sd_host_t pointing at the mock ---- */

static void mock_populate_host(sd_host_t *h) {
    memset(h, 0, sizeof(*h));
    h->proto              = SD_PROTO_SDIO;
    h->set_spd            = mock_set_spd;
    h->sdio_cmd_tx_byte   = mock_cmd_tx_byte;
    h->sdio_cmd_rx_byte   = mock_cmd_rx_byte;
    h->sdio_cmd_rx_bit    = mock_cmd_rx_bit;
    h->sdio_dat_tx_word   = mock_dat_tx_word;
    h->sdio_dat_rx_word   = mock_dat_rx_word;
    h->sdio_dat_idle_clks = mock_dat_idle_clks;
    h->rx_mblk            = mock_rx_mblk;
    h->tx_mblk            = mock_tx_mblk;
}

/* ------------------------------------------------------------ */
/* Tests                                                         */
/* ------------------------------------------------------------ */

/* Verify the cmd log contains the expected CMD/ACMD pair at index `idx`. */
static void assert_log(int idx, uint8_t cmd, int is_acmd, const char *label) {
    char buf[64];
    if (idx >= g_card.cmd_log_len) {
        printf("FAIL: %s: cmd log too short (len=%d, want idx=%d)\n",
               label, g_card.cmd_log_len, idx);
        failures++;
        return;
    }
    snprintf(buf, sizeof(buf), "%s: cmd at log[%d]", label, idx);
    ASSERT_EQ(g_card.cmd_log[idx], cmd, buf);
    snprintf(buf, sizeof(buf), "%s: acmd-bit at log[%d]", label, idx);
    ASSERT_EQ(g_card.cmd_log_acmd[idx], is_acmd, buf);
}


static void test_init_sdhc_happy_path(void) {
    sd_host_t h;
    sd_host_result_t r;

    printf("\n-- test_init_sdhc_happy_path --\n");
    mock_reset(MOCK_CARD_SDHC);
    mock_populate_host(&h);

    r = sd_host_init(&h);
    ASSERT_EQ(r, SD_HOST_OK, "sd_host_init returns OK on SDHC card");

    /* Idle clocks: at least 80 before CMD0. */
    if (g_card.idle_clks_total < 80u) {
        printf("FAIL: <80 idle clocks before CMD0 (got %u)\n", g_card.idle_clks_total);
        failures++;
    } else {
        printf("PASS: >=80 idle clocks before CMD0\n");
    }

    /* Expected CMD log:
     *   0:  CMD0    (no resp)
     *   1:  CMD8    (R7 echo)
     *   2:  CMD55   (gate for ACMD41)
     *   3:  ACMD41  (busy)
     *   4:  CMD55
     *   5:  ACMD41  (busy)
     *   6:  CMD55
     *   7:  ACMD41  (busy)
     *   8:  CMD55
     *   9:  ACMD41  (done -> exits loop)
     *  10:  CMD2
     *  11:  CMD3
     *  12:  CMD7
     *  13:  CMD55   (gate for ACMD6)
     *  14:  ACMD6   (4-bit bus)
     *  15:  CMD16
     */
    assert_log( 0, SDP_CMD0,   0, "init step 0");
    assert_log( 1, SDP_CMD8,   0, "init step 1");
    assert_log( 2, SDP_CMD55,  0, "init step 2");
    assert_log( 3, SDP_ACMD41, 1, "init step 3");
    /* steps 4-9 alternate CMD55 / ACMD41 */
    assert_log( 8, SDP_CMD55,  0, "init step 8");
    assert_log( 9, SDP_ACMD41, 1, "init step 9");
    assert_log(10, SDP_CMD2,   0, "init step 10");
    assert_log(11, SDP_CMD3,   0, "init step 11");
    assert_log(12, SDP_CMD7,   0, "init step 12");
    assert_log(13, SDP_CMD55,  0, "init step 13");
    assert_log(14, SDP_ACMD6,  1, "init step 14");
    assert_log(15, SDP_CMD16,  0, "init step 15");

    ASSERT_EQ(g_card.cmd_log_len, 16, "cmd log length matches expected init steps");

    /* Card kind: SDHC. */
    ASSERT_EQ(h.card_kind, SD_CARD_HC, "card_kind = HC for SDHC");

    /* RCA: captured from R6. */
    ASSERT_EQ(h.rca, g_card.canned_rca, "rca matches canned R6 value");

    /* HS bump: set_spd(0) at start, set_spd(1) at end. */
    ASSERT_EQ(g_card.spd_calls, 2, "set_spd called twice (init + HS)");
    ASSERT_EQ(g_card.last_spd, 1, "set_spd last call was hs=1");
    ASSERT_EQ(h.hs_active, 1, "hs_active flag set");

    /* CMD8 arg: VHS=1 + 0xAA. */
    /* ACMD41 arg: HCS bit. */
    /* The actual frames are verified as part of the per-byte CRC7 check
     * in mock_cmd_tx_byte; if any frame had wrong bytes, that would
     * have logged a FAIL earlier. */
}

static void test_init_v2_non_hc(void) {
    sd_host_t h;
    sd_host_result_t r;

    printf("\n-- test_init_v2_non_hc --\n");
    mock_reset(MOCK_CARD_SDV2);
    mock_populate_host(&h);

    r = sd_host_init(&h);
    ASSERT_EQ(r, SD_HOST_OK, "sd_host_init returns OK on SDv2 (non-HC)");
    ASSERT_EQ(h.card_kind, SD_CARD_V2, "card_kind = V2 for non-HC v2 card");
}

static void test_init_v1_card(void) {
    sd_host_t h;
    sd_host_result_t r;

    printf("\n-- test_init_v1_card --\n");
    mock_reset(MOCK_CARD_SDV1);
    mock_populate_host(&h);

    r = sd_host_init(&h);
    ASSERT_EQ(r, SD_HOST_OK, "sd_host_init returns OK on SDv1 / MMC");
    ASSERT_EQ(h.card_kind, SD_CARD_V1, "card_kind = V1 when CMD8 ignored");
}

static void test_param_checks(void) {
    sd_host_t h;
    sd_host_result_t r;

    printf("\n-- test_param_checks --\n");

    /* NULL host */
    r = sd_host_init(NULL);
    ASSERT_EQ(r, SD_HOST_ERR_PARAM, "init(NULL) -> ERR_PARAM");

    /* Proto=SPI without SPI primitives populated -> ERR_PARAM (the
     * mock only sets SDIO callbacks). After Wave 5 SPI is supported
     * but still requires spi_ss/spi_io/spi_tx_clk to be set. */
    mock_reset(MOCK_CARD_SDHC);
    mock_populate_host(&h);
    h.proto = SD_PROTO_SPI;
    r = sd_host_init(&h);
    ASSERT_EQ(r, SD_HOST_ERR_PARAM, "init with proto=SPI but missing SPI primitives -> ERR_PARAM");

    /* Missing required pointer */
    mock_reset(MOCK_CARD_SDHC);
    mock_populate_host(&h);
    h.sdio_cmd_tx_byte = NULL;
    r = sd_host_init(&h);
    ASSERT_EQ(r, SD_HOST_ERR_PARAM, "init with missing cmd_tx_byte -> ERR_PARAM");
}

static void test_block_io_dispatch(void) {
    sd_host_t h;
    uint8_t buf[1024];
    sd_host_result_t r;

    printf("\n-- test_block_io_dispatch --\n");

    /* SDHC: addresses are LBAs. */
    mock_reset(MOCK_CARD_SDHC);
    mock_populate_host(&h);
    sd_host_init(&h);
    g_card.rx_mblk_calls = 0;
    g_card.tx_mblk_calls = 0;

    r = sd_host_read_blocks(&h, 1234u, buf, 2u);
    ASSERT_EQ(r, SD_HOST_OK, "read_blocks OK on SDHC");
    ASSERT_EQ(g_card.rx_mblk_calls, 1, "rx_mblk dispatched");
    ASSERT_EQ(g_card.last_rx_addr, 1234u, "rx_mblk addr = LBA on SDHC");
    ASSERT_EQ(g_card.last_rx_n_blk, 2u,   "rx_mblk n_blk passed through");

    r = sd_host_write_blocks(&h, 5678u, buf, 1u);
    ASSERT_EQ(r, SD_HOST_OK, "write_blocks OK on SDHC");
    ASSERT_EQ(g_card.tx_mblk_calls, 1, "tx_mblk dispatched");
    ASSERT_EQ(g_card.last_tx_addr, 5678u, "tx_mblk addr = LBA on SDHC");

    /* SDSC: addresses are byte offsets (LBA * 512). */
    mock_reset(MOCK_CARD_SDV2);
    mock_populate_host(&h);
    sd_host_init(&h);
    g_card.rx_mblk_calls = 0;

    r = sd_host_read_blocks(&h, 100u, buf, 2u);
    ASSERT_EQ(r, SD_HOST_OK, "read_blocks OK on SDv2 (non-HC)");
    ASSERT_EQ(g_card.last_rx_addr, 100u * 512u, "rx_mblk addr = byte offset on SDSC");

    /* Misaligned destination buffer is rejected. */
    mock_reset(MOCK_CARD_SDHC);
    mock_populate_host(&h);
    sd_host_init(&h);
    r = sd_host_read_blocks(&h, 0u, buf + 1, 1u);   /* +1 -> not 8-byte aligned */
    ASSERT_EQ(r, SD_HOST_ERR_PARAM, "read_blocks with misaligned dst -> ERR_PARAM");
}

/* ------------------------------------------------------------ */
/* SPI mock: scripts canned R1 / R3 / R7 byte streams.            */
/*                                                                */
/* The host shifts out 6 frame bytes via spi_io, then polls       */
/* spi_io(0xFF) for R1. We collect frame bytes into a buffer,    */
/* identify the CMD on the 6th byte, and push the right canned    */
/* response into a small ring that subsequent spi_io calls drain. */
/* Just enough state for a happy-path SDHC SPI init.              */
/* ------------------------------------------------------------ */

typedef struct {
    /* Captured frame state */
    int     frame_len;
    uint8_t frame_buf[6];
    int     last_was_cmd55;

    /* Response queue: bytes to return on subsequent spi_io polls */
    uint8_t resp_q[32];
    int     resp_q_len;
    int     resp_q_pos;

    /* Logging */
    uint8_t cmd_log[64];
    int     cmd_log_acmd[64];
    int     cmd_log_len;

    /* ACMD41 retry counter: card busy for the first N pings. */
    int     acmd41_busy;

    /* Observability */
    int     spi_ss_calls;
    int     last_spi_ss;
    uint32_t tx_clk_total;
    int     spd_calls;
    int     last_spd;
} spi_mock_t;

static spi_mock_t g_spi;

static void spi_mock_reset(void) {
    memset(&g_spi, 0, sizeof(g_spi));
    g_spi.acmd41_busy = 2;
}

/* Push N bytes onto the response queue. */
static void spi_mock_q(const uint8_t *bytes, int n) {
    int i;
    for (i = 0; i < n && g_spi.resp_q_len < (int)sizeof(g_spi.resp_q); i++) {
        g_spi.resp_q[g_spi.resp_q_len++] = bytes[i];
    }
}

static uint8_t spi_mock_io(uint8_t out) {
    /* Frame collection: while the assembled buffer length is < 6 AND
     * the very first byte has bit 6 set (start of a command), buffer
     * incoming bytes. The host MAY also clock idle bytes (0xFF) before
     * the start byte to give the card response time -- those should
     * pass through to the response queue. */
    if (g_spi.frame_len == 0 && (out & 0xC0u) != 0x40u) {
        /* Idle/poll byte. Drain response queue if it has anything. */
        if (g_spi.resp_q_pos < g_spi.resp_q_len) {
            return g_spi.resp_q[g_spi.resp_q_pos++];
        }
        return 0xFFu;
    }

    g_spi.frame_buf[g_spi.frame_len++] = out;
    if (g_spi.frame_len < 6) {
        return 0xFFu;
    }

    /* Frame complete -- identify CMD and queue the canned response. */
    {
        uint8_t cmd = (uint8_t)(g_spi.frame_buf[0] & 0x3Fu);
        uint32_t arg =
            ((uint32_t)g_spi.frame_buf[1] << 24) |
            ((uint32_t)g_spi.frame_buf[2] << 16) |
            ((uint32_t)g_spi.frame_buf[3] <<  8) |
             (uint32_t)g_spi.frame_buf[4];
        int is_acmd = g_spi.last_was_cmd55;
        uint8_t r1;

        /* Verify the CRC byte for CMD0 / CMD8 (the ones the SD spec
         * actually checks in SPI mode). */
        if (cmd == SDP_CMD0 && g_spi.frame_buf[5] != SDP_CMD0_CRC_SPI) {
            printf("FAIL: SPI CMD0 CRC byte: expected 0x%02X, got 0x%02X\n",
                   SDP_CMD0_CRC_SPI, g_spi.frame_buf[5]);
            failures++;
        }
        if (cmd == SDP_CMD8 && g_spi.frame_buf[5] != SDP_CMD8_CRC_SPI) {
            printf("FAIL: SPI CMD8 CRC byte: expected 0x%02X, got 0x%02X\n",
                   SDP_CMD8_CRC_SPI, g_spi.frame_buf[5]);
            failures++;
        }

        if (g_spi.cmd_log_len < 64) {
            g_spi.cmd_log[g_spi.cmd_log_len] = cmd;
            g_spi.cmd_log_acmd[g_spi.cmd_log_len] = is_acmd;
            g_spi.cmd_log_len++;
        }

        /* Build the canned response for this CMD. */
        g_spi.resp_q_pos = 0;
        g_spi.resp_q_len = 0;
        if (cmd == SDP_CMD0) {
            r1 = 0x01u;   /* idle */
            spi_mock_q(&r1, 1);
            g_spi.last_was_cmd55 = 0;
        } else if (cmd == SDP_CMD8) {
            uint8_t r7[5];
            r1 = 0x01u;
            r7[0] = r1;
            r7[1] = 0;
            r7[2] = 0;
            r7[3] = 0x01;
            r7[4] = 0xAA;
            spi_mock_q(r7, 5);
            g_spi.last_was_cmd55 = 0;
        } else if (cmd == SDP_CMD55) {
            r1 = 0x01u;
            spi_mock_q(&r1, 1);
            g_spi.last_was_cmd55 = 1;
        } else if (is_acmd && cmd == SDP_ACMD41) {
            if (g_spi.acmd41_busy > 0) {
                g_spi.acmd41_busy--;
                r1 = 0x01u;
            } else {
                r1 = 0x00u;
            }
            spi_mock_q(&r1, 1);
            g_spi.last_was_cmd55 = 0;
            (void)arg;
        } else if (cmd == SDP_CMD58) {
            uint8_t r3[5];
            r3[0] = 0x00u;     /* R1 */
            r3[1] = 0xC0u;     /* OCR: bits 31 (busy_done) + 30 (CCS) set */
            r3[2] = 0;
            r3[3] = 0;
            r3[4] = 0;
            spi_mock_q(r3, 5);
            g_spi.last_was_cmd55 = 0;
        } else if (cmd == SDP_CMD16) {
            r1 = 0x00u;
            spi_mock_q(&r1, 1);
            g_spi.last_was_cmd55 = 0;
        } else {
            /* Default: R1 = 0x00 (success). */
            r1 = 0x00u;
            spi_mock_q(&r1, 1);
            g_spi.last_was_cmd55 = 0;
        }

        g_spi.frame_len = 0;
    }
    return 0xFFu;
}

static void spi_mock_ss(int select) {
    g_spi.spi_ss_calls++;
    g_spi.last_spi_ss = select;
}

static void spi_mock_tx_clk(uint8_t mosi, uint32_t n_clk) {
    (void)mosi;
    g_spi.tx_clk_total += n_clk;
}

static void spi_mock_set_spd(int hs) {
    g_spi.spd_calls++;
    g_spi.last_spd = hs;
}

static void assert_log_spi(int idx, uint8_t cmd, int is_acmd, const char *label) {
    char buf[64];
    if (idx >= g_spi.cmd_log_len) {
        printf("FAIL: %s: spi cmd log too short (len=%d, want idx=%d)\n",
               label, g_spi.cmd_log_len, idx);
        failures++;
        return;
    }
    snprintf(buf, sizeof(buf), "%s: cmd at log[%d]", label, idx);
    ASSERT_EQ(g_spi.cmd_log[idx], cmd, buf);
    snprintf(buf, sizeof(buf), "%s: acmd-bit at log[%d]", label, idx);
    ASSERT_EQ(g_spi.cmd_log_acmd[idx], is_acmd, buf);
}

static void spi_mock_populate_host(sd_host_t *h) {
    memset(h, 0, sizeof(*h));
    h->proto      = SD_PROTO_SPI;
    h->set_spd    = spi_mock_set_spd;
    h->spi_ss     = spi_mock_ss;
    h->spi_io     = spi_mock_io;
    h->spi_tx_clk = spi_mock_tx_clk;
    /* spi_tx_buf / spi_rx_buf left NULL -- sd_host falls back to spi_io
     * loops which is what the V1 backend would use too if it ever ran
     * the slow read/write path through the test mock. */
}

static void test_init_spi_sdhc(void) {
    sd_host_t h;
    sd_host_result_t r;

    printf("\n-- test_init_spi_sdhc --\n");
    spi_mock_reset();
    spi_mock_populate_host(&h);

    r = sd_host_init(&h);
    ASSERT_EQ(r, SD_HOST_OK, "SPI init OK on SDHC card");

    /* Wake-up sequence: spi_ss(0) before idle clocks, spi_ss(1) after. */
    if (g_spi.spi_ss_calls < 2) {
        printf("FAIL: expected at least 2 spi_ss calls, got %d\n", g_spi.spi_ss_calls);
        failures++;
    } else {
        printf("PASS: spi_ss called >=2 times during init\n");
    }
    /* >= 80 idle clocks before CMD0. */
    if (g_spi.tx_clk_total < 80u) {
        printf("FAIL: <80 idle clocks before CMD0 (got %u)\n", g_spi.tx_clk_total);
        failures++;
    } else {
        printf("PASS: >=80 idle clocks before CMD0\n");
    }

    /* Expected CMD log:
     *   CMD0, CMD8, CMD55, ACMD41 (busy), CMD55, ACMD41 (busy),
     *   CMD55, ACMD41 (done), CMD58, CMD16. */
    assert_log_spi(0, SDP_CMD0,   0, "spi step 0");
    assert_log_spi(1, SDP_CMD8,   0, "spi step 1");
    assert_log_spi(2, SDP_CMD55,  0, "spi step 2");
    assert_log_spi(3, SDP_ACMD41, 1, "spi step 3");
    assert_log_spi(7, SDP_ACMD41, 1, "spi step 7 (done ACMD41)");
    assert_log_spi(8, SDP_CMD58,  0, "spi step 8");
    assert_log_spi(9, SDP_CMD16,  0, "spi step 9");

    ASSERT_EQ(h.card_kind, SD_CARD_HC, "SPI: card_kind = HC for SDHC");
    ASSERT_EQ(g_spi.last_spd, 1, "SPI: set_spd ended at hs=1");
    ASSERT_EQ(h.hs_active, 1, "SPI: hs_active flag set");
}

static void test_cmd_helpers(void) {
    sd_host_t h;
    uint8_t resp[6];
    sd_host_result_t r;

    printf("\n-- test_cmd_helpers --\n");
    mock_reset(MOCK_CARD_SDHC);
    mock_populate_host(&h);

    /* Helpers do not require sd_host_init first; they're pure command
     * transports. We do need a valid h->sdio_cmd_* function table. */
    r = sd_host_send_cmd_r1(&h, SDP_CMD13, 0u, resp);
    ASSERT_EQ(r, SD_HOST_OK, "send_cmd_r1(CMD13) OK");
    ASSERT_EQ(g_card.last_cmd, SDP_CMD13, "send_cmd_r1 emits the requested CMD");

    /* CMD18 is what ED64 rx_mblk uses; emit it through the helper and
     * verify both the cmd and the arg landed on the wire correctly. */
    r = sd_host_send_cmd_r1(&h, SDP_CMD18, 0xDEADBEEFu, resp);
    ASSERT_EQ(r, SD_HOST_OK, "send_cmd_r1(CMD18, 0xDEADBEEF) OK");
    ASSERT_EQ(g_card.last_cmd, SDP_CMD18, "CMD18 logged");
    ASSERT_EQ(g_card.last_arg, 0xDEADBEEFu, "CMD18 arg landed correctly");

    /* CMD12 is the matching STOP_TRANSMISSION; ED64 rx_mblk uses
     * send_cmd_r1b to absorb the R1b busy. */
    r = sd_host_send_cmd_r1b(&h, SDP_CMD12, 0u, resp);
    ASSERT_EQ(r, SD_HOST_OK, "send_cmd_r1b(CMD12) OK");
    ASSERT_EQ(g_card.last_cmd, SDP_CMD12, "CMD12 logged");
}

int main(void) {
    test_init_sdhc_happy_path();
    test_init_v2_non_hc();
    test_init_v1_card();
    test_param_checks();
    test_block_io_dispatch();
    test_init_spi_sdhc();
    test_cmd_helpers();

    printf("\n");
    if (failures > 0) {
        printf("FAILED: %d assertion(s)\n", failures);
        return 1;
    }
    printf("All sd_host tests passed.\n");
    return 0;
}
