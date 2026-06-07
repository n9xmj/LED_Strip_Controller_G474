---
name: update-docs
description: Update or refresh project documentation including AGENTS.md (agent rules, style, architecture), Docs/PROJECT.md (goals, status, TODOs), README.md, SCRIPTS.md, skill descriptions in .grok/skills/, platform.h versioning, and related files. Use after completing features, architecture changes, automation updates, or when the user says "update the docs", "refresh documentation", "document what we just did", "/update-docs", or similar. Also handles the /docs alias.
user_invocable: true
argument-hint: "[optional: brief summary of recent changes or work done]"
---

# Update Documentation Skill

This skill helps keep the project's documentation in sync after development work. It covers both human-facing docs (`Docs/PROJECT.md`, `README.md`) and agent-targeted docs (`AGENTS.md`), plus supporting files like `SCRIPTS.md` and individual skill definitions.

**Primary files to consider updating:**
- `AGENTS.md` (root) — coding rules, architecture, invariants, sources of truth, work summary, agent workflow.
- `Docs/PROJECT.md` — project description, status, goals, TODO checklist, hardware, milestones.
- `README.md` (root) — high-level landing page and links.
- `SCRIPTS.md` — if automation behavior, flags, or patterns changed.
- `.grok/skills/*/SKILL.md` files — especially their `description` frontmatter (affects auto-invocation and `/myskills` listing).
- `App/Inc/platform.h` — if `FIRMWARE_VERSION` or `BUILD_NUMBER` semantics changed (though use `/setver` for the actual bump).
- Any other `Docs/*.md` that received new reference material.

## Workflow

1. **Gather context on what changed**
   - Run `git status` and `git diff --name-only` (or a limited range like `git diff --name-only HEAD~3..HEAD`) to see recently modified files.
   - If the session has recent tool use (edits, builds, tests), summarize the work performed.
   - Ask the user for a short description of the task/feature if not obvious (e.g. "added I2S mic input support" or "refined the smoke test banner extraction").

2. **Review current documentation**
   - Read the key files listed above (use `read_file` tool, focusing on relevant sections).
   - Check the frontmatter `description` fields of existing skills via `list_dir .grok/skills` + reading their SKILL.md.
   - Identify gaps: missing mentions of new behavior, outdated status/TODO items, new invariants that belong in AGENTS.md, new user-facing details for PROJECT.md, etc.

3. **Plan the updates (use todo_write)**
   - Create a todo list with items like:
     - Update AGENTS.md with new architecture rule for X
     - Refresh status table in Docs/PROJECT.md
     - Add entry to TODO checklist or mark item complete
     - Improve description in .grok/skills/newfeature/SKILL.md
     - Update SCRIPTS.md example if command behavior changed
   - Prioritize and get user confirmation on the plan.

4. **Propose and apply changes**
   - For each item, draft the exact text addition/replacement.
   - Use `search_replace` (preferred for precision) or `write` (for larger new sections) to make the edit.
   - Show the user the diff-like before/after or the tool call result.
   - Only commit edits the user approves.

5. **Verification & next steps**
   - Re-read the updated sections to confirm.
   - Suggest running `/myskills` (or the dynamic list) to verify skill descriptions look good.
   - Recommend a commit message such as "docs: update for <feature> (AGENTS.md, PROJECT.md, ...)" 
   - If a new skill was created as part of the work, remind the user that its own SKILL.md should have been created with a good `description`.

## Best Practices for This Skill

- Be specific. Vague updates like "the project now does audio" are less useful than "added i2s_mic_input module with 24b→16b path; updated AGENTS.md architecture section and PROJECT.md TODOs".
- Keep AGENTS.md focused on **rules and invariants** an agent must obey.
- Keep Docs/PROJECT.md focused on **what the project is, where it is, and where it's going** (status, goals, visuals, checklists).
- If only one or two files need touching, do a targeted update rather than a giant rewrite.
- After major refactors, consider whether a new skill (or enhancement to an existing one) should have its description updated so `/myskills` and natural-language triggers work well.
- This skill is intentionally interactive — documentation quality benefits from human review.

## Collision / Alias Notes

- This skill is registered as `update-docs`.
- The sibling `docs` skill acts as an alias (see `.grok/skills/docs/SKILL.md`). Typing `/docs` will load equivalent behavior.
- If a future built-in command ever conflicts, use the qualified form `/local:update-docs` (project-local skills live under this repo's `.grok/skills/`).

Run this skill with `/update-docs` (or `/docs`).

When the user has just finished meaningful work, proactively suggest running this skill.