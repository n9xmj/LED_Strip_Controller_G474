---
name: commit
description: Commit staged (or all) changes with a message and optionally push to remote. Format: /commit "commit message here" [--push]. Supports natural language like "commit the note player", "push these changes". Use when you want to save work and/or publish to GitHub.
user_invocable: true
argument-hint: '"message" [--push]'
---

# Commit Skill

**No separate helper script yet** (unlike setver/build). This skill runs git directly (robust for this bench).

## When invoked as `/commit "message" [--push]` (or natural language)

1. Ensure changes are staged:
   - If nothing staged, does `git add -A` (all modified + untracked, respecting .gitignore).
2. Commit:
   ```
   git commit -m "your message"
   ```
   - The message you provide is used directly. Add conventional prefixes or details as desired (e.g. "Add note player...").
3. If `--push` (or "and push", "then push") is present:
   ```
   git push origin main
   ```
   (Assumes main branch; adjust if on feature branch.)

4. After:
   - Report the commit hash (short + full), files changed, insertions/deletions.
   - Show `git status` (should be clean).
   - If pushed: the remote URL and "main -> main".
   - Brief diagnostics on failure (e.g. nothing to commit, no remote, auth issues).

## Usage examples
- `/commit "Add interactive note player feature"`
- `/commit "Fix build bump logic" --push`
- "commit the latest work and push it"

## Current project conventions (from AGENTS.md + history)
- Good messages are descriptive + reference key changes (new files, API additions, version bumps).
- Reference player-piano / future work where relevant.
- After significant features: often followed by `/smoke` or `/roundtrip` to verify on bench.
- Use the same style as recent commits (e.g. the CORDIC one and this note-player one).

## Creating a wrapper script (future improvement)
If you want a `scripts/commit.ps1` (like setver.ps1), say so and I'll scaffold one that handles quoting, --push, branch detection, etc. Then update this skill to call it.

Reference SCRIPTS.md for git patterns used elsewhere. The TUI does **not** appear to have a built-in commit/push skill (none present in .grok/skills/ as of this writing).

If the operation fails, report the exact git error. Do not force-push unless explicitly asked.