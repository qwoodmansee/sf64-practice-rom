"""Test: Selecting a CHECKPOINT phase starts gameplay at checkpoint progress position.

Navigates to Corneria, cycles to the CHECKPOINT phase, launches, and
verifies gPathProgress is set to the checkpoint position rather than 0.
"""

def run(ctx):
    S = ctx.syms

    ok = ctx.wait_for_level_select()
    ctx.assert_true(ok, "Booted to level select")

    # Corneria is index 0 (already selected). Press Right to cycle to CHECKPOINT phase.
    ctx.press_right()
    ctx.advance(2)

    # Launch with A
    ok = ctx.select_and_launch_level(0, press_only=True)
    ctx.assert_true(ok, "Launched with checkpoint phase")

    ok = ctx.wait_for_gameplay(600)
    ctx.assert_true(ok, "Gameplay became active")

    # gPathProgress should be at the checkpoint position (~93610 + 2750 = ~96360)
    path_progress = ctx.read_float(S["gPathProgress"])
    ctx.assert_true(path_progress > 50000.0, "gPathProgress at checkpoint (>50000)")
    ctx.assert_true(path_progress < 200000.0, "gPathProgress reasonable (<200000)")

    # gPracticeCheckpointProgress should be cleared after use
    ckpt_prog = ctx.read_float(S["gPracticeCheckpointProgress"])
    ctx.assert_eq(ckpt_prog, 0.0, "gPracticeCheckpointProgress cleared after use")
