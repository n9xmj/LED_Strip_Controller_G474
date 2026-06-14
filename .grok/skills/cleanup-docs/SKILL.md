---
name: cleanup-docs
description: >-
  Prune ephemeral PLAY planning handoffs — archive completed feature briefs,
  delete superseded session handoffs, fix stale cross-links, keep the permanent
  template. Complements /wrapup (session end) and /update-docs (feature docs).
  Use when the user says /cleanup-docs, "clean up handoffs", "archive G4 brief",
  or after an MSG row ships to ✅.
user_invocable: true
argument-hint: "[apply | dry-run | also theory]"
---

# cleanup-docs — planning handoff lifecycle

Automates **inventory → recommend → (optional) prune** for PLAY multi-session docs.
Does **not** replace `/wrapup` (writes handoffs) or `/update-docs` (broad doc sync).

## Doc taxonomy (permanent vs ephemeral)

| Kind | Pattern | Keep? |
| ---- | ------- | ----- |
| **Template** | `focused-implementation-handoff-template.md` | **Always** |
| **Mechanics** | `decision-log-model.md`, active `*-implementation-plan.md` | **Always** |
| **Session handoff** | `*-session-handoff-YYYY-MM-DD.md` | **Newest 1 only** |
| **Feature brief** | `<topic>-handoff.md` (not session) | Until MSG **✅**, then **archive** |
| **Theory adjunct** | e.g. `Docs/co5ths_key_signature_handoff.md` | **Review** — keep if plan still links |

Facts that shipped belong in **MSG / I10** (plan) and **code** — not in stale briefs.

## Default policy

| Step | Default |
| ---- | ------- |
| Automation script | **Dry-run** first |
| Delete session handoffs | All but **newest 1** |
| Feature briefs with MSG **✅** | **Move** to `Docs/planning/archive/` |
| `co5ths_*` theory doc | **Keep** unless user says `also theory` |
| Git | **Report only** — commit when user asks (or end with suggested `git add` list) |
| Plan link fixes | **Agent edits** stale `Related:` / footer / MEMORY.md |

Parse `<instructions>` for: **`apply`** (run script with `--apply`), **`dry-run`** (explicit), **`also theory`** (archive co5ths if MSG D8 fully in plan).

## Procedure

### 1. Run inventory script

From repo root:

```powershell
python scripts/cleanup_planning_docs.py
```

To execute deletes/archives + memory stub patch:

```powershell
python scripts/cleanup_planning_docs.py --apply
```

Optional: `--keep-sessions 2` to retain two dated session handoffs.

### 2. Extend FEATURE_MSG_MAP if needed

Edit `scripts/cleanup_planning_docs.py` → `FEATURE_MSG_MAP` when adding new focused briefs:

```python
FEATURE_MSG_MAP = {
    "labels-preparse": "G4",
    "labels-gosub": "G5",
}
```

Stem = filename without `-handoff.md`.

### 3. Agent pass — stale links (required)

Script prints lines in plan/memory/AGENTS that reference files slated for removal. Fix:

| File | What to fix |
| ---- | ----------- |
| `play-v1-implementation-plan.md` | **Related:** line; footer session-handoff link; remove archived brief links |
| `.grok/memory/MEMORY.md` | Drop archived brief pointers |
| `.grok/memory/planning_decision_log_model.md` | **Latest handoff** → newest session file (script patches if `--apply`) |
| `AGENTS.md` | Planning section only if it named a removed path |
| `labels-preparse-handoff.md` peers | Ensure **Status: DONE** before archive |

**Do not** remove links to `focused-implementation-handoff-template.md` or `decision-log-model.md`.

### 4. Verify MSG table matches reality

Before archiving a feature brief, confirm **§ MSG** row is **✅** and I10 has a one-line shipped note. If not, **abort** archive for that file.

### 5. User report

Print:

```markdown
## cleanup-docs summary

- Kept session handoff: …
- Archived: …
- Deleted: …
- Manual link fixes: …
- Next: `/read-the-docs` or start G5 with new `labels-gosub-handoff.md` from template
```

### 6. Git (optional)

If user wants commit:

```
git add Docs/planning/ .grok/memory/ scripts/cleanup_planning_docs.py .grok/skills/cleanup-docs/
git commit -m "docs: prune stale PLAY handoffs after G4 ship"
```

## Creating the *next* focused brief

Copy template → new file:

```
Docs/planning/<topic>-handoff.md
```

from [focused-implementation-handoff-template.md](../../Docs/planning/focused-implementation-handoff-template.md).

Add stem to `FEATURE_MSG_MAP` in the script when the brief is created.

## When to run

| Trigger | Action |
| ------- | ------ |
| MSG row just hit **✅** | `/cleanup-docs` same day or next `/wrapup` |
| Before G5 focused session | Archive G4 brief; keep `2026-06-13` session handoff until G5 wrapup |
| Quarterly hygiene | `/cleanup-docs dry-run` — review only |

## Related skills

- **`/wrapup`** — writes **session** handoff (start of lifecycle)
- **`/read-the-docs`** — reads **newest** session handoff at session start
- **`/update-docs`** — sync AGENTS.md / PROJECT.md after features
- **Template:** `Docs/planning/focused-implementation-handoff-template.md`

## Archive folder

`Docs/planning/archive/` — git-tracked graveyard for completed feature briefs (not session handoffs; those are **deleted**).
