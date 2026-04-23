# SF64 Practice ROM Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a practice ROM for the HIT+64 Star Fox 64 scoring community with level select, starting conditions, quick restart, in-game menu, and save/restore.

**Architecture:** Separate build target (`make practice`) compiles `src/practice/` files behind `#if PRACTICE_ROM`. Four hook points in the existing game code (`fox_game.c`, `fox_play.c`) wire in the practice system. All practice state lives in `src/practice/`, game state is accessed via existing typed globals.

**Tech Stack:** C (C90/IDO), N64 MIPS, GNU Make, Star Fox 64 decomp

---

## File Map

| File | Role | Action |
|------|------|--------|
| `Makefile` | Build system | Modify: add `PRACTICE_ROM` flag, `practice` target |
| `include/practice.h` | Public interface, structs, config types | Create |
| `src/practice/practice_main.c` | Init, per-frame update/draw dispatch | Create |
| `src/practice/practice_draw.c` | Menu box, text, cursor drawing helpers | Create |
| `src/practice/practice_input.c` | L+DPad combo detection, customizable bindings | Create |
| `src/practice/practice_level.c` | Level select screen, route/phase selection | Create |
| `src/practice/practice_state.c` | Starting conditions config (lasers, bombs, etc.) | Create |
| `src/practice/practice_menu.c` | In-game overlay menu (pause + options) | Create |
| `src/practice/practice_save.c` | In-memory save/restore checkpoint | Create |
| `src/engine/fox_game.c` | Game state machine, draw dispatch | Modify: 4 hook points |
| `src/engine/fox_play.c` | Player setup / level init | Modify: 1 hook point |

---

### Task 1: Build System — `PRACTICE_ROM` flag and `make practice` target

**Files:**
- Modify: `Makefile`

The build system already discovers all `.c` files under `src/` via `find src -type d`, so `src/practice/` files will be compiled automatically. We just need the define and a convenience target.

- [ ] **Step 1: Add PRACTICE_ROM build define**

In `Makefile`, after the `NON_MATCHING` block (line ~149), add the practice ROM configuration:

```c
// After line 149 (after the NON_MATCHING block):
ifeq ($(PRACTICE_ROM),1)
    BUILD_DEFINES   += -DPRACTICE_ROM=1 -DAVOID_UB
    COMPARE := 0
endif
```

`COMPARE := 0` disables MD5 matching since the practice ROM will differ from the original.
`AVOID_UB` is included because we don't need to match and it prevents undefined behavior.

- [ ] **Step 2: Add `practice` convenience target**

In `Makefile`, after the existing `mod:` target (near the end of the file, around line 470), add:

```makefile
practice:
	$(MAKE) PRACTICE_ROM=1
```

- [ ] **Step 3: Verify the build still works**

Run: `make clean && make`
Expected: Matching ROM builds successfully (practice code not yet created, no effect on default build).

Run: `make clean && make practice`
Expected: Build succeeds (no practice source files exist yet, so no errors either). The `PRACTICE_ROM` define is set but nothing references it.

- [ ] **Step 4: Commit**

```bash
git add Makefile
git commit -m "build: add PRACTICE_ROM flag and make practice target"
```

---

### Task 2: Header and Main Skeleton — `practice.h` + `practice_main.c`

**Files:**
- Create: `include/practice.h`
- Create: `src/practice/practice_main.c`

- [ ] **Step 1: Create `include/practice.h`**

```c
#ifndef PRACTICE_H
#define PRACTICE_H

#ifdef PRACTICE_ROM

#include "global.h"

typedef enum PracticeMenuState {
    PMENU_CLOSED,
    PMENU_OPEN,
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
} PracticeConfig;

typedef enum PracticeAction {
    PACTION_QUICK_RESTART,
    PACTION_OPEN_MENU,
    PACTION_SAVE_POS,
    PACTION_RESTORE_POS,
    PACTION_MAX,
} PracticeAction;

void Practice_Init(void);
void Practice_Update(void);
void Practice_Draw(void);
void Practice_ApplyStartConditions(void);

extern PracticeScreen gPracticeScreen;
extern PracticeConfig gPracticeConfig;
extern PracticeMenuState gPracticeMenuState;

#endif
#endif
```

- [ ] **Step 2: Create `src/practice/practice_main.c`**

```c
#include "practice.h"

#ifdef PRACTICE_ROM

PracticeScreen gPracticeScreen;
PracticeConfig gPracticeConfig;
PracticeMenuState gPracticeMenuState;

void Practice_Init(void) {
    gPracticeScreen = PSCREEN_LEVEL_SELECT;
    gPracticeMenuState = PMENU_CLOSED;

    gPracticeConfig.laserStrength = LASERS_SINGLE;
    gPracticeConfig.bombCount = 3;
    gPracticeConfig.lifeCount = 2;
    gPracticeConfig.rightWingState = WINGSTATE_INTACT;
    gPracticeConfig.leftWingState = WINGSTATE_INTACT;
    gPracticeConfig.falcoAlive = true;
    gPracticeConfig.slippyAlive = true;
    gPracticeConfig.peppyAlive = true;
}

void Practice_Update(void) {
}

void Practice_Draw(void) {
}

void Practice_ApplyStartConditions(void) {
}

#endif
```

