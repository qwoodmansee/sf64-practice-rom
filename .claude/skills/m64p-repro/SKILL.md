---
name: m64p-repro
description: Write and run a mupen64plus Python test that reproduces a user-reported SF64 practice ROM bug. The test PASSES when the bug is present, FAILS when fixed. Implementor agents then fix the red test. Use whenever a player reports a regression or silent failure.
---

## Purpose and workflow

```
User reports bug
       ↓
Write tests/test_<name>.py that manufactures the bad state and asserts it
       ↓
python3 tools/m64p_test_runner.py test_<name>  →  PASSED = bug confirmed
       ↓
git commit on fixing-kelps-bugs (or current fix branch)
       ↓
Hand to SF64 Coder agent: "fix the red test"
       ↓
python3 tools/m64p_test_runner.py test_<name>  →  FAILED = bug gone
```

A "bug repro" test asserts the *bad* state. When all assertions pass, the bug is present.
When any assertion fails, the bug is fixed. This is the inverse of a normal regression test.
Document this clearly at the top of every repro test file.

---

## Running tests

```bash
# Single test (fast, ~30s)
python3 tools/m64p_test_runner.py test_corneria_bgm_preview_silent

# All Python tests
python3 tools/m64p_test_runner.py

# Requirements
brew install mupen64plus   # once
make practice -j4          # ROM must exist at build/starfox64.us.rev1.uncompressed.z64
```

The test runner auto-compiles `tools/m64p_harness.c` if needed. No BizHawk required.

---

## Test file structure

```python
"""Bug repro: <one-line summary>.

Steps to reproduce manually:
  1. ...

Root cause:
  Explain WHY it happens, not just what. Read the source.

Test approach:
  Manufacture the stuck state via direct memory writes. Assert the bad outcome.
  Bug confirmed = test PASSES. Bug fixed = test FAILS.

This test ASSERTS THE BUG IS PRESENT. It will flip to FAIL once fixed.
"""

# RDRAM offsets (addr & 0x1FFFFFFF) from build/starfox64.us.rev1.map
_GAME_STATE = 0x00194944   # gGameState (s32)
# ... others ...

GSTATE_MAP       = 4   # practice level select (from include/sf64thread.h)
GSTATE_PLAY      = 7   # in-level gameplay
PLAY_INIT        = 1
PLAY_UPDATE      = 2
PSCREEN_GAMEPLAY = 1   # from include/practice.h


def run(ctx):
    h = ctx.harness

    ok = h.wait_for(_GAME_STATE, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "Booted to level select")
    if not ok:
        return

    h.advance(10)  # let level select initialize

    # --- manufacture bad state ---
    # ... direct memory writes ...

    # --- launch level ---
    h.write32(0x0017eb40, (level_id << 16) | GSTATE_PLAY)  # gNextLevel|gNextGameState
    h.write32(0x00195b80, PSCREEN_GAMEPLAY)                 # gPracticeScreen

    ok = h.wait_for(_GAME_STATE, GSTATE_PLAY, 30000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY")
    if not ok:
        return
    ok = h.wait_for(_PLAY_STATE, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE")
    if not ok:
        return

    h.advance(400)

    # --- assert bug present ---
    ctx.assert_eq(bad_signal, expected_bad_value, "Bug: <what this proves>")
```

---

## Key symbol addresses

Always verify against `build/starfox64.us.rev1.map` (`grep SYMBOL build/starfox64.us.rev1.map`).
Static variables (e.g. `sBgmLastSpecPacked`) are not in the map — compute from known
struct layout and confirm with a live memory dump.

| Symbol | RDRAM offset | Type |
|--------|-------------|------|
| `gGameState` | `0x00194944` | s32 |
| `gPlayState` | `0x00194964` | s32 |
| `gCurrentLevel` | `0x0019534c` | s32 |
| `gPlayer` | `0x00195398` | Player* (NULL until PLAY_UPDATE) |
| `gNextLevel` | `0x0017eb40` | u16 hi16 of shared word |
| `gNextGameState` | `0x0017eb42` | u16 lo16 of same word |
| `gPracticeScreen` | `0x00195b80` | s32 (0=SELECT, 1=GAMEPLAY) |
| `gPracticeBgmPending` | `0x00195cc4` | bool/s32 |
| `gPracticeBgmPendingSeqId` | `0x00195cc8` | u16 hi16 |
| `gPracticeBgmPendingDelay` | `0x00195ccc` | s32 |
| `gGameFrameCount` | `0x00194940` | s32 |
| `gBgmSeqId` | `0x00194da8` | u16 hi16 (level-env BGM id) |
| `sAudioResetStatus` | `0x000de258` | u8 (0=READY, 1=WAIT, 2=BLOCK) |
| `sActiveSequences[0].seqId` | `0x001680f0` | u16 hi16 (BGM player active seqId) |
| `sActiveSequences[0].isWaitingForFonts` | `0x001680fc` | u8 hi byte |
| `sBgmLastSpecPacked` (static) | `0x000ed96c` | u16 hi16 |

---

## N64 big-endian memory helpers

The harness only exposes read32/write32. N64 is big-endian: a u16 at address A sits
in the upper 16 bits of the 4-byte word; a u8 at address A sits in the upper 8 bits.

