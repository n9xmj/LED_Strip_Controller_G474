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

**Status:** v1 **SHIPPED** on G474 bench (2026-06-21) · canvas rework + **W16** viewport +
unbounded scroll-safe repaint fix **SHIPPED** (2026-06-22) · bounded mid-line insert repaint fix
**SHIPPED** (2026-06-22) · **W12** insert/overwrite toggle **SHIPPED** (2026-06-22).

**Planning model:** [`decision-log-model.md`](decision-log-model.md). Not PLAY work;
no Must-Ship-Gap fence — just a Big Board + wish list. **ID numbering continues the
sibling plan's sequence** (it ended at D12 / S6 / I4 / T3 / Q3) so the two boards
never collide.

**North star:** small composable primitives sharing the cooperative,
timeout-driven, ESC-burst core: *key input → **line editing** → **ANSI output** →
queries.* Building blocks: #1 reader, #2 queries, **#3 ANSI output wrappers (`I7`)**,
**#4 line editor**. Inspiration: GNU readline / bash line editing, scoped down to
what a bench console actually needs.

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

/** Parameters for x_term_getline_editor() (**Q5**). Zero-init friendly:
 *  `{0}` or `memset` → history off; set `pc_line` + `u16_max_len` (required). */
typedef struct
{
    char     *pc_line;          /* in/out: entry buffer (NUL-terminated)           */
    uint16_t  u16_max_len;      /* capacity incl. NUL; 0 = error                 */
    uint8_t  *pu8_hist;         /* in/out history pool; NULL = history disabled  */
    uint16_t  u16_hist_size;    /* sizeof pool when pu8_hist != NULL             */
    const char *pc_prompt;      /* optional; NULL = no prompt; plain printables  */
    uint16_t  u16_field_width;  /* 0 = unbounded canvas; else bounded viewport. */
    /* future (W11/W12/…): completion hook, default insert mode, key timeout, …  */
}
term_line_edit_t;

/* pc_line is ALWAYS valid on return (current entry text), regardless of exit code. */
term_line_t x_term_getline_editor(term_line_edit_t *px_edit);
```

Naming follows the project Hungarian + the sibling plan: enum return ⇒ `x_` prefix,
`term_` module tag, `term_line_t` / `term_line_edit_t` per the `*_t` convention (**Q4**).
Call-site pattern (**Q5**):

```c
term_line_edit_t x_edit = { .pc_line = ac_buf, .u16_max_len = sizeof ac_buf,
                            .pu8_hist = au8_hist, .u16_hist_size = sizeof au8_hist,
                            .pc_prompt = "Volume: ", .u16_field_width = 10u };
