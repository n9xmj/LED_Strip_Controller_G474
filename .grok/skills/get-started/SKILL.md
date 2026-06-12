---
name: get-started
description: Alias for /bootstrap. Load full project documentation and context at the start of a new session or whenever the user wants the agent brought up to speed. Use when the user says /get-started, "get me started", "bootstrap the project", "load the docs", "read the project docs", "read-the-docs", or similar.
user_invocable: true
argument-hint: ""
---

# Get Started (alias for /bootstrap)

**This is an alias for `/bootstrap`.**

It performs exactly the same project documentation loading workflow.

When this skill is invoked, follow the instructions in the primary `bootstrap` skill (see `.grok/skills/bootstrap/SKILL.md`).

### Quick reminder of the required sequence:
1. Read `AGENTS.md` (root) — highest priority agent contract.
2. Read `README.md` (root).
3. Read `Docs/PROJECT.md`.
4. Read `SCRIPTS.md`.
5. Read `.grok/memory/MEMORY.md` and every body file it indexes (project-local agent memory).
6. Read `Docs/planning/decision-log-model.md` (+ active plan from MEMORY); **newest** `*-session-handoff-*.md` if present.
7. Run `/myskills` (or invoke the myskills skill).
8. List the `.grok/skills/` directory.

After completing the sequence, give the user a brief readiness summary and wait for the actual work request.

Users can type either `/bootstrap` or `/get-started` (or natural language equivalents) to trigger this. Both do the same thing.

This alias exists so that common phrases like "get me started" or "get started on the project" feel natural.