/*
 * practice_shim.c -- the ONLY practice symbols engine/sys code may touch.
 *
 * The bulk of the practice feature code lives in .practice_late_pak
 * (Expansion Pak RAM, 0x80730000), which does not exist on a stock 4 MB
 * console and is only resident after Practice_Late_Init's boot DMA. Engine
 * hooks run unconditionally on every console, so anything they reference
 * must be main-resident:
 *
 *   - hook GLOBALS the engine reads/writes directly (score stats,
 *     checkpoint progress, boss-test carrier flag) are defined here;
 *   - hook FUNCTIONS the engine calls get Pak-gated shims here that bounce
 *     to the *_PakImpl definitions inside .practice_late_pak.
 *
 * Adding an engine hook? Put its symbol here (or in another main-resident
 * object), never in a .practice_late_pak object. See
 * docs/superpowers/specs/2026-07-11-scene-window-zbuffer-overlap.md.
 */
#include "practice.h"
#include "practice_late.h"

#ifdef PRACTICE_ROM

/* Score/stats counters incremented by hooks in fox_enmy.c / fox_beam.c /
 * fox_play.c etc. (formerly in practice_hud.c). Explicitly initialized: an
 * uninitialized (common-symbol) struct as this object's only BSS makes the
 * IDO recomp cc emit an object file binutils rejects ("symbol references
 * nonexistent SHT_SYMTAB_SHNDX section"). */
PracticeStats gPracticeStats = { 0, 0, 0, 0, 0, 0 };

/* Set by the boss-test launcher, read by engine hooks that stage the
 * Corneria carrier variant (formerly in practice_boss_test.c). */
bool gPracticeForceCarrier = false;

/* Checkpoint progress mirrored for the engine's warp hook (formerly in
 * practice_level.c). */
f32 gPracticeCheckpointProgress = 0.0f;

/* --- Pak-gated shims for engine-called practice functions ------------- */

bool Practice_StateMenuIsOpen(void) {
    if (!Practice_PakReady()) {
        return false;
    }
    return Practice_StateMenuIsOpen_PakImpl();
}

void Practice_LevelSelect_OnEnter(void) {
    if (!Practice_PakReady()) {
        return;
    }
    Practice_LevelSelect_OnEnter_PakImpl();
}

void Practice_Menu_Close(void) {
    if (!Practice_PakReady()) {
        return;
    }
    Practice_Menu_Close_PakImpl();
}

bool Practice_FreeCam_IsActive(void) {
    if (!Practice_PakReady()) {
        return false;
    }
    return Practice_FreeCam_IsActive_PakImpl();
}

void Practice_FreeCam_GetView(Vec3f* eye, Vec3f* at) {
    if (!Practice_PakReady()) {
        return;
    }
    Practice_FreeCam_GetView_PakImpl(eye, at);
}

void Practice_FreeCam_DrawMarkers(void) {
    if (!Practice_PakReady()) {
        return;
    }
    Practice_FreeCam_DrawMarkers_PakImpl();
}

void Practice_Hitbox_Draw(void) {
    if (!Practice_PakReady()) {
        return;
    }
    Practice_Hitbox_Draw_PakImpl();
}

#endif /* PRACTICE_ROM */
