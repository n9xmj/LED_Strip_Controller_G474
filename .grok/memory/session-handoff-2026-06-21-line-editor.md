# Session handoff — line editor + bounded fields (2026-06-21)

## Purpose

Shipped `x_term_getline_editor()` (full-line + bounded single-row fields), HuIL bench
tests under the `<term>` submenu, and golden harness vectors. Bounded-field form
navigation (Tab / Shift-Tab) bench-tested OK on hardware.

## Read first

- [`Docs/planning/line-editor-plan.md`](../Docs/planning/line-editor-plan.md) — decision log + S11 field window
- [`App/Inc/term.h`](../App/Inc/term.h) — `term_line_edit_t`, `term_line_t` exit codes
- [`App/Src/term.c`](../App/Src/term.c) — implementation
- [`App/Src/test_harness.c`](../App/Src/test_harness.c) — HuIL `[l]` / `[f]`, harness `E` op
- [`scripts/term_golden/lineedit.json`](../scripts/term_golden/lineedit.json) — golden vectors

## Shipped this session

- **`x_term_getline_editor()`** — navigation, history, kill keys, soft-wrap full-line mode
- **`term_line_edit_t`** — `pc_prompt`, `u16_field_width`, `pc_line` default-on-entry (not from history)
- **Bounded field mode** (`u16_field_width != 0`):
  - EOL clamp so field cannot wrap to next row
  - Space-overwrite clear (never DCH on field — preserves inline neighbors)
  - Tab / Shift-Tab accept with `TERM_LINE_TAB` / `TERM_LINE_SHIFT_TAB` (ignored in full-line mode)
  - No trailing newline on exit (form stays on one row)
  - Init: prompt → clear field cells → paint `pc_line` (truncated to field width) → cursor at EOL
- **HuIL:** `[l]` full-line (120 chars, `field_width=0`); `[f]` three-label form with Tab cycling + per-field buffers
- **Harness:** `E` op + `lineedit.json` + `term_lineedit_bench.py`

## Still open / next steps

- **W7** — long-line wrap polish + more golden edge cases
- **W11** — Tab completion hook (Tab now means “accept field” in bounded mode)
- **W15** — multiline sub-window editor
- Host golden runs may need COM port free (Tera Term lock on COM9)
- Wire bounded editor into real forms (PLAY bench, settings UI, etc.)

## Gotchas / invariants

- **Zero-init `term_line_edit_t`** — unset `u16_field_width` is NOT bounded; explicit `0` = full-line wrap
- **Bounded clear = spaces** — `v_term_delete_chars` / DCH shifts the rest of the row and destroys inline labels
- **`max_len`** = buffer capacity incl. NUL; **`field_width`** = display/entry window (clamped to EOL)
- **Initial text** = `pc_line` at call time only; caller sets `pc_line[0]='\0'` for empty field
- **History** append on ENTER / TAB / SHIFT_TAB accept; Up/Down never preloads on entry
- **Shift-Tab wire:** `CSI Z` (`EXT_KEY_SHIFT_TAB`) and `ESC Z` (`EXT_MOD_ALT|'Z'`)

## Git note

Branch: `main` (see commit hash printed in wrapup report).

## Suggested opener (next session)

```
/read-the-docs line editor follow-on

Continue from session-handoff-2026-06-21-line-editor.md — <your focus>.
```
