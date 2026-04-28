#ifndef LIB_SERIAL_H
#define LIB_SERIAL_H

#include "lib_types.h"

/* Hosted gcc unit tests pull system <stddef.h>. The mips-elf Clang build for the
 * ROM carries -D_LANGUAGE_C/-D_MIPS_ISA=... rather than stripping __STDC_HOSTED__,
 * so guarding on __mips__ alone is unreliable; omit system stddef whenever the ROM
 * MIPS ISA macros are active to avoid clashes with include/libc/stddef.h */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(_MIPS_ISA)
#include <stddef.h>
#endif
#if defined(_MIPS_ISA)
/* ROM build: skip system <stddef.h> (clashes with libc) but we still need size_t. */
#include "libc/stddef.h"
#endif

typedef struct {
    uint8_t *buf;
    size_t capacity;
    size_t pos;
} serial_writer_t;

typedef struct {
    const uint8_t *buf;
    size_t size;
    size_t pos;
} serial_reader_t;

typedef enum {
    SERIAL_OK_TAG_READ = 1,
    SERIAL_OK_END = 0,
    SERIAL_ERR_TRUNCATED = -1,
    SERIAL_ERR_OVERFLOW = -2,
} serial_status_t;

void serial_writer_init(serial_writer_t *w, void *buf, size_t cap);
int serial_put_tag(serial_writer_t *w, uint16_t tag, const void *data, uint32_t len);
size_t serial_writer_size(const serial_writer_t *w);

void serial_reader_init(serial_reader_t *r, const void *buf, size_t size);
serial_status_t serial_get_next(serial_reader_t *r, uint16_t *tag, uint32_t *len, const void **data);

#endif /* LIB_SERIAL_H */
