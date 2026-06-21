# PLAY — user documentation

**PLAY** is a compact, monophonic music macro language for the G474 bench interpreter (`App/Src/play.c`). Lineage: TRS-80 Model I commercial music player → this firmware path. See [PROJECT.md](../PROJECT.md) § *Product lineage*.

**Firmware status (2026-06-14):** v1 + v1.1 **required** MSG rows **G1–G10** shipped on STM32G474.

---

## Which doc to read

| You are… | Read |
| -------- | ---- |
| **Musician / author** | [howto.md](howto.md) *(Phase 2 — musician manual)* |
| **LLM or chat assistant** | [chatbot_brief.md](chatbot_brief.md) — copy-paste bounded YES/NO/PARTIAL |
| **Human needing one screen of syntax** | [cheat_sheet.md](cheat_sheet.md) — lead-char map |
| **Implementer / host parser author** | `v1_grammar.md` *(Phase 2 — normative EBNF, planned)* + [implementation plan](../planning/play-v1-implementation-plan.md) |
| **Planner / decision resolver** | [play-v1-implementation-plan.md](../planning/play-v1-implementation-plan.md) — Big Board, MSG, D/S/I IDs |

---

## Source-of-truth hierarchy

1. **Firmware** — `App/Src/play.c`, `App/Inc/play_config.h`, golden strings in `scripts/play_golden/`
2. **Decision log** — [play-v1-implementation-plan.md](../planning/play-v1-implementation-plan.md) (MSG, I10 audit)
3. **Living docs** — this folder (`cheat_sheet.md`, `chatbot_brief.md`)
4. **Legacy notebook** — [PLAY_language_design.md](../PLAY_language_design.md) (historical; not user-facing)

When `play.c` behavior changes, update **cheat sheet + chatbot brief + plan I10** in the same change set.

---

## Documents in this folder

| File | Status | Role |
| ---- | ------ | ---- |
| [cheat_sheet.md](cheat_sheet.md) | **Current** | One-screen lead-char reference |
| [chatbot_brief.md](chatbot_brief.md) | **Current** | LLM / author bounded brief |
| `v1_grammar.md` | **Planned (T4)** | Normative EBNF — programmer's reference |
| `howto.md` | **Planned (T5)** | Musician howto + tiered examples |

Plan detail: [play-v1-implementation-plan.md](../planning/play-v1-implementation-plan.md) § Tooling/docs (**T1**, **T4**, **T5**, **GP6**).

---

## Bench quick start

```text
@ smoke scale @ CQ4DEFGABC5 *
```

Host feed: `scripts/play_bench.py` · skills `/playstr`, `/playtest`, `/playfile`. See [chatbot_brief.md](chatbot_brief.md) § Bench.

**End of README**
