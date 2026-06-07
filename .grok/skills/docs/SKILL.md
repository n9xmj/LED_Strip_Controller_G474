---
name: docs
description: Alias for /update-docs. Update or refresh project documentation including AGENTS.md (agent rules, style, architecture), Docs/PROJECT.md (goals, status, TODOs), README.md, SCRIPTS.md, skill descriptions in .grok/skills/, platform.h versioning, and related files. Use after completing features, architecture changes, automation updates, or when the user says "update the docs", "refresh documentation", "document what we just did", "/docs", or "/update-docs".
user_invocable: true
argument-hint: "[optional: brief summary of recent changes or work done]"
---

# Docs (alias for update-docs)

**This is an alias for `/update-docs`.**

It provides exactly the same documentation update workflow. Typing `/docs` is equivalent to `/update-docs`.

The full instructions, workflow, files to consider, and best practices are defined in the primary `update-docs` skill (see `.grok/skills/update-docs/SKILL.md`).

When this alias is invoked, follow the identical process:

1. Gather context on recent changes (git diff, recent edits, user summary of work).
2. Review the key living documents:
   - `AGENTS.md`
   - `Docs/PROJECT.md`
   - `README.md`
   - `SCRIPTS.md`
   - Individual skill `SKILL.md` files (especially their `description` frontmatter)
   - `App/Inc/platform.h` (version-related items)
3. Use `todo_write` to plan specific updates.
4. Propose precise edits and apply them with `search_replace` (or `write` for new content) after user approval.
5. Verify, suggest commit messages, and remind the user about keeping descriptions trigger-friendly for natural language + `/myskills`.

**Collision / qualified name note (for reference):**
- Project-local skills live in this repo's `.grok/skills/`.
- If a name ever collides with a built-in Grok TUI command, the built-in wins. The skill remains reachable via a qualified form such as `/local:docs` (project scope) if the system supports it for the collision.
- This alias (`docs`) and the primary (`update-docs`) were chosen because they do not collide with current built-ins.

Run this with `/docs` (or the primary `/update-docs`).

After meaningful work, it is good practice to invoke this alias (or the primary) so the human and agent documentation stay accurate and useful for the next session or collaborator.