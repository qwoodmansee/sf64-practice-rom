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

/* Known limitation (inherited from Practice_LaunchLevel checkpoint warp,
 * not specific to boss-test): on the FIRST gameplay entry after boot,
 * late-level assets (ground, BGM banks, etc.) may not be primed yet, so
 * the warp can render with missing ground or no music. Going to the
 * menu and reloading clears it. Reproduces with CORNERIA -> CP 1 too,
 * so it's a baseline checkpoint-warp issue, not a boss-test regression. */

/* warpProgress: OBJ_BOSS_CO_CARRIER has TWO spawn entries in
 * aCoOnRailsLevelObjects. ObjectInit fields (per sf64object.h) are
 * { zPos1, zPos2, xPos, yPos, rot, id }, so for entry 2:
 *   zPos1=203017.9f, zPos2=4000, xPos=7096, yPos=600.
 * Entry 1 (zPos1=163074.7f, xPos=-1223) is the early flyby cameo seen
 * on both Granga and shortcut routes. Entry 2 is the actual shortcut
 * fight, after the waterfall (165943) and the late-Corneria path-rejoin
 * area where shortcut actors cluster.
 * Warp ~2000 units before spawn for a quick run-up; the xPath override
 * in Play_Init pins the player to xPos=7096 to align with the boss. */
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
