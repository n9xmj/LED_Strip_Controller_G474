# PLAY v1 session handoff — 2026-06-14 (G10 shipped — raw-percent `;nn` duty)

**Purpose:** Fresh-chat primer after **G10** raw-percent duty disambiguation. **v1.1 required PLAY firmware:** **G9** ✅ · **G10** ✅ — **complete**.

## Quick links

- [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — master decision log
- [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) — author-facing status
- [archive/duty-percent-handoff.md](archive/duty-percent-handoff.md) — G10 feature brief (archived)
- [decision-log-model.md](decision-log-model.md)
- [duty_percent.play](../../scripts/play_golden/duty_percent.play) — G10 golden

## Read first

| Doc | Why |
| --- | --- |
| [play-v1-implementation-plan.md](play-v1-implementation-plan.md) | **§ MSG v1.1** — closed; optional **G11** stretch |
| [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) | `;nn` percent duty **YES** |
| [archive/duty-percent-handoff.md](archive/duty-percent-handoff.md) | G10 implementation notes (archived) |

## Suggested opener

```
/read-the-docs PLAY v1.1 complete — optional G11 uart_stream or mic/DSP pivot
```

## Locked this session (G10 ✅)

### Firmware (`App/Src/play.c`)

- **`v_play_apply_duty_percent`** — `num/den = pct/100`, clamp 0–100 (`;00` = silence)
- **`b_play_scan_duty_digit_run`** — at most **2** digits after `;` (not 5-digit executive run)
- **`b_play_apply_duty_semicolon_suffix`** — 0 digits → normal; 1 → D5c n/8; 2 → D5b percent
- Wired into pitch, absolute/`N`, and **`ctx:`** suffix paths (replaces three duplicated `case ';':` blocks)

### Golden / bench

- **`scripts/play_golden/duty_percent.play`** + **`tests.json`** entry (`G10`, `D5b`, `duty-nn`)

## Bench (COM9 @ 921600, build #8)

| Test | Timeout | Result |
| --- | --- | --- |
| `duty_percent` | 60 s | PASS |
| `grammar_torture` | 120 s | PASS (`;6`, `ctx:5Q;4` regression) |
| `grammar_torture_v11` | 120 s | PASS |
| `smoke` | 30 s | PASS |

```text
python scripts/play_bench.py --reset --timeout 60  test duty_percent
python scripts/play_bench.py --reset --timeout 120 test grammar_torture
python scripts/play_bench.py --reset --timeout 120 test grammar_torture_v11
python scripts/play_bench.py --reset --timeout 30  test smoke
```

## Still open (optional)

| ID | Item |
| --- | --- |
| **G11** | `uart_stream` stretch (USART2 non-blocking console) |
| **GP*** | T1/T4/T5 docs, `m`→`g` on-device golden runner |

## Parent copy-paste block

```
G10 child complete — raw-percent ;nn duty disambiguates ;6 (75%) vs ;60 (60%).

Shipped:
- v_play_apply_duty_percent + b_play_apply_duty_semicolon_suffix (≤2 digit cap)
- Refactored ; suffix in pitch, N, and ctx: paths
- duty_percent.play golden + tests.json

Bench green (COM9, build #8): duty_percent, grammar_torture, grammar_torture_v11, smoke

Docs: MSG G10 ✅, D5b 🟢, I10, chatbot brief, session handoff 2026-06-14-g10

Next: v1.1 required MSG complete; optional G11 uart_stream stretch.
```

## Git note

Shipped on `main`: `d9a3330` (brief) · `b08fc90` (G10 firmware + golden + docs) · post-G10 cleanup pending push.

**End of handoff**
