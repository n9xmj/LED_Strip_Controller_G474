# Session handoff — line editor + bounded fields (2026-06-21)

## Purpose

Shipped `x_term_getline_editor()` (full-line + bounded single-row fields), HuIL bench
tests under the `<term>` submenu, and golden harness vectors. Bounded-field form
navigation (Tab / Shift-Tab) bench-tested OK on hardware.

**2026-06-22 follow-on (canvas rework):** removed dynamic soft-wrap machinery;
unified fixed-canvas init; bounded horizontal viewport (**W16**).

## Read first

- [`Docs/planning/line-editor-plan.md`](../Docs/planning/line-editor-plan.md) — decision log + S11/W16 canvas model
- [`App/Inc/term.h`](../App/Inc/term.h) — `term_line_edit_t`, `term_line_t` exit codes
- [`App/Src/term.c`](../App/Src/term.c) — implementation
- [`App/Src/test_harness.c`](../App/Src/test_harness.c) — HuIL `[l]` / `[f]`, harness `E` op
- [`scripts/term_golden/lineedit.json`](../scripts/term_golden/lineedit.json) — golden vectors

## Shipped (2026-06-21 session)

- **`x_term_getline_editor()`** — navigation, history, kill keys
- **`term_line_edit_t`** — `pc_prompt`, `u16_field_width`, `pc_line` default-on-entry
- **Bounded field mode** — Tab / Shift-Tab, no newline on exit, HuIL `[f]`
- **Harness:** `E` op + `lineedit.json` + `term_lineedit_bench.py`

## Shipped (2026-06-22 canvas rework)

- **Fixed canvas init (both modes):** `N` spaces → optional CEL (unbounded only) →
  `CUB(N)` → `DECSC`; cursor at end of `pc_line` on entry / history recall
- **Unbounded (`field_width == 0`):** `N = max_len - 1`; terminal scroll-up OK; no
  layout preservation; newline on ENTER
- **Bounded (`field_width != 0`):** `N = min(caller, cols to EOL)`; entry limit
  `max_len - 1`; **`u16_view_offset`** horizontal viewport (**W16**)
- **Repaint:** partial suffix + space-pad (bounded destructive); full canvas fill
  when viewport offset changes or unbounded edit; **removed** `b_wrap_mode`, CPR
  re-anchor, `v_line_full_redraw`, DCH window clear

## Still open / next steps

- **W11** — Tab completion hook (Tab = accept field in bounded mode today)
- **W12** — insert/overwrite toggle (same repaint core; buffer policy only)
- **W15** — multiline sub-window editor
- Golden vectors for bounded viewport edge cases (long preload + Left scroll)
- Wire bounded editor into real forms (PLAY bench, settings UI)
- Bench HuIL `[f]` / `[l]` after flash

## Gotchas / invariants

- **`field_width == 0`** = unbounded canvas, **not** “full line to EOL only”
- **Bounded never sends CEL** on init or tail fixup — space pad only
- **`max_len`** = buffer capacity incl. NUL; **`field_width`** = display viewport only
- **Initial text** = `pc_line` at call time; cursor at **end**; viewport end-visible
  when `len > field_width`
- **History** append on ENTER / TAB / SHIFT_TAB; ESC/Ctrl-C never append

## Git note

Branch: `main` — canvas rework builds clean (Debug, 0 warnings).