- [ ] **Step 3: Verify build**

Run: `make clean && make practice`
Expected: Build succeeds. The practice files compile but are not called from anywhere yet.

Run: `make clean && make`
Expected: Matching build still succeeds (all practice code is behind `#ifdef PRACTICE_ROM`).

- [ ] **Step 4: Commit**

```bash
git add include/practice.h src/practice/practice_main.c
git commit -m "feat: add practice.h header and practice_main.c skeleton"
```

---

### Task 3: Hook Points — Wire practice system into the game loop

**Files:**
- Modify: `src/engine/fox_game.c` (lines ~5, ~58, ~365, ~611)

This task adds the minimal hooks that connect the practice system to the game. Four changes:

- [ ] **Step 1: Add practice.h include**

In `src/engine/fox_game.c`, after line 5 (`#include "mods.h"`), add:

```c
#ifdef PRACTICE_ROM
#include "practice.h"
#endif
```

- [ ] **Step 2: Add boot redirect in `Game_Initialize`**

In `src/engine/fox_game.c`, inside `Game_Initialize()`, the `MODS_BOOT_STATE` block (lines 58-69) shows the pattern. After that block (after line 69), add:

```c
#ifdef PRACTICE_ROM
    gNextGameState = GSTATE_INIT;
    if (Save_Read() != 0) {
#ifdef AVOID_UB
        gSaveFile.save = gDefaultSave;
        gSaveFile.backup = gDefaultSave;
#else
        gSaveFile = *((SaveFile*) &gDefaultSave);
#endif
        Save_Write();
    }
#endif
```

This skips the boot logo sequence and jumps straight to `GSTATE_INIT`.

- [ ] **Step 3: Add practice init and state redirect in `GSTATE_INIT`**

In `Game_Update()`, inside the `case GSTATE_INIT:` block (line ~417), the `MODS_BOOT_STATE` redirect is at lines 432-434. After that block, add:

```c
#ifdef PRACTICE_ROM
                Practice_Init();
                gNextGameState = GSTATE_MAP;
#endif
```

