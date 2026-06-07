#include "practice.h"
#include "sf64audio_external.h"

#ifdef PRACTICE_ROM

/* Per-type audio volume control for the practice ROM.
 *
 * Star Fox 64's master-volume layer (audio_general.c) tracks three user
 * volumes in sVolumeSettings[]: AUDIO_TYPE_MUSIC (which covers BOTH the BGM
 * and FANFARE sequence players), AUDIO_TYPE_SFX, and AUDIO_TYPE_VOICE, each on
 * a 0..99 scale. Audio_SetVolume() writes that array and re-applies it via
 * Audio_RestoreVolumeSettings(), the same path the stock options menu uses.
 *
 * We deliberately set the user volume through this layer rather than issuing a
 * raw per-player SEQCMD_SET_SEQPLAYER_VOLUME. The applied volume is recomputed
 * as a product of per-player fade modifiers every time any fade fires; a raw
 * SEQCMD only sets the (transient) target and is clobbered by the next fade --
 * including the Audio_RestoreVolumeSettings() that runs during the level-start
 * audio reset (Audio_RestartSeqPlayers), which is why an earlier per-player
 * approach lost the user's mix on every restart. Because sVolumeSettings[]
 * survives a GSTATE_PLAY restart (only GSTATE_INIT reloads it from the save
 * file), the engine's own reset restore re-asserts OUR value -- no race, and
 * an in-level music duck now ducks relative to the configured level instead of
 * snapping back to full.
 *
 * One wrinkle: Audio_SetVolume applies MUSIC at the sequence-player level (it
 * fades the BGM/FANFARE mainVolume), but applies SFX/VOICE at the per-channel
 * level. A level transition's Audio_FadeOutAll() zeroes every sequence-player
 * mainVolume, and the SFX/VOICE channel-set does NOT restore it -- so after a
 * same-spec restart SFX/VOICE would stay silent. We therefore also restore the
 * SFX/VOICE sequence-player mainVolume to full, exactly as the engine's own
 * Audio_RestartSeqPlayers does; the user's level rides on the channel layer.
 *
 * Lane indices are AudioType values (MUSIC/SFX/VOICE). FANFARE is not
 * independently controllable: the engine groups it under MUSIC. */

#define PRACTICE_AUDIO_VOL_MAX  99

static u8* Practice_Audio_TypeField(s32 type) {
    switch (type) {
        case AUDIO_TYPE_MUSIC:
            return &gPracticeConfig.volMusic;
        case AUDIO_TYPE_SFX:
            return &gPracticeConfig.volSfx;
        case AUDIO_TYPE_VOICE:
            return &gPracticeConfig.volVoice;
        default:
            return NULL;
    }
}

u8 Practice_Audio_GetVolume(s32 type) {
    u8* field = Practice_Audio_TypeField(type);

    if (field == NULL) {
        return PRACTICE_AUDIO_VOL_MAX;
    }
    return *field;
}

void Practice_Audio_ApplyLane(s32 type) {
    u8* field = Practice_Audio_TypeField(type);

    if (field == NULL) {
        return;
    }
    Audio_SetVolume((u8) type, *field);

    /* SFX/VOICE master rides on the channel layer; restore the sequence-player
     * mainVolume that a transition fade-out may have zeroed (mirrors
     * Audio_RestartSeqPlayers). MUSIC's mainVolume is handled by Audio_SetVolume
     * above. */
    if (type == AUDIO_TYPE_SFX) {
        SEQCMD_SET_SEQPLAYER_VOLUME(SEQ_PLAYER_SFX, 0, 0x7F);
    } else if (type == AUDIO_TYPE_VOICE) {
        SEQCMD_SET_SEQPLAYER_VOLUME(SEQ_PLAYER_VOICE, 0, 0x7F);
    }
}

void Practice_Audio_ApplyAll(void) {
    Practice_Audio_ApplyLane(AUDIO_TYPE_MUSIC);
    Practice_Audio_ApplyLane(AUDIO_TYPE_SFX);
    Practice_Audio_ApplyLane(AUDIO_TYPE_VOICE);
}

void Practice_Audio_SetVolume(s32 type, s32 vol) {
    u8* field = Practice_Audio_TypeField(type);

    if (field == NULL) {
        return;
    }
    if (vol < 0) {
        vol = 0;
    }
    if (vol > PRACTICE_AUDIO_VOL_MAX) {
        vol = PRACTICE_AUDIO_VOL_MAX;
    }
    *field = (u8) vol;
    Practice_Audio_ApplyLane(type);
}

void Practice_Audio_AdjustVolume(s32 type, s32 delta) {
    Practice_Audio_SetVolume(type, (s32) Practice_Audio_GetVolume(type) + delta);
}

void Practice_Audio_ResetLane(s32 type) {
    Practice_Audio_SetVolume(type, PRACTICE_AUDIO_VOL_MAX);
}

void Practice_Audio_ResetAll(void) {
    Practice_Audio_ResetLane(AUDIO_TYPE_MUSIC);
    Practice_Audio_ResetLane(AUDIO_TYPE_SFX);
    Practice_Audio_ResetLane(AUDIO_TYPE_VOICE);
}

#endif
