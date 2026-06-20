# Session handoff — Extended key input (`term.*`) — 2026-06-20

**Type:** firmware feature (terminal-interaction library, building block #1).
**Branch:** `main` (not committed yet at handoff time — see Git note).

## Resume in one line
v1 extended-key reader **shipped & bench-verified**; pick up at the optional
follow-ons (raw-byte monitor, automated goldens) or new library primitives.

## Read first
- Plan + decision log (authoritative): [`extended-key-input-plan.md`](extended-key-input-plan.md)
  — Big Board fully 🟢 for v1; CODE ANCHORS P1–P7 checked; deferred items listed.
- Parent sketch (superseded): [`../extended_keyget_function.c`](../extended_keyget_function.c)

## What shipped this session
- **New module** `App/Inc/term.h` + `App/Src/term.c` — cooperative, timeout-driven
  console key reader. Public API:
  - `int16_t i16_term_get_key(uint32_t u32_timeout_ms)`
  - `void v_term_set_lead_chars(const uint8_t *pu8_leads, uint8_t u8_count)` (default `{ESC}`)
  - `void v_term_register_keymap(const term_keymap_t *, uint16_t)` (W6 user-macro hook)
- **Decodes (v1):** editing/cursor cluster (arrows, Home/End w/ both PC+VT220+rxvt
  encodings, Ins/Del/PgUp/PgDn) via CSI + SS3, **plus F1–F12** (added post-HuIL).
- **Return contract:** `0x00–0xFF` literal byte; `0x0100+` named keys
  (`EXT_KEY_*`); `EXT_MOD_*` flag bits reserved (`0x1000/2000/4000`); negative
  errors `TERM_KEY_NONE=-1 / UNKNOWN=-2 / OVERFLOW=-3` (`-4..-15` reserved);
  user-macro range `0x0E00–0x0EFF` reserved.
- **Timeout/latency:** non-blocking `getchar()` (stdin `_IONBF`, `__io_getchar`
  returns 0 when empty). `timeout=0` ⇒ near-zero block; inter-byte gather (50 ms)
  only after a registered lead; **early-out** on a complete CSI/SS3 sequence; bare
  lead char returned as its own byte.
- **`v_app_polling_task()` weak no-op stub MOVED** utils.c → `term.c` (utils.c keeps
  the `app_polling_task()` stub + a breadcrumb comment).
- **Debug menu:** top-level **`[k]`** "Terminal extended-key decode test" — HuIL
  decode-echo (prints `EXT_KEY_*` / byte / error; bare ESC exits).

## Verified
- Build **0 errors / 0 warnings** (incremental, `-Wall`).
- Flash OK (NUCLEO-G474RE, bench ST-Link `003C00193137510C39383538`, COM9).
- Smoke: boots, `[k]` present.
- HuIL (author): bare keys ✅, editing keys ✅, F1–F12 ✅ after the fix; Ctrl/Alt/meta
  off (expected); user-defined sequences not yet tested.

## Open / next steps
- **Raw-byte hex monitor** (recommended next): decoder-bypass mode to dump literal
  incoming bytes — ground truth for stray keys + **W6 user-defined Tera Term macro**
  testing (map into `0x0E00` range via `v_term_register_keymap`).
- **P6b — automated goldens:** `playstr`-style paced UART injection of escape bursts
  vs expected decodes, reusing the `[k]` hook.
- **Deferred (architecture leaves room):** **D7** Alt-meta decode (`ESC <ch>`,
  `EXT_MOD_ALT`); **Q3/W3** terminal-size/cursor query (`ESC[6n`/`ESC[18t`); **W5**
  migrate scattered terminal funcs from utils.c/debug_menu.c into `term.*`; **W1**
  enhanced `i_getline` w/ editing+history; **W2** terminal piano UI.

## Gotchas / invariants
- `ANSI.h` stays **standalone** (output-control macros, usable without `term`);
  `term.h` includes it one-way. Don't add `term` deps to `ANSI.h`.
- No Core/ edits; `.ioc` untouched. Build/flash/smoke via project skills
  (`/build`, `/flash`, `/smoke` / `build-flash-smoke`); they resolve bench defaults.
- Modified (`;mod`) sequences intentionally fall through to `TERM_KEY_UNKNOWN` (-2).

## Git note
Nothing committed yet. WIP files: `App/Inc/term.h`, `App/Src/term.c`,
`App/Src/utils.c` (stub move), `App/Src/debug_menu.c` (`[k]` test),
`Docs/planning/extended-key-input-plan.md`, this handoff, and the untracked
`Docs/extended_keyget_function.c` (original sketch). Run `/wrapup` to commit (no
push by default).
