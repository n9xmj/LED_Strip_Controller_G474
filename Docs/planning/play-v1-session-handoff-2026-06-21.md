# PLAY v1.2 session handoff — 2026-06-21

**Purpose:** Fresh-chat primer — PLAY **v1.2 grammar is shipped and pushed** on G474; next work is v1.3+ planning or another project track.

## Read first

- [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — § MSG (v1.2 closed), **PLAY v1.3+** bracket reconsideration, **W35** JSON stub
- [Docs/Player/cheat_sheet.md](../Player/cheat_sheet.md) — v1.1→v1.2 migration table
- [Docs/Player/chatbot_brief.md](../Player/chatbot_brief.md) — LLM-facing v1.2 wire
- [App/Src/play.c](../../App/Src/play.c) — interpreter source of truth
- [scripts/play_golden/tests.json](../../scripts/play_golden/tests.json) — bench golden manifest

## Shipped this session (pushed to `origin/main`)

| MSG | Feature |
| --- | ------- |
| **G13** / **G15** | Signed repeat `]:±N` (**S12**/**S14**); multi-dot (**D26**) |
| **G14** / **G20** | GOSUB `+`/`-` outside quotes; quoted labels only (**D29**) |
| **G12** | **D25** char swap — **`=`** goto · **`>`** GOSUB |
| **GP9** | Living docs + in-place golden migration (no `grammar_torture_v12`) |

**Bench:** `labels_*`, `grammar_torture`, `repeat_*`, `multi_dot`, `key_snapshot` — PASS on COM9.

**Git (HEAD):** `960aceb` on `main` — also includes `b8453ef`, `d8bb36e` from same arc.

## Still open / next steps

| Track | Items |
| ----- | ----- |
| **v1.3+ firmware** | **G16**–**G19** (ties, synth continuity, tuplets, measure `\|`) — planning only |
| **Bracket syntax** | 🟡 Author lean: `{}` loops, `[]` or bare `\|` measures — conflicts with D27 tie lean; resolve before G16 locks |
| **Doc Phase 2** | **T4** EBNF, **T5** howto (**GP4**/**GP5**) |
| **Wish** | **W35** JSON alternate ingest (parallel interpreter, not PLAY replacement) |
| **Infra** | **G11** `uart_stream` shipped — plan MSG cleaned; no PLAY work required |

**Not default next session:** PLAY v2 polyphony unless user names it.

## Gotchas / invariants

- **v1.2 wire:** `="lbl"` goto · `>"lbl"` GOSUB · `>-"lbl"` no caller restore on `/`
- **Labels:** quoted text only — bare `>99` fatal at pre-parse
- **Repeat:** `]:-N` = no `[` restore; count = **max(1, N)**
- **`grammar_torture_v11.play`:** intentional **v1.1** `>`/`=` archive for X/Y chromatic torture
- **Golden pitfall:** `read_play_file()` joins lines with **no separator** — keep spaces between tokens on one logical line
- **Linker:** `-Wl,--wrap=fflush` must survive CubeMX regen (console drain)

## Known doc drift

- LOCKED CONTEXT / long plan body still has scattered v1.1 `>` goto examples in historical sections — living docs (**GP9**) are authoritative for wire; full plan scrub optional.
- **D19** detail table still shows old `=` GOSUB in one locked wire table (historical).

## Suggested opener (next session)

```text
/read-the-docs
Focus: <pick one — e.g. v1.3+ G16 bracket pass, I2S/DSP, terminal piano, PROJECT wishlist item>
```

---

*End of handoff — written by `/wrapup` 2026-06-21.*
