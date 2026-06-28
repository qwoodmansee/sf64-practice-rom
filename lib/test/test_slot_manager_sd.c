/* lib/test/test_slot_manager_sd.c
 * slot_manager SD round-trip with RAM diskio.
 *
 * Link with:
 *   diskio_ram.c  (instead of ../fatfs/diskio.c)
 *   ../slot_manager.c
 *   ../fatfs/ff.c ../fatfs/ffunicode.c ../fatfs/ff_libc.c
 */

#define PRACTICE_ROM 1
#define SLOT_MANAGER_USE_FATFS 1

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "../slot_manager.h"
#include "../fatfs/ff.h"

/* Forward-declare the RAM disk helpers from diskio_ram.c */
extern int diskio_ram_init(void);
extern int diskio_ram_format(void);

/* --------------------------------------------------------------------------
 * Fake save/load callbacks
 * -------------------------------------------------------------------------- */

static uint32_t sLastSaved  = 0xDEADBEEFu;
static uint32_t sLastLoaded = 0u;

static uint32_t fake_save(void *buf, uint32_t cap) {
    uint32_t *p = (uint32_t *)buf;
    (void)cap;
    p[0] = sLastSaved;
    return sizeof(uint32_t);
}

static int fake_load(const void *buf, uint32_t size) {
    const uint32_t *p = (const uint32_t *)buf;
    (void)size;
    sLastLoaded = p[0];
    return 0;
}

/* --------------------------------------------------------------------------
 * Scratch + FatFs state
 * -------------------------------------------------------------------------- */

#define SCRATCH_SIZE (256u * 1024u)
static uint8_t sScratch[SCRATCH_SIZE];

static FATFS sFatFs;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static int sFailures = 0;

static void check(int cond, const char *label) {
    if (cond) {
        printf("PASS: %s\n", label);
    } else {
        printf("FAIL: %s\n", label);
        sFailures++;
    }
}

/* Re-initialise the slot manager for each test to avoid cross-test state. */
static void reset_slot_manager(void) {
    slot_manager_init(1, 1, fake_save, fake_load, 4);
    slot_manager_set_ram_storage(sScratch, SCRATCH_SIZE,
                                 SCRATCH_SIZE / 4);
    slot_manager_set_sd_scratch(sScratch, SCRATCH_SIZE);
}

/* Format the RAM disk and mount FatFs.
 * Uses diskio_ram_format() (hand-written FAT16 BPB) because FF_USE_MKFS=0
 * in the vendored ffconf.h — we don't need f_mkfs for host tests. */
static void format_and_mount(void) {
    FRESULT res;

    if (diskio_ram_format() != 0) {
        printf("ABORT: diskio_ram_format failed\n");
        assert(0);
    }

    res = f_mount(&sFatFs, "", 1);
    if (res != FR_OK) {
        printf("ABORT: f_mount failed (%d)\n", (int)res);
        assert(0);
    }

    /* Create the standard practice-ROM directory tree. */
    f_mkdir("/sf64-practice");
    f_mkdir("/sf64-practice/states");
}

/* Put the RAM disk back in a clean, formatted state before each test. */
static void setup(void) {
    int rc = diskio_ram_init();
    assert(rc == 0);
    format_and_mount();
    reset_slot_manager();
}

/* --------------------------------------------------------------------------
 * Test 1: basic save/load round-trip
 * -------------------------------------------------------------------------- */

static void test_save_and_load_roundtrip(void) {
    int result;
    const char *path = "/sf64-practice/states/TEST.SF64ST";

    printf("\n-- test_save_and_load_roundtrip --\n");
    setup();

    sLastSaved  = 0xCAFEBABEu;
    sLastLoaded = 0u;

    result = slot_manager_save_sd_named(path);
    check(result == SLOT_MANAGER_OK, "save returns OK");

    result = slot_manager_load_sd_named(path);
    check(result == SLOT_MANAGER_OK, "load returns OK");

    check(sLastLoaded == sLastSaved, "loaded value matches saved value");
}

/* --------------------------------------------------------------------------
 * Test 2: loading a truncated file
 * -------------------------------------------------------------------------- */

static void test_load_truncated_file(void) {
    int result;
    const char *path = "/sf64-practice/states/BAD.SF64ST";
    FIL   fp;
    UINT  bw;
    uint8_t garbage[4] = { 0xAA, 0xBB, 0xCC, 0xDD };

    printf("\n-- test_load_truncated_file --\n");
    setup();

    /* Write 4 bytes — below SLOT_MANAGER_HEADER_SIZE; load must reject it. */
    f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS);
    f_write(&fp, garbage, sizeof(garbage), &bw);
    f_close(&fp);

    result = slot_manager_load_sd_named(path);
    check(result != SLOT_MANAGER_OK, "truncated file is rejected");
}

/* --------------------------------------------------------------------------
 * Test 3: loading a file with wrong magic bytes
 * -------------------------------------------------------------------------- */

static void test_load_bad_magic(void) {
    int result;
    const char *path = "/sf64-practice/states/BADMAG.SF64ST";
    FIL     fp;
    UINT    bw;
    /* Build a header-sized buffer with a valid total_size field but
     * intentionally wrong magic bytes. */
    uint8_t hdr[0x3C];
    uint32_t total = (uint32_t)sizeof(hdr);

    printf("\n-- test_load_bad_magic --\n");
    setup();

    memset(hdr, 0, sizeof(hdr));
    /* Magic bytes deliberately wrong ('XXXX' instead of 'SF64') */
    hdr[0x00] = 'X'; hdr[0x01] = 'X'; hdr[0x02] = 'X'; hdr[0x03] = 'X';
    /* Versions matching so we reach the magic check */
    hdr[0x04] = 1; hdr[0x05] = 0;  /* lib_version = 1 (little-endian) */
    hdr[0x06] = 1; hdr[0x07] = 0;  /* state_version = 1 */
    /* total_size = sizeof(hdr) so the size check passes and we reach magic */
    hdr[0x08] = (uint8_t)(total >>  0);
    hdr[0x09] = (uint8_t)(total >>  8);
    hdr[0x0A] = (uint8_t)(total >> 16);
    hdr[0x0B] = (uint8_t)(total >> 24);

    f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS);
    f_write(&fp, hdr, sizeof(hdr), &bw);
    f_close(&fp);

    result = slot_manager_load_sd_named(path);
    check(result == SLOT_MANAGER_ERR_MAGIC, "bad magic returns ERR_MAGIC");
}

