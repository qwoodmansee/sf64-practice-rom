#ifndef LIB_SD_CRC_H
#define LIB_SD_CRC_H

/* SD-spec CRC implementations.
 *
 * Host-portable: no libultra dependency. Used by lib/iodev/iodev_ed64.c
 * for bare-metal SDIO command/data CRCs, and reusable from Phase 2's
 * FatFs glue if needed.
 *
 * References: SD Physical Layer Specification v3.0+ section 4.5. */

#include "lib_types.h"  /* uint8_t, uint16_t, uint64_t */

/* size_t comes from <stddef.h> on hosts and from PR/ultratypes.h on
 * IDO (transitively via lib_types.h). lib_types.h does not pull stddef
 * in itself because doing so collides with include/libc/stddef.h during
 * CC_CHECK on host. */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(_LANGUAGE_C_NO_STDINT)
  #include <stddef.h>
#endif

/* SD-spec CRC7 for command frames.
 * Polynomial: x^7 + x^3 + 1 (representation: 0x09 in 7-bit form).
 * Returns the 8-bit byte the SD spec appends to a command:
 * 7-bit CRC in bits 7..1, trailing 1 bit in bit 0. */
uint8_t sd_crc7(const uint8_t *buf, size_t len);

/* SD-spec CRC16-CCITT for a single bit-stream (used for 1-bit DAT mode
 * data blocks and as the building block for the 4-bit case below).
 * Polynomial: x^16 + x^12 + x^5 + 1 (0x1021), init 0. */
uint16_t sd_crc16_ccitt(const uint8_t *buf, size_t len);

/* SD-spec wide-bus CRC16: each of the four DAT lines runs an
 * independent CRC16-CCITT over its own bit-substream. The SD spec
 * serializes a byte high-nibble first, with bit 3 going to DAT3, bit 2
 * to DAT2, bit 1 to DAT1, bit 0 to DAT0, then likewise for the low
 * nibble. Returns the four 16-bit CRCs packed:
 *   bits  0..15 : DAT0 CRC
 *   bits 16..31 : DAT1 CRC
 *   bits 32..47 : DAT2 CRC
 *   bits 48..63 : DAT3 CRC
 *
 * `len` is the number of input bytes; the SD data block is normally
 * 512 bytes (= 1024 nibbles = 1024 bits per DAT line). */
uint64_t sd_crc16_4bit(const uint8_t *buf, size_t len);

#endif /* LIB_SD_CRC_H */
