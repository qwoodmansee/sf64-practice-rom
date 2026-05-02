/* SD-spec CRC implementations.
 *
 * Pure logic, no I/O. Host-portable so that lib/test/test_sd_crc.c can
 * verify correctness with native gcc + libc. Verified against SD spec
 * §4.5 plus independent reference implementations.
 *
 * IDO C89 compatible: declarations at top of block, no designated
 * initializers, no em-dashes, no <stdint.h> (lib_types.h bridges). */

#include "sd_crc.h"

/* CRC7 polynomial: x^7 + x^3 + 1.
 *
 * The SD spec's command CRC works on 8-bit bytes shifted MSB-first into
 * a 7-bit register (we hold it in the high 7 bits of an 8-bit accumulator,
 * so the final result naturally lives in bits 7..1, leaving bit 0 to be
 * the trailing "1" the SD spec appends).
 *
 * Polynomial taps map to mask 0x12 in the byte-aligned shift form: when
 * the bit shifted out (bit 7 before shift) is 1, XOR (poly << 1) into the
 * accumulator. (poly << 1) for x^7 + x^3 + 1 is 0x12 once we ignore the
 * implicit x^7 bit that gets shifted out anyway. */
uint8_t sd_crc7(const uint8_t *buf, size_t len) {
    uint8_t crc;
    size_t i;
    int j;

    crc = 0;
    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x12);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return (uint8_t)(crc | 0x01);
}

/* CRC16-CCITT polynomial 0x1021, init 0.
 *
 * Standard byte-wise serialization: each byte is shifted MSB-first into
 * the CRC register. */
uint16_t sd_crc16_ccitt(const uint8_t *buf, size_t len) {
    uint16_t crc;
    size_t i;
    int j;

    crc = 0;
    for (i = 0; i < len; i++) {
        crc ^= ((uint16_t)buf[i]) << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* Four-bit-bus CRC16.
 *
 * The SD spec serializes a data block over the 4 DAT lines as follows:
 * for each byte of the block, the high nibble is sent first; on each
 * SD clock the four DAT lines simultaneously carry one bit each --
 * DAT3 = bit 3, DAT2 = bit 2, DAT1 = bit 1, DAT0 = bit 0 of the current
 * nibble. Then the low nibble is sent the same way.
 *
 * Each DAT line therefore sees its own independent bit stream, and the
 * card / host both compute one CRC16-CCITT per line over that line's
 * bit stream. The block is followed by 4 * 16 = 64 bits of CRC, one
 * 16-bit CRC per line in MSB-first order on the same 4-bit bus. */
uint64_t sd_crc16_4bit(const uint8_t *buf, size_t len) {
    uint16_t crc[4];
    size_t i;
    int line;
    int nibble_idx;
    uint8_t nib;
    int bit_in;
    int top;

    crc[0] = 0;
    crc[1] = 0;
    crc[2] = 0;
    crc[3] = 0;

    for (i = 0; i < len; i++) {
        for (nibble_idx = 0; nibble_idx < 2; nibble_idx++) {
            if (nibble_idx == 0) {
                nib = (uint8_t)((buf[i] >> 4) & 0x0F);
            } else {
                nib = (uint8_t)(buf[i] & 0x0F);
            }
            /* Distribute the 4 nibble bits across the 4 lines. Each line
             * advances one bit per "clock"; one clock = one nibble. */
            for (line = 0; line < 4; line++) {
                bit_in = (nib >> line) & 1;
                top = (crc[line] >> 15) & 1;
                if (top ^ bit_in) {
                    crc[line] = (uint16_t)((crc[line] << 1) ^ 0x1021);
                } else {
                    crc[line] = (uint16_t)(crc[line] << 1);
                }
            }
        }
    }
    return ((uint64_t)crc[3] << 48) | ((uint64_t)crc[2] << 32)
         | ((uint64_t)crc[1] << 16) |  (uint64_t)crc[0];
}
