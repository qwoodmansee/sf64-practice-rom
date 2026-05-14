"""Regression test: Corneria music must play after BGM preview on level select.

Steps to reproduce the original bug manually:
  1. Boot to level select (Corneria selected by default)
  2. Press L or R to cycle the BGM preview to Corneria (same level, same song)
  3. Press A to launch Corneria
  4. Music does not play — the level runs silently

Root cause (verified by reading audio engine source):
  The BGM preview plays NA_BGM_STAGE_CO = SEQ_ID_CORNERIA | SEQ_FLAG = 0x8002.
  SEQCMD_PLAY_SEQUENCE packs the high byte of the seqId (0x80) into seqArgs.
  Audio_ProcessSeqCmd interprets seqArgs >= 0x80 as "async font load required",
  sets sActiveSequences[BGM].isWaitingForFonts = true, and queues an async DMA.

  When the user presses A, Practice_LaunchLevel calls Audio_SetAudioSpec (same-spec
  path), which calls Audio_StopSequence(SEQ_PLAYER_BGM). This cancels the in-flight
  async font load without clearing isWaitingForFonts.

  isWaitingForFonts stays permanently true. Every subsequent AUDIO_PLAY_BGM call —
  from Practice_ApplyStartConditions AND from the rescue mechanism — is silently
  dropped at the isWaitingForFonts check in Audio_ProcessSeqCmd. The level runs
  silently.

  Fix: clear isWaitingForFonts before the BGM rescue fires in Practice_Save_Tick.

Test approach:
  Manufacture the stuck state: set isWaitingForFonts=1, wipe seqId to SEQ_ID_NONE,
  queue the BGM rescue, launch Corneria. After 400 gameplay frames the rescue has
  long since fired. Assert BGM is playing (seqId == NA_BGM_STAGE_CO).

  PASSES = bug fixed (BGM playing after rescue).
  FAILS  = bug present (isWaitingForFonts stuck, BGM silently dropped).
"""

# RDRAM offsets (addr & 0x1FFFFFFF) — confirmed from map + source
_GAME_STATE        = 0x00194944  # gGameState (s32)
_PLAY_STATE        = 0x00194964  # gPlayState (s32)
_NEXT_LEVEL_WORD   = 0x0017eb40  # gNextLevel(u16,hi16) | gNextGameState(u16,lo16)
_PRACTICE_SCREEN   = 0x00195b80  # gPracticeScreen (s32)
_BGM_PENDING       = 0x00195cc4  # gPracticeBgmPending (bool/s32)
_BGM_PENDING_SEQ   = 0x00195cc8  # gPracticeBgmPendingSeqId (u16 in hi16 of word)
_BGM_PENDING_DELAY = 0x00195ccc  # gPracticeBgmPendingDelay (s32)
_BGM_LAST_SPEC     = 0x000ed96c  # sBgmLastSpecPacked (static u16, hi16 of word)

# Audio engine internals — sActiveSequences[SEQ_PLAYER_BGM=0] fields
# sActiveSequences base: 0x80167ea8; seqId at +0x248, isWaitingForFonts at +0x254
_SEQ_BGM_SEQ_ID    = 0x001680f0  # sActiveSequences[0].seqId (u16, hi16 of word)
_SEQ_BGM_WAIT_FONT = 0x001680fc  # sActiveSequences[0].isWaitingForFonts (u8, hi byte of word)

SEQ_FLAG = 0x8000
GSTATE_MAP   = 4
GSTATE_PLAY  = 7
PLAY_UPDATE  = 2
PSCREEN_GAMEPLAY = 1

SEQ_ID_NONE = 0xFFFF
# NA_BGM_STAGE_CO = SEQ_ID_CORNERIA | SEQ_FLAG = 0x0002 | 0x8000 = 0x8002
NA_BGM_STAGE_CO = 0x8002
# The audio engine strips SEQ_FLAG before storing in sActiveSequences[].seqId
SEQ_ID_CORNERIA = NA_BGM_STAGE_CO & ~SEQ_FLAG  # 0x0002
CORNERIA_SPEC_PACKED = 0x0000  # (SFX_LAYOUT_DEFAULT<<8)|AUDIOSPEC_CO


def _read_u16_hi(harness, rdram_addr):
    """Read u16 from the upper 16 bits of the 4-byte-aligned word."""
    return (harness.read32(rdram_addr & ~3) >> 16) & 0xFFFF


def _write_u8_hi(harness, rdram_addr, value):
    """Write u8 into the most-significant byte of the 4-byte-aligned word."""
    word = harness.read32(rdram_addr & ~3)
    word = (word & 0x00FFFFFF) | ((value & 0xFF) << 24)
    harness.write32(rdram_addr & ~3, word)


def _write_u16_hi(harness, rdram_addr, value):
    """Write u16 into the upper 16 bits of the 4-byte-aligned word."""
    word = harness.read32(rdram_addr & ~3)
    word = (word & 0x0000FFFF) | ((value & 0xFFFF) << 16)
    harness.write32(rdram_addr & ~3, word)


