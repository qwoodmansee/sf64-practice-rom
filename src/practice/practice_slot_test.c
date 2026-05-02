#include "practice.h"
#include "slot_manager.h"

#ifdef PRACTICE_ROM

#define SLOT_TEST_STATE_VERSION 1
#define SLOT_TEST_LIB_VERSION 1
#define SLOT_TEST_SLOT_COUNT 2
#define SLOT_TEST_SLOT_SIZE 128
#define SLOT_TEST_PASS 1

typedef struct PracticeSlotTestState {
    s32 value;
    s32 checksum;
    u8 bytes[8];
} PracticeSlotTestState;

s32 gPracticeSlotTestStatus;
s32 gPracticeSlotTestFirstLoadedValue;
s32 gPracticeSlotTestSecondLoadedValue;
s32 gPracticeSlotTestLoadCalls;
s32 gPracticeSlotTestSlotCount;

static u8 sSlotTestStorage[SLOT_TEST_SLOT_COUNT * SLOT_TEST_SLOT_SIZE];
static PracticeSlotTestState sSlotTestSource;
static PracticeSlotTestState sSlotTestLoaded;

static uint32_t SlotTest_Save(void *buf, uint32_t bufSize) {
    if (bufSize < sizeof(sSlotTestSource)) {
        return bufSize + 1;
    }
    bcopy(&sSlotTestSource, buf, sizeof(sSlotTestSource));
    return sizeof(sSlotTestSource);
}

static int SlotTest_Load(const void *buf, uint32_t size) {
    if (size != sizeof(sSlotTestLoaded)) {
        return -1;
    }
    bcopy(buf, &sSlotTestLoaded, sizeof(sSlotTestLoaded));
    gPracticeSlotTestLoadCalls++;
    return 0;
}

static void SlotTest_SetSource(s32 value, u8 seed) {
    s32 i;

    sSlotTestSource.value = value;
    sSlotTestSource.checksum = value ^ 0x5A5A5A5A;
    for (i = 0; i < (s32)sizeof(sSlotTestSource.bytes); i++) {
        sSlotTestSource.bytes[i] = seed + (u8)i;
    }
}

static bool SlotTest_LoadMatchesSource(void) {
    s32 i;

    if (sSlotTestLoaded.value != sSlotTestSource.value) {
        return false;
    }
    if (sSlotTestLoaded.checksum != sSlotTestSource.checksum) {
        return false;
    }
    for (i = 0; i < (s32)sizeof(sSlotTestSource.bytes); i++) {
        if (sSlotTestLoaded.bytes[i] != sSlotTestSource.bytes[i]) {
            return false;
        }
    }
    return true;
}

static void SlotTest_Fail(s32 code) {
    if (gPracticeSlotTestStatus == 0) {
        slot_manager_init(0, 0, 0, 0, 0);
        gPracticeSlotTestStatus = -code;
    }
}

void Practice_SlotTest_Run(void) {
    gPracticeSlotTestStatus = 0;
    gPracticeSlotTestFirstLoadedValue = 0;
    gPracticeSlotTestSecondLoadedValue = 0;
    gPracticeSlotTestLoadCalls = 0;
    gPracticeSlotTestSlotCount = 0;

    slot_manager_init(SLOT_TEST_STATE_VERSION, SLOT_TEST_LIB_VERSION,
                      SlotTest_Save, SlotTest_Load, SLOT_TEST_SLOT_COUNT);
    if (slot_manager_set_ram_storage(sSlotTestStorage, sizeof(sSlotTestStorage), SLOT_TEST_SLOT_SIZE) != SLOT_MANAGER_OK) {
        SlotTest_Fail(12);
        return;
    }
    gPracticeSlotTestSlotCount = slot_manager_ram_slot_count();

    if (slot_manager_ram_valid(0)) {
        SlotTest_Fail(1);
        return;
    }

    SlotTest_SetSource(0x13572468, 0x10);
    if (slot_manager_save_ram(0) != SLOT_MANAGER_OK) {
        SlotTest_Fail(2);
        return;
    }
    if (!slot_manager_ram_valid(0)) {
        SlotTest_Fail(3);
        return;
    }
    if (slot_manager_load_ram(0) != SLOT_MANAGER_OK) {
        SlotTest_Fail(4);
        return;
    }
    if (!SlotTest_LoadMatchesSource()) {
        SlotTest_Fail(5);
        return;
    }
    gPracticeSlotTestFirstLoadedValue = sSlotTestLoaded.value;

    SlotTest_SetSource(0x24681357, 0x20);
    if (slot_manager_save_ram(1) != SLOT_MANAGER_OK) {
        SlotTest_Fail(6);
        return;
    }
    if (slot_manager_load_ram(0) != SLOT_MANAGER_OK) {
        SlotTest_Fail(7);
        return;
    }
    if (sSlotTestLoaded.value != gPracticeSlotTestFirstLoadedValue) {
        SlotTest_Fail(8);
        return;
    }
    if (slot_manager_load_ram(1) != SLOT_MANAGER_OK) {
        SlotTest_Fail(9);
        return;
    }
    if (!SlotTest_LoadMatchesSource()) {
        SlotTest_Fail(10);
        return;
    }
    gPracticeSlotTestSecondLoadedValue = sSlotTestLoaded.value;

    if ((slot_manager_next_slot(1) != 0) || (slot_manager_prev_slot(0) != 1)) {
        SlotTest_Fail(11);
        return;
    }

    slot_manager_init(0, 0, 0, 0, 0);
    gPracticeSlotTestStatus = SLOT_TEST_PASS;
}

#endif
