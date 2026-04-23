# SF64 Practice ROM — Design Spec

**Target:** Star Fox 64 US rev 1.1 (matching decomp)
**Community:** HIT+64 (hit64.net) high-score competition
**Platform:** Mupen64Plus (development), real N64 + flashcart (future)
**Distribution:** BPS patch against `baserom.us.rev1.z64`

## Goals

Give HIT+64 players a practice ROM that lets them jump to any level with controlled starting conditions, restart instantly, and save/restore mid-level positions — all from real hardware or emulator with no code knowledge required.

## Architecture

### Build System

- `make` continues to produce the matching decomp ROM (untouched).
- `make practice` (or `PRACTICE_ROM=1 make`) produces `build/starfox64.us.rev1.practice.z64`.
- `PRACTICE_ROM` is a compile-time define. All practice code is behind `#if PRACTICE_ROM` guards.
- After building, `make patch` generates a BPS patch for distribution.

### Source Layout

```
src/practice/
  practice_main.c      -- init, per-frame update/draw dispatch, boot redirect
  practice_menu.c      -- in-game overlay menu (L + D-Pad Down)
  practice_level.c     -- level select screen, route/path control, quick restart
  practice_state.c     -- starting conditions (lives, bombs, lasers, wings, allies)
  practice_save.c      -- save/restore checkpoint (in-memory for v1)
  practice_draw.c      -- HUD drawing helpers (text, menu boxes, cursors)
  practice_input.c     -- input handling, customizable D-Pad bindings

include/practice.h     -- public interface, structs, PRACTICE_ROM guard
```

### Hook Points

Minimal changes to existing game code, all behind `#if PRACTICE_ROM`:

1. **Boot redirect** — In the game state machine (`sys_main.c` or equivalent), skip `GSTATE_BOOT` through `GSTATE_START` and jump to a new `GSTATE_PRACTICE` state that shows the practice level select menu.

2. **Per-frame update** — One `Practice_Update()` call in the main game loop. Handles: menu toggle detection (L + D-Pad Down), shortcut key checks, active feature logic.

3. **Per-frame draw** — One `Practice_Draw()` call in the render path. Draws: overlay menu when open, any persistent HUD elements.

4. **Level load hook** — After level initialization completes (`PLAYERSTATE_INIT`), call `Practice_ApplyStartConditions()` to inject configured laser/bombs/lives/wings.

## Features (v1)

### 1. Custom Boot → Level Select

On boot, skip all logos and title screen. Go directly to a full-screen practice level select menu.

**Level list** organized by map column (matching how HIT+64 thinks about routes):

| Col | Levels |
|-----|--------|
| 1 | Corneria |
| 2 | Meteo, Sector Y |
| 3 | Fortuna, Katina, Aquas |
| 4 | Sector X, Solar, Zoness |
| 5 | Titania, Macbeth, Sector Z |
| 6 | Bolse, Area 6 |
| 7 | Venom 1, Venom 2 |

Each level entry also allows selecting:
- **Phase:** Normal start, warp zone entry (Meteo, Sector X), boss-only (Andross)
- Vehicle form is implicit per level (Arwing, Landmaster, Blue Marine, On Foot).

Navigation: D-Pad to move cursor, A to launch level, Start for starting conditions sub-menu.

### 2. Starting Conditions

Configurable before launching a level (sub-menu from level select, also accessible from in-game practice menu):

| Setting | Range | Game Variable |
|---------|-------|---------------|
| Laser strength | Single / Twin / Hyper | `gLaserStrength[0]` |
| Bomb count | 0–9 | `gBombCount[0]` |
| Lives | 1–99 | `gLifeCount[0]` |
| Right wing | Intact / Broken | `Player.arwing.rightWingState` |
| Left wing | Intact / Broken | `Player.arwing.leftWingState` |
| Allies | Per-ally alive/down toggle | Team member shield values |

Applied immediately after level load completes, overwriting defaults.

### 3. Quick Restart

Instantly reload the current level with the same starting conditions. No fade, no menu transition.

**Default binding:** L + D-Pad Up

Implementation: Reset `gGameState` to reload the current `gCurrentLevel`/`gLevelPhase`, then re-apply starting conditions.

