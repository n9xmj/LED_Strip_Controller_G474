---
name: bootstrap
description: Load full project documentation and context at the start of a new session or whenever the user wants the agent brought up to speed. Use when the user says /bootstrap, "bootstrap the project", "get me started", "load the docs", "read the project docs", or similar. This is the recommended way to begin work after opening a fresh session.
user_invocable: true
argument-hint: ""
---

# Bootstrap / Get Project Context

This skill ensures the agent has the correct, current project documentation loaded before any real work begins. It is the canonical way to "get started" in a new session.

**Priority order for reading (always follow this):**

1. **AGENTS.md** (root) — This is the primary contract for AI agents. It contains non-negotiable rules (CubeMX regeneration, USER CODE discipline, architecture invariants, sources of truth, how to work in this repo). Read this first and treat its contents as highest priority.

2. **README.md** (root) — Short project landing page with links to the main docs.

3. **Docs/PROJECT.md** — Human-facing project description, current status, goals, TODO checklist, hardware layout, and milestones attained. Use this to understand what the project is trying to achieve and what is already done.

4. **SCRIPTS.md** — General reference for the automation scripts (build, flash, smoke/probe, discovery). Note that bench-specific defaults live in the local skills, not here.

5. Read the `.grok/memory/MEMORY.md` **index lines** and the always-relevant body file
   [`user_conversational_tone.md`](../../memory/user_conversational_tone.md) (tone + workflow prefs).
   **Do not** bulk-read the other memory body files — they are topic deep-dives (I2S notes,
   handoffs, planning model). Load those **on demand** per the AGENTS.md **Topic Map** when the
   user names a focus.

6. **No assumed focus.** Do **not** auto-read the PLAY plan, `decision-log-model.md`, or any
   `*-session-handoff-*.md` at session start — that burns context on a topic the user may not be
   working today. Read topic deep-dives only when the user names the area (e.g. *"PLAY v2"*,
   *"I2S audio"*, *"&lt;wishlist item&gt;"*) — follow the **Topic Map** in AGENTS.md (*Session
   start — no assumed focus*). If an argument/handoff explicitly names a topic, load that topic's
   docs as part of bootstrap.

7. Discover available project-local commands by invoking the `myskills` skill (or explicitly running `/myskills`).

8. (Strongly recommended) Use `list_dir` on `.grok/skills/` to see all available skills and their SKILL.md files.

## After loading the context

- Print a **Quick links** block first (see below) — clickable markdown paths.
- Briefly summarize the **general** project state (what it is, what shipped, where the roadmap/TODO
  lives) — **without** assuming a focus for this session.
- Echo the **Topic Map** (or a one-line pointer to it in AGENTS.md) so the user can name an area
  and you'll pull the right docs.
- State clearly: "Project context loaded, no focus assumed. AGENTS.md rules are in effect. Tell me
  the area (PLAY, I2S audio, a wishlist item, …) and I'll pull its docs."
- Then wait for the actual request.

### Quick links (required)

Use **markdown links** with **repo-relative paths** from the project root (Cursor makes these clickable):

```markdown
## Quick links

- [AGENTS.md](AGENTS.md) — agent contract + **Topic Map** (name an area to load its docs)
- [Docs/PROJECT.md](Docs/PROJECT.md) — status, roadmap, TODO / wishlist
- [SCRIPTS.md](SCRIPTS.md) — build / flash / smoke automation
- [.grok/memory/MEMORY.md](.grok/memory/MEMORY.md) — memory index
```

**Rules:**

- Default to the **general** links above — do not assume a topic.
- **Only when the user has named a focus** (or an argument/handoff did), add that topic's links
  on top (e.g. the newest `Docs/planning/*-session-handoff-*.md` and the PLAY plan for PLAY work,
  or `.grok/memory/inmp441_i2s_wiring.md` for I2S) per the AGENTS.md Topic Map.
- Resolve **newest** handoff by filename date (or mtime if ambiguous); omit links that don't exist.
- Do not use absolute Windows paths or bare backtick paths without link syntax — links are the UX goal.

## Best practices this skill enforces

- Never start making code changes, builds, or edits until the above documents have been read in the correct order.
- AGENTS.md always overrides older instructions or previous session memory.
- The split documentation exists for a reason: AGENTS.md for rules the agent must obey; Docs/PROJECT.md for goals, status, and human context.
- After significant work, the user should be encouraged to run `/update-docs` or `/docs` (see the update-docs skill).
- Before ending a long session, run **`/wrapup`** (commit WIP + session handoff for the next `/read-the-docs`).

## Usage

Run this skill at the very beginning of almost every new session with a simple:

```
/bootstrap
```

or

```
/get-started
```

The agent should then be ready to accept the real task (e.g. "I want to start working on I2S microphone input").

This skill can also be triggered naturally with phrases like "get the project context loaded", "bring me up to speed on the current state", or "read the docs first".

Do not skip or reorder the reading steps. The order matters.