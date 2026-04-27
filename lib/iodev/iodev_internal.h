#ifndef LIB_IODEV_INTERNAL_H
#define LIB_IODEV_INTERNAL_H

#include "iodev.h"

/* Per-backend function table. Backends supply one of these to the registry. */
typedef struct {
    iodev_id_t      id;
    iodev_id_t    (*detect)(void);
    iodev_result_t (*sd_init)(void);
    iodev_result_t (*sd_read_sectors)(uint32_t lba, uint32_t count, void *buf);
    iodev_result_t (*sd_write_sectors)(uint32_t lba, uint32_t count, const void *buf);
} iodev_backend_t;

/* Each backend exposes a single getter for its descriptor. */
const iodev_backend_t *iodev_backend_sc64(void);
const iodev_backend_t *iodev_backend_ed64(void);  /* Phase 1b */
const iodev_backend_t *iodev_backend_stub(void);

#endif /* LIB_IODEV_INTERNAL_H */