### 4. In-Game Practice Menu

**Opens:** L + D-Pad Down (while in gameplay)
**Closes:** B, or L + D-Pad Down again

Behavior while open:
- Game is paused (game loop stops updating, frame is still rendered)
- Semi-transparent overlay drawn on top of the frozen frame

Menu contents:
- **Status display:** Current hit count, level timer
- **Actions:** Restart level, Save position, Restore position, Return to level select
- **Settings:** Change starting conditions, Customize controls

Navigation: D-Pad up/down to select, A to confirm, B to cancel/back.

### 5. Save/Restore Position (In-Memory)

Single checkpoint slot stored in a RAM buffer.

**Save captures:**
- `Player` struct (position, velocity, rotation, state, shields, boost, form, wing state)
- Game globals: `gPathProgress`, `gSavedPathProgress`, `gHitCount`, `gLaserStrength[0]`, `gBombCount[0]`, `gLifeCount[0]`, `gLevelPhase`
- Camera state
- RNG seed

**Restore** overwrites all of the above from the buffer.

**Default bindings:** L + D-Pad Left to save, L + D-Pad Right to restore.

**v1 limitation:** In-memory only. Checkpoint is lost on power cycle or level change. Object/enemy state is NOT saved — only player state and globals. This means restoring mid-level may have different enemy positions than when saved. Full scene restore (object arrays) is a v2 goal.

**Scope note:** Saving/restoring the full object arrays (enemies, scenery, projectiles) is complex and risks instability. v1 ships with player-state-only checkpoints. If that proves insufficient for practice, v2 adds object array snapshots.

### 6. Customizable Controls

Four D-Pad directions (while holding L) are bindable shortcuts:

| Default | Action |
|---------|--------|
| L + D-Pad Up | Quick restart |
| L + D-Pad Down | Open/close practice menu |
| L + D-Pad Left | Save position |
| L + D-Pad Right | Restore position |

Rebindable from the practice menu. Available actions: quick restart, open menu, save position, restore position, return to level select.

**v1 limitation:** Bindings reset each boot (no persistence). SD card config persistence is a future addition.

## Future (v2+)

These are explicitly out of scope for v1:

- **Flashcart SD card I/O** — persist checkpoints and config to SD card. `practice_flash.c` with flashcart detection (EverDrive 64, 64drive, SC64).
- **Full scene save/restore** — snapshot object arrays (enemies, scenery, items) for exact mid-level restore.
- **Multiple save slots** — switch between named checkpoints.
- **On-screen hit counter** — persistent HUD display during gameplay (not in pause menu).
- **Segment timing / splits** — on-screen level timer with segment comparison.
- **Input display** — show controller inputs on screen.
- **Rev 0 compatibility** — port practice features to US rev 0 when that decomp progresses, or patch specific rev0 differences into the rev1 build.
- **Web-based patcher** — hosted page where users drag-and-drop their ROM + patch.

## Technical Notes

### Memory Budget

The N64 has 4MB (or 8MB with Expansion Pak). Star Fox 64 targets 4MB. The practice system needs:
- `Player` checkpoint buffer: ~0x4E0 bytes
- Globals checkpoint: ~100 bytes  
- Camera checkpoint: ~200 bytes
- Menu state / strings / UI: ~2-4KB
- Total practice overhead: ~8KB

Well within budget. If full object array saves are added later, those arrays can be large (tens of KB), which may require the Expansion Pak or careful memory management.

### Drawing

The game's existing `Graphics_DisplaySmallText()` and RCP display list system are used for all practice UI rendering. The existing level select mod already demonstrates this pattern. Menu boxes use `gDPFillRectangle` with alpha blending for the semi-transparent overlay.

### Input Conflict

L + D-Pad is chosen because L is rarely used during normal gameplay (it's the targeting/lock-on modifier). D-Pad is unused during flight. The combination avoids interfering with normal play controls (analog stick, A, B, Z, R, C-buttons).

### Matching Build Safety

All practice code is behind `#if PRACTICE_ROM`. The default `make` target does not define this symbol, so the matching decomp build is completely unaffected. CI continues to verify the matching build.
