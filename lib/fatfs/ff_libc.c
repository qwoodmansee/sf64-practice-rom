/* libc shims for FatFs.
 *
 * The project's libultra exposes memcpy, strlen, strchr, bcopy, bzero --but
 * NOT memset or memcmp. FatFs needs both. This file provides minimal C89
 * implementations.
 *
 * Why not point FatFs at bzero/bcopy? FatFs needs nonzero memset fills (e.g.
 * `memset(dp->fn, ' ', 11)` for filename padding) and signed-int memcmp
 * return semantics for FAT directory comparisons. bzero/bcopy don't cover
 * those.
 *
 * Implementations are byte-at-a-time on purpose: simple, predictable,
 * never wrong. FatFs's call frequency is low enough that word-aligned
 * fast paths aren't worth the risk. */

#include "string.h"

void *memset(void *dst, int c, size_t n) {
    unsigned char *p = (unsigned char *) dst;
    unsigned char  v = (unsigned char) c;
    while (n > 0) { *p++ = v; n--; }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *) a;
    const unsigned char *pb = (const unsigned char *) b;
    while (n > 0) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++; pb++; n--;
    }
    return 0;
}
