#include "practice.h"

#ifdef PRACTICE_ROM

bool gPracticeForceCarrier = false;

typedef struct {
    const char* name;
    LevelId hostLevel;
    s32 phase;
    f32 warpProgress;
    bool forceCarrier;
} BossEntry;

/* warpProgress: OBJ_BOSS_CO_CARRIER spawns at zPos1=163074.7f
 * (ast_corneria.c aCoOnRailsLevelObjects entry 9356).
 * Warp ~2000 units before spawn for a quick run-up; the xPath override in
 * Play_Init places the player on the Falco-shortcut lane so the boss
 * positioning math lands in the right geometry. */
static BossEntry sBossList[] = {
    { "CARRIER", LEVEL_CORNERIA, 0, 161000.0f, true },
};

#define BOSS_COUNT ARRAY_COUNT(sBossList)

s32 Practice_BossTest_GetCount(void) {
    return BOSS_COUNT;
}

const char* Practice_BossTest_GetName(s32 index) {
    if ((index < 0) || (index >= (s32)BOSS_COUNT)) {
        return "";
    }
    return sBossList[index].name;
}

void Practice_BossTest_Launch(s32 index) {
    BossEntry* e;

    if ((index < 0) || (index >= (s32)BOSS_COUNT)) {
        return;
    }
    e = &sBossList[index];

    Practice_LaunchLevel(e->hostLevel, e->phase, e->warpProgress);
    /* Set force flags AFTER Practice_LaunchLevel: the non-boss A-press
     * branch in Practice_LevelSelect_Update clears gPracticeForceCarrier,
     * but only on its own path. Setting after the launch ensures the flag
     * is set when the engine reaches Corneria_CoCarrier_Init. */
    gPracticeForceCarrier = e->forceCarrier;
}

#endif
