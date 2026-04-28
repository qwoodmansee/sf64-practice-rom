#include "iodev.h"
#include "iodev_internal.h"

/* Cached after first detection (lazy-initialized).
 * After iodev_detect() runs once, gIodevActive points at exactly one of:
 *   iodev_backend_sc64() / iodev_backend_ed64() / iodev_backend_stub()
 *
 * Declared as a global (not file-static) because IDO strips file-statics
 * from the linker map, and BizHawk functional tests need to look up this
 * address via tools/extract_symbols.py to verify iodev_detect's outcome.
 * The `gIodev` prefix is project-unique to avoid namespace collisions.
 *
 * Intentionally not declared in iodev.h -- public callers use iodev_*
 * dispatchers; this symbol is only addressable from outside via the
 * linker map (tests and debug tools). */
const iodev_backend_t *gIodevActive = 0;

iodev_id_t iodev_detect(void) {
    /* Probe order: SC64 first, then ED64, fallback to stub.
     * First-match-wins; SC64's SCv2 IDENT magic and ED64's REG_EDID magic
     * are distinct enough that cross-detection is not a concern.
     * IDO is C89, so all declarations must precede statements. */
    const iodev_backend_t *candidates[2];
    int i;

    if (gIodevActive) {
        return gIodevActive->id;
    }

    candidates[0] = iodev_backend_sc64();
    candidates[1] = iodev_backend_ed64();

    for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (candidates[i]->detect() == candidates[i]->id) {
            gIodevActive = candidates[i];
            return gIodevActive->id;
        }
    }

    gIodevActive = iodev_backend_stub();
    return IODEV_NONE;
}

iodev_result_t iodev_sd_init(void) {
    if (!gIodevActive) iodev_detect();
    return gIodevActive->sd_init();
}

iodev_result_t iodev_sd_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    if (!gIodevActive) iodev_detect();
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    return gIodevActive->sd_read_sectors(lba, count, buf);
}

iodev_result_t iodev_sd_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    if (!gIodevActive) iodev_detect();
    if (!buf || count == 0) return IODEV_ERR_PARAM;
    return gIodevActive->sd_write_sectors(lba, count, buf);
}
