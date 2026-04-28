#include "practice.h"

#ifdef PRACTICE_ROM

#include "practice_overlay.h"
#include "sf64dma.h"
#include "crc32.h"

/* Phase 4 Wave 2.1 -- overlay region map.
 *
 * Maps each saveable LevelId to (a) the SceneId that fox_load.c hands to
 * Load_SceneSetup, and (b) the gameplay overlay segment (`ovl_i1` .. `ovl_i6`)
 * that scene loads from. Both pieces of metadata are mirrored from
 * Load_SceneSetup() in src/engine/fox_load.c -- keep them in sync if the
 * engine table changes.
 *
 * Architecture notes that drive this file:
 *
 * - All six `ovl_iN_VRAM` start symbols resolve to the *same* RDRAM address
 *   because SF64 time-shares one VRAM slot across the gameplay overlays --
 *   only one is loaded at a time. Per-overlay *size* is correctly given by
 *   `ovl_iN_VRAM_END - ovl_iN_VRAM`; per-overlay *ROM bytes* live at
 *   `gDmaTable[idx].vRomAddress` for `pRom.end - pRom.start` bytes.
 *
 * - The build-id hash is a CRC32-IEEE over those bytes, computed lazily and
 *   cached per-overlay. Bytes are unique per overlay and stable across the
 *   whole boot, so the cached value is reusable for the rest of the session.
 *
 * - `get_region` just returns the linker-defined VRAM range for the ovl_iN
 *   that owns `id`. Callers must understand that range is volatile (changes
 *   when a different overlay is loaded). The size is the per-overlay max
 *   we'll bcopy at save / restore time.
 */

typedef struct {
    LevelId level;
    u8      scene;     /* SceneId */
    u8      ovl_index; /* 1..6, names ovl_iN */
} LevelOverlayEntry;

static const LevelOverlayEntry sLevelOverlayMap[] = {
    /* LevelId             SceneId               ovl_iN
     * --------------------+---------------------+------ */
    { LEVEL_CORNERIA,      SCENE_CORNERIA,      1 },
    { LEVEL_METEO,         SCENE_METEO,         2 },
    { LEVEL_SECTOR_X,      SCENE_SECTOR_X,      2 },
    { LEVEL_AREA_6,        SCENE_AREA_6,        3 },
    { LEVEL_SECTOR_Y,      SCENE_SECTOR_Y,      6 },
    { LEVEL_VENOM_1,       SCENE_VENOM_1,       1 },
    { LEVEL_SOLAR,         SCENE_SOLAR,         3 },
    { LEVEL_ZONESS,        SCENE_ZONESS,        3 },
    { LEVEL_VENOM_ANDROSS, SCENE_VENOM_ANDROSS, 6 },
    { LEVEL_MACBETH,       SCENE_MACBETH,       5 },
    { LEVEL_TITANIA,       SCENE_TITANIA,       5 },
    { LEVEL_AQUAS,         SCENE_AQUAS,         3 },
    { LEVEL_FORTUNA,       SCENE_FORTUNA,       4 },
    { LEVEL_KATINA,        SCENE_KATINA,        4 },
    { LEVEL_BOLSE,         SCENE_BOLSE,         4 },
    { LEVEL_SECTOR_Z,      SCENE_SECTOR_Z,      4 },
    { LEVEL_VENOM_2,       SCENE_VENOM_2,       6 },
};

#define OVERLAY_MAP_COUNT  ((s32)(sizeof(sLevelOverlayMap) / sizeof(sLevelOverlayMap[0])))

/* Every LevelId enum member that is intentionally NOT saveable. Membership
 * in this list OR in sLevelOverlayMap is enforced by the static invariant
 * check_overlay_table_complete in tools/practice_invariants.py -- a new
 * LevelId added to include/sf64level.h that is missing from both fails the
 * build, which is the desired behaviour. */
static const LevelId sLevelExclusionList[] = {
    LEVEL_INVALID,    /* sentinel */
    LEVEL_UNK_4,      /* unused / placeholder */
    LEVEL_TRAINING,   /* training mode -- own scene, no checkpoint logic */
    LEVEL_UNK_15,     /* unused / placeholder */
    LEVEL_VERSUS,     /* multiplayer */
    LEVEL_WARP_ZONE,  /* transient warp scene */
};

/* CRC32-IEEE of each ovl_iN segment. Index 0 is unused; ovl_index in 1..6
 * maps directly. `sBuildIdValidBits` bit (i-1) is set after slot i is
 * populated, so a hash of 0 is distinguishable from "not yet computed". */
static u32 sBuildIdCache[7];
static u8  sBuildIdValidBits;

/* gDmaTable index for each ovl_iN, lazily resolved by linear search on
 * `gDmaTable[i].pRom.start == SEGMENT_ROM_START(ovl_iN)`. -1 means the
 * lookup hasn't run yet for that slot. */
static s32  sDmaIndexCache[7] = { -1, -1, -1, -1, -1, -1, -1 };
static bool sDmaIndexCacheInit;

static const LevelOverlayEntry* find_entry(LevelId id) {
    s32 i;

    for (i = 0; i < OVERLAY_MAP_COUNT; i++) {
        if (sLevelOverlayMap[i].level == id) {
            return &sLevelOverlayMap[i];
        }
    }
    return NULL;
}

