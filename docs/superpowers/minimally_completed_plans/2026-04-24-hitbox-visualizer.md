# Hitbox Visualizer Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 3D wireframe hitbox visualization to the SF64 practice ROM with per-category toggles, player collision point display, and collision flash highlighting.

**Architecture:** New file `practice_hitbox.c` contains all visualization logic. A 3D draw hook in `Display_Update()` calls it while the camera matrix is active. Menu integration adds a dedicated HITBOX sub-menu to the existing state menu system.

**Tech Stack:** C targeting N64 MIPS, N64 SDK GBI macros (`gSPVertex`, `gSPLine3D`), game engine matrix stack (`Matrix_Push/Pop/Translate/RotateY/X/Z/Scale/SetGfxMtx`).

**Spec:** `docs/superpowers/specs/2026-04-24-hitbox-visualizer-design.md`

---

## Chunk 1: Config, Header, and Build Integration

### Task 1: Add config fields and function declaration to practice.h

**Files:**
- Modify: `include/practice.h`

- [ ] **Step 1: Add hitbox config fields to PracticeConfig**

In `include/practice.h`, add these fields after `bool showHitTracking;` (line 35):

```c
    bool showHitboxes;
    bool showHitboxActors;
    bool showHitboxScenery;
    bool showHitboxItems;
    bool showHitboxPlayer;
    bool showHitboxFlash;
```

- [ ] **Step 2: Add PSUBMENU_HITBOX to PracticeSubMenu enum**

In `include/practice.h`, add `PSUBMENU_HITBOX` after `PSUBMENU_OPTIONS` (line 40):

```c
typedef enum PracticeSubMenu {
    PSUBMENU_LOADOUT,
    PSUBMENU_OPTIONS,
    PSUBMENU_HITBOX,
} PracticeSubMenu;
```

- [ ] **Step 3: Add Practice_Hitbox_Draw declaration**

In `include/practice.h`, add after the `/* practice_input_display.c */` section (after line 107):

```c
/* practice_hitbox.c */
void Practice_Hitbox_Draw(void);
```

- [ ] **Step 4: Build to verify header compiles**

