/* Host unit tests for lib/serial.c.
 *
 * These tests document the Phase 3 TLV wire format before implementation:
 * little-endian tag headers, clean end-of-buffer handling, writer overflow
 * rejection, and malformed reader errors. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../serial.h"

static int failures = 0;

#define ASSERT_EQ(actual, expected, label) do {                              \
    long long _a = (long long)(actual);                                      \
    long long _e = (long long)(expected);                                    \
    if (_a != _e) {                                                          \
        printf("FAIL: %s: expected %lld, got %lld\n", (label), _e, _a);      \
        failures++;                                                          \
    } else {                                                                 \
        printf("PASS: %s\n", (label));                                       \
    }                                                                        \
} while (0)

#define ASSERT_MEM_EQ(actual, expected, len, label) do {                     \
    if (memcmp((actual), (expected), (len)) != 0) {                           \
        printf("FAIL: %s: buffers differ\n", (label));                       \
        failures++;                                                          \
    } else {                                                                 \
        printf("PASS: %s\n", (label));                                       \
    }                                                                        \
} while (0)

int main(void) {
    /* T1: writer emits little-endian TLV headers and preserves payload bytes. */
    {
        uint8_t buf[32];
        uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        uint8_t expected[12] = {
            0x34, 0x12, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00,
            0xDE, 0xAD, 0xBE, 0xEF,
        };
        serial_writer_t w;

        memset(buf, 0xCC, sizeof(buf));
        serial_writer_init(&w, buf, sizeof(buf));
        ASSERT_EQ(serial_put_tag(&w, 0x1234, payload, sizeof(payload)), 0, "T1 serial_put_tag succeeds");
        ASSERT_EQ(serial_writer_size(&w), sizeof(expected), "T1 writer size includes header and payload");
        ASSERT_MEM_EQ(buf, expected, sizeof(expected), "T1 wire bytes match little-endian TLV");
    }

    /* T2: reader round-trips unknown and known tags; callers can skip unknowns. */
    {
        uint8_t buf[40];
        uint8_t unknown[2] = { 1, 2 };
        uint8_t known[3] = { 3, 4, 5 };
        serial_writer_t w;
        serial_reader_t r;
        uint16_t tag;
        uint32_t len;
        const void *data;

        serial_writer_init(&w, buf, sizeof(buf));
        ASSERT_EQ(serial_put_tag(&w, 0x7777, unknown, sizeof(unknown)), 0, "T2a write unknown tag");
        ASSERT_EQ(serial_put_tag(&w, 0x0002, known, sizeof(known)), 0, "T2b write known tag");

        serial_reader_init(&r, buf, serial_writer_size(&w));
        ASSERT_EQ(serial_get_next(&r, &tag, &len, &data), SERIAL_OK_TAG_READ, "T2c read unknown tag status");
        ASSERT_EQ(tag, 0x7777, "T2d unknown tag id");
        ASSERT_EQ(len, sizeof(unknown), "T2e unknown tag len");
        ASSERT_MEM_EQ(data, unknown, sizeof(unknown), "T2f unknown tag payload");

        ASSERT_EQ(serial_get_next(&r, &tag, &len, &data), SERIAL_OK_TAG_READ, "T2g read known tag status");
        ASSERT_EQ(tag, 0x0002, "T2h known tag id");
        ASSERT_EQ(len, sizeof(known), "T2i known tag len");
        ASSERT_MEM_EQ(data, known, sizeof(known), "T2j known tag payload");

        ASSERT_EQ(serial_get_next(&r, &tag, &len, &data), SERIAL_OK_END, "T2k clean end after all tags");
    }

    /* T3: zero-length tags are valid and produce a NULL-sized payload view. */
    {
        uint8_t buf[8];
        serial_writer_t w;
        serial_reader_t r;
        uint16_t tag;
        uint32_t len;
        const void *data;

        serial_writer_init(&w, buf, sizeof(buf));
        ASSERT_EQ(serial_put_tag(&w, 0x0042, NULL, 0), 0, "T3a write zero-length tag");
        serial_reader_init(&r, buf, serial_writer_size(&w));
        ASSERT_EQ(serial_get_next(&r, &tag, &len, &data), SERIAL_OK_TAG_READ, "T3b read zero-length tag");
        ASSERT_EQ(tag, 0x0042, "T3c zero-length tag id");
        ASSERT_EQ(len, 0, "T3d zero-length len");
    }

    /* T4: writer overflow is rejected without advancing the write cursor. */
    {
        uint8_t buf[10];
        uint8_t payload[4] = { 0, 1, 2, 3 };
        serial_writer_t w;

        serial_writer_init(&w, buf, sizeof(buf));
        ASSERT_EQ(serial_put_tag(&w, 0x1000, payload, sizeof(payload)), -1, "T4a oversized write rejected");
        ASSERT_EQ(serial_writer_size(&w), 0, "T4b failed write does not advance cursor");
    }

    /* T5: incomplete header reports truncation. */
    {
        uint8_t truncated[3] = { 0x01, 0x00, 0x00 };
        serial_reader_t r;
        uint16_t tag;
        uint32_t len;
        const void *data;

        serial_reader_init(&r, truncated, sizeof(truncated));
        ASSERT_EQ(serial_get_next(&r, &tag, &len, &data), SERIAL_ERR_TRUNCATED, "T5 truncated header detected");
    }

    /* T6: claimed payload beyond remaining buffer reports overflow. */
    {
        uint8_t overflow[8] = {
            0x01, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00,
        };
        serial_reader_t r;
        uint16_t tag;
        uint32_t len;
        const void *data;

        serial_reader_init(&r, overflow, sizeof(overflow));
        ASSERT_EQ(serial_get_next(&r, &tag, &len, &data), SERIAL_ERR_OVERFLOW, "T6 payload overflow detected");
    }

    if (failures > 0) {
        printf("\n%d test(s) failed\n", failures);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}
