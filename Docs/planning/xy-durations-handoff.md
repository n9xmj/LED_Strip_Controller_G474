# Agent Handoff: G9 — X / Y durations (D4 / W1)

**Date:** 2026-06-14  
**Status:** ✅ SHIPPED — archive candidate (`/cleanup-docs`)  
**MSG:** **G9** ✅ · **Plan refs:** **D4** 🟢, **S5**, **GP8** ✅  
**Bench:** COM9 @ 921600 · build #8 · ST-Link SN `003C00193137510C39383538`

**Authoritative plan:** [play-v1-implementation-plan.md](play-v1-implementation-plan.md)  
**Session handoff:** [play-v1-session-handoff-2026-06-14-g9.md](play-v1-session-handoff-2026-06-14-g9.md)

---

## Shipped summary

- **S5:** X=0.25, Y=0.125 quarter-note beats; dot ×1.5 unchanged
- **`dur_x2` ladder ×4:** W=32, H=16, Q=8, I=4, X=2, Y=1
- **Defaults:** `PLAY_DEFAULT_DUR_X2` / `PLAY_DEFAULT_BEAT_UNIT_X2` = 8
- **Parser:** `b_play_x2_from_duration_letter` + descriptor sub-FSM (`b_play_parse_pitch_token`, rest, `ctx:`)
- **Golden:** `grammar_torture_v11.play` — dotted forms + long X/Y scale runs
- **Regression:** `grammar_torture` (v1) + `smoke` unchanged wall-clock (C4Q @ T240 = 250 ms)

## Files touched

| File | Change |
| ---- | ------ |
| `App/Src/play.c` | Constants, helper, 3× descriptor FSM |
| `App/Inc/play_config.h` | Default dur/beat unit ×4 |
| `App/Src/debug_menu.c` | Resolve trace suffix map |
| `scripts/play_golden/grammar_torture_v11.play` | Perf scale runs |

## Next

**G10** — raw-percent `;nn` duty (**D5b**).

**End of xy-durations-handoff.md**
