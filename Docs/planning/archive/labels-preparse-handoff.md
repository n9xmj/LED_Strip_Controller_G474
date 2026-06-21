# Agent Handoff: G4 — Startup Pre-Parse + Label Table (S7d / I2)

**Date:** 2026-06-13  
**From:** PLAY v1 planning (locked D16/D17/D19/S7d/I2) → implementation session  
**MSG row:** **G4** — unblocks **G5** (runtime `<` / `>` / `=` / `/` must **not** ship in this session)

**Authoritative plan (do not re-litigate 🟢 items):** [play-v1-implementation-plan.md](../play-v1-implementation-plan.md) — **D16**, **D17**, **D19** (refs only), **S7d**, **S7a**, **S7b**, **I2**, **D8b**, **D9**, **S7j**.

**Sibling reference (style):** the `co5ths_key_signature_handoff.md` K session *(since removed — same focused-session pattern; theory shipped in `App/Src/play.c`)*.

**Bench:** COM9 @ 921600 · ST-Link SN `003C00193137510C39383538` · skills: `/build`, `/flash`, `/playtest`.

---

## 1. Mission (one screen)

Implement **one linear pre-parse pass** at `b_play_start_policy()` **before** playback begins. The pass:

1. Verifies **`@ … @`** comment integrity (including **`\@`** escape — **G7** shipped).
2. Builds a **sparse label table** from every **`\<`** define (`**<n**` and **`\<"name"`**).
3. Resolves every **`>`** goto ref and **`=`** GOSUB ref — **missing target → FATAL**, refuse start.
4. Emits **WARNING** for **unreferenced** defines at pass end (**S7b**).
5. Leaves a populated table on the runtime instance for **G5** to consume next session.

**Out of scope (G5 — next session):** PC jumps, S2 forward/backward snapshot restore, GOSUB call stack, `/` RETURN, runtime execution of `<` / `>` / `=`. Those tokens may still hit **unsupported executive** at runtime until G5 lands — that is OK for G4 goldens.

---

## 2. Locked wire shapes (I2)

| Token | Form | Pre-parse action |
| ----- | ---- | ---------------- |
| **Define** | `**<n**` (numeric id, **S7j** digit run) | Record offset of `**<**`, id = n |
| **Define** | `**<"name"**` (quoted; **D8b** optional WS before `"`) | Record offset of `**<**`, name ≤ `PLAY_LABEL_MAX_LEN` (16) |
| **Goto ref** | `**>n**` or `**>"name"**` | Lookup define; **FATAL** if missing; mark define **referenced** |
| **GOSUB ref** | `**=n**` or `**="name"**` | Same lookup rules as goto |

**Caps (`play_config.h`):**

- `PLAY_LABEL_MAX_LEN` = 16 (payload chars, excluding quotes)
- `PLAY_LABEL_TABLE_MAX` = 10 defines per sequence
- `PLAY_DIGIT_RUN_MAX` = 5 — reuse `b_play_scan_digit_run_u16` for `<n` / `>n` / `=n`

**Fault policy:**

| Condition | Class | When |
| --------- | ----- | ---- |
| Unclosed `@` | **FATAL** | Pre-parse |
| Missing `>` / `=` target | **FATAL** | Pre-parse |
| Quote integrity on `<"`, `>"`, `="` | **FATAL** | Pre-parse |
| Name longer than max | **FATAL** | Pre-parse |
| Table full (11th define) | **FATAL** | Pre-parse |
| Duplicate define (same id or same string) | **WARNING** + **last wins** | Pre-parse |
| Unreferenced define | **WARNING** | End of pre-parse pass |
| Malformed numeric ref (no digits) | **FATAL** or recoverable per **S7c** — lean **FATAL** for `>Q` style garbage |

---

## 3. Pre-parse is not a LINTer (S7d)

**Do:**

- Comment-aware scan (`@` / `\@`)
- Quote-aware skip for **any** `"…"` region so `?"foo>bar"` does not count as goto
- Label define/ref discovery only

**Do not (defer S7h / runtime):**

- Bracket balance `[ ]`
- Full note-descriptor validation
- Unreachable-code analysis
- Tempo range pre-scan (optional later)

---

## 4. Suggested label table (firmware)

```c
typedef enum {
    PLAY_LABEL_KIND_UNKNOWN = 0,
    PLAY_LABEL_KIND_NUMERIC,
    PLAY_LABEL_KIND_STRING
} play_label_kind_t;

typedef struct {
    play_label_kind_t e_kind;
    uint16_t          u16_num_id;              /* KIND_NUMERIC */
    char              ac_name[PLAY_LABEL_MAX_LEN + 1U]; /* KIND_STRING */
    uint32_t          u32_define_offset;       /* offset of '<' in psz_src */
    bool              b_referenced;
} play_label_entry_t;
```

Store on `play_runtime_t`:

```c
play_label_entry_t ax_labels[PLAY_LABEL_TABLE_MAX];
uint8_t            u8_label_count;
```

**Lookup:** linear search (≤10 entries). Numeric `>7` matches `e_kind==NUMERIC && u16_num_id==7`. String `>"bk"` matches `strcmp(ac_name, "bk")==0`.

**Duplicate:** same kind + same id/name → WARNING, overwrite slot (last wins).

---

## 5. Scan algorithm sketch

Implement `static bool b_play_preparse(play_runtime_t *px_rt)` called from `b_play_start_policy()`:

