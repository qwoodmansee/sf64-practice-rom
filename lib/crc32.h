#ifndef LIB_CRC32_H
#define LIB_CRC32_H

#include "lib_types.h"

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stddef.h>
#endif

/* CRC32-IEEE (poly 0xEDB88320, reflected). Used by practice_overlay to
 * derive a stable build ID from each ovl_iN segment. Bitwise rather than
 * table-driven so the binary stays small — the practice ROM cares about
 * every byte of .rodata and we only call this a handful of times per boot. */

/* Initial state for a streaming CRC. */
uint32_t crc32_init(void);

/* Fold `len` bytes from `data` into `state`, returning the new state.
 * `data` may be NULL only when `len == 0`. */
uint32_t crc32_update(uint32_t state, const void *data, size_t len);

/* Finalize a streaming CRC and return the wire value. */
uint32_t crc32_finalize(uint32_t state);

/* One-shot helper equivalent to init + update + finalize. */
uint32_t crc32(const void *data, size_t len);

#endif /* LIB_CRC32_H */
