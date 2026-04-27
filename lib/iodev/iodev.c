#include "iodev.h"
#include "iodev_internal.h"

/* Cached after first detection (lazy-initialized).
 * After iodev_detect() runs once, sIodevActive points at exactly one of:
 *   iodev_backend_sc64() / iodev_backend_ed64() / iodev_backend_stub()
 *
 * Named with a project-unique prefix so BizHawk symbol extraction can
 * locate it without ambiguity. */
static const iodev_backend_t *sIodevActive = 0;

iodev_id_t iodev_detect(void) {
    /* Probe order: SC64 first, then ED64 (Phase 1b), fallback to stub.
     * IDO is C89, so all declarations must precede statements. */
    const iodev_backend_t *candidates[1];
    int i;

    if (sIodevActive) {
        return sIodevActive->id;
    }

    candidates[0] = iodev_backend_sc64();
    /* candidates[1] = iodev_backend_ed64();  Phase 1b */

    for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (candidates[i]->detect() == candidates[i]->id) {
            sIodevActive = candidates[i];
            return sIodevActive->id;
        }
    }

    sIodevActive = iodev_backend_stub();
    return IODEV_NONE;
}

iodev_result_t iodev_sd_init(void) {
    if (!sIodevActive) iodev_detect();
    return sIodevActive->sd_init();
}

iodev_result_t iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    if (!sIodevActive) iodev_detect();
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    return sIodevActive->sd_read_sectors(lba, count, buf);
}

iodev_result_t iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    if (!sIodevActive) iodev_detect();
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    return sIodevActive->sd_write_sectors(lba, count, buf);
}
