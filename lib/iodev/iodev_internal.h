#ifndef LIB_IODEV_INTERNAL_H
#define LIB_IODEV_INTERNAL_H

#include "iodev.h"

/* Per-backend function table. Backends supply one of these to the registry.
 * IDO C89: all struct fields must be initialized positionally in order. */
typedef struct {
    iodev_id_t      id;
    iodev_id_t    (*detect)(void);
    iodev_result_t (*sd_init)(void);
    iodev_result_t (*sd_read_sectors)(uint32_t lba, uint32_t count, void *buf);
    iodev_result_t (*sd_write_sectors)(uint32_t lba, uint32_t count, const void *buf);
    /* Release the hardware SD lock so the host can access the card via USB.
     * Backends without an explicit lock protocol return IODEV_OK (no-op). */
    iodev_result_t (*sd_release)(void);
} iodev_backend_t;

/* Each backend exposes a single getter for its descriptor. */
const iodev_backend_t *iodev_backend_sc64(void);
const iodev_backend_t *iodev_backend_ed64(void);     /* X7/X8 (Phase 1b) */
const iodev_backend_t *iodev_backend_ed64_v2(void);  /* V2/V2.5 */
const iodev_backend_t *iodev_backend_ed64_v1(void);  /* V1 */
const iodev_backend_t *iodev_backend_stub(void);

#endif /* LIB_IODEV_INTERNAL_H */
