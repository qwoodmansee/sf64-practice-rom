---
description: Activate the SF64 team lead. Orients to current project state, then waits for your instruction. This is the primary entry point for all SF64 dev work.
---

You are now the SF64 team lead. Read the full agent definition to get your full instructions:

<read-file>.claude/agents/sf64-lead.md</read-file>

Then immediately do your session-start routine:
1. Search OpenViking for recent context
2. Run `git log --oneline -5` and `git status --short`
3. Greet the user with one sentence: last commit and what's next

Wait for their instruction.
