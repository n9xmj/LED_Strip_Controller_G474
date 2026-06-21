# Line Editor — implementation plan (decision log)

**Feature:** `x_term_getline_editor()` — a cooperative, in-line text entry editor
built **on top of** the extended-key reader (`i16_term_get_key()`). Modern-shell
basics: left/right/Home/End navigation, destructive backspace, forward-delete,
whole-line erase, and an optional **caller-owned input-history buffer** (Up/Down
recall). Enter accepts, bare ESC cancels, Ctrl-C aborts.

**Parent reader:** [`extended-key-input-plan.md`](extended-key-input-plan.md) —
building block #1 (the key decoder this editor consumes). This editor is the
**line-editing layer** of the same self-designed terminal library and was tracked
there as wish-list item **W1**; promoted to its own board here.

**Home:** the `term.*` module (`App/Src/term.c` / `App/Inc/term.h`), alongside the
key reader and the size/cursor queries.

**Status:** PLANNING · **Working mode:** resolve OPEN rows in chat by ID
(*"green D17"*, *"S9 yes"*, *"D13 your call"*); agent updates this doc + detail
sections same session. No firmware written until the relevant rows lock.

**Planning model:** [`decision-log-model.md`](decision-log-model.md). Not PLAY work;
no Must-Ship-Gap fence — just a Big Board + wish list. **ID numbering continues the
sibling plan's sequence** (it ended at D12 / S6 / I4 / T3 / Q3) so the two boards
never collide.

**North star:** small composable primitives sharing the cooperative,
timeout-driven, ESC-burst core: *key input → **line editing** → screen/cursor →
queries.* This is the line-editing rung. Inspiration: GNU readline / bash line
editing, scoped down to what a bench console actually needs.

---

## Proposed shape

```c
typedef enum
{
    TERM_LINE_ENTER  = 0,   /* accepted via Enter (CR)                 */
    TERM_LINE_ESCAPE,       /* bare ESC cancel                         */
    TERM_LINE_CTRLC,        /* Ctrl-C abort                            */
    TERM_LINE_ERROR         /* bad args (NULL line, zero capacity, ...) */
}
term_line_t;

/* pc_line is ALWAYS valid on return (current entry text), regardless of how the
 * editor exited — the caller decides what to do based on the return code. */
term_line_t x_term_getline_editor(char     *pc_line,        /* in/out: entry text (NUL-terminated)            */
                                  uint16_t  u16_max_len,    /* capacity of pc_line incl. NUL                  */
                                  uint8_t  *pu8_hist,       /* in/out history pool; NULL = history disabled   */
                                  uint16_t  u16_hist_size); /* sizeof(history pool) in bytes                  */
```

Naming follows the project Hungarian + the sibling plan: enum return ⇒ `x_` prefix,
`term_` module tag, `term_line_t` per the `*_t` convention (**Q4**).

---

## The Big Board

| ID | Status | Subject (one line) |
|----|--------|-------------------|
| **D13** | 🟡 | **Redraw** = full-line repaint per edit (`DECSC` origin → `ESC8` → `CLEAR_EOL` → reprint → reposition); **v1 assumes the entry fits one terminal row** (soft-wrap deferred → W7) |
| **D14** | 🟡 | **Prompt ownership** = caller prints its own prompt; editor captures origin via `DECSC` (`ESC 7`) at entry. No prompt parameter |
| **D15** | 🟡 | **Exit code** = `term_line_t` enum (ENTER/ESCAPE/CTRLC/ERROR); `pc_line` valid on every return |
| **D16** | 🟢 | **Backspace vs Delete**: both `0x08` and `0x7F` ⇒ destructive delete-left; `EXT_KEY_DELETE` (`ESC[3~`) ⇒ forward-delete + collapse |
| **D17** | 🔴 | **Whole-line / kill key binding** — *needs author call* (Ctrl-X whole-line vs readline Ctrl-U/Ctrl-K vs both) |
| **S7** | 🟡 | **History format** = flat caller buffer of packed NUL-terminated strings, oldest→newest, used region ended by an empty string (extra NUL); rest zeroed. No header, no malloc, self-describing |
| **S8** | 🟡 | **History semantics** = Up=older/Down=newer; in-progress line kept as scratch (index 0); recalled entries editable; ENTER appends edited line as newest, **de-dupe consecutive dup**, **FIFO-evict oldest** when full; ESC/Ctrl-C never append |
| **S9** | 🔴 | **Seed initial text** — honor a non-empty `pc_line` on entry as editable preload (cursor at end)? *Needs author call* |
| **I5** | 🟡 | **Input source** = loop on `i16_term_get_key(small_timeout)`, ignore `TERM_KEY_NONE`; cooperative (pumps polling task via the reader) |
| **I6** | 🟢 | **ANSI primitives** — none new needed; `ANSI.h` already has CHA / CUF / CUB / `CLEAR_EOL` / `DECSC` / `DECRC` |
| **T4** | 🟡 | **Test** = harness `E` op (inject scripted byte stream incl. edit escapes; frame resulting line + exit code [+ history]) + golden vectors + HuIL menu entry under `<term>` submenu |
| **Q4** | 🟡 | **Placement / naming** = `term.c` / `x_term_getline_editor` returning `term_line_t` |

