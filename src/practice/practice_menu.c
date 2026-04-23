#include "practice.h"

#ifdef PRACTICE_ROM

typedef enum MenuOption {
    MOPT_RESTART,
    MOPT_SAVE_POS,
    MOPT_RESTORE_POS,
    MOPT_LEVEL_SELECT,
    MOPT_LOADOUT,
    MOPT_OPTIONS,
    MOPT_CLOSE,
    MOPT_MAX,
} MenuOption;

static s32 sMenuCursor = 0;

void Practice_Menu_Open(void) {
    gPracticeMenuState = PMENU_OPEN;
    sMenuCursor = 0;
}

void Practice_Menu_OpenFrozen(void) {
    gPracticeMenuState = PMENU_OPEN_FROZEN;
    sMenuCursor = 0;
}

void Practice_Menu_Close(void) {
    gPracticeMenuState = PMENU_CLOSED;
}

void Practice_Menu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Update();
        return;
    }

    if (press->button & B_BUTTON) {
        Practice_Menu_Close();
        return;
    }

    if (press->button & U_JPAD) {
        sMenuCursor--;
        if (sMenuCursor < 0) {
            sMenuCursor = MOPT_MAX - 1;
        }
    }
    if (press->button & D_JPAD) {
        sMenuCursor++;
        if (sMenuCursor >= MOPT_MAX) {
            sMenuCursor = 0;
        }
    }

    if (press->button & A_BUTTON) {
        switch (sMenuCursor) {
            case MOPT_RESTART:
                Practice_Menu_Close();
                Practice_LaunchLevel(gCurrentLevel, gLevelPhase);
                break;
            case MOPT_SAVE_POS:
                Practice_SaveState();
                break;
            case MOPT_RESTORE_POS:
                Practice_Menu_Close();
                Practice_LoadState();
                break;
            case MOPT_LEVEL_SELECT:
                Practice_Menu_Close();
                gPracticeScreen = PSCREEN_LEVEL_SELECT;
                gGameState = GSTATE_MAP;
                gDrawMode = DRAW_NONE;
                Audio_FadeOutAll(1);
                Audio_ClearVoice();
                break;
            case MOPT_LOADOUT:
                Practice_StateMenu_Open(PSUBMENU_LOADOUT);
                break;
            case MOPT_OPTIONS:
                Practice_StateMenu_Open(PSUBMENU_OPTIONS);
                break;
            case MOPT_CLOSE:
                Practice_Menu_Close();
                break;
        }
    }
}

void Practice_Menu_Draw(void) {
    s32 i;
    s32 y;
    static const char* sMenuLabels[MOPT_MAX] = {
        "RESTART LEVEL",
        "SAVE POSITION",
        "RESTORE POSITION",
        "LEVEL SELECT",
        "LOADOUT",
        "OPTIONS",
        "CLOSE",
    };

    Practice_DrawBox(60, 50, 200, 150, 0, 0, 0, 200);

    Practice_DrawTextColor(70, 54, "PRACTICE MENU", 0, 255, 128);

    Practice_DrawText(70, 68, "HITS:");
    Practice_DrawNumber(115, 68, gHitCount);

    for (i = 0; i < MOPT_MAX; i++) {
        y = 86 + (i * 14);
        if (i == sMenuCursor) {
            Practice_DrawBox(62, y - 1, 190, 12, 255, 255, 255, 60);
            Practice_DrawTextColor(70, y, sMenuLabels[i], 255, 255, 0);
        } else {
            Practice_DrawText(70, y, sMenuLabels[i]);
        }
    }

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Draw();
    }
}

#endif
