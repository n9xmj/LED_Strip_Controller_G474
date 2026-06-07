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

5. Discover available project-local commands by invoking the `myskills` skill (or explicitly running `/myskills`).

6. (Strongly recommended) Use `list_dir` on `.grok/skills/` to see all available skills and their SKILL.md files.

## After loading the context

- Briefly summarize the current project state for the user (key completed work, active focus areas from the TODOs, preferred workflows via the skills).
- State clearly: "Project context loaded. AGENTS.md rules are in effect. I am ready for your task."
- Then wait for the actual request.

## Best practices this skill enforces

- Never start making code changes, builds, or edits until the above documents have been read in the correct order.
- AGENTS.md always overrides older instructions or previous session memory.
- The split documentation exists for a reason: AGENTS.md for rules the agent must obey; Docs/PROJECT.md for goals, status, and human context.
- After significant work, the user should be encouraged to run `/update-docs` or `/docs` (see the update-docs skill).

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