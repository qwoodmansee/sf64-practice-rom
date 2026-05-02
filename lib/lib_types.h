/* Toolchain-portable fixed-width integer types for lib/.
 *
 * lib/ is meant to be portable to host unit tests (gcc with libc) AND to
 * the IDO MIPS toolchain that builds the practice ROM with -nostdinc.
 * <stdint.h> is unavailable in the latter, so we pull in the project's
 * Ultra64 types and bridge them. Hosts with libc get the real <stdint.h>.
 *
 * This is the ONLY file in lib/ that conditionally pulls in a libultra
 * header. Everything else under lib/ depends only on this. */

#ifndef LIB_LIB_TYPES_H
#define LIB_LIB_TYPES_H

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(_LANGUAGE_C_NO_STDINT)
  /* Host build (gcc + libc): use the real header. Callers needing
   * size_t / ptrdiff_t should also `#include <stddef.h>` directly --
   * we deliberately do NOT pull stddef in here because the project's
   * own include/libc/stddef.h is on the search path and the two would
   * collide for host CC_CHECK runs. */
  #include <stdint.h>
#else
  /* Embedded N64 / IDO build: bridge from Ultra64 types.
   * (PR/ultratypes.h defines size_t alongside the fixed-width types.) */
  #include "PR/ultratypes.h"
  typedef u8  uint8_t;
  typedef u16 uint16_t;
  typedef u32 uint32_t;
  typedef u64 uint64_t;
  typedef s8  int8_t;
  typedef s16 int16_t;
  typedef s32 int32_t;
  typedef s64 int64_t;
  /* N64 is 32-bit; matches include/libc/stdint.h:7. Used for pointer
   * arithmetic / alignment checks (e.g. iodev SC64 buf alignment). */
  #ifndef _UINTPTR_T_DEFINED
    typedef u32 uintptr_t;
    #define _UINTPTR_T_DEFINED
  #endif
  #ifndef _INTPTR_T_DEFINED
    typedef s32 intptr_t;
    #define _INTPTR_T_DEFINED
  #endif
#endif

#endif /* LIB_LIB_TYPES_H */
