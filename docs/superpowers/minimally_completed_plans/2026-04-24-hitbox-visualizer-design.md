# Hitbox Visualizer Design

## Purpose

General-purpose 3D hitbox visualization for the SF64 practice ROM. Supports route optimization (seeing gaps between scenery), combat analysis (enemy vulnerable zones), and collision debugging (understanding unexpected damage).

## Rendering Approach

3D wireframe boxes drawn in world space with proper perspective. Per-object iteration with distance-based culling. No batching — each box is an independent matrix push/draw/pop cycle.

### Wireframe Drawing

Each hitbox is drawn as 12 edges of an axis-aligned bounding box:

1. Push `gGfxMatrix`
2. Translate to object world position
3. Apply object rotation with positive angles (Y, X, Z order — the forward transform, since the engine's collision code uses negative angles for the inverse)
4. If `HITBOX_ROTATED`: apply hitbox-local rotation (also positive angles, same Y, X, Z order)
5. Translate by hitbox offsets (z.offset, y.offset, x.offset)
6. Scale by hitbox sizes (z.size, y.size, x.size)
7. `Matrix_SetGfxMtx` to push matrix to RSP
8. `gSPVertex` — load 8 unit-cube vertices (static `Vtx[8]` at corners +/-1)
9. 12x `gSPLine3D` for box edges
10. Pop `gGfxMatrix`

RCP setup: XLU (translucent) render mode, Z-buffer read enabled but write disabled (boxes don't occlude game geometry). Primitive color set per category.

### Display List Budget

~18-20 GBI commands per box (matrix load, vertex load, 12 lines, color setup, pipe sync overhead). With distance culling limiting to ~15-20 visible boxes per frame, total cost is ~300-400 commands.

### Culling

Squared distance from player position to object position. Threshold: 5000^2 = 25,000,000 units squared. Skip objects beyond this range. No `sqrtf` needed.

### Hitbox Data Parsing

Mirrors `Player_CheckHitboxCollision` in `fox_play.c` (the more complete parsing path):

- Read count from `hitboxData[0]`
- Advance pointer by 1
- For each entry (loop advances by 6 at end of each iteration):
  - If value == `HITBOX_ROTATED` (200000.0f): read 3 rotation floats, advance by 4. Parse and draw the following 6-float hitbox.
  - Else if value >= `HITBOX_SHADOW` (300000.0f): advance by 1 (skip marker float only). The 6-float hitbox data follows but is NOT drawn (shadow/whoosh hitboxes excluded per design decision). Continue to next iteration.
  - Else: no special marker. Cast next 6 floats as `Hitbox*` (z.offset, z.size, y.offset, y.size, x.offset, x.size). Draw wireframe box.

This correctly handles the data layout where SHADOW/WHOOSH markers are a single extra float before the normal 6-float hitbox data, matching how `Player_CheckHitboxCollision` parses them.

## Object Types

| Category | Arrays | Count | Color |
|----------|--------|-------|-------|
| Actors | `gActors[60]`, `gBosses[4]` | 64 max | Red (255, 50, 50, 180) |
| Scenery | `gScenery[50]` | 50 max | Blue (50, 100, 255, 180) |
| Items | `gItems[20]` | 20 max | Green (50, 255, 50, 180) |
| Player | `gPlayer[0].hit1`-`hit4` | 4 | White (255, 255, 255, 200) |

Skip objects with `status == OBJ_FREE`. Skip objects with `info.hitbox == NULL`.

## Player Collision Points

Four small wireframe cubes (size ~10 units) drawn at `gPlayer[0].hit1`, `hit2`, `hit3`, `hit4`. These are the points the engine tests against object AABBs. Uses the same `Practice_Hitbox_DrawWireframe` function with a fixed small size.

## Collision Flash

When enabled, each hitbox is tested against `gPlayer[0].hit3` and `gPlayer[0].hit4` (the two primary collision test points). The check matches the engine's collision padding: +20 units on X and Z sizes, +10 units on Y size (matching `Object_CheckHitboxCollision` fox_enmy.c:776-778). If either point falls within the padded AABB, the box color overrides to yellow (255, 255, 0, 220). The wireframe itself is drawn at the actual hitbox size (without padding) so the flash indicates "the engine considers this a collision" even when the point appears slightly outside the visible box.

## Menu Integration

New sub-menu `PSUBMENU_HITBOX` accessible from the options menu:

| Entry | Type | Default |
|-------|------|---------|
| HITBOX VIEWER | Master on/off | OFF |
| SHOW ACTORS | Toggle (requires master) | OFF |
| SHOW SCENERY | Toggle (requires master) | OFF |
| SHOW ITEMS | Toggle (requires master) | OFF |
| SHOW PLAYER | Toggle (requires master) | OFF |
| COLLISION FLASH | Toggle (requires master) | OFF |

When master is OFF, no hitboxes are drawn regardless of sub-toggles.

## Config Fields

Added to `PracticeConfig` in `include/practice.h`:

```c
bool showHitboxes;
bool showHitboxActors;
bool showHitboxScenery;
bool showHitboxItems;
bool showHitboxPlayer;
bool showHitboxFlash;
```

All default to `false` in `Practice_Init()`.

## Render Pipeline Hook

`Practice_Hitbox_Draw()` must execute while the 3D perspective + camera LookAt matrix is on `gGfxMatrix`. The existing `Practice_Draw()` call in `fox_game.c` is too late (after `Game_Draw()` has popped the 3D matrices).

New hook location: `Display_Update()` in `fox_display.c`, after `Object_Draw(1)` (line ~1817) and before `Matrix_Pop` (line ~1880):

```c
#ifdef PRACTICE_ROM
#include "practice.h"
    Practice_Hitbox_Draw();
#endif
```

The menu update/draw for the hitbox sub-menu remains in the 2D path (`Practice_StateMenu_Update/Draw` in `practice_state.c`).

## Guard Checks

`Practice_Hitbox_Draw()` early-returns if:
- `gPracticeConfig.showHitboxes == false` (master toggle off)
- `gGameState != GSTATE_PLAY`
- `gPlayState != PLAY_UPDATE`
- `gPlayer == NULL` (implied by the above checks, but defensive)

## New Files

| File | Purpose |
|------|---------|
| `src/practice/practice_hitbox.c` | All hitbox visualization logic |

## Modified Files

| File | Change |
|------|--------|
| `include/practice.h` | Add config fields, declare `Practice_Hitbox_Draw()` |
| `src/practice/practice_main.c` | Init config defaults |
| `src/practice/practice_state.c` | Add `PSUBMENU_HITBOX` enum, menu entry, update/draw functions |
| `src/engine/fox_display.c` | Add 3D draw hook in `Display_Update()` |
| `tools/patch_linker_script.py` | Add `practice_hitbox` to `PRACTICE_OBJS` |
| `linker_scripts/us/rev1/starfox64.ld` | Add `.o` to all four sections |
