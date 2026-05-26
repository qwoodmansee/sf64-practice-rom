"""Test: all PracticeConfig fields have expected defaults after boot.

Catches the case where a new field is added to PracticeConfig but not
initialized in Practice_Init(). On N64, uninitialized BSS is zeroed,
so bools would appear false accidentally. This test documents the
intended defaults.
"""

def run(ctx):
    ok = ctx.wait_for_level_select()
    ctx.assert_true(ok, "Booted to level select")

    # Loadout
    ctx.assert_eq(ctx.config_field("laserStrength"), 2, "laserStrength = LASERS_HYPER")
    ctx.assert_eq(ctx.config_field("bombCount"), 3, "bombCount = 3")
    ctx.assert_eq(ctx.config_field("lifeCount"), 2, "lifeCount = 2")

    # Team
    ctx.assert_eq(ctx.config_field("falcoAlive"), 1, "falcoAlive = true")
    ctx.assert_eq(ctx.config_field("slippyAlive"), 1, "slippyAlive = true")
    ctx.assert_eq(ctx.config_field("peppyAlive"), 1, "peppyAlive = true")

    # Display
    ctx.assert_eq(ctx.config_field("showInputDisplay"), 1, "showInputDisplay = true")
    ctx.assert_eq(ctx.config_field("skipCutscenes"), 1, "skipCutscenes = true")
    ctx.assert_eq(ctx.config_field("showHudOverlay"), 0, "showHudOverlay = false")
    ctx.assert_eq(ctx.config_field("showLagFrames"), 1, "showLagFrames = true")
    ctx.assert_eq(ctx.config_field("showSpeed"), 1, "showSpeed = true")
    ctx.assert_eq(ctx.config_field("showChargeTiming"), 0, "showChargeTiming = false")
    ctx.assert_eq(ctx.config_field("showMissedInputs"), 0, "showMissedInputs = false")
    ctx.assert_eq(ctx.config_field("showHitTracking"), 1, "showHitTracking = true")

    # Hitbox (all off)
    ctx.assert_eq(ctx.config_field("showHitboxes"), 0, "showHitboxes = false")
    ctx.assert_eq(ctx.config_field("showHitboxActors"), 0, "showHitboxActors = false")
    ctx.assert_eq(ctx.config_field("showHitboxScenery"), 0, "showHitboxScenery = false")
    ctx.assert_eq(ctx.config_field("showHitboxItems"), 0, "showHitboxItems = false")
    ctx.assert_eq(ctx.config_field("showHitboxPlayer"), 0, "showHitboxPlayer = false")
    ctx.assert_eq(ctx.config_field("showHitboxFlash"), 0, "showHitboxFlash = false")
    ctx.assert_eq(ctx.config_field("showSpawnZones"), 0, "showSpawnZones = false")
    ctx.assert_eq(ctx.config_field("expertMode"), 0, "expertMode = false")
