# PLAY v1 planning — session handoff (2026-06-11)

**Purpose:** Fresh-chat primer. Prior thread was large; **start a new session** for responsiveness.

## Quick links

- [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — master decision log
- [play-lead-char-cheat-sheet.md](play-lead-char-cheat-sheet.md) — one-screen lead-char ref
- [decision-log-model.md](decision-log-model.md) — ID / status mechanics
- [AGENTS.md](../../AGENTS.md) — agent workflow

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
Next: draft T4 EBNF outline or start v1 play.c/play.h skeleton (I9 menu wiring).
```

---

## Locked this session (high signal)

| Area | Decision |
|------|----------|
| **I1** | **v1 feature fence** — monophonic interpreter + full In/Out table locked; GOSUB/RETURN/END **in** firmware; const **`playstr`** first bench |
| **I2 / D16 / D17** | Label caps: **`PLAY_LABEL_MAX_LEN=16`**, **`PLAY_LABEL_TABLE_MAX=10`** (`#define` in **`play_config.h`**) |
| **I9** | Submenu **`--- Player tests and experiments ---`** (top **`m`**): **`1`** smoke · **`s`** playstr ≤128 · **`p`** terminal dup; async jobs + **I4** |
| **I7 (v1 API)** | Opaque **`play_handle_t`** (`void *`); **`b_play_start(…, &px_handle)`** · **`v_play_stop(px_handle)`**; **`PLAY_INSTANCE_MAX=1`** |
| **I7 (bench)** | **`play_instance_t`** exposed in **`play.h`**; bench reads via **`PLAY_HANDLE_AS_INSTANCE(h)`** — not for general mutation |
| **I9 smoke** | **`const char *psz_play_smoke_test = "@ smoke scale @ … *"`** in **`play_presets.c`** (not `#define`) |
| **S11** | **v2+ headwind captured (🟡)** — NVM/FS load + multi-instance **requires** explicit sync/staging before **S3** barriers |
| **S5 / I4** | Timing formula; `PLAY_TEMPO_BPM_MAX=240`; `PLAY_SCHED_TICK_US=1000`; shared HW tick |
| **S10** | Session defaults: `Cn4Q_`, `T90`, `V33`, `UQ`, `P0`, `&0`, `K"C"`, `O4`; first `~` → WARNING + template |
| **D7 / D22** | Natural **`n`**; **`N<n>`** absolute semitone (max 3 digits) |
| **S7e / I3** | `PLAY_STACK_MAX_DEPTH=10`; overflow → hard abort |
| **S7d / S7c** | Pre-parse label resolver; missing ref **FATAL**; unlisted faults → skip + WARNING once |
| **D8b / D12** | Quote policy; BASIC/C lexer (**`:`** optional EOS; **`;`** duty only) |

---

## Still open (user: pick up 🟡 S-items later)

| ID | Status | Notes |
|----|--------|-------|
| **S7f** | 🟡 | Strict **`playstr`** stop-on-first-fault — **no resolution yet** |
| **S7g** | 🟡 | **I8** hook on rejected tokens — **no resolution yet** |
| **S7** parent | 🟡 | Core policy 🟢; **f/g** remain until user locks |
| **S11** | 🟡 | Observations only — full design pass when v2+ polyphony scheduled |
| **T1–T5** | 🔴 | Impl fence met — **T4 EBNF** or **v1 coding** next |
| **S7h** | 🔵 | Optional LINT (post-v1) |

**User note (2026-06-11 end of session):** Will resolve remaining **yellow S-items** (**S7f**, **S7g**, **S7** parent) in a later session — not blocking v1 skeleton.

**Spec-lock minimum:** met for v1 implementation fence. Next work: **T4/T5** and/or **`play.c` skeleton**.

---

## Known doc drift (non-blocking)

- `PLAY_language_design.md` not yet trimmed (**T1**).
- Plan detail sections may be out of numeric order until T1 pass.

---

## Git

- Branch: **`main`**
- After wrapup: see commit hash in wrapup report (pushed to **`origin/main`**).

---

## Prior transcript (optional)

Cursor: `d0fe9c55-6a80-4151-bfe1-ff4264ea058b` (continued); earlier `2ee15db2-243e-45a3-8c0a-883a5d4854ed`

---

_Last updated: 2026-06-11 (wrapup) — handle API, smoke preset, **S11**; **S7f/g** intentionally open._