```python
def _read_u16_hi(harness, rdram_addr):
    return (harness.read32(rdram_addr & ~3) >> 16) & 0xFFFF

def _read_u8_hi(harness, rdram_addr):
    return (harness.read32(rdram_addr & ~3) >> 24) & 0xFF

def _write_u16_hi(harness, rdram_addr, value):
    word = harness.read32(rdram_addr & ~3)
    word = (word & 0x0000FFFF) | ((value & 0xFFFF) << 16)
    harness.write32(rdram_addr & ~3, word)

def _write_u8_hi(harness, rdram_addr, value):
    word = harness.read32(rdram_addr & ~3)
    word = (word & 0x00FFFFFF) | ((value & 0xFF) << 24)
    harness.write32(rdram_addr & ~3, word)

def _read_s32(harness, rdram_addr):
    val = harness.read32(rdram_addr)
    return val - 0x100000000 if val >= 0x80000000 else val
```

---

## Launching a level (bypassing Practice_LaunchLevel)

Use this when you need to preserve manufactured audio/memory state that
`Practice_LaunchLevel` would clobber (it calls `Audio_SetAudioSpec`, resets
`sBgmPlaying`, calls `Practice_Hud_Reset`, etc.):

```python
# gNextLevel (u16 hi16) and gNextGameState (u16 lo16) share a 32-bit word.
h.write32(0x0017eb40, (level_id << 16) | GSTATE_PLAY)
h.write32(0x00195b80, PSCREEN_GAMEPLAY)  # gPracticeScreen
```

**Button injection is not supported.** The OS controller read overwrites
`gControllerPress` each frame before game logic runs, so writing to it between
ADVANCE calls does nothing. If you must go through `Practice_LaunchLevel`, use a
DTM (input recording) — see `tests/dtm/` and `tests/dtm_generator.py`.

---

## Audio engine internals — the isWaitingForFonts trap

`sActiveSequences[SEQ_PLAYER_BGM].isWaitingForFonts` at `0x001680fc` (u8 hi byte):

- Set `true` when `SEQCMD_PLAY_SEQUENCE` has `seqArgs >= 0x80`
- **Every BGM seqId with SEQ_FLAG (0x8000) encodes 0x80 into seqArgs:**
  `NA_BGM_STAGE_CO = 0x8002` → command packs high byte (0x80) as seqArgs → triggers async font DMA
- While `true`, **every AUDIO_PLAY_BGM call is silently dropped**
- Cleared only when the async DMA completes via `AudioThread_GetAsyncLoadStatus()`
- `Audio_StopSequence()` cancels the DMA but does NOT clear the flag

**The Corneria BGM silent-music bug (tests/test_corneria_bgm_preview_silent.py):**
Preview calls `AUDIO_PLAY_BGM(0x8002)` → `isWaitingForFonts = true` → user presses A →
`Audio_StopSequence` cancels DMA but flag stays → every subsequent AUDIO_PLAY_BGM
(from Practice_ApplyStartConditions + BGM rescue) is silently dropped.

### BGM rescue mechanism (Practice_Save_Tick, called every frame)

```c
if (gPracticeBgmPending && Audio_HandleReset() == 0) {
    if (gPracticeBgmPendingDelay > 0) {
        if (gPlayState == PLAY_UPDATE) gPracticeBgmPendingDelay--;
    } else {
        AUDIO_PLAY_BGM(gPracticeBgmPendingSeqId);  // may be silently dropped!
        gPracticeBgmPending = false;               // set false regardless
    }
}
```

Rescue fires `gPracticeBgmPendingDelay` PLAY_UPDATE frames after queued.
`gPracticeBgmPending` becomes false even if `isWaitingForFonts` blocked the play.
Use `sActiveSequences[0].seqId` as the ground-truth "is BGM actually playing" signal,
not `gPracticeBgmPending`.

### sAudioResetStatus (Audio_HandleReset return value)

`sAudioResetStatus` at `0x000de258`:
- `0 = AUDIORESET_READY`: idle, seq cmds processed, rescue can fire
- `1 = AUDIORESET_WAIT`: heap reset underway (~5-24 game frames for full spec change)
- `2 = AUDIORESET_BLOCK`: spin-wait (specific spec transitions only)

Same-spec `Audio_SetAudioSpec` (old spec == new spec) skips the full heap reset and
only calls `Audio_StopSequence` — `sAudioResetStatus` stays `AUDIORESET_READY`.

---

## TestContext warnings

`TestContext` in `tools/m64p_test_runner.py` has **incorrect game-state constants**:

```python
# WRONG (in parse_symbols):
"GSTATE_MAP": 5,        # real: 4
"GSTATE_PLAY": 4,       # real: 7
"PSCREEN_LEVEL_SELECT": 2,  # real: 0
"PSCREEN_GAMEPLAY": 0,      # real: 1
```

**Always use `h.wait_for()` directly** with constants from this skill or from
`include/sf64thread.h` / `include/practice.h`. Do not call
`ctx.wait_for_level_select()` or `ctx.wait_for_gameplay()`.

---

## Existing bug repro tests

| File | Bug | Key signal |
|------|-----|-----------|
| `tests/test_corneria_bgm_preview_silent.py` | Corneria (and any same-spec level) BGM silent when same-spec BGM was previewed on level select | `sActiveSequences[0].seqId == SEQ_ID_NONE (0xFFFF)` after 400 frames |

---

## Pre-commit checklist

1. Filename: `tests/test_<short_bug_name>.py`
2. Top docstring: manual steps + root cause + "ASSERTS BUG IS PRESENT" note
3. All RDRAM offsets verified against current map file
4. Runs in < 90s: `gtimeout 90 python3 tools/m64p_test_runner.py test_<name>`
   (`gtimeout` from `brew install coreutils`; stock macOS has no `timeout`)
5. `PASSED` with bug present, `FAILED` after fix
6. Commit to fix branch, not master
