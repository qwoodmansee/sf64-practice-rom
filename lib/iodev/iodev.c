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

/* Cached result of the most-recent iodev_sd_init() call.
 * -99 = never called; 0 = IODEV_OK (success); negative = error code.
 * diskio.c reads this to avoid re-issuing SD_OP_INIT on every f_open. */
static int sIodevSdInitResult = -99;

int iodev_sd_was_ok(void) {
    return (sIodevSdInitResult == IODEV_OK);
}

int iodev_sd_init_result(void) {
    return sIodevSdInitResult;
}

iodev_id_t iodev_detect(void) {
    /* Probe order: SC64 first, then ED64, fallback to stub.
     * First-match-wins; SC64's SCv2 IDENT magic and ED64's REG_EDID magic
     * are distinct enough that cross-detection is not a concern.
     * IDO is C89, so all declarations must precede statements. */
    const iodev_backend_t *candidates[4];
    int i;

    if (gIodevActive) {
        return gIodevActive->id;
    }

    /* Probe order: SC64 (SCv2 magic) -> ED64-X (0xAA55 + 0xED64) ->
     * ED64-V2/V2.5 (0x1234 + REG_VER>=0x0116) -> ED64-V1 (0x1234 +
     * REG_VER<0x0116). Distinct unlock magics + version gates make all
     * four mutually exclusive -- a real cart of one variant fails
     * detection in the other three. */
    candidates[0] = iodev_backend_sc64();
    candidates[1] = iodev_backend_ed64();
    candidates[2] = iodev_backend_ed64_v2();
    candidates[3] = iodev_backend_ed64_v1();

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
    sIodevSdInitResult = gIodevActive->sd_init();
    return sIodevSdInitResult;
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

iodev_result_t iodev_sd_release(void) {
    if (!gIodevActive) return IODEV_ERR_NO_DEVICE;
    return gIodevActive->sd_release();
}

iodev_result_t iodev_sd_acquire(void) {
    iodev_result_t r;
    if (!gIodevActive) iodev_detect();
    r = gIodevActive->sd_init();
    /* Always overwrite the cached result. If we cached only successes, a
     * failed re-acquire after a previous success would leave was_ok()
     * returning true and let callers (diskio) skip re-init on a card the
     * hardware no longer owns. */
    sIodevSdInitResult = r;
    return r;
}
