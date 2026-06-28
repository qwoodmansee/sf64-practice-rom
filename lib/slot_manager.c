#include "slot_manager.h"
#ifdef SLOT_MANAGER_USE_FATFS
#include "fatfs/ff.h"
#endif

#define SLOT_MAGIC_0 'S'
#define SLOT_MAGIC_1 'F'
#define SLOT_MAGIC_2 '6'
#define SLOT_MAGIC_3 '4'

typedef struct {
    uint16_t state_version;
    uint16_t lib_version;
    save_state_fn save_cb;
    load_state_fn load_cb;
    uint8_t ram_slots;
    uint8_t *storage;
    uint32_t storage_size;
    uint32_t max_state_size;
    uint32_t slot_size[SLOT_MANAGER_MAX_RAM_SLOTS];
    bool slot_valid[SLOT_MANAGER_MAX_RAM_SLOTS];
    uint8_t *sd_scratch;
    uint32_t sd_scratch_size;
} slot_manager_state_t;

static slot_manager_state_t sSlotManager;

/* Diagnostic: raw FRESULT from the most recent failing SD FatFs operation, so
 * the caller can surface the exact cause on screen when there is no
 * IS-Viewer/osSyncPrintf channel. -1 = nothing recorded yet. Values follow
 * FatFs's FRESULT enum (1=DISK_ERR, 4=NO_FILE, 7=DENIED, 8=EXIST, ...). */
static int sLastFatfsErr = -1;

int slot_manager_last_fatfs_err(void) { return sLastFatfsErr; }

static void slot_zero(void *dst, uint32_t len) {
    uint8_t *p = (uint8_t *)dst;

    while (len > 0) {
        *p++ = 0;
        len--;
    }
}

static void slot_put_le16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 0);
    dst[1] = (uint8_t)(value >> 8);
}

static void slot_put_le32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 0);
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static uint16_t slot_get_le16(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8));
}

