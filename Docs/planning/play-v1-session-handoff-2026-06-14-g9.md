# PLAY v1 session handoff — 2026-06-14 (G9 shipped — X/Y durations)

**Purpose:** Fresh-chat primer after **G9** sixteenth/thirty-second durations + **chromatic parser torture** golden. **v1.1 required firmware:** **G9** ✅ · **G10** (`;nn` duty) remains.

## Quick links

- [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — master decision log
- [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) — author-facing status
- [xy-durations-handoff.md](xy-durations-handoff.md) — G9 feature brief (archive candidate)
- [decision-log-model.md](decision-log-model.md)
- [grammar_torture_v11.play](../../scripts/play_golden/grammar_torture_v11.play) — v1.1 torture golden
- [gen_v11_chromatic.py](../../scripts/gen_v11_chromatic.py) — golden regenerator (10-label cap)

## Read first

| Doc | Why |
| --- | --- |
| [play-v1-implementation-plan.md](play-v1-implementation-plan.md) | **§ MSG v1.1** — **G10** next |
| [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) | X/Y **YES** in duration table |
| [xy-durations-handoff.md](xy-durations-handoff.md) | G9 firmware notes |

## Suggested opener

```
/read-the-docs PLAY v1.1 G10 — raw-percent ;nn duty (D5b)
```

Or focused child:

```
You are a FOCUSED IMPLEMENTATION child. Ship MSG row G10 (raw-percent ;nn duty).
Read play-v1-implementation-plan.md G10 + D5b.
```

## Locked this session (G9 ✅)

### Firmware (G9 child)

- **`PLAY_DUR_W/H/Q/I/X/Y_X2`** — ladder rescaled ×4 (W=32 … Y=1)
- **`PLAY_DEFAULT_DUR_X2`** / **`PLAY_DEFAULT_BEAT_UNIT_X2`** → 8
- **`b_play_x2_from_duration_letter`** + descriptor FSM cases for **X**/**Y**
- **`psz_play_dur_suffix`** in `debug_menu.c` — resolve trace labels for X/Y
- **MSG:** **G9** ✅ · **D4** 🟢 · **GP8** ✅

### Golden torture (same session — parent revision)

- **`grammar_torture_v11.play`** — full chromatic **N0..N95** (96 semitones)
  - Ascending **X** via **`driver` → `asc0` → … → `asc3`** GOSUB chain
  - Descending **Y** via **`revdrv` → `desc3` → … → `desc0`** GOSUB chain
  - **Nested loops:** `[ [ ="driver" ]:2 ]:2` (4× each direction)
  - **Inner chunk loops:** `:3/:3/:2/:2` per 24-semitone block
  - **10 labels** (table max) — first draft used 18 labels → pre-parse `label table full`
- **`scripts/gen_v11_chromatic.py`** — regenerates golden within label cap
- **`tests.json`** — description + `chromatic-torture` alias

## Bench (COM9 @ 921600, build #8)

| Test | Timeout | Result |
| --- | --- | --- |
| `grammar_torture_v11` | **120 s** | PASS (~100 s wall-clock) |
| `grammar_torture` | 120 s | PASS (G9 regression) |
| `smoke` | 30 s | PASS |

```text
python scripts/play_bench.py --reset --timeout 120 test grammar_torture_v11
```

UART sync glitches on long runs remain **out of scope** (wishlist **W27** `uart_stream`).

## Still open (v1.1)

| ID | Item |
| --- | --- |
| **G10** | Raw-percent `;nn` duty (**D5b**) — only remaining **required** v1.1 PLAY row |
| **G11** | `uart_stream` stretch (optional) |

## Parent copy-paste block

```
G9 child complete — X/Y durations shipped; chromatic torture golden revised.

Shipped:
- PLAY_DUR ladder ×4 + X/Y parser (G9 firmware)
- grammar_torture_v11: N0..N95 chromatic, loops + 10-label GOSUB chains
- gen_v11_chromatic.py regenerator

Bench green (COM9, build #8): grammar_torture_v11 (120s), grammar_torture, smoke

Docs: session handoff 2026-06-14-g9; G10 next

Next: G10 — raw-percent ;nn duty (D5b)
```

## Known doc drift

- Run **`/cleanup-docs`** to archive `xy-durations-handoff.md` when convenient.

## Git note

Branch `main` · see wrapup commit hash below (no push by default).

**End of handoff**
