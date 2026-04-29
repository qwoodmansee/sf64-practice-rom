# SF64 Practice ROM — Agent Team

## Start here

**From any Claude Code session in this repo — just type:**
```
/lead
```

That's it. Works in the terminal (`claude`), in Cursor, anywhere Claude Code is running.
The lead orients to current project state and waits for your instruction.

> **Cursor agent picker**: if you start a new chat and see an agent dropdown,
> `sf64-lead` will appear there too — same behavior, different entry point.

---

## Worker agents (dispatched by the lead)

The lead spawns these automatically. Only open them directly if you want focused, single-purpose output.

| Agent | Model | Job |
|-------|-------|-----|
| `sf64-builder` | haiku | `make practice -j4` → PASS/FAIL |
| `sf64-checker` | haiku | Invariants + lib tests + build smoke |
| `sf64-coder` | sonnet | Write/edit practice C code |
| `sf64-reviewer` | haiku | 10-item N64-specific diff review |

---

## Slash commands (use in any Claude Code chat)

These are standalone — they don't go through the lead.

| Command | What it does |
|---------|-------------|
| `/check` | Full verification pipeline |
| `/new-feature <name> "<desc>"` | 8-step new feature checklist |
| `/phase-wave [plan-path]` | Execute next uncommitted plan wave |
| `/hw-verify [plan-path]` | Print SC64 hardware verification procedure |

---

## Typical sessions

**Continuing Phase N work (most common):**
> Open sf64-lead → say "continue" or "what's next"

The lead reads the plan, finds the next wave, dispatches sf64-coder, then sf64-checker, then reports back with a suggested commit message.

**Adding a new HUD field or counter:**
> Open sf64-lead → "add a feature that tracks X"

Lead confirms scope, dispatches sf64-coder with the 8-step checklist, verifies, reports.

**Quick build check (no lead needed):**
> `/check`

**Hardware audit step:**
> `/hw-verify` or ask the lead — it detects hardware-gated waves automatically

---

## Project skills (`.claude/skills/`)

- **`debug-ram-layout`** — linker overlap / dynamic load window vs practice BSS.
- **`practice-hw-isv-trace`** — SC64 + IS-Viewer save/load bracketing (`PRACTICE_SAVE_TRACE=1`), `gPracticeScreen` vs engine state, static `PracticeSnapshot` scratch (no giant stack locals).

## Design principles

- **CLAUDE.md is the spec.** Agents reference it — they don't duplicate it. Keep CLAUDE.md up to date and the agents stay accurate.
- **haiku for verification, sonnet for authoring.** Checker/builder/reviewer need no judgment. Coder and lead do.
- **OpenViking on every session start.** Cross-session context without re-deriving decisions.
- **Never auto-commit.** Every flow stops and asks you to review + commit.