def run(ctx):
    h = ctx.harness

    # --- Step 1: Boot to level select ---
    ok = h.wait_for(_GAME_STATE, GSTATE_MAP, 60000)
    ctx.assert_true(ok, "ROM booted to level select (GSTATE_MAP=4)")
    if not ok:
        return

    # Let the level select initialize fully (Practice_LevelSelect_OnEnter must run).
    h.advance(10)

    # --- Step 2: Manufacture the stuck audio state ---
    # Simulate what happens when the user presses L/R to preview Corneria BGM:
    #   1. AUDIO_PLAY_BGM(0x8002) is queued, seqArgs=0x80 triggers async font load
    #   2. isWaitingForFonts = true, async DMA in flight
    # Then the user presses A immediately (before DMA completes):
    #   3. Audio_StopSequence(BGM) is called — cancels DMA but does NOT clear isWaitingForFonts
    #   4. isWaitingForFonts is permanently stuck at true

    # Set isWaitingForFonts = true for BGM player
    _write_u8_hi(h, _SEQ_BGM_WAIT_FONT, 1)

    # Clear seqId to SEQ_ID_NONE (simulating Audio_StopSequence result)
    _write_u16_hi(h, _SEQ_BGM_SEQ_ID, SEQ_ID_NONE)

    # Set sBgmLastSpecPacked = Corneria spec (so same-spec check would trigger in
    # Practice_LaunchLevel if we were going through that path)
    _write_u16_hi(h, _BGM_LAST_SPEC, CORNERIA_SPEC_PACKED)

    # Verify our writes
    is_waiting = (h.read32(_SEQ_BGM_WAIT_FONT) >> 24) & 0xFF
    seq_id = _read_u16_hi(h, _SEQ_BGM_SEQ_ID)
    ctx.assert_eq(is_waiting, 1, "isWaitingForFonts set to 1 (blocking BGM)")
    ctx.assert_eq(seq_id, SEQ_ID_NONE, "BGM seqId cleared to SEQ_ID_NONE")

    # --- Step 3: Queue the BGM rescue (same as Practice_LaunchLevel would do) ---
    # gPracticeBgmPending (bool/s32) and gPracticeBgmPendingDelay (s32) are full words.
    # gPracticeBgmPendingSeqId (u16) lives in the upper 16 bits of its 4-byte word.
    h.write32(_BGM_PENDING, 1)                              # gPracticeBgmPending = true
    _write_u16_hi(h, _BGM_PENDING_SEQ, NA_BGM_STAGE_CO)   # gPracticeBgmPendingSeqId = 0x8002
    h.write32(_BGM_PENDING_DELAY, 3)                        # gPracticeBgmPendingDelay = 3 frames

    # --- Step 4: Launch Corneria (bypass Practice_LaunchLevel to preserve our stuck state) ---
    # Pack gNextLevel=0 (hi16) and gNextGameState=GSTATE_PLAY=7 (lo16) into one word.
    h.write32(_NEXT_LEVEL_WORD, (0 << 16) | GSTATE_PLAY)
    h.write32(_PRACTICE_SCREEN, PSCREEN_GAMEPLAY)

    # --- Step 5: Wait for active gameplay ---
    ok = h.wait_for(_GAME_STATE, GSTATE_PLAY, 30000)
    ctx.assert_true(ok, "Reached GSTATE_PLAY")
    if not ok:
        return
    ok = h.wait_for(_PLAY_STATE, PLAY_UPDATE, 15000)
    ctx.assert_true(ok, "Reached PLAY_UPDATE")
    if not ok:
        return

    # --- Step 6: Run 400 frames ---
    # The rescue fires after ~3 PLAY_UPDATE frames (gPracticeBgmPendingDelay counts down).
    # Practice_ApplyStartConditions also fires AUDIO_PLAY_BGM during PLAY_INIT.
    # Both calls are silently dropped by isWaitingForFonts. After 400 frames there
    # are no more rescue attempts.
    h.advance(400)

    # --- Step 7: Read final state ---
    bgm_pending = h.read32(_BGM_PENDING)
    seq_id_final = _read_u16_hi(h, _SEQ_BGM_SEQ_ID)
    is_waiting_final = (h.read32(_SEQ_BGM_WAIT_FONT) >> 24) & 0xFF

    # --- Step 8: Assert BGM is playing ---
    # Rescue must have fired (gPracticeBgmPending=0) and BGM must be playing.
    # If isWaitingForFonts was not cleared before the rescue, AUDIO_PLAY_BGM was
    # silently dropped and seqId stays SEQ_ID_NONE.
    ctx.assert_eq(bgm_pending, 0,
                  f"Rescue fired (gPracticeBgmPending=0 after 400 frames): {bgm_pending}")
    ctx.assert_eq(is_waiting_final, 0,
                  f"isWaitingForFonts must be cleared before rescue fires: {is_waiting_final}")
    ctx.assert_eq(seq_id_final, SEQ_ID_CORNERIA,
                  f"BGM must be playing after rescue — seqId should be 0x{SEQ_ID_CORNERIA:04X}: "
                  f"got 0x{seq_id_final:04X}")
