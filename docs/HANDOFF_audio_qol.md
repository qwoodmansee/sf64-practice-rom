# Handoff — Audio QoL feature (qol-updates worktree)

## UPDATE 2026-06-06 — "volume resets on RESTART" FIXED (the known gap is closed)

The restart/level-switch gap below is now fixed. Per the user's decision, the
AUDIO menu was collapsed from 4 sliders to **3** (MUSIC / SFX / VOICE) and now
routes through the engine's own master-volume layer instead of issuing raw
per-player SEQCMD volume.

Root cause recap: the engine's applied volume = product of per-player fade
modifiers (`mainVolume.fadeMod[0..2]`), recomputed on every fade. The old code
wrote `mainVolume.target` directly via `SEQCMD_SET_SEQPLAYER_VOLUME`, leaving
`fadeMod[0]` at full — so the next fade (level-restart
`Audio_RestoreVolumeSettings`, OR an in-level music duck) snapped volume back to
full. Restart was just the most visible trigger.

The fix:
- `practice_audio.c` now calls `Audio_SetVolume(audioType, 0..99)`, which writes
  `sVolumeSettings[]` (the engine master-volume layer) and applies it via
  `Audio_RestoreVolumeSettings` — the same path the stock options menu uses.
  A practice restart goes through `GSTATE_PLAY` (not `GSTATE_INIT`, which is the
  only place the save file re-seeds `sVolumeSettings`), so the value persists and
  the engine's own reset restore re-asserts OUR value. No race. In-level ducking
  also now ducks relative to the configured level.
- `AudioType` groups BGM+FANFARE under MUSIC, so the separate FANFARE slider was
  dropped (user-approved). Config: `volBgm`/`volFanfare` -> `volMusic` (+`volSfx`
  /`volVoice`), scale changed 0..0x7F -> 0..99, defaults 99. `AUDIO_VOL_STEP` 8 -> 9.
- SFX/VOICE wrinkle: `Audio_SetVolume` applies SFX/VOICE at the *channel* layer,
  which does NOT undo the transition `Audio_FadeOutAll` that zeroes the
  sequence-player `mainVolume`. So `Practice_Audio_ApplyLane` also restores the
  SFX/VOICE sequence-player `mainVolume` to full (mirroring
  `Audio_RestartSeqPlayers`); the user's level rides on the channel layer.

Tests (all GREEN): `test_restart_preserves_audio_levels.py` (was the known-RED
repro `known_issue_restart_audio_levels.py`, now promoted + flipped),
`test_load_preserves_audio_levels.py`, `test_audio_volume_menu.py`,
`test_config_defaults.py`, `test_restart_kills_audio.py`. Static invariants pass;
`check_audio_volume_menu` updated to require routing through `Audio_SetVolume`.
`main_ROM_END=0xFCF50` (still under the 0xFD000 boot cap).

Note: `test_audio_frozen_after_pause_load.py` /
`test_audio_reset_fixes_frozen_after_load.py` remain pre-existing RED repros for
a SEPARATE pause->load freeze bug; their BGM-state-byte signal model also looks
stale in the current m64p harness (sanity asserts fail). Only the latter's menu
navigation was updated (RESET ALL moved row 4 -> 3 after dropping FANFARE).

## SUPERSEDED — UPDATE 2026-06-05 — "volume resets on load" FIXED; restart is a known gap

User report: "volume sliders work but their values get changed back when you
load a savestate." ROOT CAUSE + FIX:

- The post-load BGM rescue in `Practice_Save_Tick` (`practice_save.c`) hard-coded
  four `SEQCMD_SET_SEQPLAYER_VOLUME(<lane>, 0, 0x7F)` calls — forcing FULL volume
  on every load and discarding the user's slider mix. (The earlier handoff
  claimed this was already `Practice_Audio_ApplyAll()`; it was NOT — the 0x7F
  lines were still there.) Replaced with a single `Practice_Audio_ApplyAll()`
  which re-issues the command per lane from `gPracticeConfig.vol*`.
- Why load works now: a same-scene load does NOT trigger the engine's audio
  heap reset, so the practice `mainVolume` survives + the engine preserves
  `mainVolume.mod` across the BGM restart (`Audio_StartSequence`, audio_general.c
  ~636). `Practice_Audio_ApplyAll()` re-asserts the mix to be safe.
