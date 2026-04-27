#ifndef LIB_IODEV_H
#define LIB_IODEV_H

/* The IDO toolchain used for the practice ROM build is invoked with
 * -nostdinc, so <stdint.h> is unavailable. We pull in the project's
 * Ultra64 types and provide minimal C99 fixed-width aliases when not
 * already defined. Hosts that build this header with a real libc
 * (e.g. for unit tests) should not be affected: their <stdint.h> can
 * be included before this header. */
#include "PR/ultratypes.h"

#ifndef _UINT32_T_DEFINED
#define _UINT32_T_DEFINED
typedef u32 uint32_t;
typedef u8  uint8_t;
typedef u16 uint16_t;
typedef u64 uint64_t;
#endif

/* Identifies which flashcart (if any) was detected at boot. */
typedef enum {
    IODEV_NONE = 0,
    IODEV_SC64 = 1,
    IODEV_ED64 = 2,  /* Reserved for Phase 1b */
} iodev_id_t;

/* Result codes. IODEV_OK == 0; failures are negative. */
typedef enum {
    IODEV_OK            =  0,
    IODEV_ERR_NO_CARD   = -1,  /* No SD card in slot */
    IODEV_ERR_NO_DEVICE = -2,  /* No flashcart detected (IODEV_NONE) */
    IODEV_ERR_IO        = -3,  /* Hardware I/O error */
    IODEV_ERR_TIMEOUT   = -4,
    IODEV_ERR_PARAM     = -5,  /* Bad arguments */
} iodev_result_t;

/* Run-once hardware probe. Idempotent; result cached after first call. */
iodev_id_t iodev_detect(void);

/* Initializes the SD card on the active flashcart.
 * Must be called once after iodev_detect() returns non-NONE.
 * Returns IODEV_OK on success, negative error code otherwise. */
iodev_result_t iodev_sd_init(void);

/* Read `count` 512-byte sectors starting at `lba` into `buf`.
 * `buf` must be 8-byte aligned (DMA requirement) and `count * 512` bytes long. */
iodev_result_t iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf);

/* Write `count` 512-byte sectors starting at `lba` from `buf`.
 * Same alignment requirement. */
iodev_result_t iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf);

#endif /* LIB_IODEV_H */