Run: `rm -rf build/ && make practice -j4`
Expected: Successful build (the function is declared but not yet defined — the linker won't complain until something calls it)

- [ ] **Step 5: Commit**

```bash
git add include/practice.h
git commit -m "feat: add hitbox visualizer config fields and declaration"
```

---

### Task 2: Init config defaults in practice_main.c

**Files:**
- Modify: `src/practice/practice_main.c`

- [ ] **Step 1: Add default values in Practice_Init()**

In `src/practice/practice_main.c`, add after `gPracticeConfig.showHitTracking = true;` (line 28):

```c
    gPracticeConfig.showHitboxes = false;
    gPracticeConfig.showHitboxActors = false;
    gPracticeConfig.showHitboxScenery = false;
    gPracticeConfig.showHitboxItems = false;
    gPracticeConfig.showHitboxPlayer = false;
    gPracticeConfig.showHitboxFlash = false;
```

- [ ] **Step 2: Build to verify**

Run: `make practice -j4`
Expected: Successful build

- [ ] **Step 3: Commit**

```bash
git add src/practice/practice_main.c
git commit -m "feat: init hitbox config defaults to false"
```

---

### Task 3: Add practice_hitbox to build system

**Files:**
- Modify: `tools/patch_linker_script.py`
- Modify: `linker_scripts/us/rev1/starfox64.ld`

- [ ] **Step 1: Add to PRACTICE_OBJS in patch_linker_script.py**

In `tools/patch_linker_script.py`, add `"practice_hitbox"` to the `PRACTICE_OBJS` list, after `"practice_hud"` (line 20):

```python
PRACTICE_OBJS = [
    "practice_main",
    "practice_draw",
    "practice_input",
    "practice_level",
    "practice_state",
    "practice_menu",
    "practice_save",
    "practice_input_display",
    "practice_hud",
    "practice_hitbox",
]
```

- [ ] **Step 2: Add .o entries to linker script**

In `linker_scripts/us/rev1/starfox64.ld`, add `practice_hitbox.o` entries in all four sections, directly after each `practice_hud.o` entry:

After line 243 (`.text` section, after `practice_hud.o(.text)`):
```
        build/src/practice/practice_hitbox.o(.text);
```

After line 772 (`.data` section, after `practice_hud.o(.data)`):
```
        build/src/practice/practice_hitbox.o(.data);
```

After line 816 (`.rodata` section, after `practice_hud.o(.rodata)`):
```
        build/src/practice/practice_hitbox.o(.rodata);
```

After line 1030 (`.bss` section, after `practice_hud.o(.bss)`):
```
        build/src/practice/practice_hitbox.o(.bss);
```

**Note:** Line numbers are approximate — find the `practice_hud.o` entry in each section and add the new line directly after it.

- [ ] **Step 3: Create stub practice_hitbox.c**

Create `src/practice/practice_hitbox.c` with a minimal stub:

```c
#include "practice.h"

#ifdef PRACTICE_ROM

void Practice_Hitbox_Draw(void) {
}

#endif
```

- [ ] **Step 4: Build to verify linker integration**

Run: `rm -rf build/ && make practice -j4`
Expected: Successful build with the new .o file linked in

- [ ] **Step 5: Commit**

```bash
git add tools/patch_linker_script.py linker_scripts/us/rev1/starfox64.ld src/practice/practice_hitbox.c
git commit -m "feat: add practice_hitbox to build system with stub"
```

---

## Chunk 2: 3D Wireframe Drawing Core

### Task 4: Add render pipeline hook in Display_Update

**Files:**
- Modify: `src/engine/fox_display.c`

- [ ] **Step 1: Add the 3D draw hook**

In `src/engine/fox_display.c`:

First, add the include at the top of the file, after the existing includes:

```c
#ifdef PRACTICE_ROM
#include "practice.h"
#endif
```

Then find `Object_Draw(1);` in `Display_Update()` (line ~1817). Add the practice hitbox draw call after `TexturedLine_Draw();` (line ~1818) and before `gReflectY = 1;` (line ~1819):

```c
    TexturedLine_Draw();
#ifdef PRACTICE_ROM
    Practice_Hitbox_Draw();
#endif
    gReflectY = 1;
```

**Why after TexturedLine_Draw():** This is after all objects are drawn but before player features and effects. The 3D camera LookAt matrix from `Matrix_LookAt` (line ~1771) is still on the `gGfxMatrix` stack. It gets popped at line ~1880.

- [ ] **Step 2: Build to verify hook compiles**

Run: `make practice -j4`
Expected: Successful build (Practice_Hitbox_Draw is the empty stub)

- [ ] **Step 3: Commit**

```bash
git add src/engine/fox_display.c
git commit -m "feat: add 3D hitbox draw hook in Display_Update"
```

---

### Task 5: Implement wireframe box drawing

**Files:**
- Modify: `src/practice/practice_hitbox.c`

- [ ] **Step 1: Write the unit cube vertex data and wireframe draw function**

Replace the contents of `src/practice/practice_hitbox.c` with:

```c
#include "practice.h"

#ifdef PRACTICE_ROM

#define HITBOX_DRAW_RANGE_SQ 25000000.0f

static Vtx sUnitCubeVtx[8] = {
    { { { -1, -1, -1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {  1, -1, -1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {  1,  1, -1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -1,  1, -1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -1, -1,  1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {  1, -1,  1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {  1,  1,  1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -1,  1,  1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
};

static void Hitbox_SetupRCP(void) {
    gDPPipeSync(gMasterDisp++);
    gDPSetCycleType(gMasterDisp++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMasterDisp++, G_RM_AA_ZB_XLU_LINE, G_RM_AA_ZB_XLU_LINE2);
    gDPSetCombineMode(gMasterDisp++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gSPSetGeometryMode(gMasterDisp++, G_ZBUFFER);
    gSPClearGeometryMode(gMasterDisp++, G_LIGHTING | G_CULL_BOTH | G_SHADING_SMOOTH);
}

static void Hitbox_DrawWireframeBox(f32 posX, f32 posY, f32 posZ,
                                     f32 rotX, f32 rotY, f32 rotZ,
                                     f32 hitRotX, f32 hitRotY, f32 hitRotZ,
                                     bool hasHitRot,
                                     f32 offZ, f32 sizeZ, f32 offY, f32 sizeY, f32 offX, f32 sizeX,
                                     u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gMasterDisp++);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, a);

    Matrix_Push(&gGfxMatrix);

    Matrix_Translate(gGfxMatrix, posX, posY, posZ, MTXF_APPLY);
    Matrix_RotateY(gGfxMatrix, rotY * M_DTOR, MTXF_APPLY);
    Matrix_RotateX(gGfxMatrix, rotX * M_DTOR, MTXF_APPLY);
    Matrix_RotateZ(gGfxMatrix, rotZ * M_DTOR, MTXF_APPLY);

    if (hasHitRot) {
        Matrix_RotateY(gGfxMatrix, hitRotY * M_DTOR, MTXF_APPLY);
        Matrix_RotateX(gGfxMatrix, hitRotX * M_DTOR, MTXF_APPLY);
        Matrix_RotateZ(gGfxMatrix, hitRotZ * M_DTOR, MTXF_APPLY);
    }

    Matrix_Translate(gGfxMatrix, offX, offY, offZ, MTXF_APPLY);
    Matrix_Scale(gGfxMatrix, sizeX, sizeY, sizeZ, MTXF_APPLY);

    Matrix_SetGfxMtx(&gMasterDisp);

    gSPVertex(gMasterDisp++, sUnitCubeVtx, 8, 0);

    /* Bottom face edges */
    gSPLine3D(gMasterDisp++, 0, 1, 0);
    gSPLine3D(gMasterDisp++, 1, 2, 0);
    gSPLine3D(gMasterDisp++, 2, 3, 0);
    gSPLine3D(gMasterDisp++, 3, 0, 0);

    /* Top face edges */
    gSPLine3D(gMasterDisp++, 4, 5, 0);
    gSPLine3D(gMasterDisp++, 5, 6, 0);
    gSPLine3D(gMasterDisp++, 6, 7, 0);
    gSPLine3D(gMasterDisp++, 7, 4, 0);

    /* Vertical edges */
    gSPLine3D(gMasterDisp++, 0, 4, 0);
    gSPLine3D(gMasterDisp++, 1, 5, 0);
    gSPLine3D(gMasterDisp++, 2, 6, 0);
    gSPLine3D(gMasterDisp++, 3, 7, 0);

    Matrix_Pop(&gGfxMatrix);
}

void Practice_Hitbox_Draw(void) {
}

#endif
```

**Key design notes:**
- `sUnitCubeVtx` uses `short` coordinates (-1/+1). The matrix scale handles the real hitbox size.
- `G_RM_AA_ZB_XLU_LINE` enables Z-buffer read (lines behind objects are hidden) with anti-aliased XLU lines. Z writes are off by default for XLU modes, so boxes won't occlude game geometry.
- `G_LIGHTING` is cleared so vertex colors come through as-is.
- Rotation order: Y, X, Z with positive angles (forward transform from object-local to world space).

- [ ] **Step 2: Build to verify**

Run: `make practice -j4`
Expected: Successful build

- [ ] **Step 3: Commit**

```bash
git add src/practice/practice_hitbox.c
git commit -m "feat: add wireframe box drawing core with unit cube vertices"
```

---

### Task 6: Implement hitbox data parsing and object iteration

**Files:**
- Modify: `src/practice/practice_hitbox.c`

- [ ] **Step 1: Add collision flash check helper**

Add this function before `Practice_Hitbox_Draw()`, after `Hitbox_DrawWireframeBox`:

```c
static bool Hitbox_CheckPlayerCollision(f32 objX, f32 objY, f32 objZ, Hitbox* hitbox) {
    Player* player = &gPlayer[0];
    Vec3f* pts[2] = { &player->hit3, &player->hit4 };
    s32 i;

    for (i = 0; i < 2; i++) {
        if ((fabsf(hitbox->z.offset + objZ - pts[i]->z) < (hitbox->z.size + 20.0f)) &&
            (fabsf(hitbox->x.offset + objX - pts[i]->x) < (hitbox->x.size + 20.0f)) &&
            (fabsf(hitbox->y.offset + objY - pts[i]->y) < (hitbox->y.size + 10.0f))) {
            return true;
        }
    }
    return false;
}
```

**Note:** This uses the same padding as `Object_CheckHitboxCollision` (fox_enmy.c:776-778): +20 on X/Z, +10 on Y. The collision flash check is only accurate for non-rotated hitboxes — for rotated hitboxes, the test point would need to be transformed into the hitbox's local space. This is a reasonable approximation that covers the vast majority of hitboxes.

- [ ] **Step 2: Add object hitbox iteration helper**

Add after the collision check helper:

```c
static void Hitbox_DrawObjectHitboxes(Object* obj, f32* hitboxData, u8 r, u8 g, u8 b, u8 a) {
    s32 count;
    s32 i;
    f32 hitRotX, hitRotY, hitRotZ;
    bool hasHitRot;
    Hitbox* hitbox;
    u8 drawR, drawG, drawB, drawA;

    if (hitboxData == NULL) {
        return;
    }

    count = (s32) *hitboxData;
    if (count == 0) {
        return;
    }

    hitboxData++;

    for (i = 0; i < count; i++, hitboxData += 6) {
        hasHitRot = false;
        hitRotX = hitRotY = hitRotZ = 0.0f;

        if (*hitboxData == HITBOX_ROTATED) {
            hitRotX = hitboxData[1];
            hitRotY = hitboxData[2];
            hitRotZ = hitboxData[3];
            hitboxData += 4;
            hasHitRot = true;
        } else if (*hitboxData >= HITBOX_SHADOW) {
            hitboxData++;
            continue;
        }

        hitbox = (Hitbox*) hitboxData;

        drawR = r;
        drawG = g;
        drawB = b;
        drawA = a;

        if (gPracticeConfig.showHitboxFlash) {
            if (Hitbox_CheckPlayerCollision(obj->pos.x, obj->pos.y, obj->pos.z, hitbox)) {
                drawR = 255;
                drawG = 255;
                drawB = 0;
                drawA = 220;
            }
        }

        Hitbox_DrawWireframeBox(
            obj->pos.x, obj->pos.y, obj->pos.z,
            obj->rot.x, obj->rot.y, obj->rot.z,
            hitRotX, hitRotY, hitRotZ, hasHitRot,
            hitbox->z.offset, hitbox->z.size,
            hitbox->y.offset, hitbox->y.size,
            hitbox->x.offset, hitbox->x.size,
            drawR, drawG, drawB, drawA
        );
    }
}
```

**Parsing matches `Player_CheckHitboxCollision`:** HITBOX_ROTATED advances by 4 (marker + 3 rotation floats). HITBOX_SHADOW advances by 1 (marker only) and continues — the loop's `hitboxData += 6` at the end of the iteration skips the 6 hitbox floats that follow.

- [ ] **Step 3: Implement Practice_Hitbox_Draw with object iteration**

Replace the empty `Practice_Hitbox_Draw()` with:

```c
void Practice_Hitbox_Draw(void) {
    s32 i;
    f32 dx, dy, dz, distSq;
    Player* player;

    if (!gPracticeConfig.showHitboxes) {
        return;
    }
    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }

    player = &gPlayer[0];

    Hitbox_SetupRCP();

    if (gPracticeConfig.showHitboxActors) {
        for (i = 0; i < 60; i++) {
            if (gActors[i].obj.status == OBJ_FREE) {
                continue;
            }
            dx = gActors[i].obj.pos.x - player->pos.x;
            dy = gActors[i].obj.pos.y - player->pos.y;
            dz = gActors[i].obj.pos.z - player->trueZpos;
            distSq = (dx * dx) + (dy * dy) + (dz * dz);
            if (distSq > HITBOX_DRAW_RANGE_SQ) {
                continue;
            }
            Hitbox_DrawObjectHitboxes(&gActors[i].obj, gActors[i].info.hitbox,
                                      255, 50, 50, 180);
        }

        for (i = 0; i < 4; i++) {
            if (gBosses[i].obj.status == OBJ_FREE) {
                continue;
            }
            Hitbox_DrawObjectHitboxes(&gBosses[i].obj, gBosses[i].info.hitbox,
                                      255, 50, 50, 180);
        }
    }

    if (gPracticeConfig.showHitboxScenery) {
        for (i = 0; i < 50; i++) {
            if (gScenery[i].obj.status == OBJ_FREE) {
                continue;
            }
            dx = gScenery[i].obj.pos.x - player->pos.x;
            dy = gScenery[i].obj.pos.y - player->pos.y;
            dz = gScenery[i].obj.pos.z - player->trueZpos;
            distSq = (dx * dx) + (dy * dy) + (dz * dz);
            if (distSq > HITBOX_DRAW_RANGE_SQ) {
                continue;
            }
            Hitbox_DrawObjectHitboxes(&gScenery[i].obj, gScenery[i].info.hitbox,
                                      50, 100, 255, 180);
        }
    }

    if (gPracticeConfig.showHitboxItems) {
        for (i = 0; i < 20; i++) {
            if (gItems[i].obj.status == OBJ_FREE) {
                continue;
            }
            dx = gItems[i].obj.pos.x - player->pos.x;
            dy = gItems[i].obj.pos.y - player->pos.y;
            dz = gItems[i].obj.pos.z - player->trueZpos;
            distSq = (dx * dx) + (dy * dy) + (dz * dz);
            if (distSq > HITBOX_DRAW_RANGE_SQ) {
                continue;
            }
            Hitbox_DrawObjectHitboxes(&gItems[i].obj, gItems[i].info.hitbox,
                                      50, 255, 50, 180);
        }
    }

    if (gPracticeConfig.showHitboxPlayer) {
        Hitbox_DrawWireframeBox(
            player->hit1.x, player->hit1.y, player->hit1.z,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false,
            0.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f,
            255, 255, 255, 200);
        Hitbox_DrawWireframeBox(
            player->hit2.x, player->hit2.y, player->hit2.z,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false,
            0.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f,
            255, 255, 255, 200);
        Hitbox_DrawWireframeBox(
            player->hit3.x, player->hit3.y, player->hit3.z,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false,
            0.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f,
            255, 255, 255, 200);
        Hitbox_DrawWireframeBox(
            player->hit4.x, player->hit4.y, player->hit4.z,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false,
            0.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f,
            255, 255, 255, 200);
    }
}
```

**Key details:**
- Bosses skip distance culling — there are only 4 and they're always gameplay-relevant.
- Player Z comparison uses `player->trueZpos` (the actual Z in world space), not `player->pos.z` which is camera-relative in on-rails mode.
- Player hit points are drawn as 10-unit cubes with no rotation.

- [ ] **Step 4: Build to verify**

Run: `make practice -j4`
Expected: Successful build

- [ ] **Step 5: Commit**

```bash
git add src/practice/practice_hitbox.c
git commit -m "feat: implement hitbox iteration, parsing, culling, and collision flash"
```

---

## Chunk 3: Menu Integration

### Task 7: Add HITBOX sub-menu to practice_state.c

**Files:**
- Modify: `src/practice/practice_state.c`

- [ ] **Step 1: Add HitboxOption enum**

In `src/practice/practice_state.c`, add after the `OptionsOption` enum (after line 29):

```c
typedef enum HitboxOption {
    HOPT_MASTER,
    HOPT_ACTORS,
    HOPT_SCENERY,
    HOPT_ITEMS,
    HOPT_PLAYER,
    HOPT_FLASH,
    HOPT_BACK,
    HOPT_MAX,
} HitboxOption;
```

- [ ] **Step 2: Add HITBOX VIEWER entry to OptionsOption enum**

Add `OOPT_HITBOX_MENU` before `OOPT_BACK` in the `OptionsOption` enum:

```c
typedef enum OptionsOption {
    OOPT_FALCO,
    OOPT_SLIPPY,
    OOPT_PEPPY,
    OOPT_SKIP_CUTSCENES,
    OOPT_INPUT_DISPLAY,
    OOPT_HUD_OVERLAY,
    OOPT_LAG_FRAMES,
    OOPT_SPEED,
    OOPT_CHARGE_TIMING,
    OOPT_MISSED_INPUTS,
    OOPT_HIT_TRACKING,
    OOPT_HITBOX_MENU,
    OOPT_BACK,
    OOPT_MAX,
} OptionsOption;
```

- [ ] **Step 3: Update StateMenu_GetOptionCount for HITBOX sub-menu**

Replace `StateMenu_GetOptionCount` with:

```c
static s32 StateMenu_GetOptionCount(void) {
    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT: return LOPT_MAX;
        case PSUBMENU_OPTIONS: return OOPT_MAX;
        case PSUBMENU_HITBOX:  return HOPT_MAX;
        default:               return 0;
    }
}
```

- [ ] **Step 4: Add StateMenu_UpdateHitbox function**

Add after `StateMenu_UpdateOptions`:

```c
static void StateMenu_UpdateHitbox(u16 buttons) {
    if ((buttons & R_JPAD) || (buttons & A_BUTTON) || (buttons & L_JPAD)) {
        switch (sSelectedOption) {
            case HOPT_MASTER:
                gPracticeConfig.showHitboxes ^= true;
                break;
            case HOPT_ACTORS:
                gPracticeConfig.showHitboxActors ^= true;
                break;
            case HOPT_SCENERY:
                gPracticeConfig.showHitboxScenery ^= true;
                break;
            case HOPT_ITEMS:
                gPracticeConfig.showHitboxItems ^= true;
                break;
            case HOPT_PLAYER:
                gPracticeConfig.showHitboxPlayer ^= true;
                break;
            case HOPT_FLASH:
                gPracticeConfig.showHitboxFlash ^= true;
                break;
        }
    }
}
```

- [ ] **Step 5: Handle OOPT_HITBOX_MENU in StateMenu_UpdateOptions**

In `StateMenu_UpdateOptions`, the existing toggle logic uses `(buttons & R_JPAD) || (buttons & A_BUTTON) || (buttons & L_JPAD)`. The HITBOX MENU entry should open the sub-menu instead of toggling. This is handled separately — do NOT add it to the switch inside the button check. Instead, add nothing inside the existing switch and handle it in the `Practice_StateMenu_Update` function's A_BUTTON handler.

- [ ] **Step 6: Update Practice_StateMenu_Update for HITBOX sub-menu navigation**

Replace the `Practice_StateMenu_Update` function with:

```c
void Practice_StateMenu_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    s32 optCount = StateMenu_GetOptionCount();

    if (press->button & B_BUTTON) {
        if (sActiveSubMenu == PSUBMENU_HITBOX) {
            sActiveSubMenu = PSUBMENU_OPTIONS;
            sSelectedOption = OOPT_HITBOX_MENU;
            return;
        }
        Practice_StateMenu_Close();
        return;
    }

    if (press->button & U_JPAD) {
        sSelectedOption--;
        if (sSelectedOption < 0) {
            sSelectedOption = optCount - 1;
        }
    }
    if (press->button & D_JPAD) {
        sSelectedOption++;
        if (sSelectedOption >= optCount) {
            sSelectedOption = 0;
        }
    }

    if (press->button & A_BUTTON) {
        if (sActiveSubMenu == PSUBMENU_LOADOUT && sSelectedOption == LOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
        if (sActiveSubMenu == PSUBMENU_OPTIONS && sSelectedOption == OOPT_BACK) {
            Practice_StateMenu_Close();
            return;
        }
        if (sActiveSubMenu == PSUBMENU_OPTIONS && sSelectedOption == OOPT_HITBOX_MENU) {
            sActiveSubMenu = PSUBMENU_HITBOX;
            sSelectedOption = 0;
            return;
        }
        if (sActiveSubMenu == PSUBMENU_HITBOX && sSelectedOption == HOPT_BACK) {
            sActiveSubMenu = PSUBMENU_OPTIONS;
            sSelectedOption = OOPT_HITBOX_MENU;
            return;
        }
    }

    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:
            StateMenu_UpdateLoadout(press->button);
            break;
        case PSUBMENU_OPTIONS:
            StateMenu_UpdateOptions(press->button);
            break;
        case PSUBMENU_HITBOX:
            StateMenu_UpdateHitbox(press->button);
            break;
    }
}
```

**Key behavior:** B button in the HITBOX sub-menu goes back to OPTIONS (not closing the state menu entirely). Cursor returns to the HITBOX MENU entry.

- [ ] **Step 7: Add StateMenu_DrawHitbox function**

Add after `StateMenu_DrawOptions`:

```c
static void StateMenu_DrawHitbox(void) {
    s32 y;
    s32 i;

    for (i = 0; i < HOPT_MAX; i++) {
        y = 60 + (i * 14);

        if (i == sSelectedOption) {
            Practice_DrawBox(42, y - 1, 230, 12, 255, 255, 255, 60);
        }

        switch (i) {
            case HOPT_MASTER:
                Practice_DrawText(54, y, "HITBOXES:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxes ? "ON" : "OFF",
                    gPracticeConfig.showHitboxes ? 0 : 255, gPracticeConfig.showHitboxes ? 255 : 100, 0);
                break;
            case HOPT_ACTORS:
                Practice_DrawText(54, y, "  ACTORS:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxActors ? "ON" : "OFF",
                    gPracticeConfig.showHitboxActors ? 0 : 255, gPracticeConfig.showHitboxActors ? 255 : 100, 0);
                break;
            case HOPT_SCENERY:
                Practice_DrawText(54, y, "  SCENERY:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxScenery ? "ON" : "OFF",
                    gPracticeConfig.showHitboxScenery ? 0 : 255, gPracticeConfig.showHitboxScenery ? 255 : 100, 0);
                break;
            case HOPT_ITEMS:
                Practice_DrawText(54, y, "  ITEMS:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxItems ? "ON" : "OFF",
                    gPracticeConfig.showHitboxItems ? 0 : 255, gPracticeConfig.showHitboxItems ? 255 : 100, 0);
                break;
            case HOPT_PLAYER:
                Practice_DrawText(54, y, "  PLAYER:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxPlayer ? "ON" : "OFF",
                    gPracticeConfig.showHitboxPlayer ? 0 : 255, gPracticeConfig.showHitboxPlayer ? 255 : 100, 0);
                break;
            case HOPT_FLASH:
                Practice_DrawText(54, y, "  FLASH:");
                Practice_DrawTextColor(150, y, gPracticeConfig.showHitboxFlash ? "ON" : "OFF",
                    gPracticeConfig.showHitboxFlash ? 0 : 255, gPracticeConfig.showHitboxFlash ? 255 : 100, 0);
                break;
            case HOPT_BACK:
                Practice_DrawTextColor(54, y, "BACK", 150, 150, 150);
                break;
        }
    }
}
```

- [ ] **Step 8: Add HITBOX MENU draw entry in StateMenu_DrawOptions**

In `StateMenu_DrawOptions`, add after the `OOPT_HIT_TRACKING` case (before `OOPT_BACK`):

```c
            case OOPT_HITBOX_MENU:
                Practice_DrawTextColor(54, y, "HITBOX VIEWER...", 200, 200, 255);
                break;
```

- [ ] **Step 9: Update Practice_StateMenu_Draw for HITBOX sub-menu**

Replace `Practice_StateMenu_Draw` with:

```c
void Practice_StateMenu_Draw(void) {
    const char* title;
    s32 boxHeight;
    s32 helpY;

    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:
            title = "LOADOUT";
            boxHeight = 115;
            helpY = 148;
            break;
        case PSUBMENU_OPTIONS:
            title = "OPTIONS";
            boxHeight = 213;
            helpY = 246;
            break;
        case PSUBMENU_HITBOX:
            title = "HITBOX VIEWER";
            boxHeight = 143;
            helpY = 176;
            break;
        default:
            return;
    }

    Practice_DrawBox(40, 40, 240, boxHeight, 0, 0, 60, 200);
    Practice_DrawTextColor(50, 44, title, 0, 255, 128);

    switch (sActiveSubMenu) {
        case PSUBMENU_LOADOUT:
            StateMenu_DrawLoadout();
            Practice_DrawTextColor(50, helpY, "D-PAD:CHANGE  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_OPTIONS:
            StateMenu_DrawOptions();
            Practice_DrawTextColor(50, helpY, "A:TOGGLE  B:BACK", 150, 150, 150);
            break;
        case PSUBMENU_HITBOX:
            StateMenu_DrawHitbox();
            Practice_DrawTextColor(50, helpY, "A:TOGGLE  B:BACK", 150, 150, 150);
            break;
    }
}
```

**Box height calculations:**
- OPTIONS: was 199, now 213 (added 1 entry × 14px = 14 more)
- OPTIONS helpY: was 232, now 246 (same +14)
- HITBOX: 7 entries × 14px + 3px padding = 143
- HITBOX helpY: 40 (box top) + 143 - 7 (padding) = 176

- [ ] **Step 10: Build and verify**

Run: `make practice -j4`
Expected: Successful build

- [ ] **Step 11: Commit**

```bash
git add src/practice/practice_state.c
git commit -m "feat: add HITBOX VIEWER sub-menu with all toggles"
```

---

## Chunk 4: Manual Testing

### Task 8: Test in emulator

- [ ] **Step 1: Full rebuild**

Run: `rm -rf build/ && make practice -j4`
Expected: Successful build producing practice ROM

- [ ] **Step 2: Launch in emulator and test menu**

Launch the ROM in mupen64plus. Navigate to Options menu, verify "HITBOX VIEWER..." entry appears. Enter it, verify all 6 toggles display correctly. Verify B goes back to Options, not closing the menu.

- [ ] **Step 3: Test hitbox rendering**

Enable master toggle and ACTORS toggle. Start Corneria. Verify red wireframe boxes appear on enemies. Verify boxes disappear when enemies go out of range. Verify boxes follow enemy movement and rotation.

- [ ] **Step 4: Test player points**

Enable PLAYER toggle. Verify 4 small white cubes track with the arwing. Move the ship around and verify they follow correctly.

- [ ] **Step 5: Test collision flash**

Enable FLASH toggle. Fly into an enemy hitbox. Verify the box turns yellow momentarily.

- [ ] **Step 6: Test scenery and items**

Enable SCENERY and ITEMS toggles. Verify blue boxes on buildings/obstacles and green boxes on ring/bomb pickups.

- [ ] **Step 7: Test performance**

Play through a busy section of Corneria with all hitbox toggles enabled. Watch for frame drops or visual glitches. If display list overflow occurs (crash or garbled graphics), reduce `HITBOX_DRAW_RANGE_SQ` to 10000000.0f and retest.

- [ ] **Step 8: Commit any fixes from testing**

If any issues found during testing, fix and commit:
```bash
git add -A
git commit -m "fix: address hitbox visualizer issues found during testing"
```
