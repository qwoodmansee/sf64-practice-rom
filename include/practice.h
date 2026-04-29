#ifndef PRACTICE_H
#define PRACTICE_H

#ifdef PRACTICE_ROM

#include "global.h"

typedef enum PracticeMenuState {
    PMENU_CLOSED,
    PMENU_OPEN,
    PMENU_OPEN_FROZEN,
} PracticeMenuState;

typedef enum PracticeScreen {
    PSCREEN_LEVEL_SELECT,
    PSCREEN_GAMEPLAY,
} PracticeScreen;

typedef struct PracticeConfig {
    LaserStrength laserStrength;
    s32 bombCount;
    s32 lifeCount;
    u8 rightWingState;
    u8 leftWingState;
    bool falcoAlive;
    bool slippyAlive;
    bool peppyAlive;
    bool showInputDisplay;
    bool skipCutscenes;
    bool showHudOverlay;
    bool showLagFrames;
    bool showSpeed;
    bool showChargeTiming;
    bool showMissedInputs;
    bool showHitTracking;
    bool showHitboxes;
    bool showHitboxActors;
    bool showHitboxScenery;
    bool showHitboxItems;
    bool showHitboxPlayer;
    bool showHitboxFlash;
    bool showSpawnZones;
    bool showSpawnActors;
    bool showSpawnItems;
    bool showSpawnScenery;
    bool expertMode;
} PracticeConfig;

typedef enum PracticeSubMenu {
    PSUBMENU_LOADOUT,
    PSUBMENU_DISPLAY,
    PSUBMENU_STATS,
    PSUBMENU_VISUALIZERS,
} PracticeSubMenu;

typedef enum PracticeAction {
    PACTION_OPEN_MENU,
    PACTION_SAVE_POS,
    PACTION_RESTORE_POS,
    PACTION_MAX,
} PracticeAction;

extern PracticeScreen gPracticeScreen;
extern PracticeConfig gPracticeConfig;
extern PracticeMenuState gPracticeMenuState;
extern s32 gPracticeDirectHits;
extern s32 gPracticeIndirectCount;
extern s32 gPracticeIndirectBonus;
extern s32 gPracticeDespawns;
extern s32 gPracticeAqTraceStage;
extern s32 gPracticeAqTraceFrame;
extern s32 gPracticeAqTraceState;
extern f32 gPracticeCheckpointProgress;
extern s32 gPracticeSlotTestStatus;
extern s32 gPracticeSlotTestFirstLoadedValue;
extern s32 gPracticeSlotTestSecondLoadedValue;
extern s32 gPracticeSlotTestLoadCalls;
extern s32 gPracticeSlotTestSlotCount;

/* Phase 4 — slot-manager-backed save state diagnostics. Defined in
 * practice_save.c (Wave 2.2 owns the real definitions; Wave 1 ships
 * temporary zero stubs in practice_save.c so the build links). */
extern s32 gPracticeActiveSlot;
extern s32 gPracticeSlotValidBits;
extern s32 gPracticeLastSaveResult;
extern s32 gPracticeLastLoadResult;

/* Phase 3 — RAM checkpoint selection (Practice_Save_Init at boot).
 * These live in .main_bss. The slot pool megabyte sits in Expansion Pak DRAM
 * (practice_save_slotpool.c, vma 0x80400000) and is unused on stock hardware.
 *
 * gPracticeRamSlotCount: 0 = stock (save disabled), 4 with Expansion Pak.
 * gPracticeSaveDisabled: non-zero when save/load cannot run. */
extern s32 gPracticeRamSlotCount;
extern s32 gPracticeSaveDisabled;

/* practice_heap_audit.c — Phase 4 §7 (IS-Viewer heap / memory pressure). */
extern s32 gPracticeMaxMemAllocHWM;
extern s32 gPracticeFreeRamLow;
extern s32 gPracticeOverlaySizes[6];

/* practice_main.c */
void Practice_Init(void);
void Practice_Update(void);
void Practice_Draw(void);
void Practice_ApplyStartConditions(void);

/* practice_slot_test.c -- Phase 3 in-ROM fake-state slot_manager smoke test. */
void Practice_SlotTest_Run(void);

