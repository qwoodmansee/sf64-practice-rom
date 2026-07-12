#include "practice.h"

#ifdef PRACTICE_ROM

/* gPracticeForceCarrier moved to practice_shim.c: engine hooks read it and
 * this object is Pak-resident (.practice_late_pak). */

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
    /* Corneria Carrier: shortcut route, xPath=7096 lane.
     * OBJ_BOSS_CO_CARRIER entry 2 in aCoOnRailsLevelObjects: zPos1=203017.9, xPos=7096.
     * Warp ~2000 before spawn; the xPath override in Play_Init aligns player to carrier lane. */
    { "CARRIER",    LEVEL_CORNERIA,      0, 201000.0f, true  },
    /* Meteo Crusher: sole on-rails boss in aMeLevelObjects entry 872: zPos1=302180.9, xPos=-5. */
    { "CRUSHER",    LEVEL_METEO,         0, 300000.0f, false },
    /* Aquas Bacoon: aAqLevelObjects entry 321: zPos1=90735.6, xPos=0. */
    { "BACOON",     LEVEL_AQUAS,         0,  88700.0f, false },
    /* Sector X Spyborg: aSxLevelObjects entry 1024: zPos1=226773.5, xPos=0.
     * Spyborg_Update case 0 snaps pos.x to gPlayer[0].xPath; no xPath override needed. */
    { "SPYBORG",    LEVEL_SECTOR_X,      0, 224000.0f, false },
    /* Solar Vulkain: aSoLevelObjects entry 178: zPos1=170800.0, xPos=0. */
    { "VULKAIN",    LEVEL_SOLAR,         0, 168800.0f, false },
    /* Zoness Sarumarine: aZoLevelObjects entry 499: zPos1=291489.9, xPos=-30.
     * gMissedZoSearchlight initialises false in Play_Init -- "all searchlights hit" route. */
    { "SARUMAR",    LEVEL_ZONESS,        0, 289000.0f, false },
    /* Titania Goras: aTiLevelObjects entry 598: zPos1=87100.0, xPos=0. */
    { "GORAS",      LEVEL_TITANIA,       0,  85100.0f, false },
    /* Area 6 Gorgon: aA6LevelObjects entry 435: zPos1=255100.0, xPos=0. */
    { "GORGON",     LEVEL_AREA_6,        0, 253100.0f, false },
    /* Venom 1 Golemech: aVe1LevelObjects entry 1190: zPos1=174177.2, xPos=0. Phase 0 only. */
    { "GOLEMECH",   LEVEL_VENOM_1,       0, 172000.0f, false },
    /* Andross: aVe1AndLevelObjects entry 66: zPos1=58986.0, xPos=20.
     * LEVEL_VENOM_ANDROSS is the dedicated Andross sub-level. */
    { "ANDROSS",    LEVEL_VENOM_ANDROSS, 0,  56000.0f, false },
    /* gLevelPhase=1 routes Andross face death through state 32, spawning the brain
     * (OBJ_BOSS_AND_BRAIN, gBosses[1]) at frame 180 of the death sequence.
     * gLevelPhase=0 (ANDROSS entry above) routes to James rescue instead. */
    { "A.BRAIN",    LEVEL_VENOM_ANDROSS, 1,  56000.0f, false },
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