```
state = SCAN_NORMAL
for offset = 0 .. strlen-1:
  if inside @ block: use same rules as b_play_skip_comment (\@ does not close)
  if inside " string: skip with C-escape rules (reuse b_play_parse_quoted_string logic in scan-only/at mode)
  if '<': parse define → insert/overwrite table entry
  if '>': parse ref → lookup, fatal if missing, else b_referenced=true
  if '=': parse ref → same as '>'
  else: advance 1 (note letters, T, V, etc. need not be fully parsed)
```

**Reuse existing helpers where possible:**

- `b_play_is_ws`
- `b_play_scan_digit_run_u16` / `b_play_consume_digit_run_u16_at`
- `b_play_parse_quoted_string` — may need `…_at(px_rt, u32_off, …)` variant for scan pass
- `b_play_fault` — pass must respect policy; **FATAL** always refuses start

**State machine during start:**

```
LOADING → (pre-parse) → RUNNING on success
LOADING → FAULT on fatal; b_play_start_policy returns false
```

Set `px_rt->x_public.e_state = PLAY_STATE_LOADING` before pre-parse; only transition to `PLAY_STATE_RUNNING` if `b_play_preparse` returns true. Reset `u32_src_offset = 0` after successful pre-parse.

---

## 6. Code anchors (today)

| File | What exists |
| ---- | ----------- |
| `App/Src/play.c` | `b_play_start_policy` jumps straight to **RUNNING** — **no pre-parse** |
| `App/Src/play.c` | `b_play_skip_comment` — **runtime** `@` skip + unclosed fatal (reuse logic) |
| `App/Src/play.c` | `b_play_breaks_note_token` lists `<` `>` `=` as executives |
| `App/Src/play.c` | `<` `>` `=` fall through to **unsupported executive** at runtime |
| `App/Inc/play.h` | `PLAY_STATE_LOADING` defined, unused |
| `App/Inc/play_config.h` | `PLAY_LABEL_MAX_LEN`, `PLAY_LABEL_TABLE_MAX` |

**Do not edit** `Core/` outside USER CODE. Application logic stays in `App/`.

---

## 7. Golden tests (create in this session)

### `scripts/play_golden/labels_scan.play` (PASS — STRICT)

Exercises define + ref resolution **without** requiring runtime goto (music only after table built):

```text
@ label pre-parse smoke @
T120 O4 %Q
<1 C4Q D4Q
<"hook" E4Q
>1
="hook"
C4G4 *
```

Pre-parse: numeric `<1` referenced by `>1`; string `<"hook"` referenced by `="hook"`. Runtime may WARN on `>1` / `="hook"` until G5 — golden pass criteria for G4: **`b_play_start` succeeds** under STRICT and score plays through `*` (or documents xfail on unsupported if you add stub skip — prefer **start-only API test** via bench if runtime noise is a problem).

**Pragmatic G4 pass line:** `play_bench.py test labels_scan` — **no fatal at start**; `PLAY ended` OR explicit bench check that pre-parse completed (log tag `preparse OK` optional).

### `scripts/play_golden/labels_fatal_missing.play` (FAIL at start)

```text
T120
>99
C4Q *
```

Expect: **`b_play_start_policy` returns false** or instance **FAULT** — missing numeric define.

### `scripts/play_golden/labels_fatal_quote.play` (FAIL at start)

```text
<"unterminated
C4Q *
```

Expect: **FATAL** quote integrity at pre-parse.

Register all three in `scripts/play_golden/tests.json`. For fatal vectors, extend `play_bench.py` with `--expect-start-fail` if not present (small harness change OK in G4 session).

---

## 8. Integration checklist

- [x] `play_label_entry_t` + table on `play_runtime_t`
- [x] `b_play_preparse()` — comment + quote aware linear scan
- [x] `b_play_start_policy()` — LOADING → pre-parse → RUNNING or refuse
- [x] Duplicate define → WARNING + last wins
- [x] Unreferenced define → WARNING at end (log `PLAY warn:` under NORMAL/STRICT)
- [x] Reuse **S7j** digit scanner for `<n` / `>n` / `=n`
- [x] Golden: `labels_scan` + fatal vectors
- [x] Update **MSG G4 → ✅**, **I10**, [chatbot_brief.md](../../Player/chatbot_brief.md) (pre-parse row only — leave `<`/`>` **NO** until G5)
- [x] `/wrapup` → new `play-v1-session-handoff-2026-06-13.md`

---

## 9. Sub-task IDs (optional commit messages)

| ID | Deliverable |
| -- | ----------- |
| **G4a** | `@` / `\@` integrity in scan-only path |
| **G4b** | Label define collection (`<n`, `<"name"`) + I2 caps |
| **G4c** | Ref resolve (`>`, `=`) + unreferenced WARNING + wire into `b_play_start` |

---

## 10. What G5 will consume (read-only context)

Do **not** implement in G4 — documented so table shape stays compatible:

- **S2:** On runtime `<` define, capture `play_ctx_snapshot_t` at marker; on backward `>`, restore snapshot.
- **D19:** `=` pushes return PC + caller snapshot; `/` pops.
- **Offset landing:** Execution resumes at `u32_define_offset` (char `<`), re-parses define token.

Table must expose `u32_define_offset` per entry — G4 sets it; G5 jumps there.

---

**End of labels-preparse-handoff.md**
