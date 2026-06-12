---
name: wrapup
description: >-
  End-of-session handoff prep — sanity-check docs/memory, update session handoff
  for /read-the-docs, commit WIP (no push by default). Optional args override
  policy, e.g. "push", "no commit", or extra tasks. Use when the user says
  /wrapup, "wrap up the session", "prepare handoff", or before starting a fresh chat.
user_invocable: true
argument-hint: "[push | no commit | free-form extras]"
---

# Wrapup — session handoff + WIP commit

Run when the user is **ending a session** and wants the next agent (or a fresh chat)
to pick up cleanly. Complements `/read-the-docs` / `/bootstrap` at session **start**.

## Default policy (unless `<instructions>` override)

| Step | Default |
|------|---------|
| Documentation sanity | **Non-blocking** — fix obvious drift; log gaps, do not block wrapup |
| Memory + AGENTS.md | Update if material planning/workflow changed this session |
| Handoff artifact | Write/update `Docs/planning/<topic>-session-handoff-YYYY-MM-DD.md` (or refresh latest) |
| Git | **`git commit` WIP, no push** |
| Push | **Never** unless user says `push` in `<instructions>` |
| Commit | **Always** unless user says `no commit` in `<instructions>` |

Parse `<instructions>` case-insensitively for tokens: **`push`**, **`no commit`** / **`no-commit`**. Remaining text = extra scope (e.g. "also bump plan status footer").

## Procedure

### 1. Capture session outcome (agent)

Briefly list what was locked/changed this session (IDs, files). Use git diff if helpful.

### 2. Documentation sanity (non-blocking)

- **Active plan** (`Docs/planning/*-plan.md`): summary table statuses match detail sections + LOCKED CONTEXT.
- **Handoff doc**: exists, dated, lists locked vs open, next suggested prompt.
- **Cheat sheets / quick refs** referenced by plan are consistent.
- **AGENTS.md** planning section points at handoff + `/wrapup` / `/read-the-docs`.
- **`.grok/memory/`**: MEMORY.md index + body files reference latest handoff path.
- **`Docs/planning/decision-log-model.md`**: only if workflow itself changed.
- Do **not** block on T1/T4/T5 formal docs or stale parent spec unless user asked.

Log any unfixed drift in the handoff doc under **Known doc drift**.

### 3. Handoff artifact (required)

Path (PLAY v1 today):

`Docs/planning/play-v1-session-handoff-YYYY-MM-DD.md`

Must include:

- **Purpose** — fresh-chat primer
- **Read first** table (plan, cheat sheet, decision-log model, handoff itself) — use **markdown links** with repo-relative paths (see **Clickable links** below)
- **Suggested opener** for `/read-the-docs` (copy-paste block)
- **Locked this session** / **Still open**
- **Next suggested prompt**
- **Git note** — branch, commit hash after wrapup commit

Update **plan footer** `Next suggested chat prompt` + handoff link if present.

### 4. Wire into `/read-the-docs`

Ensure **bootstrap** / **read-the-docs** skills step 6 reads:

1. `Docs/planning/decision-log-model.md`
2. Active `*-implementation-plan.md`
3. **Newest** `*-session-handoff-*.md` in `Docs/planning/` (if any)

Ensure `.grok/memory/planning_decision_log_model.md` links latest handoff.

### 5. Git commit (default)

From repo root:

```
git status
git diff --stat
```

Stage **specific** files (planning, memory, skills, AGENTS.md — not build artifacts):

```
git add Docs/planning/ .grok/memory/ .grok/skills/wrapup/ AGENTS.md
# … plus any other session files
git commit -m "<imperative subject>"
```

**Commit message:** one line, why-focused (e.g. "PLAY v1 planning: lock S7c, D12 lexer, session handoff").

**Do not push** unless `<instructions>` contains `push`. If `push`, run `git push` only after user-facing confirmation in the wrapup summary (skill default still no push unless overridden).

**If `no commit`:** skip step 5; handoff doc must say "uncommitted WIP on disk".

### 6. User-facing wrapup report

Print:

1. **Quick links** block (required) — see **Clickable links** below
2. Handoff file + one-line "start here"
3. Suggested `/read-the-docs …` opener for next session
4. Commit hash (or "no commit per instructions")
5. Whether push happened
6. Optional: "start a new chat" when context is large

## Clickable links (required in wrapup report + handoff doc)

Help the user re-open WIP files in the editor or side pane with **one click**.

**Format:** markdown links using **repo-relative paths** from the project root (no `file://`, no absolute `C:\…` paths):

```markdown
## Quick links

- [play-v1-implementation-plan.md](Docs/planning/play-v1-implementation-plan.md) — master decision log
- [play-v1-session-handoff-YYYY-MM-DD.md](Docs/planning/play-v1-session-handoff-YYYY-MM-DD.md) — start here next session
- [play-lead-char-cheat-sheet.md](Docs/planning/play-lead-char-cheat-sheet.md)
- [decision-log-model.md](Docs/planning/decision-log-model.md)
- [AGENTS.md](AGENTS.md)
```

**Always include** in the user-facing wrapup report:

- Active implementation plan (whatever `*-implementation-plan.md` is in flight)
- The handoff doc you just wrote/updated
- Cheat sheet / quick ref if the session touched planning
- `AGENTS.md` when workflow or agent rules changed

**Also link** any other files heavily edited this session (e.g. `Docs/PROJECT.md`, a spec draft).

**Handoff doc:** the "Read first" table must use the same link style so links work inside the handoff file too (paths relative to `Docs/planning/` for files in that folder, or full repo-relative paths from root — be consistent per file location).

**Bootstrap parity:** session-start skills (`/bootstrap`, `/read-the-docs`, `/get-started`) must echo the same **Quick links** block after loading context — see `.grok/skills/bootstrap/SKILL.md`.

## Overrides (examples)

| User says | Effect |
|-----------|--------|
| `/wrapup` | Default: docs + handoff + commit, no push |
| `/wrapup push` | Commit **and** push |
| `/wrapup no commit` | Handoff + docs only |
| `/wrapup also update PROJECT.md todo` | Default + extra scope |

## Cross-project note

PLAY planning lives in this repo (`LED_Strip_Controller_G474`). Mirror/STM32 repos have their own `CLAUDE.md` — run `/wrapup` **in the repo you edited**. If no `Docs/planning/` exists, handoff goes in `.grok/memory/session-handoff-YYYY-MM-DD.md` instead.

## Related skills

- `/read-the-docs` / `/bootstrap` — session **start** (reads handoff)
- `/update-docs` — broader doc sync after **features** ship
- `/commit` — ad-hoc commit; wrapup owns end-of-session WIP commit discipline
