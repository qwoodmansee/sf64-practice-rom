#ifndef PRACTICE_SAVE_TAGS_H
#define PRACTICE_SAVE_TAGS_H

/* Phase 4 — TLV tag registry for practice save state.
 *
 * STABLE FOREVER. Removed entries become // REMOVED comments and the
 * numeric value is reserved for life. Tag IDs may not be reused.
 * The static invariant check_tag_registry enforces this.
 *
 * Numeric blocks:
 *   0x0001 .. 0x000F   Header / overlay metadata (cross-scene routing)
 *   0x0010 .. 0x001F   Audio (Phase 4 emit-only; Phase 5 turns on apply)
 *   0x0020 .. 0x003F   Bulk arrays (one tag per array, raw-bcopy payload)
 *   0x0040 .. 0x00FF   Scalars (one tag per PracticeScalarState field)
 *
 * Append new entries at the end of the appropriate block; never reorder.
 */
typedef enum {
    /* Header / overlay metadata. */
    TAG_LEVEL_ID                       = 0x0001,
    TAG_LEVEL_PHASE                    = 0x0002,
    TAG_OVERLAY_BUILD_ID               = 0x0003,
    TAG_OVERLAY_VRAM                   = 0x0004,
    TAG_OVERLAY_BYTES                  = 0x0005,
    TAG_SEGMENTS                       = 0x0006,

    /* Audio (emit-only in Phase 4; Phase 5 turns on apply). */
    TAG_AUDIO_SEQ_ID                   = 0x0010,
    TAG_AUDIO_SPEC_PACKED              = 0x0011,
    TAG_AUDIO_BANK_VOICE               = 0x0012, /* reserved; emitted as 0 in Phase 4 */

    /* Bulk arrays — payload is the raw bcopy bytes of the whole array. */
    TAG_PLAYER_ARRAY                   = 0x0020,
    TAG_ACTORS                         = 0x0021,
    TAG_BOSSES                         = 0x0022,
    TAG_SCENERY                        = 0x0023,
    TAG_SPRITES                        = 0x0024,
    TAG_EFFECTS                        = 0x0025,
    TAG_ITEMS                          = 0x0026,
    TAG_PLAYER_SHOTS                   = 0x0027,
    TAG_TEXTURED_LINES                 = 0x0028,
    TAG_RADAR_MARKS                    = 0x0029,
    TAG_BONUS_TEXT                     = 0x002A,

    /* Scalars — one tag per field in PracticeScalarState (declared in
     * src/practice/practice_save.c). Order matches the struct so the
     * Wave 2.2 callbacks can walk fields and tags in lockstep. */
    TAG_PATH_PROGRESS                  = 0x0040,
    TAG_SAVED_PATH_PROGRESS            = 0x0041,
    TAG_OBJECT_LOAD_INDEX              = 0x0042,
    TAG_SAVED_OBJECT_LOAD_INDEX        = 0x0043,
    TAG_PATH_VEL_Z                     = 0x0044,
    TAG_PATH_VEL_X                     = 0x0045,
    TAG_PATH_VEL_Y                     = 0x0046,
    TAG_PATH_GROUND_SCROLL             = 0x0047,
    TAG_PATH_TEX_SCROLL                = 0x0048,
    TAG_GROUND_HEIGHT                  = 0x0049,
    TAG_WATER_LEVEL                    = 0x004A,
    TAG_GROUND_CLIP_MODE               = 0x004B,
    TAG_GROUND_TYPE                    = 0x004C,
    TAG_GROUND_SURFACE                 = 0x004D,
    TAG_SAVED_GROUND_SURFACE           = 0x004E,
    TAG_LEVEL_MODE                     = 0x004F,
    /* Distinct from the 0x0002 header tag: the header value is metadata
     * for cross-scene routing; this is the live gLevelPhase scalar at
     * save time. The Wave 2.2 callbacks emit both. */
    TAG_SCALAR_LEVEL_PHASE             = 0x0050,
    TAG_LOAD_LEVEL_OBJECTS             = 0x0051,

    TAG_LASER_STRENGTH                 = 0x0052,
    TAG_BOMB_COUNT                     = 0x0053,
    TAG_LIFE_COUNT                     = 0x0054,
    TAG_CHARGE_TIMERS                  = 0x0055,
    TAG_SHIELD_TIMER                   = 0x0056,
    TAG_HAS_SHIELD                     = 0x0057,
    TAG_PLAYER_FORMS                   = 0x0058,

    TAG_HIT_COUNT                      = 0x0059,
    TAG_DISPLAYED_HIT_COUNT            = 0x005A,
    TAG_RING_PASS_COUNT                = 0x005B,

    TAG_TEAM_SHIELDS                   = 0x005C,
    TAG_TEAM_DAMAGE                    = 0x005D,
    TAG_STAR_WOLF_TEAM_ALIVE           = 0x005E,
    TAG_SAVED_STAR_WOLF_TEAM_ALIVE     = 0x005F,
    TAG_RIGHT_WING_HEALTH              = 0x0060,
    TAG_LEFT_WING_HEALTH               = 0x0061,
    TAG_FORMATION_LEADER_INDEX         = 0x0062,

    TAG_PLAY_CAM_EYE                   = 0x0063,
    TAG_PLAY_CAM_AT                    = 0x0064,
    TAG_CS_CAM_EYE_X                   = 0x0065,
    TAG_CS_CAM_EYE_Y                   = 0x0066,
    TAG_CS_CAM_EYE_Z                   = 0x0067,
    TAG_CS_CAM_AT_X                    = 0x0068,
    TAG_CS_CAM_AT_Y                    = 0x0069,
    TAG_CS_CAM_AT_Z                    = 0x006A,
    TAG_CAMERA_SHAKE_Y                 = 0x006B,
    TAG_CAMERA_SHAKE                   = 0x006C,
    TAG_CAM_COUNT                      = 0x006D,
    TAG_FOV_Y                          = 0x006E,
    TAG_PROJECT_NEAR                   = 0x006F,
    TAG_PROJECT_FAR                    = 0x0070,

    TAG_GAME_FRAME_COUNT               = 0x0071,
    TAG_CS_FRAME_COUNT                 = 0x0072,
    TAG_LEVEL_CLEAR_SCREEN_TIMER       = 0x0073,
    TAG_LEVEL_START_STATUS_SCREEN_TIMER= 0x0074,
    TAG_BOSS_HEALTH_BAR                = 0x0075,
    TAG_BOSS_ACTIVE                    = 0x0076,
    TAG_ALL_RANGE_EVENT_TIMER          = 0x0077,
    TAG_ALL_RANGE_FRAME_COUNT          = 0x0078,
    TAG_ALL_RANGE_SPAWN_EVENT          = 0x0079,
    TAG_ALL_RANGE_CHECKPOINT           = 0x007A,
    TAG_ALL_RANGE_COUNTDOWN            = 0x007B,
    TAG_SHOW_ALL_RANGE_COUNTDOWN       = 0x007C,
    TAG_BOSS_FRAME_COUNT               = 0x007D,

    TAG_SHOW_HUD                       = 0x007E,
    TAG_SHOW_RETICLES                  = 0x007F,
    TAG_FILL_SCREEN_ALPHA              = 0x0080,
    TAG_FILL_SCREEN_RED                = 0x0081,
    TAG_FILL_SCREEN_GREEN              = 0x0082,
    TAG_FILL_SCREEN_BLUE               = 0x0083,
    TAG_FILL_SCREEN_ALPHA_TARGET       = 0x0084,
    TAG_FILL_SCREEN_ALPHA_STEP         = 0x0085,

    TAG_RADIO_STATE                    = 0x0086,
    TAG_RADIO_STATE_TIMER              = 0x0087,
    TAG_RADIO_MSG_ID                   = 0x0088,

    TAG_KILL_EVENT_ACTORS              = 0x0089,
    TAG_PREV_EVENT_ACTOR_INDEX         = 0x008A,

    TAG_BGM_SEQ_ID                     = 0x008B
} practice_save_tag_t;

#endif /* PRACTICE_SAVE_TAGS_H */
