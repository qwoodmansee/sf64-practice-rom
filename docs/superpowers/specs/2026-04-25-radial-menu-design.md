# Radial Practice Menu

## Summary

Replace the list-based in-game practice menu with a 6-slice radial menu controlled by the analog stick. Sub-menus (Loadout, Display) remain as traditional D-pad lists. Teammates move from Options into Loadout; Options is renamed to Display.

## Radial Slices (clockwise from top)

| Slice | Direction | Type | Action |
|-------|-----------|------|--------|
| RESTART | Up | Immediate | Restart current level |
| SAVE | Up-Right | Immediate | Save position |
| LOAD | Down-Right | Immediate | Restore position |
| LEVELS | Down | Immediate | Return to level select |
| LOADOUT | Down-Left | Sub-menu | Opens Loadout list |
| DISPLAY | Up-Left | Sub-menu | Opens Display list |

## Stick Input Detection

Read `stick_x` (positive=right) and `stick_y` (positive=up) from held controller state.

- **Dead zone:** `abs(x) < 20 && abs(y) < 20` = no selection
- **Sector detection** (6 sectors of 60 degrees, integer math only):
  - `abs(y) * 100 > abs(x) * 173` determines vertical vs angled sectors (approximates tan(60) boundary)
  - Combined with sign of x and y, yields one of 6 sectors

```
if abs(y)*100 > abs(x)*173:
    y > 0 → UP (RESTART)
    y < 0 → DOWN (LEVELS)
else if x > 0:
    y > 0 → UP-RIGHT (SAVE)
    y < 0 → DOWN-RIGHT (LOAD)
else:
    y > 0 → UP-LEFT (DISPLAY)
    y < 0 → DOWN-LEFT (LOADOUT)
```

## Visual Layout (320x240 screen)

- Semi-transparent background box covers menu area
- 6 labels positioned in a hex ring around center (~160, 120)
- Center shows hit count and description of hovered slice
- Selected slice: yellow text + highlight box
- Non-selected slices: dimmed gray when something is selected, white when neutral
- Bottom text: "STICK:SELECT  A:GO  B:CLOSE"
- A press on immediate slices executes and closes menu
- A press on sub-menu slices opens the traditional list overlay

## Sub-Menu Reorganization

### Loadout (gains teammates)

LASER, BOMBS, LIVES, R WING, L WING, FALCO, SLIPPY, PEPPY, BACK

### Display (renamed from Options)

CUTSCENES, INPUT, HUD, (indented) LAG, SPEED, CHARGE, MISSED, HITS, HITBOX VIEWER..., BACK

### Hitbox Viewer (unchanged)

HITBOXES, ACTORS, SCENERY, ITEMS, PLAYER, FLASH, BACK

## Code Changes

- **practice_menu.c**: Replace list rendering/update with radial logic. Read analog stick from `gControllerHold` (not press). Detect sector, draw hex ring, handle A/B.
- **practice_state.c**: Move Falco/Slippy/Peppy from Options to Loadout. Rename Options to Display. Add cutscene skip to Display. Adjust box heights and option counts.
- **practice.h**: Rename `PSUBMENU_OPTIONS` to `PSUBMENU_DISPLAY`.
- **practice_level.c**: Update `Practice_StateMenu_Open(PSUBMENU_LOADOUT)` call (still works, just the Display rename needs checking).