/* practice_test_fatfs.c -- Phase 2 hardware verification probe.
 * Only built/called when IODEV_DIAG_FATFS=1. Writes SF64TEST.TXT to the
 * SD card root and reads it back; prints results via IS-Viewer.
 * Remove this from production builds before shipping. */
#ifdef IODEV_DIAG_FATFS
void Practice_TestFatfs(void);
#endif

/* practice_draw.c */
void Practice_DrawBox(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a);
void Practice_DrawText(s32 x, s32 y, const char* text);
void Practice_DrawTextColor(s32 x, s32 y, const char* text, u8 r, u8 g, u8 b);
void Practice_DrawNumber(s32 x, s32 y, s32 value);
void Practice_DrawFloat(s32 x, s32 y, f32 value, s32 decimals);
void Practice_DrawTextOutline(s32 x, s32 y, const char* text, u8 r, u8 g, u8 b);
void Practice_DrawCursor(s32 x, s32 y);

/* practice_input.c */
bool Practice_InputTriggered(PracticeAction action);
u16 Practice_GetBinding(PracticeAction action);
void Practice_SetBinding(PracticeAction action, u16 button);
const char* Practice_GetActionName(PracticeAction action);
const char* Practice_GetDPadName(u16 button);

/* practice_level.c */
void Practice_LevelSelect_OnEnter(void);
void Practice_LevelSelect_Update(void);
void Practice_LevelSelect_Draw(void);
void Practice_LaunchLevel(LevelId levelId, s32 phase, f32 checkpointProgress);
LevelId Practice_GetSelectedLevelId(void);
s32 Practice_GetSelectedPhase(void);

/* practice_state.c */
bool Practice_StateMenuIsOpen(void);
void Practice_StateMenu_Open(PracticeSubMenu subMenu);
void Practice_StateMenu_Close(void);
void Practice_StateMenu_Update(void);
void Practice_StateMenu_Draw(void);

/* practice_menu.c */
void Practice_Menu_Open(void);
void Practice_Menu_OpenFrozen(void);
void Practice_Menu_Close(void);
void Practice_Menu_Update(void);
void Practice_Menu_Draw(void);

/* practice_hud.c */
void Practice_Hud_Reset(void);
void Practice_Hud_ShowStatus(const char* text, u8 r, u8 g, u8 b);
void Practice_Hud_Update(void);
void Practice_Hud_Draw(void);
void Practice_Hud_DrawAqTrace(void);

/* practice_input_display.c */
void Practice_InputDisplay_Draw(void);

/* practice_hitbox.c */
void Practice_Hitbox_Draw(void);

/* practice_freecam.c */
bool Practice_FreeCam_IsActive(void);
bool Practice_FreeCam_OverlayVisible(void);
void Practice_FreeCam_Enter(void);
void Practice_FreeCam_Exit(void);
void Practice_FreeCam_GetView(Vec3f* eye, Vec3f* at);
void Practice_FreeCam_Update(void);
void Practice_FreeCam_Draw(void);

/* practice_save.c */
void Practice_SaveState(void);
void Practice_LoadState(void);
/* s32 0/1 — avoids Clang CC_CHECK bool vs libc/stdbool quirks on some hosts */
s32 Practice_HasCheckpoint(void);
void Practice_ClearCheckpoint(void);
/* Phase 4 — slot-manager-backed save state. Wave 2.2 implements; Wave 1
 * ships forward decls only with stub bodies in practice_save.c. */
void Practice_Save_Init(void);
/* ISV: logs [save_tr] when PRACTICE_SAVE_TRACE=1 (see Makefile). */
void Practice_SaveTrace_HotkeyIsv(void);
void Practice_SaveTrace_LoadHotkeyIsv(void);
s32 Practice_CanSaveHere(void);
void Practice_SaveStateSlot(s32 slot);
void Practice_LoadStateSlot(s32 slot);
s32  Practice_GetActiveSlot(void);
void Practice_CycleSlot(s32 delta);

/* practice_heap_audit.c */
void Practice_HeapAudit_Boot(void);
void Practice_HeapAudit_PerFrame(void);

uintptr_t Practice_Save_SlotPoolBase(void);

#endif
#endif
