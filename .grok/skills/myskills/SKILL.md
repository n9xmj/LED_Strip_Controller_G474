---
name: myskills
description: List project-local skills (in this repo's .grok/skills/) with 1-liner usage explanations in CLI-tool style. Supports optional [skill] argument to show details for one. Use when user invokes /myskills, asks for local skills help, "what skills do I have here?", or similar.
user_invocable: true
argument-hint: "[skill-name]"
---

# MySkills - Local Project Skills Help

This skill provides a `/myskills` command (and `/myskills <name>`) that acts like a CLI tool to discover and explain the skills that are local to this project.

## When invoked as `/myskills` (no argument)

1. Use the `list_dir` tool on the path `.grok/skills` (relative to repo root) to discover all project-local skill directories. Ignore this `myskills` directory itself in the listing.

2. For each discovered skill directory (e.g. `build`, `cleanbuild`, `smoke`, etc.):
   - Read the `SKILL.md` file inside it (use `read_file` tool).
   - Parse the YAML frontmatter (the `---` block at the top).
   - Extract the `name` field.
   - Extract the `description` field.
   - Create a concise 1-liner usage explanation:
     - Use the `name` (lowercase).
     - If the description mentions aliases (e.g. "aliases: /fullbuild"), include them in parentheses after the name, like `cleanbuild (fullbuild)`.
     - Take the first sentence or main action from the description and shorten it to a clear, CLI-style 1-liner (max ~70 chars if possible). Make it action-oriented and useful.
     - Examples of good 1-liners (style to match):
       - `build          Incremental debug build (no BUILD_NUMBER bump)`
       - `cleanbuild     Clean debug build with BUILD_NUMBER bump (aliases: fullbuild)`
       - `smoke          Run smoke test (/smoke = reset-driven; /probe = COM-only no-reset)`
       - `roundtrip      Full roundtrip: cleanbuild + flash + smoke (alias: /full)`

3. Output the results in clean CLI-tool style, something like:

```
Available project-local skills (from .grok/skills/):

  build          Incremental debug build (no BUILD_NUMBER bump)
  cleanbuild     Clean debug build with BUILD_NUMBER bump (aliases: fullbuild)
  fixme          Attempt to fix errors/warnings from the last build
  flash          Flash the last build product using local bench hardware
  roundtrip      Full roundtrip: cleanbuild + flash + smoke (alias: /full)
  smoke          Run smoke tests using local bench (/smoke and /probe modes)
  myskills       List local skills with 1-liner usage (this command)

Use `/myskills <name>` for more details on a specific skill.
See SCRIPTS.md for the underlying automation scripts these skills drive.
```

Sort the list alphabetically by name.

## When invoked as `/myskills <skill-name>`

1. Look for a directory `.grok/skills/<skill-name>` (case-insensitive match on the name).

2. If found:
   - Read its `SKILL.md`.
   - Print a header like "=== <name> ===".
   - Extract and print the full `description` from frontmatter.
   - Print the main usage command(s) derived from the skill body (look for the example command lines like the `powershell ...` or key steps).
   - Print the bench defaults if present (COM port, ST-Link SN, etc.).
   - Summarize the key behavior in 3-6 bullet points (e.g. what it does, abort rules, what it echoes, etc.).
   - End with "Run the skill with `/<name>` (or natural language equivalent)."

3. If not found, list the available skills (as in the no-arg case) and say "Skill '<skill-name>' not found locally."

## General rules for this skill

- Always be concise and output in a clean, readable, CLI-tool aesthetic (use fixed-width alignment where helpful, short lines).
- Do not execute any of the other skills' actions — this skill only lists and describes.
- Prefer reading the actual SKILL.md files dynamically so the list stays accurate if skills are added/removed/edited.
- If the user just says "myskills" or "list my skills", treat it as `/myskills`.
- This skill is project-local only (it looks in `.grok/skills` relative to the current project). It does not list global/user skills from `~/.grok/skills`.

Reference: other project skills and SCRIPTS.md for context when describing.
