/* Host unit tests for lib/slot_manager.c.
 *
 * Phase 3 is RAM-only: the manager writes a versioned SF64 header, delegates
 * payload encode/decode to injected callbacks, validates headers before load,
 * and cycles fixed RAM slots with wraparound. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slot_manager.h"

typedef struct FakeState {
    uint32_t counter;
    uint16_t flags;
    uint8_t bytes[6];
} FakeState;

static int failures = 0;
static FakeState source_state;
static FakeState loaded_state;
static uint32_t save_size_override;
static int load_calls;

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

static uint32_t save_fake_state(void *buf, uint32_t buf_size) {
    if (save_size_override != 0) {
        return save_size_override;
    }
    if (buf_size < sizeof(source_state)) {
        return buf_size + 1;
    }
    memcpy(buf, &source_state, sizeof(source_state));
    return sizeof(source_state);
}

static int load_fake_state(const void *buf, uint32_t size) {
    load_calls++;
    if (size != sizeof(loaded_state)) {
        return -99;
    }
    memcpy(&loaded_state, buf, sizeof(loaded_state));
    return 0;
}

static void set_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 0);
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

int main(void) {
    uint8_t storage[3 * 128];

    memset(storage, 0, sizeof(storage));
    slot_manager_init(0x0102, 0x0001, save_fake_state, load_fake_state, 3);
    ASSERT_EQ(slot_manager_set_ram_storage(storage, sizeof(storage), 128), SLOT_MANAGER_OK, "T0 storage accepted");

    /* T1: slots start invalid, then save/load round-trips through callbacks. */
    source_state.counter = 0x11223344;
    source_state.flags = 0x55AA;
    source_state.bytes[0] = 9;
    source_state.bytes[1] = 8;
    source_state.bytes[2] = 7;
    source_state.bytes[3] = 6;
    source_state.bytes[4] = 5;
    source_state.bytes[5] = 4;

    ASSERT_EQ(slot_manager_ram_valid(0), 0, "T1a slot 0 starts invalid");
    ASSERT_EQ(slot_manager_save_ram(0), SLOT_MANAGER_OK, "T1b save slot 0 succeeds");
    ASSERT_EQ(slot_manager_ram_valid(0), 1, "T1c slot 0 is valid after save");

    memset(&loaded_state, 0, sizeof(loaded_state));
    source_state.counter = 0;
    ASSERT_EQ(slot_manager_load_ram(0), SLOT_MANAGER_OK, "T1d load slot 0 succeeds");
    ASSERT_EQ(loaded_state.counter, 0x11223344, "T1e loaded counter restored from payload");
    ASSERT_EQ(loaded_state.flags, 0x55AA, "T1f loaded flags restored from payload");

    /* T2: independent slots keep independent payloads. */
    source_state.counter = 0xAABBCCDD;
    source_state.flags = 0x1234;
    ASSERT_EQ(slot_manager_save_ram(1), SLOT_MANAGER_OK, "T2a save slot 1 succeeds");
    ASSERT_EQ(slot_manager_load_ram(0), SLOT_MANAGER_OK, "T2b reload slot 0");
    ASSERT_EQ(loaded_state.counter, 0x11223344, "T2c slot 0 retained original state");
    ASSERT_EQ(slot_manager_load_ram(1), SLOT_MANAGER_OK, "T2d load slot 1");
    ASSERT_EQ(loaded_state.counter, 0xAABBCCDD, "T2e slot 1 has new state");

    /* T3: clear invalidates a slot and load refuses it. */
    slot_manager_clear_ram(0);
    ASSERT_EQ(slot_manager_ram_valid(0), 0, "T3a clear invalidates slot");
    ASSERT_EQ(slot_manager_load_ram(0), SLOT_MANAGER_ERR_INVALID_SLOT, "T3b load cleared slot refused");

    /* T4: corrupt header magic is refused before the load callback runs. */
    ASSERT_EQ(slot_manager_save_ram(2), SLOT_MANAGER_OK, "T4a save slot 2 succeeds");
    load_calls = 0;
    storage[2 * 128] = 'B';
    ASSERT_EQ(slot_manager_load_ram(2), SLOT_MANAGER_ERR_MAGIC, "T4b bad magic refused");
    ASSERT_EQ(load_calls, 0, "T4c bad magic does not call load callback");

    /* T5: state version mismatch is refused before payload application. */
    ASSERT_EQ(slot_manager_save_ram(2), SLOT_MANAGER_OK, "T5a resave slot 2");
    load_calls = 0;
    storage[2 * 128 + 0x06] = 0x03;
    storage[2 * 128 + 0x07] = 0x01;
    ASSERT_EQ(slot_manager_load_ram(2), SLOT_MANAGER_ERR_VERSION, "T5b state version mismatch refused");
    ASSERT_EQ(load_calls, 0, "T5c version mismatch does not call load callback");

    /* T6: total_size larger than the slot capacity is refused as corrupt. */
    ASSERT_EQ(slot_manager_save_ram(2), SLOT_MANAGER_OK, "T6a resave slot 2");
    load_calls = 0;
    set_le32(&storage[2 * 128 + 0x08], 0x00010000);
    ASSERT_EQ(slot_manager_load_ram(2), SLOT_MANAGER_ERR_CORRUPT, "T6b impossible total_size refused");
    ASSERT_EQ(load_calls, 0, "T6c corrupt size does not call load callback");

    /* T6d: a corrupted PAYLOAD byte (valid header) is refused via CRC32.
     * SD transports on real hardware corrupted payloads in flight; magic +
     * version + size cannot see that, the payload CRC at header 0x0C can. */
    ASSERT_EQ(slot_manager_save_ram(2), SLOT_MANAGER_OK, "T6d-a resave slot 2");
    load_calls = 0;
    storage[2 * 128 + SLOT_MANAGER_HEADER_SIZE] ^= 0xFF;
    ASSERT_EQ(slot_manager_load_ram(2), SLOT_MANAGER_ERR_CORRUPT, "T6d-b payload byte flip refused (CRC)");
    ASSERT_EQ(load_calls, 0, "T6d-c corrupt payload does not call load callback");

    /* T7: callback payload overflow rejects the save and leaves slot invalid. */
    slot_manager_clear_ram(2);
    save_size_override = 500;
    ASSERT_EQ(slot_manager_save_ram(2), SLOT_MANAGER_ERR_OVERFLOW, "T7a oversized callback payload rejected");
    ASSERT_EQ(slot_manager_ram_valid(2), 0, "T7b overflow save leaves slot invalid");
    save_size_override = 0;

    /* T8: slot cycling wraps both directions. */
    ASSERT_EQ(slot_manager_next_slot(0), 1, "T8a next slot advances");
    ASSERT_EQ(slot_manager_next_slot(2), 0, "T8b next slot wraps");
    ASSERT_EQ(slot_manager_prev_slot(0), 2, "T8c prev slot wraps");
    ASSERT_EQ(slot_manager_ram_slot_count(), 3, "T8d slot count reported");

    /* T9: SD entry points are present but unavailable in Phase 3. */
    ASSERT_EQ(slot_manager_save_sd_named("/x.sf64st"), SLOT_MANAGER_ERR_UNSUPPORTED, "T9a SD save unsupported in Phase 3");
    ASSERT_EQ(slot_manager_load_sd_named("/x.sf64st"), SLOT_MANAGER_ERR_UNSUPPORTED, "T9b SD load unsupported in Phase 3");

    /* T10: impossible 32-bit slot arena sizes are rejected before save-time
     * pointer arithmetic can wrap on N64. */
    slot_manager_init(0x0102, 0x0001, save_fake_state, load_fake_state, 3);
    ASSERT_EQ(slot_manager_set_ram_storage(storage, 0xFFFFFFFFu, 0x80000000u),
              SLOT_MANAGER_ERR_OVERFLOW, "T10 slot arena size overflow rejected");

    if (failures > 0) {
        printf("\n%d test(s) failed\n", failures);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}