Status: 🔴 open · 🟡 leaning · 🟢 resolved · 🔵 deferred.

**Open rows needing an author call:** **D17**, **S9** — plus confirm/adjust the 🟡
leanings (especially **D13** single-row scope and **S8** dedupe/evict policy).

---

## v1 key bindings (proposed)

| Key | Bytes (via reader) | Action |
|-----|--------------------|--------|
| Left / Right | `EXT_KEY_LEFT` / `EXT_KEY_RIGHT` | move cursor one char |
| Home / End | `EXT_KEY_HOME` / `EXT_KEY_END` | jump to start / end |
| Backspace | `0x08` or `0x7F` | delete char left, collapse (**D16**) |
| Delete | `EXT_KEY_DELETE` (`ESC[3~`) | delete char right, collapse (**D16**) |
| Up / Down | `EXT_KEY_UP` / `EXT_KEY_DOWN` | history older / newer (**S8**) |
| *(erase)* | TBD | erase line / kill (**D17**) |
| Enter | `0x0D` (CR) | accept → `TERM_LINE_ENTER` |
| ESC (bare) | `0x1B` | cancel → `TERM_LINE_ESCAPE` |
| Ctrl-C | `0x03` | abort → `TERM_LINE_CTRLC` |
| printable | `0x20..0x7E` | insert at cursor |
| anything else | — | ignored in v1 |

**ESC disambiguation is free:** the reader only returns a bare `0x1B` when nothing
follows within the inter-byte gap, so arrow/function keys arrive as `EXT_KEY_*` and a
real Escape press arrives as `0x1B` — the editor never has to time ESC itself.

---

## Detail sections

### D13 — redraw strategy
**Leaning:** repaint the whole entry on every edit (simplest correct model for a
serial console; at 921600 baud a full line is microseconds):
1. **At entry:** emit `ANSI_SAVE_CURSOR` (`ESC 7`, DECSC) to pin the origin = wherever
   the caller's prompt left the cursor (**D14**).
2. **Each redraw:** `ANSI_RESTORE_CURSOR` (`ESC 8`) → `ANSI_CLEAR_EOL` (`CSI K`) →
   `printf` the buffer → reposition the cursor (`ANSI_CURSOR_LEFT_FMT` by
   `len − cursor`, or restore + `ANSI_CURSOR_RIGHT_FMT` by `cursor`).
3. **On exit:** emit a newline so subsequent output starts cleanly.

**v1 scope limit:** assumes the entry stays on **one terminal row** (no soft-wrap
repaint). `CSI K` only clears the origin row, and cursor-forward/back motion doesn't
cross the right margin, so a wrapped line would mis-render. Multi-row wrap-aware
redraw is deferred (**W7**). Practical ceiling ≈ terminal width − prompt width; we
*could* clamp `u16_max_len` against a queried width (`b_term_get_size`) — optional.

### D14 — prompt ownership
**Leaning:** the **caller** prints its prompt before calling; the editor just pins the
origin with DECSC at entry and redraws from there. No prompt parameter — keeps the
signature lean and lets the caller use any prompt styling (color/SGR) it wants.
Trade-off: relies on DECSC/DECRC (one shared save register) — fine here; nothing else
nests inside the editor.

### D15 — exit code & line contract
**Leaning:** small enum `term_line_t`. `pc_line` is **always** NUL-terminated and
holds the current line on every return path (accept *or* abort), mirroring the
existing `i_getline` "what's on the line now" behavior — the caller keys off the
return code to accept/discard. `TERM_LINE_ERROR` for bad args (NULL `pc_line`, zero
capacity); on error `pc_line` is left untouched if possible.

### D16 — backspace vs delete *(resolved)*
Treat **both** `0x08` (BS) and `0x7F` (DEL byte) as **destructive backspace**
(delete-left). Modern terminals (incl. Tera Term defaults) send `0x7F` for the
Backspace key, while the dedicated Delete key sends `ESC[3~` → `EXT_KEY_DELETE`, which
the editor maps to **forward delete + collapse**. This is the conventional split and
avoids the classic BS/DEL ambiguity biting the user.

### D17 — whole-line / kill key *(OPEN — author call)*
Options:
- **(A) Ctrl-X = erase entire line** — matches the legacy `i_getline` (it already uses
  Ctrl-X for cancel/re-enter), so existing muscle memory carries over. One key.
- **(B) Ctrl-U = kill to start + Ctrl-K = kill to end** — the readline/bash standard;
  two keys, more capable, sets up a future kill-ring (**W9**).
- **(C) Both** — Ctrl-X whole-line *and* the Ctrl-U/Ctrl-K pair.

*No leaning recorded yet — your pick.* (A) is the consistency play; (B)/(C) are the
power-user play.

### S7 — history buffer format
**Leaning:** the caller-owned `pu8_hist` pool holds **packed NUL-terminated strings,
oldest first**; the used region ends at the first **empty string** (a lone extra
NUL), and the remainder stays zeroed. Example pool after entering `ls`, then `cd /`:

