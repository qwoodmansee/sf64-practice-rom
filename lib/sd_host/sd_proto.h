/* SD Physical Layer protocol constants — clean-room from SD Spec v3.0+.
 *
 * No vendor- or implementation-specific bits live here; just numbers
 * lifted from the public SD specification. Used internally by sd_host.c
 * and (rarely) by backends that need to know e.g. data token bytes.
 *
 * IDO C89 compatible: no enums-as-types, no designated initializers. */

#ifndef LIB_SD_HOST_SD_PROTO_H
#define LIB_SD_HOST_SD_PROTO_H

/* ---- Standard commands (CMD0..CMD55) ---- */
#define SDP_CMD0    0u   /* GO_IDLE_STATE */
#define SDP_CMD2    2u   /* ALL_SEND_CID                  -> R2 (136 bits)  */
#define SDP_CMD3    3u   /* SEND_RELATIVE_ADDR            -> R6             */
#define SDP_CMD6    6u   /* SWITCH_FUNC (high-speed mode) -> R1 + 64-byte block */
#define SDP_CMD7    7u   /* SELECT/DESELECT_CARD          -> R1b            */
#define SDP_CMD8    8u   /* SEND_IF_COND                  -> R7             */
#define SDP_CMD9    9u   /* SEND_CSD                      -> R2             */
#define SDP_CMD58  58u   /* READ_OCR (SPI mode only)      -> R3             */
#define SDP_CMD12  12u   /* STOP_TRANSMISSION             -> R1b            */
#define SDP_CMD13  13u   /* SEND_STATUS                   -> R1             */
#define SDP_CMD16  16u   /* SET_BLOCKLEN                  -> R1             */
#define SDP_CMD17  17u   /* READ_SINGLE_BLOCK             -> R1 + data      */
#define SDP_CMD18  18u   /* READ_MULTIPLE_BLOCK           -> R1 + data...   */
#define SDP_CMD24  24u   /* WRITE_BLOCK                   -> R1 + data      */
#define SDP_CMD25  25u   /* WRITE_MULTIPLE_BLOCK          -> R1 + data...   */
#define SDP_CMD55  55u   /* APP_CMD (prefix for ACMDs)    -> R1             */

/* ---- Application commands (post-CMD55) ---- */
#define SDP_ACMD6   6u   /* SET_BUS_WIDTH (1 or 4 bit)    -> R1             */
#define SDP_ACMD41 41u   /* SD_SEND_OP_COND (init loop)   -> R3             */

/* ---- Command argument constants ---- */
/* CMD8 SEND_IF_COND: VHS=1 (2.7-3.6V) + check pattern 0xAA.
 * Card echoes the low 12 bits in its R7; mismatch => not v2+ or busted. */
#define SDP_CMD8_ARG_VHS_27_36   0x000001AAu
#define SDP_CMD8_ECHO_MASK       0x00000FFFu
#define SDP_CMD8_ECHO_EXPECTED   0x000001AAu

/* ACMD41 SD_SEND_OP_COND: HCS bit (host supports SDHC/SDXC).
 * Card replies with OCR; bit 31 = power-up done, bit 30 = CCS (HC). */
#define SDP_ACMD41_ARG_HCS       0x40000000u
#define SDP_OCR_BUSY_DONE        0x80000000u
#define SDP_OCR_CCS_IS_HC        0x40000000u

/* ACMD6 SET_BUS_WIDTH argument: 0 = 1-bit, 2 = 4-bit. */
#define SDP_ACMD6_ARG_4BIT       2u
#define SDP_ACMD6_ARG_1BIT       0u

/* CMD6 SWITCH_FUNC argument: bit 31 = 1 (set/switch), 0 (check).
 * Bits [3:0] of group 1 = 1 selects HS50. Other groups untouched (0xF). */
#define SDP_CMD6_ARG_SET_HS50    0x80FFFFF1u
#define SDP_CMD6_ARG_CHECK_HS50  0x00FFFFF1u

/* CMD16 SET_BLOCKLEN argument: 512 bytes — the only size SDHC supports
 * and what we always use for SDSC. */
#define SDP_BLOCKLEN_512         512u

/* ---- Native-mode response sizes (in bytes, including direction byte) ---- */
/* All native responses except R2 are 48 bits == 6 bytes. R2 is 136 bits. */
#define SDP_RESP_R1_BYTES   6
#define SDP_RESP_R2_BYTES  17
#define SDP_RESP_R3_BYTES   6
#define SDP_RESP_R6_BYTES   6
#define SDP_RESP_R7_BYTES   6

/* ---- SPI-mode tokens ----
 *
 * In SPI mode the card validates the CRC byte ONLY for CMD0 (which
 * sets idle state) and CMD8 (which probes voltage). After CMD0+ACMD41
 * the card stops checking unless CMD59 explicitly enables it. The host
 * still has to send a valid CRC byte for CMD0 and CMD8; for everything
 * else any byte ending in bit 0 = 1 works (we send sd_crc7 anyway). */

/* Pre-computed CRC byte (CRC7 << 1 | 1) for CMD0 with arg=0. */
#define SDP_CMD0_CRC_SPI         0x95u
/* Pre-computed CRC byte for CMD8 with arg=0x000001AA. */
#define SDP_CMD8_CRC_SPI         0x87u
/* Idle byte placeholder: spi_io(0xFF) until R1 arrives. */
#define SDP_SPI_IDLE_BYTE        0xFFu

/* R1 (single-byte) error bits we care about in init / error paths. */
#define SDP_R1_IDLE              0x01u
#define SDP_R1_ERASE_RST         0x02u
#define SDP_R1_ILLEGAL_CMD       0x04u
#define SDP_R1_CRC_ERROR         0x08u
#define SDP_R1_ERASE_SEQ_ERROR   0x10u
#define SDP_R1_ADDR_ERROR        0x20u
#define SDP_R1_PARAM_ERROR       0x40u

/* Data tokens. */
#define SDP_TOK_DATA_START_SBR   0xFEu  /* single block read / write,
                                           multi-block read */
#define SDP_TOK_DATA_START_MBW   0xFCu  /* multi-block write data block */
#define SDP_TOK_STOP_TRAN        0xFDu  /* multi-block write stop */

/* Data-response token (CMD24/CMD25 reply) layout:
 *   bits 7..5 = X (don't care)
 *   bit 4     = 0 (start)
 *   bits 3..1 = status (0b010 = data accepted)
 *   bit 0     = 1 (end) */
#define SDP_DATA_RESP_MASK       0x1Fu
#define SDP_DATA_RESP_ACCEPTED   0x05u
#define SDP_DATA_RESP_CRC_ERR    0x0Bu
#define SDP_DATA_RESP_WRITE_ERR  0x0Du

#endif /* LIB_SD_HOST_SD_PROTO_H */
