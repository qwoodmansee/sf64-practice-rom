#ifndef LIB_IODEV_H
#define LIB_IODEV_H

#include "lib_types.h"

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

/* Diagnostic-only breadcrumb hook. When set non-NULL by the app, the SC64
 * backend calls it at each waypoint inside sc64_execute_cmd with a small step
 * code (see iodev_sc64.c). Lets the app visualize exactly where a hardware SD
 * command wedges without lib/ depending on any game header. NULL = no-op.
 * Codes are 1-based and stable; the app maps code -> on-screen color. */
typedef void (*iodev_breadcrumb_fn)(int code);
extern iodev_breadcrumb_fn gIodevBreadcrumb;

/* Run-once hardware probe. Idempotent; result cached after first call. */
iodev_id_t iodev_detect(void);

/* Initializes the SD card on the active flashcart.
 * Must be called once after iodev_detect() returns non-NONE.
 * Returns IODEV_OK on success, negative error code otherwise.
 * Result is cached; iodev_sd_was_ok() reads it without re-hitting hardware. */
iodev_result_t iodev_sd_init(void);

/* Returns non-zero if the most-recent iodev_sd_init() returned IODEV_OK.
 * Returns 0 if sd_init was never called or returned an error.
 * diskio.c uses this to avoid re-issuing the SD_OP_INIT command on every f_open. */
int iodev_sd_was_ok(void);

/* Returns the raw result code from the most-recent iodev_sd_init() call.
 * -99 = never called; 0 = IODEV_OK; negative = iodev_result_t error code.
 * Useful for diagnostic HUD messages on carts without IS-Viewer (e.g. ED64). */
int iodev_sd_init_result(void);

/* Read `count` 512-byte sectors starting at `lba` into `buf`.
 * `buf` must be 8-byte aligned (DMA requirement) and `count * 512` bytes long. */
iodev_result_t iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf);

/* Write `count` 512-byte sectors starting at `lba` from `buf`.
 * Same alignment requirement. */
iodev_result_t iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf);

/* Release the hardware SD lock so the host (sc64deployer sd) can access the
 * card. The ROM must not call any FatFs function after this returns until
 * iodev_sd_acquire() re-establishes the lock. On backends without an explicit
 * lock protocol (ED64, stub) this is a no-op that returns IODEV_OK. */
iodev_result_t iodev_sd_release(void);

/* Re-acquire the hardware SD lock (SD_OP_INIT) before a FatFs operation
 * that follows a prior iodev_sd_release(). Updates the init-result cache so
 * diskio.c continues to function correctly. */
iodev_result_t iodev_sd_acquire(void);

/* Returns the raw 32-bit value read from the ED64 EDID register during
 * iodev_detect().  Upper 16 bits should be 0xED64 on a real ED64 X7/X8.
 * Returns 0 if ed64_detect() was never called.
 * Used for diagnostic HUD messages when IODEV_NONE is detected. */
uint32_t iodev_ed64_raw_edid(void);

#endif /* LIB_IODEV_H */
