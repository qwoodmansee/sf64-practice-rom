/* Host unit tests for lib/sd_crc.c.
 *
 * Builds with native gcc + libc; no libultra. Run from repo root via
 *   make lib-test
 *
 * Reference vectors:
 *   - CMD0/CMD8/CMD17 CRC7 results from SD spec §4.5 examples and
 *     widely cross-checked against open-source SD stacks.
 *   - CRC16-CCITT vectors against the classic "123456789" -> 0x31C3
 *     and the all-zeros / all-0xFF blocks (0x0000 / 0x7FA1).
 *   - 4-bit CRC pattern verified independently with a reference Python
 *     implementation; chosen so all four lines agree (catches DAT-line
 *     misordering) and the packed value is non-trivial. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../sd_crc.h"

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

int main(void) {
    /* CRC7 test vectors. Each command frame is 5 bytes (start+cmd byte +
     * 4-byte argument); the spec appends a 6th byte = (CRC7 << 1) | 1. */
    {
        uint8_t cmd0[]  = {0x40, 0x00, 0x00, 0x00, 0x00};
        uint8_t cmd8[]  = {0x48, 0x00, 0x00, 0x01, 0xAA};
        uint8_t cmd17[] = {0x51, 0x00, 0x00, 0x00, 0x00};
        ASSERT_EQ(sd_crc7(cmd0,  sizeof(cmd0)),  0x95, "CRC7 CMD0  -> 0x95");
        ASSERT_EQ(sd_crc7(cmd8,  sizeof(cmd8)),  0x87, "CRC7 CMD8  -> 0x87");
        ASSERT_EQ(sd_crc7(cmd17, sizeof(cmd17)), 0x55, "CRC7 CMD17 -> 0x55");
    }

    /* CRC16-CCITT (init 0). */
    {
        uint8_t zeros[512];
        uint8_t ones[512];
        uint8_t classic[] = "123456789";
        memset(zeros, 0, sizeof(zeros));
        memset(ones, 0xFF, sizeof(ones));
        ASSERT_EQ(sd_crc16_ccitt(zeros, sizeof(zeros)), 0x0000, "CRC16 zeros -> 0x0000");
        ASSERT_EQ(sd_crc16_ccitt(ones,  sizeof(ones)),  0x7FA1, "CRC16 0xFF*512 -> 0x7FA1");
        ASSERT_EQ(sd_crc16_ccitt(classic, 9), 0x31C3, "CRC16 \"123456789\" -> 0x31C3");
    }

    /* 4-bit CRC. */
    {
        uint8_t zeros[512];
        uint8_t pat[512];
        size_t i;
        uint64_t crc;
        uint16_t l0;
        uint16_t l1;
        uint16_t l2;
        uint16_t l3;

        memset(zeros, 0, sizeof(zeros));
        ASSERT_EQ(sd_crc16_4bit(zeros, sizeof(zeros)), 0ULL, "CRC16 4-bit zeros -> 0");

        /* Pattern 0xF0 produces a bit stream where all 4 DAT lines see the
         * same alternating "1010..." sequence (high nibble bit n = 1, low
         * nibble bit n = 0 for any line index). The 4 per-line CRCs must
         * therefore be equal. The expected packed value 0xB6CEB6CEB6CEB6CE
         * was verified with an independent Python reference. */
        for (i = 0; i < 512; i++) {
            pat[i] = 0xF0;
        }
        crc = sd_crc16_4bit(pat, sizeof(pat));
        l0 = (uint16_t)(crc >>  0);
        l1 = (uint16_t)(crc >> 16);
        l2 = (uint16_t)(crc >> 32);
        l3 = (uint16_t)(crc >> 48);
        if (l0 != l1 || l1 != l2 || l2 != l3) {
            printf("FAIL: 4-bit DAT-line symmetry broken: l0=%04X l1=%04X l2=%04X l3=%04X\n",
                   l0, l1, l2, l3);
            failures++;
        } else {
            printf("PASS: CRC16 4-bit symmetry (all 4 lines = 0x%04X)\n", l0);
        }
        ASSERT_EQ(crc, 0xB6CEB6CEB6CEB6CEULL, "CRC16 4-bit 0xF0 pattern -> 0xB6CEB6CEB6CEB6CE");
    }

    if (failures > 0) {
        printf("\n%d test(s) failed\n", failures);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}
