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

/* warpProgress: OBJ_BOSS_CO_CARRIER has TWO spawn entries in
 * aCoOnRailsLevelObjects: 163074.7f (xPos=1118, intro flyby seen on both
 * paths) and 203017.9f (xPos=4000, the actual shortcut-path fight, after
 * the waterfall and the path rejoin area). We target entry 2.
 * Warp ~2000 units before spawn for a quick run-up; the xPath override in
 * Play_Init places the player on the shortcut lane (xPath ~= xPos of the
 * boss entry) so the boss positioning math lands in the right geometry. */
static BossEntry sBossList[] = {
    { "CARRIER", LEVEL_CORNERIA, 0, 201000.0f, true },
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
