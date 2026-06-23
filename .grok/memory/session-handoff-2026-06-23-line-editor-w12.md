# Session handoff — 2026-06-23 — line editor W12 + bench skill

**Purpose:** Fresh-chat primer for the `term.*` line editor. This session fixed two
bounded-mode render regressions, shipped **W12** (insert/overwrite toggle), and added a
**`/bench`** config skill. Picks up from
[`session-handoff-2026-06-21-line-editor.md`](session-handoff-2026-06-21-line-editor.md).

## Read first
- [`Docs/planning/line-editor-plan.md`](../Docs/planning/line-editor-plan.md) — Big Board +
  wish list; W12 rows (D18/S12/I8/Q6/T5) all 🟢, **W17** added (field SGR attributes).
- [`App/Src/term.c`](../App/Src/term.c) — editor; `v_line_repaint_suffix`, `v_line_insert_char`,
  `x_term_getline_editor`, `v_line_mode_cursor_update/restore`.
- [`AGENTS.md`](../AGENTS.md) § *Terminal line editor* — updated with the repaint invariant +
  W12 + the Tera Term DECSCUSR setting.
- [`scripts/term_golden/lineedit.json`](../scripts/term_golden/lineedit.json) +
  [`lineedit_field.json`](../scripts/term_golden/lineedit_field.json) — golden vectors.

## Shipped this session
- **Bounded mid-line insert repaint fix** (`a53d52f`): `v_line_repaint_suffix()` now takes the
  first *changed* buffer index — `cursor-1` for insert, `cursor` for delete/backspace — and
  **clamps the reprint to the viewport** (`view_offset+canvas_cells`). Fixed: typed `*` showing
  as stranded `iiiii`, and a scrolled line's tail spilling over the next field (`Label 2:`).
- **W12 insert/overwrite toggle** (`d55fad1`): `Insert` flips INS⇄OVR (session-local, always
  starts insert — Q6 option B, no `b_start_overwrite`). Overwrite replaces in place (no length
  change), EOL-appends. Opt-in DECSCUSR cursor cue via new `term_line_edit_t.b_show_mode_cursor`
  (bar=INS / block=OVR), restored on every exit; new `v_term_cursor_style()` + `ANSI.h` macros.
  4 new golden vectors. Benches 15/15 + 13/13. HuIL-verified on bench.
- **`/bench` skill** (`ebab0cb`): bench config report + freeform editor over `discover.py`;
  mirrored in `.claude/skills/bench/` and `.grok/skills/bench/`.
- **CubeIDE Debug launch config** committed (`14a2e78`).

## Still open / next steps
- **W17** (new wishlist) — field display attributes (SGR `ESC[…m`): reverse/underline/bold
  applied to entry-field text, both modes (most useful bounded). Opt-in like `b_show_mode_cursor`;
  wrap painted cells + `ANSI_ATTR_OFF`, restore on exit. Stretch: FG/BG colors. Open Qs: attr
  bitmask vs caller SGR string; style prompt too or just the field.
- Other wish-list backlog: W8 (word motion), W9 (kill-ring/yank), W10 (Ctrl-R history search),
  W11 (completion hook), W15 (multiline mini-editor), W14 (complete `v_term_*` set).

## Gotchas / invariants
- **Repaint start index is load-bearing** — see the AGENTS.md invariant. Insert ≠ delete.
- **DECSCUSR is gated in Tera Term** — *Setup → Additional settings → Control Sequence →
  "Cursor control sequence"* ships **off**; the cue is silently dropped until enabled (mechanics
  still work). The space intermediate byte (`ESC [ Ps SP q`) is mandatory.
- Golden benches assert **buffer only**, not screen — render bugs pass them. Visual checks are
  HuIL (`<term>` → `l`/`f`).
- HuIL handlers enable `b_show_mode_cursor`; the harness `E`/`B` golden ops leave it off (no
  DECSCUSR in captured output).

## Git note
Branch `main`, after wrapup commit (see hash below). 8 commits ahead of `origin/main`, **not
pushed** (per default).

## Suggested opener for next session
```
/read-the-docs line editor — resume term.* work. Last session shipped W12 (insert/overwrite)
and a /bench skill; next candidate is W17 (field SGR display attributes). See
.grok/memory/session-handoff-2026-06-23-line-editor-w12.md.
```
