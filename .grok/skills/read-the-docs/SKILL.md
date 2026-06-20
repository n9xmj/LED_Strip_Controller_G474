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

### General standing layer (always — no assumed focus):
- `AGENTS.md` (most important for agents — includes the **Topic Map** + *Session start* policy)
- `README.md`
- `Docs/PROJECT.md`
- `SCRIPTS.md`
- `.grok/memory/MEMORY.md` **index** + `user_conversational_tone.md` only
- Then run `/myskills` and list `.grok/skills/`

### Topic deep-dives (on demand only):
- **Do not** bulk-read the other `.grok/memory/` bodies, `Docs/planning/decision-log-model.md`,
  the PLAY plan, or `*-session-handoff-*.md` at session start. Read those only when the user
  names a focus (PLAY, I2S audio, a wishlist item, …) — follow the AGENTS.md **Topic Map**. If
  an argument names a topic, load that topic's docs now.

After loading the general layer:

1. Print the **Quick links** block (general links; add topic links only once a focus is named) — see **After loading the context** in `.grok/skills/bootstrap/SKILL.md`.
2. Summarize readiness **without assuming a focus**, echo the Topic Map pointer, and ask what area to work.

This alias is provided for natural phrases such as "read the docs first" or "read-the-docs before we start". It is functionally identical to `/bootstrap` and `/get-started`.