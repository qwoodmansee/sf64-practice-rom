/* Minimal <string.h> shim for the IDO build of vendored FatFs.
 *
 * The project compiles with -nostdinc, so the system <string.h> is unreachable.
 * FatFs's ff.c does `#include <string.h>` and uses memcmp, memcpy, memset,
 * strchr, strlen. This shim declares all five so the compile succeeds.
 *
 * - memcpy, strlen, strchr are provided by libultra (already linked into the ROM).
 * - memcmp and memset are provided by lib/fatfs/ff_libc.c (the project's libc
 *   doesn't expose them; libultra prefers bcopy/bzero).
 *
 * This file is only seen when -Ilib/fatfs is on the include path, which is the
 * case for files under lib/fatfs (per-file include set by the build).
 * Other code in the project should continue to use "libc/string.h" (which
 * does NOT include this shim). */

#ifndef LIB_FATFS_STRING_H
#define LIB_FATFS_STRING_H

/* size_t source -- toolchain-conditional like lib/lib_types.h. On host
 * test builds the system stddef provides it; on IDO the project shim does. */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#  include <stddef.h>
#else
#  include "libc/stddef.h"
#endif

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);

#endif
