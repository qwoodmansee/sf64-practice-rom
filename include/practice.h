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
} PracticeConfig;

typedef enum PracticeSubMenu {
    PSUBMENU_LOADOUT,
    PSUBMENU_OPTIONS,
} PracticeSubMenu;

typedef enum PracticeAction {
    PACTION_OPEN_MENU_FROZEN,
    PACTION_OPEN_MENU,
    PACTION_SAVE_POS,
    PACTION_RESTORE_POS,
    PACTION_MAX,
} PracticeAction;

extern PracticeScreen gPracticeScreen;
extern PracticeConfig gPracticeConfig;
extern PracticeMenuState gPracticeMenuState;

/* practice_main.c */
void Practice_Init(void);
void Practice_Update(void);
void Practice_Draw(void);
void Practice_ApplyStartConditions(void);

/* practice_draw.c */
void Practice_DrawBox(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a);
void Practice_DrawText(s32 x, s32 y, const char* text);
void Practice_DrawTextColor(s32 x, s32 y, const char* text, u8 r, u8 g, u8 b);
void Practice_DrawNumber(s32 x, s32 y, s32 value);
void Practice_DrawCursor(s32 x, s32 y);

/* practice_input.c */
bool Practice_InputTriggered(PracticeAction action);
u16 Practice_GetBinding(PracticeAction action);
void Practice_SetBinding(PracticeAction action, u16 button);
const char* Practice_GetActionName(PracticeAction action);
const char* Practice_GetDPadName(u16 button);

/* practice_level.c */
void Practice_LevelSelect_Update(void);
void Practice_LevelSelect_Draw(void);
void Practice_LaunchLevel(LevelId levelId, s32 phase);
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

/* practice_input_display.c */
void Practice_InputDisplay_Draw(void);

/* practice_save.c */
void Practice_SaveState(void);
void Practice_LoadState(void);
bool Practice_HasCheckpoint(void);
void Practice_ClearCheckpoint(void);

#endif
#endif