- Tests: `tests/test_load_preserves_audio_levels.py` GREEN (asserts applied
  `gSeqPlayers[0].fadeVolumeMod` still ~0.496 after load). `test_audio_volume_menu`
  and `test_config_defaults` GREEN. `test_restart_kills_audio` GREEN (default
  volumes still restored to 1.0). Invariant `check_audio_volume_menu()` was
  MISSING (registered in main() but undefined → NameError crashed the whole
  suite); now defined and guards the load-rescue `ApplyAll` form. The old
  `check_bgm_rescue_restores_main_volume` was updated to accept `ApplyAll()` as
  satisfying the "restore all four lanes after AUDIO_PLAY_BGM" rule.
- Test harness fix: `m64p_test_runner.py` `config_field`/`set_config_field` are
  now size- and big-endian-aware (track per-field sizes in `parse_symbols`), so
  a u8 field returns its single byte (127), not the packed word 0x7F7F7F7F.

KNOWN GAP (deferred, user OK'd shipping load-only): restarting a level / switching
levels STILL resets the slider mix. Root cause is different and harder: the
level-start audio reset runs `Audio_RestartSeqPlayers` →
`Audio_RestoreVolumeSettings(AUDIO_TYPE_MUSIC)` (audio_general.c ~2596), which
re-asserts the engine's MASTER music volume (`sVolumeSettings[MUSIC]`, 0-99
options scale) onto SEQ_PLAYER_BGM/FANFARE late in the async heap reset. That
OVERRIDES any practice-side `SEQCMD_SET_SEQPLAYER_VOLUME` re-apply regardless of
timing — empirically verified that applying after reset-idle + BGM-up + a
45-frame settle still loses the race (`mainVolume.target` forced back to 1.0).
The clean fix is to integrate the practice volume with the engine master-volume
layer (`sVolumeSettings`) instead of racing the reset. Repro preserved at
`tests/known_issue_restart_audio_levels.py` (renamed out of the `test_*.py`
glob so the suite doesn't collect it).

---

Last updated: 2026-05-31. Worktree:
`/Users/qwoodmansee/code/sf64-practice-rom/.claude/worktrees/qol-updates`
Branch: `qol-updates`. Base: `master`.

## What this feature is

Replace SAVE/LOAD on the practice radial menu with an **AUDIO** panel:
per-lane volume sliders (Music/Fanfare/SFX/Voice) plus a per-lane "reset"
and a "RESET ALL". Motivation: savestates/pause sometimes leave audio
muted or frozen; the user wants a quick way to fix audio from the menu.

## STATUS: what's DONE and verified

Feature code is implemented, builds, links, fits under the ROM boot cap,
passes all static invariants, and `test_config_defaults` passes 23/23.
It was uploaded to the SummerCart64 once already (`./tools/sc64dev all`).

Implemented:
- `src/practice/practice_audio.c` — per-lane volume API. Lane index ==
  `SequencePlayerId` (BGM=0, FANFARE=1, SFX=2, VOICE=3). Functions:
  `Practice_Audio_GetVolume/SetVolume/AdjustVolume/ApplyLane/ApplyAll/
  ResetLane/ResetAll`. Uses `SEQCMD_SET_SEQPLAYER_VOLUME`.
- `include/practice.h` — added `u8 volBgm/volFanfare/volSfx/volVoice` to
  `PracticeConfig` (default 0x7F); added `PSUBMENU_AUDIO`; declared the
  `Practice_Audio_*` API.
- `src/practice/practice_main.c` — init the 4 vol defaults to 0x7F. ALSO:
  stripped debug `osSyncPrintf` breadcrumbs here + in `practice_level.c` +
  `practice_overlay.c` to reclaim ROM headroom (see ROM CAP below).
- `src/practice/practice_menu.c` — root radial: E wedge = AUDIO (was SAVE),
  SE wedge now empty, `RSLICE_SAVE`/`RSLICE_LOAD` removed, Z / Z+B save-load
  shortcuts removed. AUDIO wedge opens `PSUBMENU_AUDIO`.
- `src/practice/practice_state.c` — `AudioOption` enum (AOPT_MUSIC, FANFARE,
  SFX, VOICE, RESET_ALL, BACK — first 4 MUST stay in SequencePlayerId order),
  `StateMenu_UpdateAudio` (D-pad L/R adjust hovered lane by AUDIO_VOL_STEP=8,
  A resets lane / RESET ALL), `StateMenu_DrawAudio`, wired into dispatch +
  `Practice_StateMenu_Draw` (title "AUDIO", boxHeight 110, helpY 142).
- `src/practice/practice_save.c` — post-load BGM rescue now calls
  `Practice_Audio_ApplyAll()` instead of four hardcoded
  `SEQCMD_SET_SEQPLAYER_VOLUME(..., 0x7F)` lines (restores the user's mix,
  not just full volume).
- `tools/patch_linker_script.py` — `practice_audio` added to `PRACTICE_OBJS`.
- `linker_scripts/us/rev1/starfox64.ld` — `practice_audio.o` added to all 4
  sections (.text/.data/.rodata/.bss). NOTE: this file is gitignored and
  generated; it was hand-patched because the patcher skips an already-patched
  .ld.
- `tools/practice_invariants.py` — added `check_audio_volume_menu()` and
  registered it in `main()`; added `PSUBMENU_AUDIO -> StateMenu_DrawAudio` to
  the `check_state_menu_layout` map.
- `tests/test_config_defaults.py` — added vol* default assertions (127 each).
- `tests/test_audio_volume_menu.py` — load/restart-stability test for the
  config fields (does NOT drive the radial; harness can't hover wedges — see
  KEYMAP note). This one PASSES.

## ROM CAP (critical, do not regress)

`check_boot_main_rom_budget` enforces `main_ROM_END < 0x0FD000` (boot-safe
IPL limit; over it = blue screen on hardware). Adding `practice_audio` to
`main` pushed it over. On THIS branch `.practice_late_core` (0x80720000) is
Pak-only and NOT gameplay-accessible on hardware, so audio (a gameplay
feature) MUST stay in `main` — it cannot be moved to late_core. Headroom was
reclaimed by deleting debug `osSyncPrintf` lines. Current `main_ROM_END =
0x0FCF80` (≈128 bytes of headroom). If you add code to main, watch this.

Build: `make practice -j4` (incremental) or `rm -rf build/ && make practice -j4`
(NEVER `make clean`/`make init`). After `rm -rf build/`, the worktree's
symlinked `build/src/assets` is destroyed and must be re-linked:
`ln -sfn /Users/qwoodmansee/code/sf64-practice-rom/build/src/assets <wt>/build/src/assets`
(this bit us once — link error `cannot find ast_common.o`).

## REPRO TESTS — STATUS (updated 2026-05-31, all empirically verified)

The audio-frozen-after-pause→load bug IS real and is now captured by two RED
tests. Key correction from the first handoff draft: `gPlayState` is NOT the
bug signal (it correctly returns to PLAY_UPDATE=2 after load). The real,
isolated fingerprint is the BGM **sequence player state byte**:

  gSeqPlayers[0]  base 0x8016d038, stride 0x14C, [0] = BGM.
    +0x001  u8 state   <-- THE SIGNAL. 0 = healthy/running, 1 = wedged.
    +0x000  bitfield   (bit5 0x20 fontDmaInProgress also stuck set on the bug)

Control-vs-repro proof (single run, same saved state):
  running baseline        state=0
  load WITHOUT pausing     state=0   (control: load path itself is fine)
  pause, THEN load         state=1   (stuck 4+ seconds == frozen audio)
So the defect is specific to the pause->load interaction. isWaitingForFonts
(sActiveSequences[0]+0x254) was checked and is NOT the culprit here.

Tests written (both RED as intended; sanity/control asserts all PASS):
- `tests/test_audio_frozen_after_pause_load.py` — pause+load, assert BGM
  state==0. RED (state stuck at 1). Includes a control assert that plain load
  leaves state==0.
- `tests/test_audio_reset_fixes_frozen_after_load.py` — same wedge, then drive
  radial -> AUDIO -> RESET ALL, assert BGM state==0. RED (reset only re-issues
  volume; doesn't un-wedge the player).

Still TODO (user's Bug 2): Tests C (load must not change set volumes) and D
(level restart must not change set volumes), asserting on APPLIED volume
(gSeqPlayers[lane].fadeVolume at +0x1C, f32). Not yet written.

### CORRECTED HARNESS FACTS (critical — the first draft was WRONG)
`ctx.harness.key_down(n)` passes `n` as an SDL **SCANCODE**, NOT the keycode
in the mupen config string. Calibrated empirically against gControllerHold[0]
(0x800fc380, button = upper u16 of first word):
    A        = 27   (CONT_A    0x8000)
    START    = 40   (CONT_START 0x1000)   <-- NOT 13
    L_TRIG   = 20   (CONT_L    0x0020)
    DPad L   = 4    (CONT_LEFT  0x0200)
    DPad R   = 7    (CONT_RIGHT 0x0100)  -- also drives stick East (X Axis)
    C-Right  = 15   (CONT_F    0x0001)
    DPad Dn  = 22   ('s')
Menu open = hold C-Right(15) + START(40) together (PACTION_OPEN_MENU).
Save = L_TRIG(20)+DPadL(4). Load = L_TRIG(20)+DPadR(7).
Radial AUDIO wedge = hold East (sc7) then tap A. AUDIO submenu rows:
MUSIC0 FANFARE1 SFX2 VOICE3 RESET_ALL4 BACK5 -> RESET ALL = 4x DPad-Down + A.
PlayState enum: PLAY_STANDBY=0, PLAY_INIT=1, PLAY_UPDATE=2, PLAY_PAUSE=100.
gPauseEnabled @ 0x801964cc (latches true ~1s after level intro; pause needs it).
gPracticeMenuState: 0 closed, 1 open, 2 open_frozen.

### TEST-INFRA FIX (committed in working tree, do not lose)
`tools/m64p_test_runner.py` parse_symbols(): the PracticeConfig field-offset
parser used to treat ANY `}` as the struct end. The field
`s32 enemyHealthMinHp; /* one of {0,1,5,10,25,50} */` has a `}` in its COMMENT,
so the parser stopped early and never saw volBgm/volFanfare/volSfx/volVoice
(KeyError 'volBgm'). Fixed by stripping /* */ and // comments before the `}`
check. The four vol fields parse at offsets 180-183. They are contiguous u8 in
one 32-bit word, so `config_field("volBgm")` returns the packed word
0x7F7F7F7F at defaults (see test_config_defaults). For a single lane, mask the
byte yourself.

## CURRENT TASK: four RED repro tests (in progress)

The user reported bugs and wants tests that are RED against current source
(they document bugs we have NOT fixed yet). DO NOT fix the source — only
write tests and confirm their red/green status empirically.

Bug 1 — "audio frozen after pause→load":
  Repro: start level, save state, PAUSE (START), load state. Audio stays
  frozen after load.
  Mechanism (CONFIRMED): `src/engine/fox_play.c` pause sets
  `gPlayState = PLAY_PAUSE`; `src/engine/fox_game.c Game_Update` freezes the
  world/audio while paused. NOTHING in the load path writes gPlayState back
  to PLAY_UPDATE, so load-while-paused stays frozen.
  Signal to assert: `gPlayState` should become `PLAY_UPDATE` (=2) after load.

  - Test A (`tests/test_audio_frozen_after_pause_load.py`, ALREADY WRITTEN):
    do the repro, assert game resumes (gPlayState==PLAY_UPDATE) after load.
    EXPECTED RED. <-- needs a clean run to confirm.
  - Test B (NOT YET WRITTEN): same repro, then use the AUDIO submenu RESET
    to fix it, assert resumed. ALSO EXPECTED RED (reset doesn't touch
    gPlayState today; nothing fixes the freeze yet).

Bug 2 — "load / restart must NOT change my audio levels":
  Repro: set non-default lane volumes via the AUDIO submenu, then load a
  state (Test C) or restart the level via the menu (Test D). The volumes the
  user set should NOT change.
  - Test C (load): user's framing says this is a bug. My static read: load
    does not touch `gPracticeConfig.vol*`; the APPLIED volume
    (`gSeqPlayers[lane].fadeVolume`) is what reverts. User chose to assert on
    APPLIED/audible volume. Expectation uncertain — CONFIRM EMPIRICALLY.
  - Test D (restart): restart re-applies audio spec which resets applied
    volume to 0x7F. EXPECTED RED on the applied-volume layer.
  User decision if a test comes up GREEN: KEEP it as a green regression test.

### Decisions already made (from AskUserQuestion)
- Freeze signal for A/B: use `gPlayState` (PLAY_UPDATE==2). (NOTE: I had
  earlier wrongly suggested a nonexistent `gAudioEngineActiveFlag` — it does
  NOT exist in the source. Do not use it.)
- Volume layer for C/D: assert on APPLIED/audible volume
  (`gSeqPlayers[lane].fadeVolume`), not the config fields.
- If C is green: keep as a passing regression test.

## TEST HARNESS FACTS (m64p, keyboard-only)

Runner: `python3 tools/m64p_test_runner.py <test_name>` (run from the
WORKTREE dir, NOT /tmp). Harness source `tools/m64p_harness.c`, recompiled
automatically when newer than the binary.

- Keyboard-only input. NO analog-stick injection — cannot hover a radial
  wedge (needs stick_x). Keymap (SDL keycodes), already complete:
    START=13, A=120, B=99, Z Trig=122,
    DPad R=100, DPad L=97, DPad D=115, DPad U=119,
    C R=108, C L=106, C D=107, C U=105,
    R Trig=101, L Trig=113.
  (X Axis=key(97,100) and Y Axis=key(115,119) share WASD with the d-pad.)
- Practice input bindings (`src/practice/practice_input.c`):
    PACTION_OPEN_MENU   = R_CBUTTONS + START   (C-Right=108 + START=13)
    PACTION_SAVE_POS    = L_TRIG + L_JPAD      (L Trig=113 + DPad L=97)
    PACTION_RESTORE_POS = L_TRIG + R_JPAD      (L Trig=113 + DPad R=100)
  `Practice_InputTriggered` accepts the combo in either press order.
- ctx API (TestContext): `ctx.harness.key_down(kc)/key_up(kc)`,
  `ctx.harness.advance(n)`, `ctx.harness.read32(addr)` (returns unsigned;
  use `ctx.read_s32(addr)` for signed), `ctx.write_s32`, `ctx.read_float`,
  `ctx.wait_for_level_select()`, `ctx.wait_for_gameplay()`,
  `ctx.wait_for_play_update()`, `ctx.select_and_launch_level(0)` (presses A
  on level select to launch Corneria), `ctx.config_field(name)`,
  `ctx.set_config_field(name,val)`, `ctx.assert_eq/assert_neq/assert_true`.
- `ctx.syms.addrs[name]` = symbol RDRAM addr (already masked &0x1FFFFFFF in
  read path). `ctx.syms.const`: GSTATE_MAP=4, GSTATE_PLAY=7, PLAY_INIT=0,
  PLAY_UPDATE=2. `ctx.syms.config[name]` = PracticeConfig field offset.
- Reproducing the menu: open the AUDIO submenu by combos is HARD without
  the stick. For tests that must touch lane volume, prefer
  `ctx.set_config_field("volBgm", N)` then exercise the apply/restore path,
  OR open the state submenu another way. (Test A only needs save/pause/load
  combos, all keyboard-doable.)

### Key symbol addresses (from build/starfox64.us.rev1.map)
    gPlayState        0x80196364   (PLAY_UPDATE = 2)
    gGameState        0x80196344   (GSTATE_PLAY = 7, GSTATE_MAP = 4)
    gGameFrameCount   0x801968c8
    gPracticeConfig   0x80197588
    gSeqPlayers       0x8016d038   (SequencePlayer stride 0x14C;
                                    .fadeVolume at +0x1C, f32)
Always re-read these from the map after a rebuild; they shift.

## PROCESS RULES (learned the hard way this session)

- Run ONE command at a time. Do NOT fire large parallel batches — if any one
  call errors, the harness cancels the whole batch (we lost a huge batch this
  way).
- Do NOT `cd /tmp` for python: /tmp has a `.tool-versions` pinning `python`
  with no `python3` shim, so `python3` fails there. Run from the worktree.
- Do NOT use base64/python encoding gymnastics to read files — use the Read
  tool. (Earlier output buffering led me down that path; it caused the batch
  failure.)
- Build/test commands: from the worktree dir. `make practice -j4`,
  `python3 tools/practice_invariants.py`, `python3 tools/m64p_test_runner.py
  <name>`. Do not run the FULL m64p suite unless asked — user finds it
  overkill pre-merge; running just the audio/save-related tests is fine.
- Never `make clean` / `make init`.

## NEXT STEPS
1. Run Test A cleanly, confirm RED (final assert fails: gPlayState not
   PLAY_UPDATE after load-while-paused). Sanity asserts should pass.
2. Write Test B (repro + AUDIO reset), confirm RED.
3. Write Tests C and D (load / restart vs. applied lane volume), report
   actual red/green. Keep greens as regression tests.
4. Report all four statuses to the user. Do NOT fix source unless asked.
