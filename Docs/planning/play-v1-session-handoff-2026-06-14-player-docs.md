# PLAY v1 session handoff — 2026-06-14 (Player docs Phase 1 + W29 dyn syntax)

**Purpose:** Fresh-chat primer after **Player/** doc suite Phase 1 and **W29** dynamics syntax leaning. **Firmware:** v1 + v1.1 required MSG **complete** (G1–G10).

## Quick links

- [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — master decision log
- [../Player/README.md](../Player/README.md) — user doc suite hub
- [../Player/cheat_sheet.md](../Player/cheat_sheet.md) · [../Player/chatbot_brief.md](../Player/chatbot_brief.md)
- [uart_stream-port-notes.md](uart_stream-port-notes.md) — **G11** stretch (next code candidate)
- [decision-log-model.md](decision-log-model.md)

## Read first

| Doc | Why |
| --- | --- |
| [../Player/README.md](../Player/README.md) | Doc suite routing — Phase 2 targets |
| [play-v1-implementation-plan.md](play-v1-implementation-plan.md) | **W29** `dyn:` leaning · **T4/T5** · **G11** |
| [uart_stream-port-notes.md](uart_stream-port-notes.md) | If spawning **G11** firmware child |

## Suggested openers

**Docs Phase 2 (T4 EBNF):**

```
/read-the-docs PLAY doc Phase 2 — T4 v1_grammar.md in Docs/Player/
```

**G11 uart_stream (parallel track):**

```
/read-the-docs G11 uart_stream — port per uart_stream-port-notes.md; App/ + Core USER CODE only with approval
```

## Locked this session

### Shipped (committed `0c5409a`)

- **`Docs/Player/`** — README hub, `cheat_sheet.md`, `chatbot_brief.md` (post-G10 sync)
- Planning redirect stubs; legacy `PLAY_language_design.md` header trim
- Plan **GP6** / **T1** partial; cross-links (AGENTS, PROJECT, skills)

### Locked in plan (wrapup commit)

- **W29** 🟡 — step dynamics via **`\"dyn:xxx"`** (D18): `p` `mp` `f` `ff` `sfz` `fp`, …; **V-relative** scaling
- Ramp cmds leaning **`\"cresc:`** / **`\"dim:`** (🔵 open arg grammar)
- **D18** table — **`dyn:`** noted as v2 planned handler

### Workflow decisions

- **Composer 2.5** for doc Phase 2 (T4/T5); **Auto** OK for fenced firmware children
- **Parallel OK:** uart_stream child now; resume T4/T5 in archived/fresh doc session

## Still open

| ID | Item |
| --- | --- |
| **T4** | [`Docs/Player/v1_grammar.md`](../Player/v1_grammar.md) — normative EBNF |
| **T5** | [`Docs/Player/howto.md`](../Player/howto.md) — musician manual |
| **T1** | Full dedupe of legacy design notebook |
| **G11** | `uart_stream` on USART2 — [uart_stream-port-notes.md](uart_stream-port-notes.md) |
| **W29** | Ramp syntax + scale factor table; firmware v2 |

## Git note

- **`0c5409a`** — Player/ Phase 1 docs (on `origin/main`)
- Wrapup commit on `main` — W29 plan + this handoff (local; not pushed unless user asks)

**End of handoff**