term_line_t x_rc = x_term_getline_editor(&x_edit);
```

---

## The Big Board

| ID | Status | Subject (one line) |
|----|--------|-------------------|
| **D13** | 🟢 | **Redraw** = **edit-in-place** by default (minimal ANSI per op); **full/partial repaint from DECSC origin** only on destructive ops (BS/DEL/kill/history/Ctrl-X) and on entry preload; cursor motion + append/insert = no whole-line wipe |
| **D14** | 🟢 | **Prompt ownership** = optional `pc_prompt` on `term_line_edit_t`; editor prints it, flushes, then `DECSC`-pins first editable cell; NULL = no prompt |
| **D15** | 🟢 | **Exit code** = `term_line_t` enum (ENTER/ESCAPE/CTRLC/ERROR); `pc_line` valid on every return |
| **D16** | 🟢 | **Backspace vs Delete**: both `0x08` and `0x7F` ⇒ destructive delete-left; `EXT_KEY_DELETE` (`ESC[3~`) ⇒ forward-delete + collapse |
| **D17** | 🟢 | **Whole-line / kill keys** = **both**: Ctrl-X erases entire line (legacy `i_getline` muscle memory); Ctrl-U kill-to-start + Ctrl-K kill-to-end (readline/bash; sets up **W9**) |
| **S7** | 🟢 | **History format** = flat caller buffer of packed NUL-terminated strings, oldest→newest, used region ended by an empty string (extra NUL); rest zeroed. No header, no malloc, self-describing |
| **S8** | 🟢 | **History semantics** = Up=older/Down=newer; in-progress line kept as scratch (index 0); recalled entries editable; ENTER appends edited line as newest, **de-dupe consecutive dup**, **FIFO-evict oldest** when full; ESC/Ctrl-C never append |
| **S9** | 🟢 | **Seed initial text** = yes — non-empty `pc_line` on entry is editable preload; cursor starts at end |
| **S10** | 🟢 | **Space at cursor** — `0x20` (Space) uses the same insert-at-cursor tier as other printables: mid-line inserts `' '` and shifts suffix (buffer memmove + on-screen suffix + `CUF`); EOL appends a space |
| **I5** | 🟢 | **Input source** = loop on `i16_term_get_key(small_timeout)`, ignore `TERM_KEY_NONE`; cooperative (pumps polling task via the reader) |
| **I6** | 🟢 | **`ANSI.h` macro layer** — raw escape `#define`s stay standalone; no new macros required for v1 line editor |
| **I7** | 🟢 | **Term ANSI output primitives** — **public** `v_term_*` wrappers in `term.h` over `ANSI.h`; v1 ships subset (line editor + existing query emits); set grows organically toward full coverage (**W14**) |
| **T4** | 🟢 | **Test** = harness `E` op + `scripts/term_golden/lineedit.json` + HuIL `[l]` under `<term>` submenu |
| **Q4** | 🟢 | **Placement / naming** = `term.c` / `x_term_getline_editor` returning `term_line_t` |
| **Q5** | 🟢 | **Call shape** = `term_line_edit_t` options struct (zero-init defaults); includes `pc_prompt`, `u16_field_width`; single `px_edit` arg |
| **D18** | 🟢 | **W12 overwrite mechanics** = `EXT_KEY_INSERT` (`ESC[2~`) toggles a **session-local** insert⇄overwrite flag; in overwrite a printable **replaces** the char at the cursor and advances (no suffix shift, `u16_len` unchanged); at EOL overwrite falls back to append/insert. Nav / kill / BS / DEL unchanged — BS & DEL still **collapse** (**D16**), they do not "replace with space" |
| **S12** | 🟢 | **W12 overwrite bounds/length** = mid-line overwrite never changes `u16_len`; only EOL append grows it (still clamped to `u16_max_len-1`); bounded fixed-width fields (**W16**) become editable **in place**; Space (**S10**) overwrites like any other printable when in overwrite |
| **I8** | 🟢 | **W12 cursor-shape hint** = **RESOLVED: include, opt-in.** `DECSCUSR` (`CSI n q`) steady-bar = INS / steady-block = OVR, via a new `v_term_cursor_style()` + `ANSI.h` macros, **gated behind a new `term_line_edit_t` opt-in flag** (default off → no shape emits, fully back-compatible). When enabled, the default shape (`CSI 0 q`) **must be restored on every exit path** (ENTER/ESC/Ctrl-C/error) — the same restore discipline that rejected `CSI 4h` in **D13** |
| **Q6** | 🟢 | **W12 default-mode field** = **RESOLVED: option B.** Always start in **insert**; the only way into overwrite is the live `Insert`-key toggle (resets to insert each call). **No** `b_start_overwrite` member added — the sole new `term_line_edit_t` field is the **I8** cursor-cue opt-in |
| **T5** | 🟢 | **W12 test** = harness Insert byte `1B5B327E` toggles mode; golden vectors (overwrite-replace mid-line, EOL-extend, toggle round-trip, bounded in-place overwrite) added to `lineedit.json` + `lineedit_field.json`; HuIL via `[l]` / `[f]` |

Status: 🔴 open · 🟡 leaning · 🟢 resolved · 🔵 deferred.

**W12 fully resolved (2026-06-22):** **I8** → include cursor-shape cue, opt-in via a
new `term_line_edit_t` flag, default off, restore on every exit. **Q6** → option B
(always start insert; live toggle only, no default-mode field). **D18 / S12 / T5** green.
v1 board (D13–Q5) remains locked/shipped. Ready to implement.

---

## v1 key bindings (proposed)

| Key | Bytes (via reader) | Action |
|-----|--------------------|--------|
| Left / Right | `EXT_KEY_LEFT` / `EXT_KEY_RIGHT` | move cursor one char |
| Home / End | `EXT_KEY_HOME` / `EXT_KEY_END` | jump to start / end |
| Backspace | `0x08` | delete char left, collapse (**D16**) |
| Delete | `0x7F` or `EXT_KEY_DELETE` (`ESC[3~`) | delete char right, collapse (**D16**) |
| Up / Down | `EXT_KEY_UP` / `EXT_KEY_DOWN` | history older / newer (**S8**) |
| Ctrl-X | `0x18` | erase entire line (**D17**) |
| Ctrl-U | `0x15` | kill to start of line (**D17**) |
| Ctrl-K | `0x0B` | kill to end of line (**D17**) |
| Enter | `0x0D` (CR) | accept → `TERM_LINE_ENTER` |
| ESC (bare) | `0x1B` | cancel → `TERM_LINE_ESCAPE` |
| Ctrl-C | `0x03` | abort → `TERM_LINE_CTRLC` |
| Space | `0x20` | insert `' '` at cursor — mid-line shifts suffix (**S10**) |
| printable | `0x21..0x7E` | insert at cursor (v1 default); in **overwrite** mode replace char at cursor + advance (**W12** / **D18**) |
| Insert | `EXT_KEY_INSERT` (`ESC[2~`) | v1: ignored. **W12** / **D18**: toggle insert ⇄ overwrite (session-local, resets to default each call) |
| anything else | — | ignored in v1 |

**ESC disambiguation is free:** the reader only returns a bare `0x1B` when nothing
follows within the inter-byte gap, so arrow/function keys arrive as `EXT_KEY_*` and a
real Escape press arrives as `0x1B` — the editor never has to time ESC itself.

---

## Detail sections

### D13 — redraw strategy *(resolved 2026-06-21 — edit-in-place)*

Full-line wipe on every keystroke is rejected — even at 921600 baud, `DECRC` +
`CLEAR_EOL` + reprint causes visible flicker (especially with a blinking cursor).
Instead, track **buffer + cursor index** and emit the **smallest** ANSI update per op.

**At entry (once):**
1. CPR fetch cursor **before** any prompt output.
2. If **D14** `pc_prompt` is set: `u16_prompt_col` = fetched col; print prompt;
   `fflush(stdout)`; anchor editable origin at `col + strlen(prompt)` on same row.
3. `v_term_save_cursor()` at the anchor (first editable cell).
4. If **S9** preload is non-empty, print or full-redraw it (cursor ends at `strlen`).
4. Optionally `v_term_cursor_visible(false)` during bulk redraw paths only — defer
   unless HuIL shows flicker on history swap.

**Cheap paths (no whole-line repaint):**

| Op | Screen (**I7**) | Buffer |
|----|-----------------|--------|
| Left / Right | `v_term_cursor_left(1)` / `v_term_cursor_right(1)` | cursor ± 1 |
| Home / End | `v_term_cursor_left(n)` / `v_term_cursor_right(n)` or column abs | cursor = 0 / len |
| Append at EOL | `putchar(c)` | insert at len |
| Insert mid-line (incl. **S10** Space) | print char + suffix, `v_term_cursor_left(suffix_len)` | memmove insert |

**Repaint paths (destructive / content swap — author OK with flicker here):**

| Op | Screen (**I7**) | Buffer |
|----|-----------------|--------|
| BS at EOL | classic `\b \b` | delete-left |
| BS mid-line | `v_term_cursor_left(1)` + `v_term_delete_chars(1)` | delete-left + shift |
| Delete forward | `v_term_delete_chars(1)` | delete + shift |
| Ctrl-K | `v_term_clear_eol()` | truncate at cursor |
| Ctrl-U | `v_term_restore_cursor()` → reprint suffix → `v_term_clear_eol()` → reposition | shift to front |
| Ctrl-X | `v_term_restore_cursor()` → `v_term_clear_eol()` | clear all, cursor 0 |
| Up/Down history | `v_term_restore_cursor()` → `v_term_clear_eol()` → print line → reposition | replace from pool |

**On exit:** newline; cursor shown if hidden.

**Why not terminal insert mode (`CSI 4h`)?** Tera Term supports it, but leaving OVR
mode restored on every exit path (ENTER / ESC / Ctrl-C / error) is fragile; manual
suffix print + `CUF` keeps terminal state predictable for the rest of the app.

**v1 scope limit:** ~~soft-wrap + auto-scroll at viewport bottom~~ **Removed 2026-06-22**
(dynamic wrap / CPR re-anchor deleted). **Fixed canvas** at session open: `N` spaces →
optional CEL (unbounded only) → `CUB(N)` → `DECSC`. Unbounded: `N = u16_max_len - 1`
(freeform scroll-up OK). Bounded: `N = field_width` (EOL-clamped, one row) +
horizontal viewport (**W16**). Entry limit is always `u16_max_len - 1` in both modes.
Initial cursor at end of `pc_line` (preload / history). Destructive edits: partial
suffix repaint + space-pad (bounded) or full canvas fill (unbounded); full viewport
repaint when `u16_view_offset` changes.

**Sync risk:** incremental updates assume no external output during edit. Same as
today's blocking `i_getline`; history/recall and wrap paths do bounded origin-repaint to
resync.

### D14 — prompt ownership *(resolved 2026-06-21; revised 2026-06-22)*
Optional `pc_prompt` on `term_line_edit_t`. **NULL** or empty ⇒ no prompt (origin =
CPR cursor at call time). When set: **CPR before prompt print** → store prompt column;
print prompt at that cell; anchor editable origin at `prompt_col + strlen(prompt)`;
`fflush` + `DECSC`. Wrap full redraw re-emits prompt at `u16_prompt_col` (preserves
text to the left on the origin row). Plain printables only in the prompt string.

**Caller contract:** mid-line entry is supported; text left of `u16_prompt_col` on
the origin row is preserved across wrap redraw. BOL start remains the common case.

### S11 — bounded field window *(resolved 2026-06-22; canvas model 2026-06-22)*
`u16_field_width` (0 = unbounded canvas) sets the **display viewport width** on one
row (EOL-clamped). Entry limit is **`u16_max_len - 1`** independently (**W16**).
Session open reserves the canvas (`N` spaces, no CEL in bounded mode, `CUB(N)`,
`DECSC`). Clear/paint never uses DCH on the row — space overwrite / suffix repaint
only — so inline neighbors (`Tempo:` / `Volume:` on one status line) stay put.
`u16_view_offset` slides the viewport when the cursor would leave the window.
Tab / Shift-Tab accept in bounded mode; no trailing newline on exit.

### D15 — exit code & line contract *(resolved 2026-06-21)*
Small enum `term_line_t`. `pc_line` is **always** NUL-terminated and holds the current
line on every return path (accept *or* abort), mirroring the existing `i_getline`
"what's on the line now" behavior — the caller keys off the return code to accept/discard.
`TERM_LINE_ERROR` for bad args (NULL `px_edit`, NULL `pc_line`, zero capacity); on error
`pc_line` is left untouched if possible.

### D16 — backspace vs delete *(resolved; updated 2026-06-22)*
**`0x08` (BS)** ⇒ destructive backspace (delete-left). **`0x7F` (DEL byte)** and
**`ESC[3~` (`EXT_KEY_DELETE`)** ⇒ forward delete + collapse. Matches common PC-terminal
split (Tera Term Delete often sends `0x7F`).

### D17 — whole-line / kill key *(resolved 2026-06-21 — option C)*
**Both** bindings ship in v1:
- **Ctrl-X (`0x18`)** — erase the entire line (matches legacy `i_getline`; clears buffer,
  cursor → 0, redraw). Does **not** exit the editor (unlike ESC).
- **Ctrl-U (`0x15`)** — kill from cursor to start of line (readline/bash).
- **Ctrl-K (`0x0B`)** — kill from cursor to end of line (readline/bash; no kill-ring yet —
  killed text is discarded until **W9**).

Ctrl-U/Ctrl-K are partial-line edits; Ctrl-X is the whole-line reset. All three redraw.

### S7 — history buffer format *(resolved 2026-06-21)*
The caller-owned `pu8_hist` pool holds **packed NUL-terminated strings,
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

### S9 — seed initial text *(resolved 2026-06-21 — yes)*
On entry, if `pc_line` is already NUL-terminated and non-empty, treat it as the starting
buffer with the cursor at **end of string**. Empty or NULL-on-first-byte ⇒ start blank
at cursor 0. Preload does **not** auto-append to history until ENTER.

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

**Resolved 2026-06-21:** dedupe consecutive duplicates = yes; evict-oldest when full = yes.

### S10 — space at cursor *(resolved 2026-06-21 — yes, v1)*
The **Space** key (`0x20`) is a first-class insert-at-cursor operation (not deferred
to **W12**). Mid-line: insert `' '` into the buffer, print `' '` + suffix from cursor,
`v_term_cursor_left(suffix_len)` to land after the new space. End-of-line: append `' '`
with `putchar(' ')`. Golden vectors must cover mid-line space (e.g. `abc|def` → Left×2
→ Space → `ab cdef`). Same internal path as other printable insert-at-cursor chars.

**Author use case (PLAY strings):** v1 is **insert-by-default** for all printables
(including Space) — functionally similar to always-on insert mode without **W12**'s
toggle. Primary workflow: Up to recall last PLAY line (**S9** preload + **S8**), then
mid-line insert/delete/Space edits while iterating by ear. Golden vectors and HuIL must
exercise recall → edit → accept paths, not just empty-line entry.

### I5 — input source / cooperative loop *(resolved 2026-06-21)*
Drive entirely off `i16_term_get_key()` (the building-block-#1 reader),
called with a small timeout in a loop; ignore `TERM_KEY_NONE`. That reader already
pumps `v_app_polling_task()` each spin, so the editor inherits the cooperative,
non-starving behavior for free — no second input path, no driver poking.

### I6 — `ANSI.h` macro layer *(resolved)*
Raw escape `#define`s live in `ANSI.h` (standalone — must not depend on `term.*`).
v1 line editor needs no **new** macros there; existing CHA / CUF / CUB / `CLEAR_*` /
`DEL_CHAR` / DECSC / DECRC / SGR / 16-color macros cover the sequences.

### I7 — Term ANSI output primitives *(resolved 2026-06-21 — public, organic growth)*

**Role:** building block **#3** — a **public**, documented `term.h` API of thin output
primitives. Application code (menus, PLAY bench, future piano UI, etc.) uses these to
compose its own terminal control; higher-level `term.*` functions (reader, queries, line
editor) are built from the same blocks — not a separate internal-only layer.

**Layering:**

```
API user / line editor / b_term_get_*  →  v_term_*  (term.c, public)
                                              ↓
                                         ANSI.h macros (standalone)
                                              ↓
                                         stdout / __io_putchar
```

**Growth model:**
- **v1 subset:** implement only what the **line editor** and **existing query emits**
  need (table below). Refactor `b_term_get_size*` / `b_term_get_cursor` to call these
  instead of raw `printf(ANSI_…)`.
- **Organic expansion:** add primitives as new features need them; each maps 1:1 to an
  `ANSI.h` macro (or small fixed sequence). No big-bang "implement everything" pass.
- **North star (**W14**):** eventually wrap **most/all** of `ANSI.h` — including
  sequences no current `term.*` higher-level function uses — so the primitive set is
  complete for direct API-user use.

Implementations are thin `printf`/`fputs` wrappers. `u16_count` / column / row of **0**
is a no-op where the underlying sequence would be meaningless. Naming: `v_term_*`,
Hungarian params, declared in `term.h` alongside reader/query APIs (Doxygen per public fn).

**v1 ship set** (first tranche):

| Function | Maps to | v1 consumer |
|----------|---------|-------------|
| `v_term_cursor_up/down/left/right(u16_count)` | CSI `nA`/`nB`/`nD`/`nC` | line editor |
| `v_term_cursor_move(u16_row, u16_col)` | CSI `r;cH` | queries (CPR trick), API users |
| `v_term_cursor_column(u16_col)` | CSI `nG` | line editor (Home-from-origin) |
| `v_term_cursor_visible(bool b_on)` | `?25h` / `?25l` | line editor (optional history redraw) |
| `v_term_save_cursor()` / `v_term_restore_cursor()` | DECSC / DECRC | line editor, queries |
| `v_term_delete_chars(u16_count)` | CSI `nP` | line editor |
| `v_term_clear_eol()` / `v_term_clear_bol()` / `v_term_clear_line()` | `K` / `1K` / `2K` | line editor |
| `v_term_clear_screen()` | `ANSI_CLEAR_AND_HOME` | API users; not required by line editor v1 |

**Later tranches (W14 examples — public as added):** `v_term_set_text_color16`,
`v_term_set_text_attribute`, scroll up/down, insert/delete line, SGR reset, RGB colors,
cursor shape, insert/overwrite mode, report/request helpers, etc.

### T4 — test strategy *(resolved 2026-06-21)*
Mirror the existing harness pattern (T2/T3).
- **Harness `E` op:** inject a scripted byte stream (printables + edit escapes like
  `1B5B44` for Left, `08` for BS, terminator `0D`/`1B`/`03`) via `v_term_inject`, run
  the editor, and frame the result: `<HRN E rc=<code> line="..." [hist=...]>`. The
  editor must read its input through the same inject-aware `i_term_getbyte()` path the
  reader uses, so a host can script a full edit session deterministically.
- **Golden vectors:** `scripts/term_golden/lineedit.json` — burst → expected
  `{rc, line}` (and optional resulting history) covering insert, mid-line insert,
  Left/Right/Home/End, BS, Delete, **mid-line Space (S10)**, whole-line erase, Up/Down
  recall, ESC/Ctrl-C.
- **Bounded field (W16):** harness **`B [preload_hex]/key_hex`** op (field_width=21,
  max_len=81); golden `scripts/term_golden/lineedit_field.json`; bench
  `scripts/term_lineedit_field_bench.py` — Tab/Shift-Tab accept, long entry beyond
  viewport, preload + Home/Left scroll-path edits.
- **HuIL menu entry** under the `<term>` submenu (suggest key `l` = "line") for live
  feel-testing.

### Q4 — placement / naming *(resolved 2026-06-21)*
Lives in `term.c` / `term.h` next to the reader, **I7** primitives, and queries.
`x_term_getline_editor` (enum return ⇒ `x_`), `term_line_t` exit enum, `term_line_edit_t`
options struct (**Q5**), `TERM_LINE_*` codes. Eventually replaces legacy `i_getline`
(**W13** / **W5** migration).

### Q5 — call shape / options struct *(resolved 2026-06-21 — author rule-of-thumb)*
When parameter count approaches ~4+ and growth is expected, pass a **single options
struct** instead of a long flat arg list. v1 fields: `pc_line`, `u16_max_len`,
`pu8_hist`, `u16_hist_size`. **Zero-init defaults:** `{0}` / `memset` → history off;
caller sets required line buffer fields only. Optional members use NULL / 0 as "disabled"
or "module default". Future fields (completion hook **W11**, insert-mode default **W12**,
key-loop timeout, width clamp) append to `term_line_edit_t` without changing the
function signature. `TERM_LINE_ERROR` if `px_edit` is NULL, `pc_line` is NULL, or
`u16_max_len` is 0.

---

## W12 — insert/overwrite toggle *(planning — opened 2026-06-22)*

Promotes wish-list **W12** to a real design. Goal: a classic INS/OVR toggle like a
PC text editor, scoped to the single-line editor. Touches only the printable-insert
path and the (currently ignored) Insert key — nav/kill/history stay as-is.

**Grounding (current code, `App/Src/term.c`):**
- `term_line_state_t` holds `u16_cursor` / `u16_len`; **no** mode flag yet — W12 adds one (`b_overwrite`).
- `v_line_insert_char()` already splits **append** (`cursor == len`) vs **mid-line memmove-insert**. Overwrite reuses the append branch verbatim and swaps the mid-line branch for a single-cell store.
- The key-dispatch `switch` decodes `EXT_KEY_INSERT` (intro table line ~72) but the loop ignores it; W12 wires it to flip the flag.
- Redraw is already mode-agnostic: `v_line_refresh_unbounded()` (unbounded) and the bounded suffix/canvas painters repaint from the buffer, so overwrite just feeds them a mutated buffer + cursor.

### D18 — overwrite mechanics *(leaning)*
`EXT_KEY_INSERT` toggles `x_st.b_overwrite` (session-local, always **starts false = insert** per **Q6** option B).
New `v_line_overwrite_char()` (or a branch in `v_line_insert_char`):
- **cursor < len:** `pc_line[cursor] = c; cursor++;` — `u16_len` unchanged, no memmove, no suffix shift.
- **cursor == len (EOL):** identical to insert-append (extends the line, still clamped to `u16_max_len-1`).
- Then redraw via the existing path (`v_line_redraw`) so unbounded scroll + bounded `u16_view_offset` (**W16**) keep working. *Cheap-path option:* mid-line overwrite is just `putchar(c)` over the current cell (terminal auto-advances) — can specialise later if flicker shows; default to the shared redraw for correctness first.
- **BS / DEL stay collapsing (D16).** We do **not** implement DOS-style "BS replaces with space + moves left." *(Minor confirm point — author can override.)*

### S12 — overwrite bounds & length *(leaning)*
Mid-line overwrite holds `u16_len` constant; the only growth is EOL append (clamped to
`u16_max_len-1`, same limit as insert). This makes **bounded fixed-width fields (W16)**
editable in place: park the cursor on a digit, type to replace it, no length churn — ideal
for `Tempo:`/`Volume:` style status-line fields. **Space (S10)** overwrites like any printable.

### I8 — cursor-shape hint *(SHIPPED — include, opt-in)*
Emit `DECSCUSR` (`CSI Ps SP q`, space intermediate required) on toggle: `CSI 6 SP q` steady bar =
INS, `CSI 2 SP q` steady block = OVR, through the new public `v_term_cursor_style()` primitive
(**I7**/**W14** family) + `ANSI.h` macros. **Gated behind the `term_line_edit_t.b_show_mode_cursor`
opt-in flag** (zero-init → off → no shape emits at all, fully back-compatible — every existing
caller is untouched). When enabled:
- Set the shape to match the current mode on session open (start = INS = bar) and on every toggle.
- **Restore the default shape (`CSI 0 SP q`) on *every* exit path** — ENTER / TAB / ESC / Ctrl-C —
  via a single restore in the `done:` path so there's exactly one place to get right (the D13
  fragility lesson). Skip both the set and the restore when the flag is off.
- **Tera Term gotcha (verified 2026-06-22):** DECSCUSR is gated behind *Setup → Additional
  settings → Control Sequence → "Cursor control sequence"*, which ships **OFF**. With it off the
  cue is silently dropped (mechanics still work); with it on, bar↔block tracks INS/OVR as expected.
  HuIL `[l]`/`[f]` enable the flag; the golden-vector harness ops leave it off (no DECSCUSR in
  captured output).

### Q6 — default-mode field *(RESOLVED — option B)*
**No `b_start_overwrite` field.** The editor always starts in **insert**; the only path into
overwrite is the live `Insert`-key toggle, which resets to insert on each `x_term_getline_editor()`
call. The sole new `term_line_edit_t` member from W12 is the **I8** cursor-cue opt-in
(`b_show_mode_cursor`). Keeps the common case dead simple; a caller wanting OVR-by-default can
revisit later if a real need shows up.

### T5 — test plan *(leaning)*
Extend **T4**:
- Harness `E`/`B`: Insert toggle byte is `1B 5B 32 7E` (`ESC[2~`). Scripts emit it to flip mode mid-burst.
- Goldens — add to `scripts/term_golden/lineedit.json` (unbounded) and `lineedit_field.json` (bounded):
  - overwrite replace mid-line (e.g. `abc` → Home → Ins → `XY` ⇒ `XYc`),
  - overwrite at EOL extends (`ab` → End → Ins → `c` ⇒ `abc`),
  - toggle round-trip (Ins on, type, Ins off, insert) leaves correct final line,
  - bounded in-place overwrite of a preloaded fixed-width field (length unchanged).
- HuIL: exercise via `[l]` / `[f]` — watch the I8 cursor cue (if adopted) and confirm restore on exit.

---

## Wish list (v2+ / out-of-scope companion)

| ID | Subject |
|----|---------|
| **W7** | ~~Soft-wrap-aware redraw~~ **Retired** — dynamic wrap machinery removed; unbounded uses fixed `max_len-1` canvas + terminal scroll |
| **W16** | **Bounded horizontal viewport** — ✅ shipped 2026-06-22: `field_width` = display window; entry up to `max_len-1`; `u16_view_offset` scroll |
| **W15** | **Multiline mini text editor** — dedicated sub-window (DECSTBM or alternate screen), basic keys only (motion, insert, delete, kill); not vim/nano; builds on line editor + **I7** |
| **W8** | **Word-wise motion / edit** — Ctrl/Alt-Left/Right, delete-word (`Ctrl-W`), if the terminal delivers the modified-key sequences |
| **W9** | **Kill-ring / yank** (`Ctrl-K`/`Ctrl-U`/`Ctrl-Y`) — readline-style cut buffer (builds on D17 option B) |
| **W10** | **Reverse-incremental history search** (`Ctrl-R`) over the S7 pool |
| **W11** | **Completion hook** — caller-supplied callback on Tab for command/arg completion |
| **W12** | **Insert/overwrite toggle** — **SHIPPED 2026-06-22** (Big Board **D18 / S12 / I8 / Q6 / T5** all 🟢). Live `Insert` toggle, in-place overwrite, EOL-append fallback; opt-in INS/OVR DECSCUSR cursor cue (`b_show_mode_cursor`); bench-verified 15/15 + 13/13 |
| **W13** | Retire legacy `i_getline` once this lands (part of the **W5** migration sweep) |
| **W14** | **Complete the public `v_term_*` primitive set** — wrap remaining `ANSI.h` sequences (colors, attributes, scroll/region, line insert/delete, RGB, cursor shape, modes, …) even when no higher-level `term.*` function uses them yet; API users compose freely |

---

## Decision history

- **2026-06-20** — board opened; promoted from sibling plan **W1**. D16/I6 resolved on
  inspection; D13/D14/D15/S7/S8/I5/T4/Q4 leaning; **D17** (kill key) and **S9** (seed
  initial text) left open for author. ANSI.h confirmed sufficient (no new macros).
- **2026-06-21** — **D17** 🟢 option C (Ctrl-X + Ctrl-U/Ctrl-K). **S9** 🟢 yes (preload
  `pc_line`, cursor at end). **S8** 🟢 confirmed. **D13** 🟢 edit-in-place default,
  repaint only on destructive ops / history (author UX preference vs naive full repaint).
- **2026-06-21** — **S10** 🟢 Space-at-cursor in v1 (insert path). **I7** 🟢 public
  `v_term_*` primitives — v1 subset (line editor + query refactor), organic growth, **W14**
  = complete the set for API users. **W12** confirmed stretch goal.
- **2026-06-22** — **Canvas rework:** unified init (spaces / optional CEL / CUB /
  DECSC); removed dynamic soft-wrap (`b_wrap_mode`, CPR re-anchor, `v_line_full_redraw`).
  Unbounded canvas = `max_len-1` cells; bounded = EOL-clamped viewport + **W16**
  horizontal scroll; entry limit decoupled from display width; cursor starts at EOL.
- **2026-06-21** — **D14/D15/S7/I5/T4/Q4** 🟢. **Q5** 🟢 `term_line_edit_t` options struct
  (author ~>4-param rule). v1 board locked; PLAY recall+mid-line edit called out under **S10**.
- **2026-06-22** — **Unbounded repaint fix shipped** (scroll-safe relative refresh; verified on
  bench at 120x30 — bottom-of-screen entry, wrapped lines, mid-line DEL/BS, deep history recall).
- **2026-06-22** — **W12 promoted to planning** (insert/overwrite toggle): **D18** (overwrite
  mechanics) 🟡, **S12** (bounds/length) 🟡, **T5** (tests) 🟡; **I8** (cursor-shape hint) 🔴 and
  **Q6** (default-mode struct field) 🔴 await author calls. Grounded in current `term.c`
  (`v_line_insert_char` append/insert split, ignored `EXT_KEY_INSERT`, mode-agnostic redraw).
- **2026-06-22** — **Bounded mid-line insert repaint fix shipped:** `v_line_repaint_suffix`
  now takes an explicit start index (insert = cursor-1, del/BS = cursor) and clamps the reprint
  to the viewport, fixing stranded glyphs and tail spill into the next field.
- **2026-06-22** — **W12 SHIPPED:** **I8** → opt-in DECSCUSR cue (`b_show_mode_cursor`, bar=INS /
  block=OVR, restore on all exits; needs Tera Term "Cursor control sequence" enabled). **Q6** →
  option B (always start insert, live toggle only). **D18/S12/T5** 🟢. Bench 15/15 + 13/13;
  HuIL-verified on bench (mechanics + cue).
