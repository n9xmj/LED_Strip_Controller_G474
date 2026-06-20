---
name: wrapup
description: End-of-session handoff prep for LED_Strip_Controller_G474 — sanity-check docs/memory/AGENTS.md, write or refresh a dated session handoff, then commit WIP (no push by default). Optional args override policy, e.g. "push", "no commit", or extra tasks. Use when the user says /wrapup, "wrap up the session", "prepare handoff", or before leaving a long session.
---

# wrapup — session handoff + WIP commit

Run when the user is **ending a session** and wants the next agent (or a fresh chat)
to pick up cleanly. Complements `/read-the-docs` at session **start**.

This is the Claude twin of `.grok/skills/wrapup/SKILL.md` — keep the two in rough
parity if you change session-handoff conventions.

## Default policy (unless the user's args override)

| Step | Default |
|------|---------|
| Documentation sanity | **Non-blocking** — fix obvious drift; log gaps, don't block wrapup |
| Memory + AGENTS.md | Update if material architecture / invariant / workflow changed this session |
| Handoff artifact | Write/update a dated session handoff (see **Handoff location**) |
| Git | **`git commit` WIP** (stage specific files, never build artifacts) |
| Push | **No push** unless the user says `push` |
| Commit | **Always** unless the user says `no commit` |

Parse the user's args case-insensitively for **`push`** and **`no commit`** / **`no-commit`**.
Remaining text = extra scope (e.g. "also bump PROJECT.md todo").

## Procedure

### 1. Capture session outcome
Briefly list what shipped/changed this session (files, features, IDs, bug fixes).
Use `git status` / `git diff --stat` to ground it.

### 2. Documentation sanity (non-blocking)
- **AGENTS.md** — architecture/invariants section reflects any new driver behavior,
  peripheral config, or workflow rule introduced this session.
- **`.grok/memory/`** — relevant body file(s) updated; `MEMORY.md` index line points at
  the newest handoff and any new memory file.
- **Active PLAY plan** (`Docs/planning/*-implementation-plan.md`) — only if the session
  touched PLAY planning: summary-table statuses match detail sections.
- Fix obvious drift; log anything you didn't fix under **Known doc drift** in the handoff.

### 3. Handoff artifact (required)

**Handoff location** (pick by session type):
- **PLAY planning session** → `Docs/planning/play-v1-session-handoff-YYYY-MM-DD.md`
- **Firmware / feature / bring-up session** → `.grok/memory/session-handoff-YYYY-MM-DD[-topic].md`

Must include:
- **Purpose** — fresh-chat primer (one or two lines)
- **Read first** — markdown links (repo-relative paths) to the key files for this topic
- **Shipped this session** / **Still open / next steps**
- **Gotchas / invariants** worth not re-breaking
- **Git note** — branch + commit hash after the wrapup commit
- **Suggested opener** for the next session (copy-paste block)

If the session touched PLAY planning, also update the plan footer's
`Next suggested chat prompt` + handoff link.

### 4. Wire into session start
Make sure `.grok/memory/MEMORY.md` (and the `/read-the-docs` skill, if it enumerates
handoffs) references the **newest** handoff you just wrote.

### 5. Git commit (default)
From repo root:
```powershell
git status
git diff --stat
```
Stage **specific** files (App/, Core/ USER CODE, docs, memory, skills, AGENTS.md,
.ioc + regenerated Core/ when peripherals changed) — **never** `Debug/` build artifacts
(they're gitignored anyway). Commit with a one-line, why-focused subject and a body if
the change is subtle (e.g. a latent bug fix).

**Do not push** unless the user said `push`. If `no commit`: skip this step and note
"uncommitted WIP on disk" in the handoff.

> IDE metadata: `.cproject` and `.mxproject` **are tracked on purpose** (restored on
> CubeIDE import on another machine). `.settings/` is **not** tracked. Keep it that way.

### 6. User-facing wrapup report
Print, in order:
1. **Quick links** block — markdown links (repo-relative) to the handoff + any heavily
   edited files (AGENTS.md, memory note, plan).
2. Handoff file + one-line "start here".
3. Suggested `/read-the-docs …` opener for next session.
4. Commit hash (or "no commit per instructions") and whether push happened.
5. Optional: suggest starting a fresh chat if context is large.

## Overrides (examples)

| User says | Effect |
|-----------|--------|
| `/wrapup` | Default: docs + handoff + commit, no push |
| `/wrapup push` | Commit **and** push |
| `/wrapup no commit` | Handoff + docs only |
| `/wrapup also update PROJECT.md roadmap` | Default + extra scope |

## Related skills
- `/read-the-docs` — session **start** (reads memory + handoff)
- `/cleanup-docs` — prune ephemeral PLAY handoffs after an MSG row ships
- `/build-flash-smoke`, `/roundtrip` — dev loop (run before wrapup if code changed)
