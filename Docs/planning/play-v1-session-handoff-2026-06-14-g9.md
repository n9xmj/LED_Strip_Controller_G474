# PLAY v1 session handoff — 2026-06-14 (G9 shipped — X/Y durations)

**Purpose:** Fresh-chat primer after **G9** sixteenth/thirty-second durations. **v1.1 required firmware:** **G9** ✅ · **G10** (`;nn` duty) remains.

## Quick links

- [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — master decision log
- [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) — author-facing status
- [xy-durations-handoff.md](xy-durations-handoff.md) — G9 feature brief (archive candidate)
- [decision-log-model.md](decision-log-model.md)

## Read first

| Doc | Why |
| --- | --- |
| [play-v1-implementation-plan.md](play-v1-implementation-plan.md) | **§ MSG v1.1** — **G10** next |
| [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) | X/Y **YES** in duration table |
| [xy-durations-handoff.md](xy-durations-handoff.md) | G9 implementation notes |

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

- **`PLAY_DUR_W/H/Q/I/X/Y_X2`** — ladder rescaled ×4 (W=32 … Y=1)
- **`PLAY_DEFAULT_DUR_X2`** / **`PLAY_DEFAULT_BEAT_UNIT_X2`** → 8
- **`b_play_x2_from_duration_letter`** + descriptor FSM cases for **X**/**Y**
- **`psz_play_dur_suffix`** in `debug_menu.c` — trace labels for X/Y
- **Golden:** `grammar_torture_v11.play` — dotted smoke + two-octave X/Y scale runs (perf stress)
- **MSG:** **G9** ✅ · **D4** 🟢 · **GP8** ✅

## Bench (all PASS, COM9 @ 921600, build #8)

- `grammar_torture_v11` · `grammar_torture` · `smoke` (STRICT)

## Still open (v1.1)

| ID | Item |
| --- | --- |
| **G10** | Raw-percent `;nn` duty (**D5b**) — only remaining **required** v1.1 PLAY row |
| **G11** | `uart_stream` stretch (optional) |

## Known doc drift

- Run **`/cleanup-docs`** to archive `xy-durations-handoff.md` when convenient.

## Git note

WIP commit from `/wrapup` on `main` — see commit hash in wrapup report.

**End of handoff**