This initializes the practice system and sends the game to the map state (which we'll intercept for the level select).

- [ ] **Step 4: Add per-frame update/draw hooks**

In `Game_Update()`, at lines 612-620 (after `Audio_dummy_80016A50()`, alongside the existing mod hooks), add:

```c
#ifdef PRACTICE_ROM
        Practice_Update();
        Practice_Draw();
#endif
```

This runs every frame after the game has drawn, giving the practice overlay the final say on what appears on screen.

- [ ] **Step 5: Verify build**

Run: `make clean && make practice`
Expected: Build succeeds. The practice ROM boots and skips the Nintendo logo, goes through GSTATE_INIT, then to GSTATE_MAP (showing the normal map screen for now).

Run: `make clean && make`
Expected: Matching build still succeeds (all hooks are behind `#ifdef PRACTICE_ROM`).

- [ ] **Step 6: Commit**

```bash
git add src/engine/fox_game.c
git commit -m "feat: wire practice system hooks into game loop"
```

---

### Task 4: Drawing Helpers — `practice_draw.c`

**Files:**
- Create: `src/practice/practice_draw.c`
- Modify: `include/practice.h` (add declarations)

The game already has `Graphics_DisplaySmallText()` and display list primitives. This file wraps them into convenient helpers for building menus.

- [ ] **Step 1: Create `src/practice/practice_draw.c`**

```c
#include "practice.h"

#ifdef PRACTICE_ROM

void Practice_DrawBox(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    RCP_SetupDL(&gMasterDisp, SETUPDL_76);
    gDPSetRenderMode(gMasterDisp++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gMasterDisp++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, a);
    gDPFillRectangle(gMasterDisp++, x, y, x + w, y + h);
    gDPPipeSync(gMasterDisp++);
}

void Practice_DrawText(s32 x, s32 y, const char* text) {
    RCP_SetupDL(&gMasterDisp, SETUPDL_83);
    gDPSetPrimColor(gMasterDisp++, 0, 0, 255, 255, 255, 255);
    Graphics_DisplaySmallText(x, y, 1.0f, 1.0f, text);
}

void Practice_DrawTextColor(s32 x, s32 y, const char* text, u8 r, u8 g, u8 b) {
    RCP_SetupDL(&gMasterDisp, SETUPDL_83);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, 255);
    Graphics_DisplaySmallText(x, y, 1.0f, 1.0f, text);
}

void Practice_DrawNumber(s32 x, s32 y, s32 value) {
    char buf[12];
    s32 i = 0;
    s32 neg = 0;
    s32 v;

    if (value < 0) {
        neg = 1;
        value = -value;
    }
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
    } else {
        v = value;
        while (v > 0) {
            i++;
            v /= 10;
        }
        buf[i] = '\0';
        v = value;
        while (v > 0) {
            buf[--i] = '0' + (v % 10);
            v /= 10;
        }
    }
    if (neg) {
        Practice_DrawTextColor(x - 8, y, "-", 255, 255, 255);
    }
    Practice_DrawText(x, y, buf);
}

void Practice_DrawCursor(s32 x, s32 y) {
    Practice_DrawTextColor(x, y, ">", 255, 255, 0);
}

#endif
```

- [ ] **Step 2: Add declarations to `include/practice.h`**

Inside the `#ifdef PRACTICE_ROM` block, before the closing `#endif`, add:

```c
void Practice_DrawBox(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a);
void Practice_DrawText(s32 x, s32 y, const char* text);
void Practice_DrawTextColor(s32 x, s32 y, const char* text, u8 r, u8 g, u8 b);
void Practice_DrawNumber(s32 x, s32 y, s32 value);
void Practice_DrawCursor(s32 x, s32 y);
```

- [ ] **Step 3: Verify build**

Run: `make clean && make practice`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/practice/practice_draw.c include/practice.h
git commit -m "feat: add practice drawing helpers (box, text, cursor)"
```

---

### Task 5: Input System — `practice_input.c`

**Files:**
- Create: `src/practice/practice_input.c`
- Modify: `include/practice.h` (add declarations)

Handles L+DPad combo detection with customizable action bindings.

- [ ] **Step 1: Create `src/practice/practice_input.c`**

```c
#include "practice.h"

#ifdef PRACTICE_ROM

static u16 sPracticeBindings[PACTION_MAX] = {
    U_JPAD,  /* PACTION_QUICK_RESTART: L + D-Pad Up */
    D_JPAD,  /* PACTION_OPEN_MENU:     L + D-Pad Down */
    L_JPAD,  /* PACTION_SAVE_POS:      L + D-Pad Left */
    R_JPAD,  /* PACTION_RESTORE_POS:   L + D-Pad Right */
};

static const char* sPracticeActionNames[PACTION_MAX] = {
    "RESTART",
    "MENU",
    "SAVE POS",
    "LOAD POS",
};

bool Practice_InputTriggered(PracticeAction action) {
    OSContPad* press = &gControllerPress[gMainController];
    OSContPad* hold = &gControllerHold[gMainController];

    if (!(hold->button & L_TRIG)) {
        return false;
    }
    return (press->button & sPracticeBindings[action]) != 0;
}

u16 Practice_GetBinding(PracticeAction action) {
    return sPracticeBindings[action];
}

void Practice_SetBinding(PracticeAction action, u16 button) {
    sPracticeBindings[action] = button;
}

const char* Practice_GetActionName(PracticeAction action) {
    return sPracticeActionNames[action];
}

const char* Practice_GetDPadName(u16 button) {
    switch (button) {
        case U_JPAD: return "D-UP";
        case D_JPAD: return "D-DOWN";
        case L_JPAD: return "D-LEFT";
        case R_JPAD: return "D-RIGHT";
        default:     return "???";
    }
}

#endif
```

- [ ] **Step 2: Add declarations to `include/practice.h`**

```c
bool Practice_InputTriggered(PracticeAction action);
u16 Practice_GetBinding(PracticeAction action);
void Practice_SetBinding(PracticeAction action, u16 button);
const char* Practice_GetActionName(PracticeAction action);
const char* Practice_GetDPadName(u16 button);
```

- [ ] **Step 3: Verify build**

Run: `make clean && make practice`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/practice/practice_input.c include/practice.h
git commit -m "feat: add practice input system with customizable L+DPad bindings"
```

---

### Task 6: Level Select Screen — `practice_level.c`

**Files:**
- Create: `src/practice/practice_level.c`
- Modify: `include/practice.h` (add declarations)
- Modify: `src/practice/practice_main.c` (wire up update/draw)

This is the first visible feature — the custom boot screen showing all 15 levels.

- [ ] **Step 1: Create `src/practice/practice_level.c`**

```c
#include "practice.h"
#include "fox_map.h"

#ifdef PRACTICE_ROM

typedef struct LevelEntry {
    const char* name;
    LevelId levelId;
    PlanetId planetId;
    s32 column;
    bool hasWarpPhase;
} LevelEntry;

static LevelEntry sLevelList[] = {
    { "CORNERIA",  LEVEL_CORNERIA,  PLANET_CORNERIA,  1, false },
    { "METEO",     LEVEL_METEO,     PLANET_METEO,     2, true },
    { "SECTOR Y",  LEVEL_SECTOR_Y,  PLANET_SECTOR_Y,  2, false },
    { "FORTUNA",   LEVEL_FORTUNA,   PLANET_FORTUNA,   3, false },
    { "KATINA",    LEVEL_KATINA,    PLANET_KATINA,    3, false },
    { "AQUAS",     LEVEL_AQUAS,     PLANET_AQUAS,     3, false },
    { "SECTOR X",  LEVEL_SECTOR_X,  PLANET_SECTOR_X,  4, true },
    { "SOLAR",     LEVEL_SOLAR,     PLANET_SOLAR,     4, false },
    { "ZONESS",    LEVEL_ZONESS,    PLANET_ZONESS,    4, false },
    { "TITANIA",   LEVEL_TITANIA,   PLANET_TITANIA,   5, false },
    { "MACBETH",   LEVEL_MACBETH,   PLANET_MACBETH,   5, false },
    { "SECTOR Z",  LEVEL_SECTOR_Z,  PLANET_SECTOR_Z,  5, false },
    { "BOLSE",     LEVEL_BOLSE,     PLANET_BOLSE,     6, false },
    { "AREA 6",    LEVEL_AREA_6,    PLANET_AREA_6,    6, false },
    { "VENOM 1",   LEVEL_VENOM_1,   PLANET_VENOM,     7, false },
    { "VENOM 2",   LEVEL_VENOM_2,   PLANET_VENOM,     7, false },
};

#define LEVEL_COUNT (s32)(sizeof(sLevelList) / sizeof(sLevelList[0]))

static s32 sSelectedLevel = 0;
static s32 sSelectedPhase = 0;

void Practice_LevelSelect_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];

    if (press->button & U_JPAD) {
        sSelectedLevel--;
        if (sSelectedLevel < 0) {
            sSelectedLevel = LEVEL_COUNT - 1;
        }
        sSelectedPhase = 0;
    }
    if (press->button & D_JPAD) {
        sSelectedLevel++;
        if (sSelectedLevel >= LEVEL_COUNT) {
            sSelectedLevel = 0;
        }
        sSelectedPhase = 0;
    }

    if (sLevelList[sSelectedLevel].hasWarpPhase) {
        if ((press->button & L_JPAD) || (press->button & R_JPAD)) {
            sSelectedPhase ^= 1;
        }
    }

    if (press->button & A_BUTTON) {
        Practice_LaunchLevel(sLevelList[sSelectedLevel].levelId, sSelectedPhase);
    }
}

void Practice_LevelSelect_Draw(void) {
    s32 i;
    s32 y;
    s32 startIdx;
    s32 visibleCount = 12;

    Practice_DrawBox(16, 16, 288, 208, 0, 0, 0, 180);

    Practice_DrawTextColor(20, 20, "SF64 PRACTICE ROM", 0, 255, 128);
    Practice_DrawTextColor(20, 30, "SELECT LEVEL", 200, 200, 200);

    startIdx = sSelectedLevel - (visibleCount / 2);
    if (startIdx < 0) {
        startIdx = 0;
    }
    if (startIdx > LEVEL_COUNT - visibleCount) {
        startIdx = LEVEL_COUNT - visibleCount;
    }
    if (startIdx < 0) {
        startIdx = 0;
    }

    for (i = startIdx; (i < LEVEL_COUNT) && (i < startIdx + visibleCount); i++) {
        y = 46 + ((i - startIdx) * 12);

        if (i == sSelectedLevel) {
            Practice_DrawCursor(20, y);
            Practice_DrawTextColor(30, y, sLevelList[i].name, 255, 255, 0);
        } else {
            Practice_DrawText(30, y, sLevelList[i].name);
        }
    }

    if (sLevelList[sSelectedLevel].hasWarpPhase) {
        Practice_DrawText(30, 196, "PHASE:");
        if (sSelectedPhase == 0) {
            Practice_DrawTextColor(75, 196, "NORMAL", 255, 255, 255);
        } else {
            Practice_DrawTextColor(75, 196, "WARP ZONE", 0, 200, 255);
        }
    }

    Practice_DrawTextColor(20, 210, "A:START  START:OPTIONS", 150, 150, 150);
}

void Practice_LaunchLevel(LevelId levelId, s32 phase) {
    gCurrentLevel = levelId;
    gLevelPhase = phase;
    gClearPlayerInfo = true;

    gGameState = GSTATE_PLAY;
    gPlayState = PLAY_STANDBY;
    gDrawMode = DRAW_NONE;
    gNextGameStateTimer = 0;
    Play_Setup();

    gPracticeScreen = PSCREEN_GAMEPLAY;
}

LevelId Practice_GetSelectedLevelId(void) {
    return sLevelList[sSelectedLevel].levelId;
}

s32 Practice_GetSelectedPhase(void) {
    return sSelectedPhase;
}

#endif
```

- [ ] **Step 2: Add declarations to `include/practice.h`**

```c
void Practice_LevelSelect_Update(void);
void Practice_LevelSelect_Draw(void);
void Practice_LaunchLevel(LevelId levelId, s32 phase);
LevelId Practice_GetSelectedLevelId(void);
s32 Practice_GetSelectedPhase(void);
```

- [ ] **Step 3: Wire level select into `practice_main.c`**

Update `Practice_Update` and `Practice_Draw` in `src/practice/practice_main.c`:

```c
void Practice_Update(void) {
    switch (gPracticeScreen) {
        case PSCREEN_LEVEL_SELECT:
            Practice_LevelSelect_Update();
            break;
        case PSCREEN_GAMEPLAY:
            break;
    }
}

void Practice_Draw(void) {
    switch (gPracticeScreen) {
        case PSCREEN_LEVEL_SELECT:
            Practice_LevelSelect_Draw();
            break;
        case PSCREEN_GAMEPLAY:
            break;
    }
}
```

- [ ] **Step 4: Verify build and test in mupen**

Run: `make clean && make practice`
Expected: Build succeeds.

Test in mupen: Boot the practice ROM. You should see the level select screen instead of the Nintendo logo. D-Pad up/down scrolls through levels. A button launches a level.

Note: The level launch may not work perfectly yet since we haven't set up all the level loading prerequisites (audio spec, DMA). We may need to adjust `Practice_LaunchLevel` based on what `Map_PlayLevel` does. The key validation is that the level select screen renders and is navigable.

- [ ] **Step 5: Commit**

```bash
git add src/practice/practice_level.c src/practice/practice_main.c include/practice.h
git commit -m "feat: add practice level select screen with all 15 levels"
```

---

### Task 7: Starting Conditions — `practice_state.c`

**Files:**
- Create: `src/practice/practice_state.c`
- Modify: `include/practice.h` (add declarations)

A sub-screen accessible from the level select (Start button) to configure laser/bombs/lives/wings/allies.

- [ ] **Step 1: Create `src/practice/practice_state.c`**

```c
#include "practice.h"

#ifdef PRACTICE_ROM

typedef enum StateOption {
    SOPT_LASERS,
    SOPT_BOMBS,
    SOPT_LIVES,
    SOPT_RIGHT_WING,
    SOPT_LEFT_WING,
    SOPT_FALCO,
    SOPT_SLIPPY,
    SOPT_PEPPY,
    SOPT_BACK,
    SOPT_MAX,
} StateOption;

static s32 sSelectedOption = 0;
static bool sStateMenuOpen = false;

static const char* sLaserNames[] = { "SINGLE", "TWIN", "HYPER" };
static const char* sWingNames[] = { "NONE", "BROKEN", "INTACT" };

bool Practice_StateMenuIsOpen(void) {
    return sStateMenuOpen;
}

void Practice_StateMenu_Open(void) {
    sStateMenuOpen = true;
    sSelectedOption = 0;
}

void Practice_StateMenu_Close(void) {
    sStateMenuOpen = false;
}

void Practice_StateMenu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];

    if (press->button & B_BUTTON) {
        Practice_StateMenu_Close();
        return;
    }

    if (press->button & U_JPAD) {
        sSelectedOption--;
        if (sSelectedOption < 0) {
            sSelectedOption = SOPT_MAX - 1;
        }
    }
    if (press->button & D_JPAD) {
        sSelectedOption++;
        if (sSelectedOption >= SOPT_MAX) {
            sSelectedOption = 0;
        }
    }

    if (press->button & A_BUTTON) {
        if (sSelectedOption == SOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
    }

    if ((press->button & R_JPAD) || (press->button & A_BUTTON)) {
        switch (sSelectedOption) {
            case SOPT_LASERS:
                gPracticeConfig.laserStrength++;
                if (gPracticeConfig.laserStrength > LASERS_HYPER) {
                    gPracticeConfig.laserStrength = LASERS_SINGLE;
                }
                break;
            case SOPT_BOMBS:
                gPracticeConfig.bombCount++;
                if (gPracticeConfig.bombCount > 9) {
                    gPracticeConfig.bombCount = 0;
                }
                break;
            case SOPT_LIVES:
                gPracticeConfig.lifeCount++;
                if (gPracticeConfig.lifeCount > 99) {
                    gPracticeConfig.lifeCount = 1;
                }
                break;
            case SOPT_RIGHT_WING:
                gPracticeConfig.rightWingState++;
                if (gPracticeConfig.rightWingState > WINGSTATE_INTACT) {
                    gPracticeConfig.rightWingState = WINGSTATE_NONE;
                }
                break;
            case SOPT_LEFT_WING:
                gPracticeConfig.leftWingState++;
                if (gPracticeConfig.leftWingState > WINGSTATE_INTACT) {
                    gPracticeConfig.leftWingState = WINGSTATE_NONE;
                }
                break;
            case SOPT_FALCO:
                gPracticeConfig.falcoAlive ^= true;
                break;
            case SOPT_SLIPPY:
                gPracticeConfig.slippyAlive ^= true;
                break;
            case SOPT_PEPPY:
                gPracticeConfig.peppyAlive ^= true;
                break;
        }
    }

    if (press->button & L_JPAD) {
        switch (sSelectedOption) {
            case SOPT_LASERS:
                if (gPracticeConfig.laserStrength == LASERS_SINGLE) {
                    gPracticeConfig.laserStrength = LASERS_HYPER;
                } else {
                    gPracticeConfig.laserStrength--;
                }
                break;
            case SOPT_BOMBS:
                gPracticeConfig.bombCount--;
                if (gPracticeConfig.bombCount < 0) {
                    gPracticeConfig.bombCount = 9;
                }
                break;
            case SOPT_LIVES:
                gPracticeConfig.lifeCount--;
                if (gPracticeConfig.lifeCount < 1) {
                    gPracticeConfig.lifeCount = 99;
                }
                break;
            case SOPT_RIGHT_WING:
                if (gPracticeConfig.rightWingState == WINGSTATE_NONE) {
                    gPracticeConfig.rightWingState = WINGSTATE_INTACT;
                } else {
                    gPracticeConfig.rightWingState--;
                }
                break;
            case SOPT_LEFT_WING:
                if (gPracticeConfig.leftWingState == WINGSTATE_NONE) {
                    gPracticeConfig.leftWingState = WINGSTATE_INTACT;
                } else {
                    gPracticeConfig.leftWingState--;
                }
                break;
            case SOPT_FALCO:
                gPracticeConfig.falcoAlive ^= true;
                break;
            case SOPT_SLIPPY:
                gPracticeConfig.slippyAlive ^= true;
                break;
            case SOPT_PEPPY:
                gPracticeConfig.peppyAlive ^= true;
                break;
        }
    }
}

void Practice_StateMenu_Draw(void) {
    s32 y;
    s32 i;

    Practice_DrawBox(40, 40, 240, 160, 0, 0, 60, 200);
    Practice_DrawTextColor(50, 44, "STARTING CONDITIONS", 0, 255, 128);

    for (i = 0; i < SOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawCursor(44, y);
        }

        switch (i) {
            case SOPT_LASERS:
                Practice_DrawText(54, y, "LASER:");
                Practice_DrawTextColor(120, y, sLaserNames[gPracticeConfig.laserStrength], 255, 255, 0);
                break;
            case SOPT_BOMBS:
                Practice_DrawText(54, y, "BOMBS:");
                Practice_DrawNumber(120, y, gPracticeConfig.bombCount);
                break;
            case SOPT_LIVES:
                Practice_DrawText(54, y, "LIVES:");
                Practice_DrawNumber(120, y, gPracticeConfig.lifeCount);
                break;
            case SOPT_RIGHT_WING:
                Practice_DrawText(54, y, "R WING:");
                Practice_DrawTextColor(120, y, sWingNames[gPracticeConfig.rightWingState], 255, 255, 0);
                break;
            case SOPT_LEFT_WING:
                Practice_DrawText(54, y, "L WING:");
                Practice_DrawTextColor(120, y, sWingNames[gPracticeConfig.leftWingState], 255, 255, 0);
                break;
            case SOPT_FALCO:
                Practice_DrawText(54, y, "FALCO:");
                Practice_DrawTextColor(120, y, gPracticeConfig.falcoAlive ? "ALIVE" : "DOWN", 
                    gPracticeConfig.falcoAlive ? 0 : 255, gPracticeConfig.falcoAlive ? 255 : 100, 0);
                break;
            case SOPT_SLIPPY:
                Practice_DrawText(54, y, "SLIPPY:");
                Practice_DrawTextColor(120, y, gPracticeConfig.slippyAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.slippyAlive ? 0 : 255, gPracticeConfig.slippyAlive ? 255 : 100, 0);
                break;
            case SOPT_PEPPY:
                Practice_DrawText(54, y, "PEPPY:");
                Practice_DrawTextColor(120, y, gPracticeConfig.peppyAlive ? "ALIVE" : "DOWN",
                    gPracticeConfig.peppyAlive ? 0 : 255, gPracticeConfig.peppyAlive ? 255 : 100, 0);
                break;
            case SOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }

    Practice_DrawTextColor(50, 190, "D-PAD:CHANGE  B:BACK", 150, 150, 150);
}

#endif
```

- [ ] **Step 2: Add declarations to `include/practice.h`**

```c
bool Practice_StateMenuIsOpen(void);
void Practice_StateMenu_Open(void);
void Practice_StateMenu_Close(void);
void Practice_StateMenu_Update(void);
void Practice_StateMenu_Draw(void);
```

- [ ] **Step 3: Wire state menu into level select**

In `src/practice/practice_level.c`, update `Practice_LevelSelect_Update` to handle the Start button opening the state menu. At the beginning of the function, add:

```c
    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Update();
        return;
    }
```

And before the A button check, add:

```c
    if (press->button & START_BUTTON) {
        Practice_StateMenu_Open();
        return;
    }
```

In `Practice_LevelSelect_Draw`, at the end (before the closing brace), add:

```c
    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Draw();
    }
```

- [ ] **Step 4: Verify build and test**

Run: `make clean && make practice`
Expected: Build succeeds.

Test in mupen: On the level select screen, press Start to open the starting conditions sub-menu. D-Pad up/down selects options, left/right changes values. B closes the sub-menu.

- [ ] **Step 5: Commit**

```bash
git add src/practice/practice_state.c src/practice/practice_level.c include/practice.h
git commit -m "feat: add starting conditions menu (lasers, bombs, lives, wings, allies)"
```

---

### Task 8: Apply Starting Conditions — Hook into `fox_play.c`

**Files:**
- Modify: `src/engine/fox_play.c` (in `Player_Setup`, after the `gClearPlayerInfo` block)
- Modify: `src/practice/practice_main.c` (implement `Practice_ApplyStartConditions`)

- [ ] **Step 1: Implement `Practice_ApplyStartConditions` in `practice_main.c`**

Replace the empty stub:

```c
void Practice_ApplyStartConditions(void) {
    gLaserStrength[gPlayerNum] = gPracticeConfig.laserStrength;
    gBombCount[gPlayerNum] = gPracticeConfig.bombCount;
    gLifeCount[gPlayerNum] = gPracticeConfig.lifeCount;

    gPlayer[0].arwing.rightWingState = gPracticeConfig.rightWingState;
    gPlayer[0].arwing.leftWingState = gPracticeConfig.leftWingState;

    if (!gPracticeConfig.falcoAlive) {
        gTeamShields[TEAM_ID_FALCO] = 0;
    }
    if (!gPracticeConfig.slippyAlive) {
        gTeamShields[TEAM_ID_SLIPPY] = 0;
    }
    if (!gPracticeConfig.peppyAlive) {
        gTeamShields[TEAM_ID_PEPPY] = 0;
    }
}
```

- [ ] **Step 2: Add hook in `fox_play.c`**

In `src/engine/fox_play.c`, add `#include "practice.h"` near the top includes.

Then, after the `gClearPlayerInfo` block ends (after line ~4770, after `gClearPlayerInfo = false;`), add:

```c
#ifdef PRACTICE_ROM
    Practice_ApplyStartConditions();
#endif
```

This runs after the game sets its defaults but before gameplay begins, overwriting with the player's chosen values.

- [ ] **Step 3: Verify build and test**

Run: `make clean && make practice`
Expected: Build succeeds.

Test in mupen: Set lasers to HYPER in the starting conditions menu, then launch Corneria. You should start with hyper lasers.

- [ ] **Step 4: Commit**

```bash
git add src/practice/practice_main.c src/engine/fox_play.c
git commit -m "feat: apply practice starting conditions on level load"
```

---

### Task 9: In-Game Practice Menu — `practice_menu.c`

**Files:**
- Create: `src/practice/practice_menu.c`
- Modify: `include/practice.h` (add declarations)
- Modify: `src/practice/practice_main.c` (wire up)

The overlay menu that opens with L + D-Pad Down during gameplay.

- [ ] **Step 1: Create `src/practice/practice_menu.c`**

```c
#include "practice.h"

#ifdef PRACTICE_ROM

typedef enum MenuOption {
    MOPT_RESTART,
    MOPT_SAVE_POS,
    MOPT_RESTORE_POS,
    MOPT_LEVEL_SELECT,
    MOPT_SETTINGS,
    MOPT_CLOSE,
    MOPT_MAX,
} MenuOption;

static s32 sMenuCursor = 0;
static bool sShowSettings = false;

void Practice_Menu_Open(void) {
    gPracticeMenuState = PMENU_OPEN;
    sMenuCursor = 0;
    sShowSettings = false;
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
                break;
            case MOPT_SETTINGS:
                Practice_StateMenu_Open();
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
        "SETTINGS",
        "CLOSE",
    };

    Practice_DrawBox(60, 50, 200, 140, 0, 0, 0, 200);

    Practice_DrawTextColor(70, 54, "PRACTICE MENU", 0, 255, 128);

    Practice_DrawText(70, 68, "HITS:");
    Practice_DrawNumber(115, 68, gHitCount);

    for (i = 0; i < MOPT_MAX; i++) {
        y = 86 + (i * 14);
        if (i == sMenuCursor) {
            Practice_DrawCursor(64, y);
        }
        Practice_DrawText(74, y, sMenuLabels[i]);
    }

    if (Practice_StateMenuIsOpen()) {
        Practice_StateMenu_Draw();
    }
}

#endif
```

- [ ] **Step 2: Add declarations to `include/practice.h`**

```c
void Practice_Menu_Open(void);
void Practice_Menu_Close(void);
void Practice_Menu_Update(void);
void Practice_Menu_Draw(void);
void Practice_SaveState(void);
void Practice_LoadState(void);
```

- [ ] **Step 3: Wire menu into `practice_main.c`**

Update the `PSCREEN_GAMEPLAY` cases in `Practice_Update` and `Practice_Draw`:

```c
void Practice_Update(void) {
    switch (gPracticeScreen) {
        case PSCREEN_LEVEL_SELECT:
            Practice_LevelSelect_Update();
            break;
        case PSCREEN_GAMEPLAY:
            if (gPracticeMenuState == PMENU_OPEN) {
                Practice_Menu_Update();
            } else {
                if (Practice_InputTriggered(PACTION_OPEN_MENU)) {
                    Practice_Menu_Open();
                } else if (Practice_InputTriggered(PACTION_QUICK_RESTART)) {
                    Practice_LaunchLevel(gCurrentLevel, gLevelPhase);
                } else if (Practice_InputTriggered(PACTION_SAVE_POS)) {
                    Practice_SaveState();
                } else if (Practice_InputTriggered(PACTION_RESTORE_POS)) {
                    Practice_LoadState();
                }
            }
            break;
    }
}

void Practice_Draw(void) {
    switch (gPracticeScreen) {
        case PSCREEN_LEVEL_SELECT:
            Practice_LevelSelect_Draw();
            break;
        case PSCREEN_GAMEPLAY:
            if (gPracticeMenuState == PMENU_OPEN) {
                Practice_Menu_Draw();
            }
            break;
    }
}
```

- [ ] **Step 4: Verify build and test**

Run: `make clean && make practice`
Expected: Build succeeds.

Test in mupen: Launch a level from level select. During gameplay, press L + D-Pad Down. The practice menu should appear. Navigate with D-Pad, press A on "RESTART LEVEL" to restart, "LEVEL SELECT" to return to the level select screen.

- [ ] **Step 5: Commit**

```bash
git add src/practice/practice_menu.c src/practice/practice_main.c include/practice.h
git commit -m "feat: add in-game practice menu with L+D-Pad Down toggle"
```

---

### Task 10: Save/Restore Position — `practice_save.c`

**Files:**
- Create: `src/practice/practice_save.c`

In-memory checkpoint: saves player state and key globals, restores them on demand.

- [ ] **Step 1: Create `src/practice/practice_save.c`**

```c
#include "practice.h"

#ifdef PRACTICE_ROM

typedef struct PracticeCheckpoint {
    bool valid;
    Player playerData;
    f32 pathProgress;
    f32 savedPathProgress;
    s32 hitCount;
    s32 displayedHitCount;
    LaserStrength laserStrength;
    s32 bombCount;
    s16 lifeCount;
    s32 levelPhase;
    Vec3f camEye;
    Vec3f camAt;
} PracticeCheckpoint;

static PracticeCheckpoint sCheckpoint;

void Practice_SaveState(void) {
    sCheckpoint.playerData = gPlayer[0];
    sCheckpoint.pathProgress = gPathProgress;
    sCheckpoint.savedPathProgress = gSavedPathProgress;
    sCheckpoint.hitCount = gHitCount;
    sCheckpoint.displayedHitCount = gDisplayedHitCount;
    sCheckpoint.laserStrength = gLaserStrength[0];
    sCheckpoint.bombCount = gBombCount[0];
    sCheckpoint.lifeCount = gLifeCount[0];
    sCheckpoint.levelPhase = gLevelPhase;
    sCheckpoint.camEye = gCsCamEyeL;
    sCheckpoint.camAt = gCsCamAtL;
    sCheckpoint.valid = true;
}

void Practice_LoadState(void) {
    if (!sCheckpoint.valid) {
        return;
    }

    gPlayer[0] = sCheckpoint.playerData;
    gPathProgress = sCheckpoint.pathProgress;
    gSavedPathProgress = sCheckpoint.savedPathProgress;
    gHitCount = sCheckpoint.hitCount;
    gDisplayedHitCount = sCheckpoint.displayedHitCount;
    gLaserStrength[0] = sCheckpoint.laserStrength;
    gBombCount[0] = sCheckpoint.bombCount;
    gLifeCount[0] = sCheckpoint.lifeCount;
    gLevelPhase = sCheckpoint.levelPhase;
    gCsCamEyeL = sCheckpoint.camEye;
    gCsCamAtL = sCheckpoint.camAt;

    gPlayer[0].state = PLAYERSTATE_ACTIVE;
}

bool Practice_HasCheckpoint(void) {
    return sCheckpoint.valid;
}

void Practice_ClearCheckpoint(void) {
    sCheckpoint.valid = false;
}

#endif
```

- [ ] **Step 2: Add declarations to `include/practice.h`**

```c
bool Practice_HasCheckpoint(void);
void Practice_ClearCheckpoint(void);
```

(`Practice_SaveState` and `Practice_LoadState` were already declared in Task 9.)

- [ ] **Step 3: Clear checkpoint on level change**

In `src/practice/practice_level.c`, in `Practice_LaunchLevel`, add before `gPracticeScreen = PSCREEN_GAMEPLAY;`:

```c
    Practice_ClearCheckpoint();
```

- [ ] **Step 4: Verify build and test**

Run: `make clean && make practice`
Expected: Build succeeds.

Test in mupen: Launch Corneria. Fly for a few seconds, then press L + D-Pad Left to save. Fly further, take some hits. Press L + D-Pad Right to restore. Your position and hit count should snap back to where you saved. The save/restore should also work from the in-game practice menu.

- [ ] **Step 5: Commit**

```bash
git add src/practice/practice_save.c src/practice/practice_level.c include/practice.h
git commit -m "feat: add in-memory save/restore position checkpoint"
```

---

### Task 11: Polish and Integration Testing

**Files:**
- Possibly adjust: any of the above based on testing

- [ ] **Step 1: Full integration test in mupen**

Test the complete flow:
1. Boot → practice level select screen appears (no logos)
2. Navigate levels with D-Pad, see all 16 entries
3. Press Start → starting conditions sub-menu opens
4. Change lasers to HYPER, bombs to 9
5. Press B → back to level select
6. Select Corneria, press A → level launches with hyper lasers and 9 bombs
7. During gameplay, L + D-Pad Down → practice menu opens
8. Navigate menu, select "RESTART LEVEL" → instant restart with same conditions
9. Fly for a bit, L + D-Pad Left → save position
10. Fly further, L + D-Pad Right → restore to saved position
11. Open menu, select "LEVEL SELECT" → back to level select
12. Select a different level, launch it

Document any issues found and fix them.

- [ ] **Step 2: Fix any level launch issues**

The level launch in `Practice_LaunchLevel` may need adjustments based on testing. Common issues:
- Audio spec not set up (may need to call `Map_LevelStart_AudioSpecSetup`)
- DMA table not loaded for the level's overlay
- Scene setup missing

Compare against `Map_PlayLevel` in `fox_map.c` (lines 4284-4296) and the level start sequence above it to ensure all prerequisites are met.

- [ ] **Step 3: Fix any rendering issues**

The practice draw calls happen after `Audio_dummy_80016A50()` in the Game_Update function. If the display list setup isn't right (e.g. Z-buffer interfering with 2D text), the draw helpers may need adjustments to their RCP setup.

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "fix: integration testing fixes for practice ROM"
```

---

## Verification Checklist

After all tasks:
- [ ] `make` produces a matching decomp ROM (practice code has no effect)
- [ ] `make practice` produces a bootable practice ROM
- [ ] Level select screen appears on boot with all 16 levels
- [ ] Starting conditions are configurable and applied on level launch
- [ ] Quick restart works (L + D-Pad Up during gameplay)
- [ ] In-game menu opens/closes (L + D-Pad Down)
- [ ] Save/restore position works (L + D-Pad Left/Right)
- [ ] Returning to level select from the in-game menu works
