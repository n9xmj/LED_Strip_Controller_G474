# PLAY v1 planning — session handoff (2026-06-11)

**Purpose:** Fresh-chat primer. Prior thread was large; **start a new session** for responsiveness.

**Read first (in order):**

| # | File | Role |
|---|------|------|
| 1 | **This file** | Where we left off |
| 2 | [play-v1-implementation-plan.md](play-v1-implementation-plan.md) | Master decision log — summary table + LOCKED CONTEXT |
| 3 | [play-lead-char-cheat-sheet.md](play-lead-char-cheat-sheet.md) | One-screen lead-char quick ref |
| 4 | [decision-log-model.md](decision-log-model.md) | ID / status mechanics |

Parent spec [PLAY_language_design.md](../PLAY_language_design.md) is **stale** until **T1**.

**Suggested `/read-the-docs` opener:**

```
/read-the-docs PLAY v1 planning
Read Docs/planning/play-v1-session-handoff-2026-06-11.md first,
then play-v1-implementation-plan.md summary table.
Next: lock I1 (v1 feature fence).
```

---

## Locked this session (high signal)

| Area | Decision |
|------|----------|
| **S5 / I4** | Timing formula; `PLAY_TEMPO_BPM_MAX=240`; `PLAY_SCHED_TICK_US=1000`; shared HW tick |
| **S10** | Session defaults: `Cn4Q_`, `T90`, `V33`, `UQ`, `P0`, `&0`, `K"C"`, `O4`; first `~` → WARNING + template |
| **D7** | Natural = lowercase **`n`** only; top-level **`N`** = absolute semitone (**D22**) |
| **D22** | `N` + 1..3 digits + suffix; `N60Q4` not `N604Q`; OOR → WARNING + clamp |
| **S7e / I3** | `PLAY_STACK_MAX_DEPTH=10`; overflow → hard abort |
| **S7d** | Pre-parse = sanity + label resolver; **missing ref → FATAL**; **unreferenced define → WARNING** |
| **S7c** | Unlisted faults → **skip + WARNING once + continue** (not ERROR) |
| **D8b** | Quote faults; **WS before `"`** on all string consumers |
| **D12** | BASIC/C lexer: WS readability; **`:`** optional EOS; **`;`** duty only (not C-EOS) |
| **T5** | Pedagogy: gradual snippets → fragments → full pieces |

---

## Still open

| ID | Status | Notes |
|----|--------|-------|
| **I1** | 🟡 | v1 feature fence — **next major lock** |
| **I2** | 🔴 | Label table cap; duplicate `<` policy |
| **D16 / D17** | 🟡 | String labels + `<`/`>` — leaning v1 |
| **S7f / S7g** | 🟡 | Optional strict mode; I8 hook on reject |
| **S7** parent | 🟡 | Core policy done; f/g optional |
| **T1–T5** | 🔴 | After spec-lock |
| **S7h** | 🔵 | Optional LINT (post-v1) |

**Spec-lock minimum:** I1, I2, D16/D17, then T4/T5.

---

## Known doc drift (non-blocking)

- `PLAY_language_design.md` not yet trimmed (**T1**).
- Plan detail sections may be out of numeric order until T1 pass.

---

## Git

- Branch: **`main`**
- WIP committed on **`main`**: **`d16aec6`** — not pushed.

---

## Prior transcript (optional)

Cursor: `2ee15db2-243e-45a3-8c0a-883a5d4854ed`

---

_Last updated: 2026-06-11 — `/wrapup` session (S7c lock, `/wrapup` skill added)._
