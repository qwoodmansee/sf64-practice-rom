/* Host unit tests for lib/crc32.c.
 *
 * Known-answer vectors come from the published CRC32-IEEE spec (the same
 * polynomial used by zip, gzip, PNG, Ethernet, etc.). We need bit-for-bit
 * agreement with that wire value because the practice overlay build IDs
 * are persisted in save files and must round-trip across rebuilds of the
 * tooling. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../crc32.h"

static int failures = 0;

#define ASSERT_EQ(actual, expected, label) do {                              \
    unsigned long long _a = (unsigned long long)(actual);                    \
    unsigned long long _e = (unsigned long long)(expected);                  \
    if (_a != _e) {                                                          \
        printf("FAIL: %s: expected 0x%llX, got 0x%llX\n",                    \
               (label), _e, _a);                                             \
        failures++;                                                          \
    } else {                                                                 \
        printf("PASS: %s\n", (label));                                       \
    }                                                                        \
} while (0)

int main(void) {
    /* T1: empty input has CRC 0 by spec (init ^ finalize cancels). */
    ASSERT_EQ(crc32("", 0), 0x00000000u, "T1 empty input");

    /* T2..T4: well-known canonical CRC32-IEEE vectors. */
    ASSERT_EQ(crc32("a", 1), 0xE8B7BE43u, "T2 single-byte 'a'");
    ASSERT_EQ(crc32("123456789", 9), 0xCBF43926u, "T3 check-string '123456789'");

    {
        const char *fox = "The quick brown fox jumps over the lazy dog";
        ASSERT_EQ(crc32(fox, strlen(fox)), 0x414FA339u, "T4 'fox' pangram");
    }

    /* T5: streaming the same input across two updates must match the
     * one-shot helper. This is the property practice_overlay relies on
     * when it walks an ovl_iN segment that may straddle a boundary. */
    {
        const char *fox = "The quick brown fox jumps over the lazy dog";
        size_t total = strlen(fox);
        size_t split = 13;
        uint32_t s = crc32_init();
        s = crc32_update(s, fox, split);
        s = crc32_update(s, fox + split, total - split);
        ASSERT_EQ(crc32_finalize(s), 0x414FA339u, "T5 streaming matches one-shot");
    }

    /* T6: zero-length update is a no-op on the running state. */
    {
        uint32_t s1 = crc32_update(crc32_init(), "abc", 3);
        uint32_t s2 = crc32_update(s1, NULL, 0);
        ASSERT_EQ(s1, s2, "T6 zero-length update is identity");
    }

    if (failures > 0) {
        printf("\n%d test(s) failed\n", failures);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}
