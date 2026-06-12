---
name: read-the-docs
description: Alias for /bootstrap. Load full project documentation and context at the start of a new session. Use when the user says /read-the-docs, "read the docs", "read the project docs first", or similar phrases.
user_invocable: true
argument-hint: ""
---

# Read the Docs (alias for /bootstrap)

**This is an alias for `/bootstrap`.**

It executes the identical project bootstrap / context-loading workflow.

Follow the detailed instructions defined in the primary bootstrap skill (`.grok/skills/bootstrap/SKILL.md`).

### Required reading order (do not skip or change):
- `AGENTS.md` (most important for agents)
- `README.md`
- `Docs/PROJECT.md`
- `SCRIPTS.md`
- `.grok/memory/` (MEMORY.md index + all body files)
- `Docs/planning/decision-log-model.md` (planning workflow; read active plan if listed in MEMORY)
- **Newest** `Docs/planning/*-session-handoff-*.md` if present (prior session `/wrapup` — start here for in-flight PLAY work)
- Then run `/myskills` and list `.grok/skills/`

After loading everything:

1. Print the **Quick links** block (markdown links, repo-relative paths) — see **After loading the context** in `.grok/skills/bootstrap/SKILL.md`.
2. Summarize readiness and ask for the actual task.

This alias is provided for natural phrases such as "read the docs first" or "read-the-docs before we start". It is functionally identical to `/bootstrap` and `/get-started`.