#include "serial.h"

#define SERIAL_HEADER_SIZE 8u

static void serial_put_le16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 0);
    dst[1] = (uint8_t)(value >> 8);
}

static void serial_put_le32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 0);
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static uint16_t serial_get_le16(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8));
}

static uint32_t serial_get_le32(const uint8_t *src) {
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void serial_copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t len) {
    while (len > 0) {
        *dst++ = *src++;
        len--;
    }
}

void serial_writer_init(serial_writer_t *w, void *buf, size_t cap) {
    if (!w) {
        return;
    }
    w->buf = (uint8_t *)buf;
    w->capacity = cap;
    w->pos = 0;
}

int serial_put_tag(serial_writer_t *w, uint16_t tag, const void *data, uint32_t len) {
    size_t remaining;

    if (!w || !w->buf || ((len > 0) && !data)) {
        return -1;
    }

    if (w->pos > w->capacity) {
        return -1;
    }
    remaining = w->capacity - w->pos;
    if ((remaining < SERIAL_HEADER_SIZE) ||
        ((size_t)len > (remaining - SERIAL_HEADER_SIZE))) {
        return -1;
    }

    serial_put_le16(&w->buf[w->pos + 0], tag);
    serial_put_le16(&w->buf[w->pos + 2], 0);
    serial_put_le32(&w->buf[w->pos + 4], len);
    w->pos += SERIAL_HEADER_SIZE;

    if (len > 0) {
        serial_copy_bytes(&w->buf[w->pos], (const uint8_t *)data, len);
        w->pos += len;
    }
    return 0;
}

size_t serial_writer_size(const serial_writer_t *w) {
    if (!w) {
        return 0;
    }
    return w->pos;
}

void serial_reader_init(serial_reader_t *r, const void *buf, size_t size) {
    if (!r) {
        return;
    }
    r->buf = (const uint8_t *)buf;
    r->size = size;
    r->pos = 0;
}

serial_status_t serial_get_next(serial_reader_t *r, uint16_t *tag, uint32_t *len, const void **data) {
    uint32_t payload_len;

    if (!r || !r->buf || !tag || !len || !data) {
        return SERIAL_ERR_TRUNCATED;
    }

    if (r->pos == r->size) {
        return SERIAL_OK_END;
    }

    if ((r->pos > r->size) || ((r->size - r->pos) < SERIAL_HEADER_SIZE)) {
        return SERIAL_ERR_TRUNCATED;
    }

    *tag = serial_get_le16(&r->buf[r->pos + 0]);
    payload_len = serial_get_le32(&r->buf[r->pos + 4]);
    *len = payload_len;
    r->pos += SERIAL_HEADER_SIZE;

    if ((size_t)payload_len > (r->size - r->pos)) {
        return SERIAL_ERR_OVERFLOW;
    }

    *data = &r->buf[r->pos];
    r->pos += payload_len;
    return SERIAL_OK_TAG_READ;
}
