#include "iodev.h"
#include "iodev_internal.h"

static iodev_id_t      stub_detect(void)                           { return IODEV_NONE; }
static iodev_result_t  stub_sd_init(void)                          { return IODEV_ERR_NO_DEVICE; }
static iodev_result_t  stub_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    (void)lba; (void)count; (void)buf;
    return IODEV_ERR_NO_DEVICE;
}
static iodev_result_t  stub_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    (void)lba; (void)count; (void)buf;
    return IODEV_ERR_NO_DEVICE;
}

/* IDO does not support C99 designated initializers; the order below must
 * track the field order in iodev_backend_t (id, detect, sd_init,
 * sd_read_sectors, sd_write_sectors). */
static const iodev_backend_t STUB_BACKEND = {
    IODEV_NONE,
    stub_detect,
    stub_sd_init,
    stub_sd_read_sectors,
    stub_sd_write_sectors,
};

const iodev_backend_t *iodev_backend_stub(void) { return &STUB_BACKEND; }