```
6c 73 00   2f? ...        "ls\0" "cd /\0" "\0"  (trailing empty string = end marker)
'l''s'\0 'c''d'' ''/'\0 \0
```

- **Self-describing**, no header struct, no `malloc` (so no leak risk — addresses the
  author's stated concern), survives across calls, easy to pre-seed by hand.
- **Append (on ENTER):** scan to the end marker, write the new string + its NUL, write
  the new end-marker NUL. **Evict oldest** (front) by `memmove` when it won't fit
  (**S8**).
- **Robustness:** the editor re-parses the pool from scratch each call and **bounds
  every scan by `u16_hist_size`**, tolerating a malformed/unterminated pool without
  running off the end.

*Alternative considered & rejected:* array of pointers to `malloc`'d sub-buffers —
cleaner indexing but reintroduces the leak/ownership risk the author flagged.

### S8 — history navigation semantics
**Leaning (readline-style):**
- **Up** = older entry, **Down** = newer. A session-local index walks the pool.
- **Index 0 = the live working line.** Before the first Up, stash the in-progress line
  to a scratch so Down can restore it (you don't lose what you were typing).
- Recalled entries are **editable**; editing a recalled entry does **not** mutate the
  stored copy.
- **On ENTER** (non-empty line): append the *current* (possibly edited) line as the
  newest entry. **De-dupe** if identical to the most-recent entry (bash-like).
  **FIFO-evict** the oldest when the pool is full.
- **ESC / Ctrl-C never append.**

*Open sub-questions:* dedupe consecutive duplicates (lean yes) and evict-oldest vs
refuse-new when full (lean evict-oldest).

### I5 — input source / cooperative loop
**Leaning:** drive entirely off `i16_term_get_key()` (the building-block-#1 reader),
called with a small timeout in a loop; ignore `TERM_KEY_NONE`. That reader already
pumps `v_app_polling_task()` each spin, so the editor inherits the cooperative,
non-starving behavior for free — no second input path, no driver poking.

### I6 — ANSI primitives *(resolved by inspection)*
Everything the redraw needs already exists in `ANSI.h`:
`ANSI_SAVE_CURSOR`/`ANSI_RESTORE_CURSOR` (DECSC/DECRC), `ANSI_CLEAR_EOL` (`CSI K`),
`ANSI_CURSOR_LEFT_FMT`/`ANSI_CURSOR_RIGHT_FMT` (`CSI nD`/`CSI nC`),
`ANSI_HORIZONTAL_ABS_FMT` (`CSI nG`). No additions required.

### T4 — test strategy
**Leaning:** mirror the existing harness pattern (T2/T3).
- **Harness `E` op:** inject a scripted byte stream (printables + edit escapes like
  `1B5B44` for Left, `08` for BS, terminator `0D`/`1B`/`03`) via `v_term_inject`, run
  the editor, and frame the result: `<HRN E rc=<code> line="..." [hist=...]>`. The
  editor must read its input through the same inject-aware `i_term_getbyte()` path the
  reader uses, so a host can script a full edit session deterministically.
- **Golden vectors:** `scripts/term_golden/lineedit.json` — burst → expected
  `{rc, line}` (and optional resulting history) covering insert, mid-line insert,
  Left/Right/Home/End, BS, Delete, whole-line erase, Up/Down recall, ESC/Ctrl-C.
- **HuIL menu entry** under the `<term>` submenu (suggest key `l` = "line") for live
  feel-testing.

### Q4 — placement / naming
**Leaning:** lives in `term.c` / `term.h` next to the reader and queries.
`x_term_getline_editor` (enum return ⇒ `x_`), `term_line_t` exit enum,
`TERM_LINE_*` codes. Eventually this is the higher-function replacement the **W5**
migration retires the legacy `i_getline` in favor of.

---

## Wish list (v2+ / out-of-scope companion)

| ID | Subject |
|----|---------|
| **W7** | **Soft-wrap-aware redraw** — multi-row entry lines (lifts the D13 single-row limit); needs row tracking + wrap-safe cursor repositioning |
| **W8** | **Word-wise motion / edit** — Ctrl/Alt-Left/Right, delete-word (`Ctrl-W`), if the terminal delivers the modified-key sequences |
| **W9** | **Kill-ring / yank** (`Ctrl-K`/`Ctrl-U`/`Ctrl-Y`) — readline-style cut buffer (builds on D17 option B) |
| **W10** | **Reverse-incremental history search** (`Ctrl-R`) over the S7 pool |
| **W11** | **Completion hook** — caller-supplied callback on Tab for command/arg completion |
| **W12** | **Insert/overwrite toggle** (Insert key) with cursor-shape hint |
| **W13** | Retire legacy `i_getline` once this lands (part of the **W5** migration sweep) |

---

## Decision history

- **2026-06-20** — board opened; promoted from sibling plan **W1**. D16/I6 resolved on
  inspection; D13/D14/D15/S7/S8/I5/T4/Q4 leaning; **D17** (kill key) and **S9** (seed
  initial text) left open for author. ANSI.h confirmed sufficient (no new macros).
