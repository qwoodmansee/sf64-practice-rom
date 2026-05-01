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

int slot_manager_save_sd_named(const char *path) {
#ifndef SLOT_MANAGER_USE_FATFS
    (void)path;
    return SLOT_MANAGER_ERR_UNSUPPORTED;
#else
    FIL      fp;
    FRESULT  res;
    UINT     written;
    uint8_t *base;
    uint8_t *payload;
    uint32_t payload_cap;
    uint32_t payload_size;
    uint32_t total_size;

    if (!path || !sSlotManager.save_cb)      return SLOT_MANAGER_ERR_PARAM;
    if (!sSlotManager.sd_scratch ||
        sSlotManager.sd_scratch_size < sSlotManager.max_state_size)
        return SLOT_MANAGER_ERR_NO_STORAGE;

    base        = sSlotManager.sd_scratch;
    payload     = base + SLOT_MANAGER_HEADER_SIZE;
    payload_cap = sSlotManager.sd_scratch_size - SLOT_MANAGER_HEADER_SIZE;

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

    res = f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) return SLOT_MANAGER_ERR_IO_OPEN;

    res = f_write(&fp, base, total_size, &written);
    f_close(&fp);

    if (res != FR_OK || written != total_size) {
        f_unlink(path);
        return SLOT_MANAGER_ERR_IO_WRITE;
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
