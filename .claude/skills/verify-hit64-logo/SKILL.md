---
name: verify-hit64-logo
description: Verify the HIT64 logo is correctly wired into the practice ROM level-select menu. Checks invariants, rebuilds if needed, and optionally launches the ROM in Dolphin to visually confirm the logo appears in the top-right corner of the menu.
---

## What this verifies

The HIT64 logo (`src/practice/practice_logo_tex.c`) renders in the top-right corner
of the level-select menu (screen position ~238, 14). It's a 64×32 RGBA16 5551 texture
with the black background made transparent, drawn via `RCP_SetupDL_76` +
`Lib_TextureRect_RGBA16`.

## Step 1 — Static invariants

```bash
python3 tools/practice_invariants.py
```

The `check_hit64_logo()` check verifies:
- `sPracticeLogoTex` array exists in `practice_logo_tex.c`
- `Practice_Logo_Draw` function exists in `practice_logo_tex.c`
- `RCP_SetupDL_76` and `Lib_TextureRect_RGBA16` are both called
- `Practice_Logo_Draw` is called from `practice_level.c`
- `Practice_Logo_Draw` is declared in `include/practice.h`

Expected output: `Practice ROM invariant checks passed.`

## Step 2 — Build

```bash
make practice -j4
```

The logo texture is 4096 bytes (64×32 × 2) in the `.data` section of
`practice_logo_tex.o`, linked just after `practice_freecam.o`. Build
should produce no errors related to logo or linker placement.

## Step 3 — Visual check via Dolphin (optional)

If Dolphin MCP is available:

1. Use `mcp__dolphin__connect` to connect to a running Dolphin instance
2. Launch the ROM: `mcp__dolphin__launch_dolphin_movie` or load the ROM manually
3. Let the ROM boot to the level-select screen (the "SF64 PRACTICE ROM" / "SELECT LEVEL" menu)
4. Use `mcp__dolphin__read_memory` to confirm the logo texture data is loaded:
   - Find `sPracticeLogoTex` address from the map file:
     ```bash
     grep sPracticeLogoTex build/starfox64.us.rev1.map
     ```
   - Read 8 bytes at that address — first non-zero u16 word should appear within
     the opaque pixel region (428 opaque pixels out of 2048 total)

## Regenerating the texture

If `HIT64.png` changes, re-run the converter:

```bash
python3 tools/gen_hit64_logo.py
```

Then rebuild: `make practice -j4`

The converter script (`tools/gen_hit64_logo.py`) auto-crops to content, scales to
64×32, converts black pixels (R<40 AND G<40 AND B<40) to alpha=0, and emits the
`static u16 sPracticeLogoTex[]` array with the draw function.

## Key constraints

- **TMEM limit**: 64×32 RGBA16 = 4096 bytes exactly — do not increase dimensions
  without splitting into strips (like the Nintendo logo does in `fox_game.c`)
- **Numpy int cast**: the converter must cast pixel channels to Python `int` before
  bit-shifting; numpy uint8 silently overflows on `<< 11`
- **Position**: `Practice_Logo_Draw(238.0f, 14.0f)` — top-right of the 288×192 menu
  box, clear of "SF64 PRACTICE ROM" text (x=20–122) and the level list (y=46+)
- **Render state**: `RCP_SetupDL_76` sets texture mode; subsequent `Practice_DrawBox`
  and `Graphics_DisplaySmallText` calls restore their own state — no manual teardown needed