static uint32_t slot_get_le32(const uint8_t *src) {
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static bool slot_index_valid(int slot) {
    return (slot >= 0) && (slot < (int)sSlotManager.ram_slots);
}

static uint8_t *slot_base(int slot) {
    return &sSlotManager.storage[(uint32_t)slot * sSlotManager.max_state_size];
}

static int slot_validate_storage(void) {
    uint32_t required;

    if (!sSlotManager.storage || sSlotManager.ram_slots == 0) {
        return SLOT_MANAGER_ERR_NO_STORAGE;
    }
    if (sSlotManager.max_state_size < SLOT_MANAGER_HEADER_SIZE) {
        return SLOT_MANAGER_ERR_NO_STORAGE;
    }
    if (sSlotManager.max_state_size > (0xFFFFFFFFu / (uint32_t)sSlotManager.ram_slots)) {
        return SLOT_MANAGER_ERR_OVERFLOW;
    }

    required = (uint32_t)sSlotManager.ram_slots * sSlotManager.max_state_size;
    if (required > sSlotManager.storage_size) {
        return SLOT_MANAGER_ERR_NO_STORAGE;
    }
    return SLOT_MANAGER_OK;
}

void slot_manager_init(uint16_t state_version,
                       uint16_t lib_version,
                       save_state_fn save_cb,
                       load_state_fn load_cb,
                       uint8_t ram_slots) {
    uint8_t i;

    if (ram_slots > SLOT_MANAGER_MAX_RAM_SLOTS) {
        ram_slots = SLOT_MANAGER_MAX_RAM_SLOTS;
    }

    sSlotManager.state_version = state_version;
    sSlotManager.lib_version = lib_version;
    sSlotManager.save_cb = save_cb;
    sSlotManager.load_cb = load_cb;
    sSlotManager.ram_slots = ram_slots;
    sSlotManager.storage = 0;
    sSlotManager.storage_size = 0;
    sSlotManager.max_state_size = 0;

    for (i = 0; i < SLOT_MANAGER_MAX_RAM_SLOTS; i++) {
        sSlotManager.slot_size[i] = 0;
        sSlotManager.slot_valid[i] = false;
    }
}

int slot_manager_set_ram_storage(void *storage, uint32_t storage_size, uint32_t max_state_size) {
    uint8_t i;
    int status;

    sSlotManager.storage = (uint8_t *)storage;
    sSlotManager.storage_size = storage_size;
    sSlotManager.max_state_size = max_state_size;

    for (i = 0; i < SLOT_MANAGER_MAX_RAM_SLOTS; i++) {
        sSlotManager.slot_size[i] = 0;
        sSlotManager.slot_valid[i] = false;
    }

    status = slot_validate_storage();
    if (status != SLOT_MANAGER_OK) {
        sSlotManager.storage = 0;
        sSlotManager.storage_size = 0;
        sSlotManager.max_state_size = 0;
    }
    return status;
}

int slot_manager_save_ram(int slot) {
    uint8_t *base;
    uint8_t *payload;
    uint32_t payload_cap;
    uint32_t payload_size;
    uint32_t total_size;
    int storage_status;

    if (!slot_index_valid(slot) || !sSlotManager.save_cb) {
        return SLOT_MANAGER_ERR_INVALID_SLOT;
    }

    storage_status = slot_validate_storage();
    if (storage_status != SLOT_MANAGER_OK) {
        return storage_status;
    }

    base = slot_base(slot);
    payload = &base[SLOT_MANAGER_HEADER_SIZE];
    payload_cap = sSlotManager.max_state_size - SLOT_MANAGER_HEADER_SIZE;

    slot_zero(base, sSlotManager.max_state_size);
    sSlotManager.slot_valid[slot] = false;
    sSlotManager.slot_size[slot] = 0;

    payload_size = sSlotManager.save_cb(payload, payload_cap);
    if (payload_size > payload_cap) {
        return SLOT_MANAGER_ERR_OVERFLOW;
    }

    total_size = SLOT_MANAGER_HEADER_SIZE + payload_size;
    base[0x00] = SLOT_MAGIC_0;
    base[0x01] = SLOT_MAGIC_1;
    base[0x02] = SLOT_MAGIC_2;
    base[0x03] = SLOT_MAGIC_3;
    slot_put_le16(&base[0x04], sSlotManager.lib_version);
    slot_put_le16(&base[0x06], sSlotManager.state_version);
    slot_put_le32(&base[0x08], total_size);
    /* Header name, level_id, level_phase, and reserved bytes stay zero until
     * game-side metadata lands with the real practice_save rewrite. */

    sSlotManager.slot_size[slot] = total_size;
    sSlotManager.slot_valid[slot] = true;
    return SLOT_MANAGER_OK;
}

int slot_manager_load_ram(int slot) {
    uint8_t *base;
    uint32_t total_size;
    uint32_t payload_size;
    int cb_result;
    int storage_status;

    if (!slot_index_valid(slot) || !sSlotManager.load_cb || !sSlotManager.slot_valid[slot]) {
        return SLOT_MANAGER_ERR_INVALID_SLOT;
    }

    storage_status = slot_validate_storage();
    if (storage_status != SLOT_MANAGER_OK) {
        return storage_status;
    }

    base = slot_base(slot);
    if ((base[0x00] != SLOT_MAGIC_0) ||
        (base[0x01] != SLOT_MAGIC_1) ||
        (base[0x02] != SLOT_MAGIC_2) ||
        (base[0x03] != SLOT_MAGIC_3)) {
        return SLOT_MANAGER_ERR_MAGIC;
    }

    if ((slot_get_le16(&base[0x04]) != sSlotManager.lib_version) ||
        (slot_get_le16(&base[0x06]) != sSlotManager.state_version)) {
        return SLOT_MANAGER_ERR_VERSION;
    }

    total_size = slot_get_le32(&base[0x08]);
    if ((total_size < SLOT_MANAGER_HEADER_SIZE) ||
        (total_size > sSlotManager.max_state_size) ||
        (total_size != sSlotManager.slot_size[slot])) {
        return SLOT_MANAGER_ERR_CORRUPT;
    }

    payload_size = total_size - SLOT_MANAGER_HEADER_SIZE;
    cb_result = sSlotManager.load_cb(&base[SLOT_MANAGER_HEADER_SIZE], payload_size);
    if (cb_result != 0) {
        return SLOT_MANAGER_ERR_CALLBACK;
    }
    return SLOT_MANAGER_OK;
}

bool slot_manager_ram_valid(int slot) {
    if (!slot_index_valid(slot)) {
        return false;
    }
    return sSlotManager.slot_valid[slot];
}

void slot_manager_clear_ram(int slot) {
    if (!slot_index_valid(slot)) {
        return;
    }
    sSlotManager.slot_valid[slot] = false;
    sSlotManager.slot_size[slot] = 0;
}

uint8_t slot_manager_ram_slot_count(void) {
    return sSlotManager.ram_slots;
}

int slot_manager_next_slot(int slot) {
    if (sSlotManager.ram_slots == 0) {
        return SLOT_MANAGER_ERR_INVALID_SLOT;
    }
    if (slot < 0) {
        return 0;
    }
    return (slot + 1) % (int)sSlotManager.ram_slots;
}

int slot_manager_prev_slot(int slot) {
    if (sSlotManager.ram_slots == 0) {
        return SLOT_MANAGER_ERR_INVALID_SLOT;
    }
    if (slot <= 0) {
        return (int)sSlotManager.ram_slots - 1;
    }
    return (slot - 1) % (int)sSlotManager.ram_slots;
}

void slot_manager_set_sd_scratch(void *buf, uint32_t buf_size) {
    sSlotManager.sd_scratch      = (uint8_t *)buf;
    sSlotManager.sd_scratch_size = buf_size;
}

#ifdef SLOT_MANAGER_USE_FATFS
/* Atomic save scratch: file path + ".tmp" suffix. Pak-size paths are
 * comfortably under 255 chars; the buffer covers FatFs's FF_MAX_LFN
 * plus the suffix. Lives in .bss because game-thread stacks are tight. */
#define SLOT_TMP_PATH_MAX  280u
static char sSlotTmpPath[SLOT_TMP_PATH_MAX];

/* Build "<path>.tmp" in sSlotTmpPath. Returns 0 on success, -1 if the
 * full path wouldn't fit -- in which case the caller falls back to a
 * non-atomic write (still safer than truncating the path). */
static int slot_build_tmp_path(const char *path) {
    int i = 0;
    while (path[i] != '\0') {
        if ((unsigned)i >= SLOT_TMP_PATH_MAX - 5u) return -1;  /* leave room for .tmp + NUL */
        sSlotTmpPath[i] = path[i];
        i++;
    }
    sSlotTmpPath[i++] = '.';
    sSlotTmpPath[i++] = 't';
    sSlotTmpPath[i++] = 'm';
    sSlotTmpPath[i++] = 'p';
    sSlotTmpPath[i]   = '\0';
    return 0;
}
#endif

int slot_manager_save_sd_named(const char *path) {
#ifndef SLOT_MANAGER_USE_FATFS
    (void)path;
    return SLOT_MANAGER_ERR_UNSUPPORTED;
#else
    FIL      fp;
    FRESULT  res;
    FRESULT  rc;
    UINT     written;
    uint8_t *base;
    uint8_t *payload;
    uint32_t payload_cap;
    uint32_t payload_size;
    uint32_t total_size;
    const char *write_path;
    int        atomic;

    if (!path || !sSlotManager.save_cb)      return SLOT_MANAGER_ERR_PARAM;
    if (!sSlotManager.sd_scratch ||
        sSlotManager.sd_scratch_size < sSlotManager.max_state_size)
        return SLOT_MANAGER_ERR_NO_STORAGE;

    /* Stage the serialized image in the RAM slot pool, NOT sd_scratch.
     *
     * The save_cb may fill a working snapshot into sd_scratch and serialize
     * OUT of it (the practice ROM's Practice_Save_Cb does exactly this via
     * Practice_SaveScratch()). If the serializer's destination is sd_scratch
     * (base), the large early tags (e.g. overlay bytes) overwrite the
     * snapshot's later fields before their own tags are emitted -- writing
     * code/garbage into the file. Hardware symptom: the restored player array
     * read back as MIPS opcode bytes (corrupt baseSpeed) and the next game
     * frame faulted on (s32)NaN.
     *
     * storage (slot pool) and sd_scratch are separate allocations, so staging
     * in storage keeps the serializer's source (sd_scratch) and destination
     * (storage) disjoint. This mirrors the load path's identical fix. We
     * clobber slot 0's RAM image (mark it invalid), same as load. Fall back to
     * sd_scratch only when no distinct storage is configured -- in that case
     * the caller's save_cb must not read from sd_scratch. */
    if (sSlotManager.storage &&
        sSlotManager.storage_size >= sSlotManager.max_state_size &&
        sSlotManager.storage != sSlotManager.sd_scratch) {
        base        = sSlotManager.storage;
        payload_cap = sSlotManager.max_state_size - SLOT_MANAGER_HEADER_SIZE;
        sSlotManager.slot_valid[0] = false;
        sSlotManager.slot_size[0]  = 0;
    } else {
        base        = sSlotManager.sd_scratch;
        payload_cap = sSlotManager.sd_scratch_size - SLOT_MANAGER_HEADER_SIZE;
    }
    payload     = base + SLOT_MANAGER_HEADER_SIZE;

    slot_zero(base, sSlotManager.max_state_size);
    payload_size = sSlotManager.save_cb(payload, payload_cap);
    if (payload_size > payload_cap) return SLOT_MANAGER_ERR_OVERFLOW;

    total_size = SLOT_MANAGER_HEADER_SIZE + payload_size;
    base[0x00] = SLOT_MAGIC_0;
    base[0x01] = SLOT_MAGIC_1;
    base[0x02] = SLOT_MAGIC_2;
    base[0x03] = SLOT_MAGIC_3;
    slot_put_le16(&base[0x04], sSlotManager.lib_version);
    slot_put_le16(&base[0x06], sSlotManager.state_version);
    slot_put_le32(&base[0x08], total_size);

    /* Atomic write: send data to "<path>.tmp" first, then rename to
     * <path> on success. If the ROM crashes mid-write, the user keeps
     * their previous save instead of getting a half-finished one.
     * If the path is too long for the .tmp suffix to fit, fall back to
     * a direct write -- still safer than silently truncating. */
    atomic = (slot_build_tmp_path(path) == 0);
    write_path = atomic ? sSlotTmpPath : path;

    res = f_open(&fp, write_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) { sLastFatfsErr = (int)res; return SLOT_MANAGER_ERR_IO_OPEN; }

    res = f_write(&fp, base, total_size, &written);
    /* f_close performs the final sync: it flushes the file's directory entry
     * (final size + first cluster) and the FAT. Its result was previously
     * dropped -- a failed close leaves the tmp's directory entry uncommitted,
     * which then surfaces downstream as f_rename FR_NO_FILE. Capture it. */
    rc = f_close(&fp);

    if (res != FR_OK || written != total_size || rc != FR_OK) {
        /* Encoding: 1xx = short write (xx = 00), 2xx = close FRESULT (xx),
         * otherwise xx = write FRESULT. */
        sLastFatfsErr = (res != FR_OK) ? (int)res
                      : (rc  != FR_OK) ? 200 + (int)rc
                      : 100;
        f_unlink(write_path);
        return SLOT_MANAGER_ERR_IO_WRITE;
    }

    if (atomic) {
        /* f_rename refuses to overwrite, so unlink any prior file at
         * `path` first. Both unlink and rename are best-effort.
         *
         * Diagnostic encoding for sLastFatfsErr (3 digits): hundreds =
         * f_stat(tmp) FRESULT (is the file we just wrote findable on disk?),
         * tens = f_unlink(dest) FRESULT, ones = f_rename FRESULT.
         * Examples: 444 = tmp NOT found by stat AND rename -> the tmp's
         * directory entry never committed (write/sync transport problem);
         * 044 = stat found tmp(0) but rename can't(4) -> rename-internal
         * lookup/LFN issue; xx7 = rename DENIED (dest still present). */
        FRESULT rs = f_stat(sSlotTmpPath, 0);
        FRESULT ru = f_unlink(path);
        res = f_rename(sSlotTmpPath, path);
        if (res != FR_OK) {
            sLastFatfsErr = ((int)rs * 100) + ((int)ru * 10) + (int)res;
            f_unlink(sSlotTmpPath);
            return SLOT_MANAGER_ERR_IO_RENAME;
        }
    }

    return SLOT_MANAGER_OK;
#endif
}

int slot_manager_load_sd_named(const char *path) {
#ifndef SLOT_MANAGER_USE_FATFS
    (void)path;
    return SLOT_MANAGER_ERR_UNSUPPORTED;
#else
    FIL      fp;
    FRESULT  res;
    FSIZE_t  fsize;
    UINT     bytes_read;
    uint8_t *base;
    uint32_t io_cap;
    uint32_t total_size;
    uint32_t payload_size;
    int      cb_result;

    if (!path || !sSlotManager.load_cb)      return SLOT_MANAGER_ERR_PARAM;
    if (!sSlotManager.sd_scratch ||
        sSlotManager.sd_scratch_size < sSlotManager.max_state_size)
        return SLOT_MANAGER_ERR_NO_STORAGE;

    /* Use the RAM slot pool as the file I/O staging buffer so the load_cb's
     * bzero(sd_scratch) cannot destroy the TLV payload.  sd_scratch and the
     * slot pool are separate allocations at different RDRAM addresses.
     * Overwriting slot 0's bytes is acceptable; we mark the slot invalid. */
    if (sSlotManager.storage &&
        sSlotManager.storage_size >= sSlotManager.max_state_size) {
        base   = sSlotManager.storage;
        io_cap = sSlotManager.max_state_size;
        sSlotManager.slot_valid[0] = false;
        sSlotManager.slot_size[0]  = 0;
    } else {
        base   = sSlotManager.sd_scratch;
        io_cap = sSlotManager.sd_scratch_size;
    }

    res = f_open(&fp, path, FA_READ);
    if (res != FR_OK) return SLOT_MANAGER_ERR_IO_OPEN;

    fsize = f_size(&fp);
    if (fsize < SLOT_MANAGER_HEADER_SIZE || fsize > io_cap) {
        f_close(&fp);
        return SLOT_MANAGER_ERR_CORRUPT;
    }

    res = f_read(&fp, base, (UINT)fsize, &bytes_read);
    f_close(&fp);

    if (res != FR_OK || bytes_read != (UINT)fsize) return SLOT_MANAGER_ERR_IO_READ;

    if (base[0x00] != SLOT_MAGIC_0 || base[0x01] != SLOT_MAGIC_1 ||
        base[0x02] != SLOT_MAGIC_2 || base[0x03] != SLOT_MAGIC_3) {
        return SLOT_MANAGER_ERR_MAGIC;
    }

    if (slot_get_le16(&base[0x04]) != sSlotManager.lib_version ||
        slot_get_le16(&base[0x06]) != sSlotManager.state_version) {
        return SLOT_MANAGER_ERR_VERSION;
    }

    total_size = slot_get_le32(&base[0x08]);
    if (total_size != (uint32_t)fsize || total_size < SLOT_MANAGER_HEADER_SIZE) {
        return SLOT_MANAGER_ERR_CORRUPT;
    }

    payload_size = total_size - SLOT_MANAGER_HEADER_SIZE;
    cb_result = sSlotManager.load_cb(&base[SLOT_MANAGER_HEADER_SIZE], payload_size);
    if (cb_result != 0) return SLOT_MANAGER_ERR_CALLBACK;
    return SLOT_MANAGER_OK;
#endif
}
