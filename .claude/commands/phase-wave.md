---
description: Execute the next uncommitted wave from a Phase plan doc. Pass the plan doc path as argument, e.g. /phase-wave docs/superpowers/plans/2026-04-27-phase4-practice-save-tlv-and-overlay.md
---

Execute one wave from a Phase plan document. Read → identify next wave → implement → verify → report. Do NOT advance past one wave per invocation.

## Step 1 — Locate the plan doc

If an argument was provided, use it. Otherwise look in `docs/superpowers/plans/` for the most recently modified plan:

```bash
ls -t docs/superpowers/plans/*.md | head -5
```

Read the plan doc fully before doing anything else.

## Step 2 — Find the next wave

Determine the phase number from the plan filename (e.g.
`2026-04-27-phase4-...md` → phase 4) and search the **entire** history of
the current branch for matching wave commits — never a fixed last-N
window. Once a phase has more than ~20 commits behind it, a `-20` cap will
silently re-classify already-done waves as pending and re-run them.

```bash
PHASE="<N>"  # e.g. 4
git log --oneline --grep="Phase ${PHASE}" --grep="phase${PHASE}" \
        --regexp-ignore-case --extended-regexp
```

Match commit messages against the wave names in the plan (e.g. "Phase 4
Wave 1", "Phase 4 Wave 2.1"). The next wave is the lowest-numbered one
with no matching commit. If the plan uses a non-default commit-message
convention, fall back to `git log --all --oneline | grep -iE "wave[ ._-]?N"`
across the whole log.

Report: "Found next wave: **Wave N.M — <title>**. Proceeding."

If all waves are committed, report that and stop.

## Step 3 — Read all files the wave touches

The plan lists files per wave. Read each one before making any edits.

Also search OpenViking for prior context:
```
mcp__openviking__search: "sf64 phase <N> wave <M> <keywords>"
```

## Step 4 — Implement

Follow the plan's file-by-file instructions exactly. Apply CLAUDE.md rules:
- `gPlayer` null guard on every access
- `bcopy` not `memcpy`
- All practice code inside `#ifdef PRACTICE_ROM`
- Naming conventions: `gCamelCase` / `sCamelCase` / `PascalCase` / `UPPER_CASE`

## Step 5 — Verify

Run the verification pipeline:

```bash
python3 tools/practice_invariants.py && make -C lib test && make practice -j4
```

If anything fails, fix it before proceeding to Step 6.

## Step 6 — Report and stop

Print a summary:
```
Wave N.M complete — <title>
Files changed: <list>
Verification: PASS
Next wave: Wave N.M+1 — <next title>

Run /check to re-confirm, then commit with:
  git add -p
  git commit -m "Phase N Wave N.M: <title>"
```

**Do not commit automatically.** Do not start the next wave. Let the user review and commit.

## Hardware verification waves

If the wave description says "heap audit", "hardware verify", "HW_VERIFY", or "IS-Viewer log required":

Stop and print:
```
Wave N.M requires hardware verification on real N64 hardware.

Procedure (from the plan doc):
1. Start: sc64deployer debug --isv 0x03FF0000
2. Upload ROM and hard reset
3. [steps from the plan's audit procedure]
4. Paste results into HW_VERIFY_phase<N>.md
5. Then run /phase-wave again to pin the constants.
```
