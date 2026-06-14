# PLAY v1 — personal-account session handoff (2026-06-13)

> **Continuity superseded (2026-06-14):** **G9** shipped (`8b3f621`/`d36b356`). Read **[play-v1-session-handoff-2026-06-14-g9.md](play-v1-session-handoff-2026-06-14-g9.md)** — **G10** (`;nn` duty) is the only remaining required v1.1 PLAY row.

**Read this first** after switching Cursor to your **personal Pro** account and opening this repo fresh.

**Repo:** `LED_Strip_Controller_G474` · **Branch:** `main` · **Remote:** up to date (`7a20622`)  
**Bench:** COM9 @ 921600 · ST-Link `003C00193137510C39383538` (see `scripts/bench.defaults.json`)  
**Firmware:** `3.0.7` build **8** (`App/Inc/platform.h`)

---

## Ducks in a row (committed & pushed)

| Item | Status |
| ---- | ------ |
| **G1–G9** firmware | ✅ on `main` — v1 + **G9** v1.1 X/Y |
| **G10** | ❌ v1.1 next (`;nn` duty) |
| Plan MSG table + I10 audit | ✅ through G9 |
| Chatbot brief | ✅ X/Y durations YES |
| G9 feature brief | ✅ archived → `archive/xy-durations-handoff.md` |
| Session continuity doc | ✅ [play-v1-session-handoff-2026-06-14-g9.md](play-v1-session-handoff-2026-06-14-g9.md) |

**Do not depend on old chat history.** Continuity lives in git + these docs.

---

## Switching Cursor account (plain steps)

You only need **one folder** in the workspace: this repo.

1. **Sign out** — click the **gear** (bottom-left) → **Account** → **Sign out**
2. **Sign in** with your **personal** Cursor email
3. **Open the project** — **File → Open Folder** →  
   `c:\STM32\CubeSource\LED_Strip_Controller_G474`
4. **Ignore** “save untitled workspace” nagging unless you *want* a saved workspace file — opening the folder alone is enough
5. **New Agent chat** — **Ctrl+N** (do not continue the bloated work-account parent thread)
6. Paste the **initial prompt** below into that new chat

### Chat history — what to expect

- History is **stored on this PC**, not in the cloud account — you *may* see old threads after sign-in, or you may not (Cursor’s split UI update lost some threads for many users)
- **Treat history as optional.** If a thread is missing, that is fine — this handoff replaces it
- Parent orchestrator work should stay **thin**; ship firmware in **focused child chats** with a copied handoff brief

### Visual preferences

- Theme, font size, panel layout — **may reset** or differ per account; tweak once in **Settings** if something annoys you
- No need to recreate a multi-repo workspace — **G474 only** for PLAY hobby work

---

## Your workflow (parent + child)

| Session | Role | Model hint |
| ------- | ---- | ---------- |
| **Parent** (this new chat) | Plan, MSG triage, write G5 brief, commit/push, `/cleanup-docs` | Personal Pro — use a **stronger API model** for brief + acceptance review |
| **Child** (separate chat per MSG) | Implement one row from a focused handoff | **Auto** is fine if the brief is tight; escalate to API for **G5** if bench stalls |

**Parent does not implement G5 firmware** — it authors `labels-gosub-handoff.md` and reviews the child’s diff.

---

## Next work — **G5** (D16–D19)

**G4 shipped:** startup `b_play_preparse()` + label table; runtime `<` `>` `=` are **skip stubs** (no PC jump yet).

**G5 ships:**

- PC jump on `>` (forward/backward + S2 snapshot restore)
- GOSUB `=` / RETURN `/` with call stack
- Re-enter at `u32_define_offset` from G4 table
- `grammar_torture.play` labels/goto block passes

**Author the child brief:**

1. Copy [`focused-implementation-handoff-template.md`](focused-implementation-handoff-template.md) → `labels-gosub-handoff.md`
2. Pull spec from plan **G5** row + [`archive/labels-preparse-handoff.md`](archive/labels-preparse-handoff.md) §10
3. Spawn a **new child chat** with that file attached — do not implement in the parent chat

**Bench exit (minimum):**

```text
python scripts/play_bench.py --reset --timeout 60 test labels_scan
python scripts/play_bench.py --reset --timeout 120 test grammar_torture
```

---

## MSG snapshot (v1 firmware)

| G | Feature | FW |
| - | ------- | -- |
| G1–G4, G6, G7 | Volume, voice, notes, pre-parse, ctx ext, `\@` | ✅ |
| **G5** | Runtime labels / goto / GOSUB / RETURN | ❌ **next** |
| G8 | Key LUT in repeat/label snapshots | 🟡 |

Full table: [`play-v1-implementation-plan.md`](play-v1-implementation-plan.md) § MSG.

---

## Key files (authoritative)

| What | Path |
| ---- | ---- |
| Interpreter | `App/Src/play.c` |
| Plan / Big Board / MSG | `Docs/planning/play-v1-implementation-plan.md` |
| Author/chatbot status | `Docs/planning/play-v1-chatbot-brief.md` |
| Agent contract | `AGENTS.md` |
| Focused-session template | `Docs/planning/focused-implementation-handoff-template.md` |
| Bench harness | `scripts/play_bench.py` · goldens in `scripts/play_golden/` |
| Prior session notes | [play-v1-session-handoff-2026-06-14-g9.md](play-v1-session-handoff-2026-06-14-g9.md) |

**Prime command:** `/read-the-docs` (loads memory index + `AGENTS.md` + plan context).

**After G5 ships:** `/wrapup` → update handoff → `/cleanup-docs apply` → commit → push.

---

## Initial prompt (copy-paste into new personal-account parent chat)

```text
/read-the-docs PLAY parent orchestrator (personal account)

Read these in order:
1. Docs/planning/play-v1-account-switch-handoff-2026-06-13.md
2. Docs/planning/play-v1-session-handoff-2026-06-13.md
3. Docs/planning/play-v1-implementation-plan.md — MSG table only (G5/G8 open)

Role: PARENT ONLY — no large firmware edits in this chat.
- Draft labels-gosub-handoff.md from focused-implementation-handoff-template.md for MSG G5
- When brief is ready, give me a copy-paste child-session prompt
- After child ships: review diff vs G5 row, update MSG/chatbot/handoff, commit, push

Context: G4 pre-parse shipped (48d4819). Runtime goto/GOSUB is G5. Bench COM9 @ 921600.
Do not re-litigate green D16/D17/S7d decisions.
```

---

## Parent log

| Date | Event |
| ---- | ----- |
| 2026-06-13 | G4 + cleanup-docs pushed; work-account parent session ends |
| 2026-06-13 | Personal-account handoff prepared — **G5 brief is first task** |
| 2026-06-14 | **G9** shipped (`8b3f621`, `d36b356`); chromatic v11 torture; **G10** next |

---

**Related:** [decision-log-model.md](decision-log-model.md) · [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md)
