#include "crc32.h"

#define CRC32_POLY  0xEDB88320u
#define CRC32_INIT  0xFFFFFFFFu

uint32_t crc32_init(void) {
    return CRC32_INIT;
}

uint32_t crc32_update(uint32_t state, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    int bit;

    if (len == 0) {
        return state;
    }
    if (p == 0) {
        return state;
    }

    for (i = 0; i < len; i++) {
        state ^= (uint32_t)p[i];
        for (bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)0u - (state & 1u);
            state = (state >> 1) ^ (CRC32_POLY & mask);
        }
    }
    return state;
}

uint32_t crc32_finalize(uint32_t state) {
    return state ^ 0xFFFFFFFFu;
}

uint32_t crc32(const void *data, size_t len) {
    return crc32_finalize(crc32_update(crc32_init(), data, len));
}