/* --------------------------------------------------------------------------
 * Test 4: atomic write leaves only the final file
 * -------------------------------------------------------------------------- */

static void test_atomic_write_produces_final_file(void) {
    int result;
    const char *path     = "/sf64-practice/states/ATOMIC.SF64ST";
    const char *tmp_path = "/sf64-practice/states/ATOMIC.SF64ST.tmp";
    FILINFO fi;
    FRESULT fres;

    printf("\n-- test_atomic_write_produces_final_file --\n");
    setup();

    result = slot_manager_save_sd_named(path);
    check(result == SLOT_MANAGER_OK, "atomic save returns OK");

    /* Final file must exist. */
    fres = f_stat(path, &fi);
    check(fres == FR_OK, "final file exists after save");

    /* Temp file must NOT exist (rename completed atomically). */
    fres = f_stat(tmp_path, &fi);
    check(fres == FR_NO_FILE, "tmp file is gone after save");
}

/* --------------------------------------------------------------------------
 * Test 5: a save_cb that serializes OUT of sd_scratch must not have its
 * source clobbered by slot_manager's staging buffer.
 *
 * Regression for the SD-save overlap bug: the practice ROM's Practice_Save_Cb
 * fills a PracticeSnapshot into sd_scratch and then serializes from it. If
 * slot_manager stages the serialized image in sd_scratch too (base =
 * sd_scratch, payload = sd_scratch + HEADER), the large early tags (overlay
 * bytes) overwrite the snapshot's player/actor region before their own tags
 * are emitted -- writing engine code/garbage into the file. On hardware this
 * showed up as a corrupt player array (baseSpeed read back as MIPS opcode
 * bytes). slot_manager must stage in a buffer distinct from sd_scratch (the
 * RAM slot pool), as the load path already does.
 * -------------------------------------------------------------------------- */

#define OVL_STATE_SIZE (16u * 1024u)
static uint8_t sOvlStorage[4u * OVL_STATE_SIZE];  /* slot pool (4 slots) */
static uint8_t sOvlScratch[OVL_STATE_SIZE];       /* sd_scratch: SEPARATE buffer */

static const uint32_t kOvlMarker = 0xC0FFEE11u;
#define OVL_MARK_OFF 0x100u   /* deep enough that base+HEADER writes reach it */
#define OVL_BLOB     0x2000u  /* a big early "overlay bytes" tag */

static uint32_t overlap_save(void *buf, uint32_t cap) {
    uint8_t *out = (uint8_t *)buf;
    uint32_t i;
    if (cap < OVL_BLOB + 4u) return cap + 1u;
    /* (1) Snapshot fill: marker lands deep in sd_scratch (like playerData). */
    memcpy(&sOvlScratch[OVL_MARK_OFF], &kOvlMarker, 4);
    /* (2) Emit a big "overlay" blob first. If the output buffer overlaps
     *     sd_scratch, this stomps the marker before step (3) reads it. */
    for (i = 0; i < OVL_BLOB; i++) out[i] = 0xCC;
    /* (3) Emit the marker, read back from the snapshot scratch. */
    memcpy(&out[OVL_BLOB], &sOvlScratch[OVL_MARK_OFF], 4);
    return OVL_BLOB + 4u;
}

static uint32_t sOvlLoaded;
static int overlap_load(const void *buf, uint32_t size) {
    const uint8_t *in = (const uint8_t *)buf;
    (void)size;
    memcpy(&sOvlLoaded, &in[OVL_BLOB], 4);
    return 0;
}

static void test_save_cb_scratch_not_clobbered(void) {
    int result;
    const char *path = "/sf64-practice/states/OVERLAP.SF64ST";

    printf("\n-- test_save_cb_scratch_not_clobbered --\n");

    /* Fresh RAM disk + FatFs, but wire storage and sd_scratch to SEPARATE
     * buffers (as the real ROM does: slot pool vs sSaveScratchPak). */
    assert(diskio_ram_init() == 0);
    format_and_mount();
    slot_manager_init(1, 1, overlap_save, overlap_load, 4);
    slot_manager_set_ram_storage(sOvlStorage, sizeof(sOvlStorage), OVL_STATE_SIZE);
    slot_manager_set_sd_scratch(sOvlScratch, sizeof(sOvlScratch));

    sOvlLoaded = 0;
    result = slot_manager_save_sd_named(path);
    check(result == SLOT_MANAGER_OK, "overlap save returns OK");

    result = slot_manager_load_sd_named(path);
    check(result == SLOT_MANAGER_OK, "overlap load returns OK");

    check(sOvlLoaded == kOvlMarker,
          "save_cb sd_scratch source survives serialization (no staging overlap)");
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */

int main(void) {
    printf("=== test_slot_manager_sd ===\n");

    test_save_and_load_roundtrip();
    test_load_truncated_file();
    test_load_bad_magic();
    test_atomic_write_produces_final_file();
    test_save_cb_scratch_not_clobbered();

    printf("\n");
    if (sFailures > 0) {
        printf("%d test(s) FAILED\n", sFailures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