static s32 dma_index_for_ovl(u8 ovl_index) {
    void* keys[7];
    s32   ov;
    s32   i;

    if (ovl_index < 1 || ovl_index > 6) {
        return -1;
    }

    if (!sDmaIndexCacheInit) {
        keys[0] = NULL;
        keys[1] = SEGMENT_ROM_START(ovl_i1);
        keys[2] = SEGMENT_ROM_START(ovl_i2);
        keys[3] = SEGMENT_ROM_START(ovl_i3);
        keys[4] = SEGMENT_ROM_START(ovl_i4);
        keys[5] = SEGMENT_ROM_START(ovl_i5);
        keys[6] = SEGMENT_ROM_START(ovl_i6);

        for (ov = 1; ov <= 6; ov++) {
            sDmaIndexCache[ov] = -1;
            for (i = 0; gDmaTable[i].pRom.end != NULL; i++) {
                if (gDmaTable[i].pRom.start == keys[ov]) {
                    sDmaIndexCache[ov] = i;
                    break;
                }
            }
        }
        sDmaIndexCacheInit = true;
    }

    return sDmaIndexCache[ovl_index];
}

bool practice_overlay_is_saveable(LevelId id) {
    return find_entry(id) != NULL;
}

s32 practice_overlay_get_region(LevelId id, void** vram, u32* size) {
    const LevelOverlayEntry* entry = find_entry(id);

    if (entry == NULL) {
        if (vram != NULL) {
            *vram = NULL;
        }
        if (size != NULL) {
            *size = 0;
        }
        return -1;
    }

    /* Per-overlay VRAM range. All six start at the same RDRAM address
     * (SF64 time-shares the slot), but per-overlay sizes differ. */
    switch (entry->ovl_index) {
        case 1:
            if (vram != NULL) { *vram = SEGMENT_VRAM_START(ovl_i1); }
            if (size != NULL) { *size = (u32)SEGMENT_VRAM_SIZE(ovl_i1); }
            break;
        case 2:
            if (vram != NULL) { *vram = SEGMENT_VRAM_START(ovl_i2); }
            if (size != NULL) { *size = (u32)SEGMENT_VRAM_SIZE(ovl_i2); }
            break;
        case 3:
            if (vram != NULL) { *vram = SEGMENT_VRAM_START(ovl_i3); }
            if (size != NULL) { *size = (u32)SEGMENT_VRAM_SIZE(ovl_i3); }
            break;
        case 4:
            if (vram != NULL) { *vram = SEGMENT_VRAM_START(ovl_i4); }
            if (size != NULL) { *size = (u32)SEGMENT_VRAM_SIZE(ovl_i4); }
            break;
        case 5:
            if (vram != NULL) { *vram = SEGMENT_VRAM_START(ovl_i5); }
            if (size != NULL) { *size = (u32)SEGMENT_VRAM_SIZE(ovl_i5); }
            break;
        case 6:
            if (vram != NULL) { *vram = SEGMENT_VRAM_START(ovl_i6); }
            if (size != NULL) { *size = (u32)SEGMENT_VRAM_SIZE(ovl_i6); }
            break;
        default:
            if (vram != NULL) { *vram = NULL; }
            if (size != NULL) { *size = 0; }
            return -1;
    }

    return 0;
}

u32 practice_overlay_build_id(LevelId id) {
    const LevelOverlayEntry* entry;
    const LevelOverlayEntry* current;
    s32       dma_idx;
    u8        cache_bit;
    DmaEntry* dma;
    u32       size;
    u32       hash;

    entry = find_entry(id);
    if (entry == NULL) {
        return 0;
    }

    cache_bit = (u8)(1u << (entry->ovl_index - 1));
    if ((sBuildIdValidBits & cache_bit) != 0) {
        return sBuildIdCache[entry->ovl_index];
    }

    /* Defensive: only hash when `id`'s overlay is the one currently loaded
     * in the shared VRAM slot. Otherwise the bytes at `vRomAddress` belong
     * to whichever overlay was loaded last and would produce a wrong hash.
     * Phase 4 only calls build_id for saves, where `id == gCurrentLevel`,
     * so this guard never triggers in normal use. Phase 5 will revisit
     * when cross-scene loads need a build-id for a non-active overlay. */
    current = find_entry(gCurrentLevel);
    if (current == NULL || current->ovl_index != entry->ovl_index) {
        return 0;
    }

    dma_idx = dma_index_for_ovl(entry->ovl_index);
    if (dma_idx < 0) {
        return 0;
    }

    dma = &gDmaTable[dma_idx];
    size = (u32)((uintptr_t)dma->pRom.end - (uintptr_t)dma->pRom.start);
    hash = crc32(dma->vRomAddress, size);

    sBuildIdCache[entry->ovl_index] = hash;
    sBuildIdValidBits |= cache_bit;
    return hash;
}

void practice_overlay_request_load(LevelId id, s32 phase) {
    /* Wave 5 fills this in: drives the cross-scene transition state machine
     * (sets gNextLevel/gNextGameStateTimer, primes overlay DMA, applies the
     * saved phase). Wave 2.1 keeps it as a logging stub so callers compile. */
    osSyncPrintf("[overlay] request_load stub level=%d phase=%d\n",
                 (s32)id, phase);
}

#endif /* PRACTICE_ROM */
