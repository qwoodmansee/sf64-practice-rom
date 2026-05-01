#ifndef LIB_SLOT_MANAGER_H
#define LIB_SLOT_MANAGER_H

#include "lib_types.h"

#ifndef __bool_true_false_are_defined
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdbool.h>
#else
typedef int bool;
#define false 0
#define true 1
#endif
#define __bool_true_false_are_defined 1
#endif

#define SLOT_MANAGER_HEADER_SIZE 0x3Cu
#define SLOT_MANAGER_MAX_RAM_SLOTS 8u

typedef enum {
    SLOT_MANAGER_OK = 0,
    SLOT_MANAGER_ERR_PARAM = -1,
    SLOT_MANAGER_ERR_INVALID_SLOT = -2,
    SLOT_MANAGER_ERR_NO_STORAGE = -3,
    SLOT_MANAGER_ERR_OVERFLOW = -4,
    SLOT_MANAGER_ERR_MAGIC = -5,
    SLOT_MANAGER_ERR_VERSION = -6,
    SLOT_MANAGER_ERR_CORRUPT = -7,
    SLOT_MANAGER_ERR_CALLBACK = -8,
    SLOT_MANAGER_ERR_UNSUPPORTED = -9,
    /* Caller-driven: the slot manager itself never returns this. Used by
     * cross-scene practice_save load when the destination scene fails to
     * reach PLAY_UPDATE within the load deadline. */
    SLOT_MANAGER_ERR_TIMEOUT = -10,
    SLOT_MANAGER_ERR_IO_OPEN   = -11,
    SLOT_MANAGER_ERR_IO_WRITE  = -12,
    SLOT_MANAGER_ERR_IO_RENAME = -13,
    SLOT_MANAGER_ERR_IO_READ   = -14,
} slot_manager_result_t;

typedef uint32_t (*save_state_fn)(void *buf, uint32_t buf_size);
typedef int (*load_state_fn)(const void *buf, uint32_t size);

void slot_manager_init(uint16_t state_version,
                       uint16_t lib_version,
                       save_state_fn save_cb,
                       load_state_fn load_cb,
                       uint8_t ram_slots);

/* Phase 3 uses caller-owned RAM so lib/ stays portable and does not depend on
 * either host malloc/free or the game's one-way Memory_Allocate arena. */
int slot_manager_set_ram_storage(void *storage, uint32_t storage_size, uint32_t max_state_size);

int slot_manager_save_ram(int slot);
int slot_manager_load_ram(int slot);
bool slot_manager_ram_valid(int slot);
void slot_manager_clear_ram(int slot);
uint8_t slot_manager_ram_slot_count(void);
int slot_manager_next_slot(int slot);
int slot_manager_prev_slot(int slot);

/* Phase 7: SD persistence. Call once after slot_manager_init() with a
 * caller-owned scratch buffer of at least max_state_size bytes.
 * Required before slot_manager_save_sd_named / load_sd_named will work. */
void slot_manager_set_sd_scratch(void *buf, uint32_t buf_size);

/* SD methods are compiled only when SLOT_MANAGER_USE_FATFS is defined.
 * Without it, save_sd_named and load_sd_named return ERR_UNSUPPORTED. */
int slot_manager_save_sd_named(const char *path);
int slot_manager_load_sd_named(const char *path);

#endif /* LIB_SLOT_MANAGER_H */
