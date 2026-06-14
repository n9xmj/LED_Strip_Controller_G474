# Agent Handoff: G9 — X / Y durations (D4 / W1)

**Date:** 2026-06-14  
**Status:** IN PROGRESS (archive candidate after MSG **G9** ✅)  
**MSG:** **G9** · **Plan refs:** **D4**, **S5**, **S10** (🟢 / locked for v1.1 — do not re-litigate deferral)  
**Bench:** COM9 @ 921600 · ST-Link SN from `scripts/bench.defaults.json` (`003C00193137510C39383538`)

**Authoritative plan:** [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — MSG **G9** row, **D4** detail, **S5** duration map  
**Host reference:** [tools/play_melody.py](../../tools/play_melody.py) `duration_map` (X/Y tentative values match plan)  
**Template:** [focused-implementation-handoff-template.md](focused-implementation-handoff-template.md)

---

## 1. Mission (one screen)

Ship **v1.1 required firmware row G9**: accept **`X`** (sixteenth) and **`Y`** (thirty-second) as **note/rest duration letters** inside descriptors — same postfix slot as **`W H Q I`**, order-flexible, dotted forms supported (`C4X.`, `RY.`).

Today `b_play_x2_from_duration_letter` only maps W/H/Q/I; unknown duration letters fail parse. Extend timing via existing **`u32_play_calc_note_ticks`** / **`u8_dur_x2`** path (**S5** / **I4**).

**Exit:** `grammar_torture_v11.play` passes STRICT; **`grammar_torture` (v1)** still passes; MSG **G9** → ✅.

**Out of scope (G10 — next session):**
- Raw-percent **`;nn`** duty (**D5b**) — do not touch duty digit-run logic beyond regression.
- **`%X` / `%Y` beat-unit** executives — not required for G9 (only note/rest duration letters).
- Tuplets (**D15**), dynamics (**W29**), **`uart_stream`** (**G11**).

---

## 2. Locked wire / behavior (no design debate)

### S5 duration table (extend — quarter-note = 1.0 beat)

| Letter | `duration_beats` (quarter = 1.0) | Role |
| ------ | -------------------------------- | ---- |
| W | 4.0 | whole |
| H | 2.0 | half |
| Q | 1.0 | quarter |
| I | 0.5 | eighth |
| **X** | **0.25** | sixteenth |
| **Y** | **0.125** | thirty-second |

Dot multiplier unchanged: **×1.5** on the computed interval (**S5**).

Applies to **notes (A–G, N)** and **rests (R)**. Inheritance: omitted duration still inherits from note memory (now may inherit X or Y once parsed).

### Parser

- **`X` / `Y` only inside note/rest descriptor** — not top-level executives (top-level **`X`** was reserved for expansion in **D18**; duration **`X`** is postfix-only, same as **`I`** vs note **`E`**).
- **`b_play_breaks_note_token`:** do **not** add `X`/`Y` to the executive switch — they are consumed in the descriptor sub-FSM like `W/H/Q/I`.
- Invalid / unknown duration letter → existing recoverable/fatal paths unchanged.

### Internal `dur_x2` encoding (implementation note — you must solve)

Current ladder: `Q=2, I=1` with `ms ∝ dur_x2 / beat_unit_x2`. **X and Y need smaller distinct values than `I=1`.**

**Recommended approach:** rescale the whole **`×2` ladder ×4** so timing is unchanged but sixteenth/thirty-second fit as integers, e.g.:

| Letter | New `PLAY_DUR_*_X2` (example) |
| ------ | ------------------------------- |
| W | 32 |
| H | 16 |
| Q | 8 |
| I | 4 |
| X | 2 |
| Y | 1 |

Scale **`PLAY_DEFAULT_DUR_X2`** and **`PLAY_DEFAULT_BEAT_UNIT_X2`** (and any `%` beat-unit x2 mapping) by the **same factor** so existing goldens keep the same wall-clock times. Document final constants in commit / **I10**.

**Verify:** `C4Q` at T240 still same ms as before; `C4X` = half of `C4I`, `C4Y` = half of `C4X`.

Alternative encodings OK only if **`~` replay**, snapshots, and **`\"ctx:…"`** suffix loads stay consistent.

---

## 3. Code anchors (today — `App/Src/play.c`)

| Symbol / line | What exists before this session |
| ------------- | ------------------------------- |
| `PLAY_DUR_W/H/Q/I_X2` (~L21–24) | Local `#define`s — extend or move to `play_config.h` |
| `b_play_x2_from_duration_letter` (~L1223) | W/H/Q/I only — **add X/Y** |
| `b_play_parse_pitch_token` / rest path | Calls duration letter helper in order-flex loop |
| `u32_play_calc_note_ticks` (~L1622) | Integer ms from `dur_x2`, `%` beat unit, dot |
| `play_ctx_snapshot_t` | Stores `u8_dur_x2` — inherits/resets with rescaled defaults |
| `scripts/play_golden/grammar_torture_v11.play` | Waiting golden — **register in `tests.json`** if missing |
| `tools/play_melody.py` | Host preview already lists X=0.25, Y=0.125 — optional sync comment |

**Hard rule:** `App/` only. No `Core/` edits.

---

## 4. Implementation checklist

- [ ] Add **`PLAY_DUR_X_X2`** / **`PLAY_DUR_Y_X2`**; rescale existing W/H/Q/I + defaults + beat-unit x2 if needed (§2).
- [ ] **`b_play_x2_from_duration_letter`** — cases **`X`**, **`Y`**.
- [ ] Confirm descriptor parse accepts `C4X`, `C4Y`, `C4X.`, `RX`, `RY` (golden covers these).
- [ ] Register **`grammar_torture_v11`** in `scripts/play_golden/tests.json` (STRICT, tier torture).
- [ ] Bench green (§5); **`grammar_torture` (v1)** regression PASS.
- [ ] MSG **G9** → ✅; **D4** detail status → 🟢; **I10** + **GP8**; [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) X/Y YES.
- [ ] `/wrapup` → session handoff; this file → archive candidate.

---

## 5. Golden / bench exit criteria

```text
python scripts/play_bench.py --reset --timeout 60  test grammar_torture_v11
python scripts/play_bench.py --reset --timeout 120 test grammar_torture
python scripts/play_bench.py --reset --timeout 30  test smoke
```

Optional sanity: `C4X` at T240 audibly half the length of `C4I` on scope/ear.

---

## 6. Sub-task IDs (optional commit messages)

| ID | Deliverable |
| -- | ----------- |
| **G9a** | `PLAY_DUR_X/Y_X2` + rescale ladder / defaults |
| **G9b** | Parser + timing path for X/Y (notes + rests, dotted) |
| **G9c** | `grammar_torture_v11` in tests.json + MSG/docs ✅ |

---

## 7. Golden vector — `grammar_torture_v11.play` (existing)

Already in repo:

```text
@ PLAY v1.1 X Y appendix @
T240 O4 %Q
C4X C4Y C4X. C4Y. RX RY
*
```

Pass = STRICT, zero fatals, `PLAY ended`. No golden file changes expected unless parse quirks require minimal fixes.

---

## 8. Next session pointer

After **G9** ✅: **G10** raw-percent **`;nn`** duty (**D5b**) — small duty sub-FSM branch; can share a focused **`duty-percent-handoff.md`** or inline from plan **G10** row.

---

**Lifecycle:** when **G9** is ✅, run **`/cleanup-docs`**.

**End of xy-durations-handoff.md**
