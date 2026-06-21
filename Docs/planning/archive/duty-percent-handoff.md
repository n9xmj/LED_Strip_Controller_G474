# Agent Handoff: G10 — Raw-percent `;nn` duty (D5b / W2)

**Date:** 2026-06-14  
**Status:** COMPLETE — archive via **`/cleanup-docs`** after WIP commit  
**MSG:** **G10** ✅ · **Plan refs:** **D5b** 🟢, **D5c**, **S9**  
**Bench:** COM9 @ 921600 · ST-Link SN from `scripts/bench.defaults.json` (`003C00193137510C39383538`)

**Authoritative plan:** [play-v1-implementation-plan.md](../play-v1-implementation-plan.md) — MSG **G10** row, **D5b** / **D5c** detail  
**Predecessor:** v1 **`;n`** single-digit → **n/8** via `v_play_apply_duty_shorthand` (**D5c** ✅)  
**Template:** [focused-implementation-handoff-template.md](../focused-implementation-handoff-template.md)

---

## 1. Mission (one screen)

Ship the **last required v1.1 firmware row G10**: extend the note/rest/**`ctx:`** descriptor **`;`** duty suffix so **two digits** mean **raw percent 0–100**, while **one digit** keeps **D5c** semantics (**n/PLAY_DUTY_NUMERATOR**).

Today `;` uses `b_play_consume_digit_run_u16` (up to **5** digits) and always routes through `v_play_apply_duty_shorthand` — so **`;60`** incorrectly clamps to **8/8** (100%), same as **`;6`** intended as **6/8** (75%). Fix that disambiguation.

**Exit:** new golden **`duty_percent.play`**; **`grammar_torture`** still PASS (**`;6`** and **`\"ctx:5Q;4"`** unchanged); MSG **G10** → ✅ → **v1.1 required PLAY firmware complete** (with **G9** ✅).

**Out of scope:**
- **G11** `uart_stream` stretch.
- **D5d** pizzicato, **D13** ADSR, dynamics (**W29**).
- Changing **`_` / `!` / bare `;`** shorthands (**D5** locked).
- Host **`tools/play_melody.py`** sync (optional comment only).

---

## 2. Locked wire / behavior (no design debate)

| Form | Meaning | Storage |
| ---- | ------- | ------- |
| **`;`** (no digits) | Normal duty (~**6/8** today via `PLAY_DUTY_NORMAL_NUM`) | `num/den` = **6/8** |
| **`_`** | Legato **8/8** | unchanged |
| **`!`** | Staccato **2/8** | unchanged |
| **`;n`** (exactly **one** digit) | **D5c** — **n/8**; **n=0** or **n>8** → clamp **n=8** (100%) | `num=n`, `den=PLAY_DUTY_NUMERATOR` |
| **`;nn`** (exactly **two** digits) | **D5b** — **nn%**; **nn>100** → clamp **100**; **`;00`** = **0%** | `num=nn`, `den=100` |

**Disambiguation examples (must hold):**

| Token | Duty ratio | Approx on-time |
| ----- | ---------- | -------------- |
| `C4Q;6` | 6/8 | **75%** (unchanged — torture regression) |
| `C4Q;60` | 60/100 | **60%** (new) |
| `C4Q;4` | 4/8 | **50%** (`\"ctx:5Q;4"` regression) |

**S9 last-wins:** unchanged — last duty modifier in descriptor wins (`;6!` still staccato, etc.).

**Parse rule:** After **`;`**, read **at most two** digit characters for duty (do **not** use the global **5-digit** executive run here). If a **third** digit immediately follows a two-digit percent, treat as **bad note suffix** (STRICT) or skip per existing suffix fault policy — do **not** silently fold into percent.

**Refactor strongly recommended:** one helper (e.g. `b_play_parse_duty_semicolon_suffix`) called from **all three** copy-pasted `case ';':` blocks in `play.c` (~pitch token, rest path, **`ctx:`** suffix loader).

---

## 3. Code anchors (today — `App/Src/play.c`)

| Symbol / area | What exists before this session |
| ------------- | ------------------------------- |
| `v_play_apply_duty_shorthand` (~L1653) | Sets **num/den = n/8** with D5c clamp — keep for **1-digit** path |
| `case ';':` in `b_play_parse_pitch_token` (~L1781) | Uses full digit run → always shorthand — **fix** |
| Duplicate `case ';':` in absolute/`N` path (~L2031) | Same bug — **fix** |
| Duplicate `case ';':` in **`ctx:`** suffix path (~L2407) | Same bug — **fix** |
| `v_play_start_note` (~L2109) | `u32_active = note_ticks * num / den` — no change if num/den set correctly |
| `play_config.h` | `PLAY_DUTY_NUMERATOR=8`, `PLAY_DUTY_NORMAL_NUM=6`, etc. |

**Hard rule:** `App/` only. No `Core/` edits.

---

## 4. Implementation checklist

- [x] Add **`v_play_apply_duty_percent(play_note_memory_t *, uint8_t u8_pct)`** — sets `num=pct`, `den=100`, clamp pct **0–100**.
- [x] Add shared **`;` suffix parser** — 0 digits → normal; 1 → D5c shorthand; 2 → D5b percent; **≤2 digit cap**.
- [x] Wire helper into **all three** `case ';':` sites (pitch, rest/N, ctx).
- [x] Create **`scripts/play_golden/duty_percent.play`** + **`tests.json`** entry (STRICT).
- [x] Bench green (§5); **`grammar_torture`** **`;6`** / **`ctx:…;4`** unchanged behavior.
- [x] MSG **G10** → ✅; **D5b** → 🟢; **I10** + [chatbot_brief.md](../../Player/chatbot_brief.md).
- [x] `/wrapup` → session handoff noting **v1.1 required MSG complete**.

---

## 5. Golden / bench exit criteria

```text
python scripts/play_bench.py --reset --timeout 60  test duty_percent
python scripts/play_bench.py --reset --timeout 120 test grammar_torture
python scripts/play_bench.py --reset --timeout 120 test grammar_torture_v11
python scripts/play_bench.py --reset --timeout 30  test smoke
```

---

## 6. Sub-task IDs (optional commit messages)

| ID | Deliverable |
| -- | ----------- |
| **G10a** | `v_play_apply_duty_percent` + shared `;` suffix parser |
| **G10b** | Wire all three descriptor paths; D5c regression preserved |
| **G10c** | `duty_percent.play` + MSG/docs ✅ |

---

## 7. Golden vector — `duty_percent.play` (NEW)

Minimal STRICT file proving **1-digit vs 2-digit** disambiguation:

```text
@ G10 duty percent — ;6 vs ;60 disambiguation @
T120 O4 %Q
C4Q;6
C4Q;60
C4Q;4
R4Q;6
*
```

**Expect:** all notes schedule under STRICT with zero fatals; **`;6`** ≠ **`;60`** duty ratio in resolve hook / timing (75% vs 60% active window). Optional: add `C4Q;00` short silence test if scheduler supports very low duty without zero-length edge bugs.

Register in `tests.json` (tier `feature`, policy `strict`, aliases `G10`, `duty-nn`, `D5b`).

---

## 8. Next session pointer

After **G10** ✅: **v1.1 required PLAY firmware is done** (**G9** + **G10**). Optional stretch: **G11** [`uart_stream` port notes](../uart_stream-port-notes.md) — infra, not PLAY grammar. Post-v1.1 pivot per [PROJECT.md](../../PROJECT.md) (mic/DSP/vTree Mk 5) when author chooses.

---

**Lifecycle:** when **G10** is ✅, run **`/cleanup-docs`**.

**End of duty-percent-handoff.md**
