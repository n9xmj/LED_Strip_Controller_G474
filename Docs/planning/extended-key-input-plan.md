# Extended Key Input — implementation plan (decision log)

**Feature:** `i16_get_extended_key()` — a cooperative, timeout-driven console key
reader that returns single bytes as-is and decodes multi-byte ESC-led ANSI/xterm
function-key bursts into stable 2-byte key codes. Eventual home: the kitchen-sink
**`utils.c` / `utils.h`**.

**Parent sketch:** [`Docs/extended_keyget_function.c`](../extended_keyget_function.c)
(first-pass concept + comment notes; *not* syntax-checked — superseded by this plan).

**Status:** PLANNING · **Working mode:** resolve OPEN rows in chat by ID
(*"green D3"*, *"D2 option B"*, *"S4 your call"*); agent updates this doc + detail
sections same session. No firmware written until the relevant rows lock.

**Planning model:** [`decision-log-model.md`](decision-log-model.md). This is *not*
PLAY work and has no Must-Ship-Gap fence — just a Big Board + wish list.

**North star (author, 2026-06-20):** this reader is **building block #1** of a
self-designed **terminal-interaction library** — "sort of like ncurses, but with my
own API conventions." Keep that lens on naming/scope: small composable primitives
(key input → line editing → screen/cursor → queries) that share the cooperative,
timeout-driven, ESC-burst core.

**Scope extension (author, 2026-06-20):** the end state is **one clean terminal API
module** that the scattered terminal functions in `utils.c` / `debug_menu.c` get
**migrated into** under a single consistent naming convention. Some of those
existing funcs survive mostly unchanged; some get **discarded in favor of more
general, higher-functionality** replacements. So:
- This function's real home is the **new terminal module**, *not* `utils.c` (revises
  the earlier "lands in utils" note — see **D1**).
- The migration sweep itself is tracked as **W5** (its own effort/plan later); but
  the **naming convention** must be decided now since this function adopts it first.

---

## The Big Board

| ID | Status | Subject (one line) |
|----|--------|-------------------|
| **D1** | 🟢 | Module `term.*`, tag `term_` (`i16_term_get_key()`) — **locked** |
| **D2** | 🟢 | Return-code map: grouped ranges + `EXT_MOD_*` flag bits — **locked** |
| **D3** | 🟢 | Hybrid parser+LUT + **configurable lead-char set** API — **locked** |
| **D4** | 🟢 | v1 editing/cursor + F1–F12 + Alt-meta (**D7**) shipped — **locked** |
| **D5** | 🟢 | Lead-ins: CSI `ESC [` + SS3 `ESC O`; Alt `ESC <ch>` **decoded** (D7) |
| **D6** | 🟢 | `ANSI.h` standalone (output ctrl); all `term` types in `term.h` — **locked** |
| **D7** | 🟢 | Alt-meta decode — `ESC <ch>` → `EXT_MOD_ALT \| ch` — **shipped** |
| **S1** | 🟢 | Non-blocking contract: `timeout=0` ⇒ near-zero block in typical case — **locked** |
| **S2** | 🟢 | Inter-byte gap applies only **after a lead char**; early-out on complete seq — **locked** |
| **S3** | 🟢 | Bare-ESC returns `ESC` after gap; accepted latency — **locked** |
| **S4** | 🟢 | Negative error codes `-1..-15`; bare lead char → returned as byte — **locked** |
| **S5** | 🟢 | Buffer overflow = error `-3` (drain to gap, then return) — **locked** |
| **I1** | 🟢 | "No char" = `getchar()` returns `0` (non-blocking stdin, `_IONBF`) |
| **I2** | 🟢 | Hand-roll the dual-timeout loop on non-blocking `getchar()` — **locked** |
| **I3** | 🟢 | Compact data-driven table (tuple match), code-space-minded — **locked** |
| **T1** | 🟢 | Reference + HuIL debug-menu test, reusable for automated injection — **locked** |
| **T2** | 🟢 | Golden-vector harness (`term_key_bench.py` + `term_golden/keys.json`) — **shipped** |
| **Q1** | 🟢 | Target terminal = **Tera Term v5.3+** (its VT100/VT220/xterm support) |
| **Q2** | 🟢 | Control chars pass bare; Alt-meta deferred (→ D7) — **locked** |
| **Q3** | 🟡 | Terminal-size / cursor-pos query — **activated 2026-06-20**; design board in **§ Terminal size / cursor query** (D8–D11, S6, I4, T3) |

Status: 🔴 open · 🟡 leaning · 🟢 resolved · 🔵 deferred.

---

## Wish list (v2+ / out-of-scope companion)

| ID | Subject |
|----|---------|
| **W1** | Enhanced `i_getline()` — in-line editing + input history (readline-lite) on top of this reader. **Promoted to its own board:** [`line-editor-plan.md`](line-editor-plan.md) |
| **W2** | Terminal piano UI consumer (PLAY **I8**; depends on `uart_stream`) |
| **W3** | Terminal-size / cursor-position query helper (`ESC[6n` report / `ESC[18t`) — if not pulled in via **Q3** |
| **W4** | Mouse reporting / bracketed-paste / focus events (likely never on a bench console) |
| **W5** | **Migrate scattered terminal funcs** (`utils.c` / `debug_menu.c`) into the new `term.*` module under one naming convention; discard/replace some with higher-function versions. (Inventory in **W5** detail.) |
| **W6** | Custom **user-defined key-macro** decoding (TT keyboard macros) into the reserved enum range — design hooks land now (**D2/D3**), full support later |

---

## LOCKED CONTEXT (do not re-litigate unless reopened)

- **Eventual home = a NEW dedicated terminal API module** (working name `term.*` —
  see **D1**), *not* `utils.c`. The kitchen-sink console funcs currently in `utils.c`
  get migrated in under a consistent module-tagged naming convention (**W5**).
  ~~(Superseded: earlier note said this lands in utils.c.)~~
- **`App/Inc/ANSI.h` already exists** and defines `ESC` (0x1B), `ESC_S`, `CSI_S`
  (`"\x1B["`), plus the full cursor/SGR/erase macro set. Reuse `ESC` — do not
  re-`#define` a raw `'\x1B'`.
- **Cooperative super-loop invariant:** any in-loop wait MUST pump
  `v_app_polling_task()` each spin (see AGENTS.md). Blocking/polled style for now;
  the non-blocking `uart_stream` USART2 port is **separate future work**, not a
  dependency here.
- **`v_app_polling_task()` weak stub lives in the `term` module** (author,
  2026-06-20): `term.c` provides the **weak no-op** definition; bare-metal /
  non-RTOS apps (like this project today) override it with a strong def (currently
  in `app_main.c`). Moves the stub out of `utils.c` into the module that depends on
  it. (W5 cleanup.)
- **stdio hooks stay application-side, NOT in the API module** (author,
  2026-06-20): `__io_putchar` / `__io_getchar` are newlib/stdio retargeting hooks
  the **application** defines to route I/O to its app-specific driver. They remain
  in the app (`app_main.c` / syscalls), and `term.*` is just their main client.
  Do not migrate them into `term`.
- **First consumer:** existing blocking `i_getline()` is the obvious upgrade target
  (W1) once the reader exists; the terminal piano (W2) is the longer-term one.
- **Console / target terminal:** USART2 VCP @ 921600; **Tera Term v5.3+** is the
  authoritative target (**Q1** 🟢). Decode against its VT100/VT220/xterm sequences.
- **Control chars pass through bare** (**Q2**): BS/DEL, CR, LF, Tab, etc. are
  returned as their literal ASCII codes — never intercepted or re-interpreted. The
  reader only *adds* decoding for ESC-led bursts.
- **Non-blocking contract (author, 2026-06-20):** the API is built for
  single-threaded super-loops where long blocking is unacceptable — that's the
  reason for `u32_timeout_ms`. **Typical case must be near-zero block:** with
  `timeout=0`, an empty input queue returns `-1` immediately, and a single
  *non-lead* char returns immediately. The inter-byte gather window (which *may*
  block "a little") only engages **after a lead char is seen**. (See **S1/S2**.)
- **stdin is non-blocking (verified):** `Core/Src/main.c` USER-CODE sets
  `setvbuf(stdin/stdout/stderr, NULL, _IONBF, 0)`; `__io_getchar()` (`app_main.c`)
  returns **`0` immediately** when the RX queue is empty. So `getchar()` never
  blocks and **"no char" = `0`** (a genuine NUL is indistinguishable — acceptable).
  The dual-timeout loop polls this directly.
- **Error/exception returns are negatives** `0xFFF0`–`0xFFFF` (`-16`..`-1`),
  enumerated in **S4**; valid keys are always positive (**D2**).
- **Configurable lead-char set (D3):** an API lets the caller register a small set
  of valid lead-in bytes (default `{ESC}`); only those trigger burst gathering.
- **Style:** Allman braces, GNU C11, Doxygen on public API.

### Reference: Tera Term / xterm key sequences (for T1 + decode table)

Verified against the Tera Term 5 manual, the xterm function-key table
(invisible-island.net), and MS Console VT docs. `ESC` = `0x1B`.

| Key | Normal (CSI) | App mode | Notes |
|-----|--------------|----------|-------|
| Up / Down / Right / Left | `ESC [ A/B/C/D` | `ESC O A/B/C/D` (SS3) | App-cursor mode flips CSI→SS3 |
| Home | `ESC [ H` *or* `ESC [ 1 ~` | `ESC O H` | PC vs VT220 encodings both exist |
| End | `ESC [ F` *or* `ESC [ 4 ~` | `ESC O F` | (sketch's `7~/8~` = rxvt flavor) |
| Insert / Delete | `ESC [ 2 ~` / `ESC [ 3 ~` | — | Delete key may instead send `DEL` (0x7F) per TT setting |
| Page Up / Page Down | `ESC [ 5 ~` / `ESC [ 6 ~` | — | |
| F1–F4 | `ESC O P/Q/R/S` (SS3) | — | VT100 PF-key form |
| F5–F12 | `ESC [ 15/17/18/19/20/21/23/24 ~` | — | (16/22 skipped by convention) |
| Modified special key | `ESC [ 1 ; <mod> X` | — | `<mod>` = 1 + (Shift 1 \| Alt 2 \| Ctrl 4 \| Meta 8); e.g. `ESC [ 1 ; 5 C` = Ctrl-Right |
| **Alt + `<ch>` (meta)** | `ESC <ch>` | — | Only when Tera Term **Meta key** (`MetaKey=on`) enabled; else Alt = TT shortcut |

**Decoder consequence:** Home/End (and Delete) have multiple legal encodings, so a
small **parser** that maps several byte patterns onto one `EXT_KEY_*` beats a rigid
exact-string table (see **D3**).

---

## Detail sections

### D1 — Terminal module name + naming convention
**Status:** 🟡 · **Needs user:** pick a convention

**Question:** What is the new terminal module called, and what's the function
naming convention everything (this reader + the W5 migrants) adopts?

**Context:** The scope extension means a **dedicated module** is the home, and we
want a **consistent module tag** (the loose untagged `utils` style no longer fits a
named API). `ANSI.h` stays as the raw escape-code macro header; the new module is
the *functional* API layer that uses it.

**Options (module tag → file → example names):**
- `term_` → `term.c/.h` → `i16_term_get_key()`, `i_term_get_line()`, `v_term_newline()`
- `tc_` (terminal-control) → `tc.c/.h` → `i16_tc_get_key()` …
- `con_` (console) → `con.c/.h`
- `vt_` (VT/ANSI) → `vt.c/.h`
- a branded lib name (e.g. `tui_`, `xterm_`, a personal tag)

**Leaning:** **`term_`** tag, files `App/Src/term.c` + `App/Inc/term.h`. Reader =
**`i16_term_get_key()`**; file-private decoder `i16_term_decode_key()`. Fits the
project's `{hung}_{module}_{verb}` rule and reads clearly.

**Resolution (2026-06-20 🟢):** **Approved.** Module = `App/Src/term.c` +
`App/Inc/term.h`, tag `term_`, public reader `i16_term_get_key()`. Convention
propagates to the **W5** migrants.

---

### D2 — Return-code map (grouped ranges + modifier bits + reserved user range)
**Status:** 🟡 · **Needs user:** confirm layout

**Direction (author):** group the `int16_t` return codes into **ranges** — bare
ASCII, editing keys, F-keys, Alt-keys, etc. — with a **reserved range for
user-defined macros**. Confirmed; here's a concrete layout that does that *and*
keeps modifiers composable.

**Proposed map** (`int16_t`; valid codes stay in positive `0x0000`–`0x7FFF`):

| Range | Meaning |
|-------|---------|
| `0x0000`–`0x00FF` | Literal byte (ASCII / control / 8-bit) — the **Q2** passthrough set |
| `0x0100`–`0x01FF` | **Editing / cursor** keys: arrows, Home, End, Ins, Del, PgUp, PgDn |
| `0x0200`–`0x02FF` | **Function** keys F1–F12 (+ headroom) |
| `0x0300`–`0x03FF` | Keypad / misc (reserved for growth) |
| `0x0E00`–`0x0EFF` | **User-defined key macros** (reserved — **D3 / W6**) |
| `0xFFF0`–`0xFFFF` (`-16`..`-1`) | **Error / exception codes** (negative; see **S4**) |

**Modifiers as orthogonal flag bits** (OR'd onto any base code, mirroring xterm's
`shift1 \| alt2 \| ctrl4` bitmask):

| Bit | Flag |
|-----|------|
| `0x1000` | `EXT_MOD_SHIFT` |
| `0x2000` | `EXT_MOD_ALT` (meta) |
| `0x4000` | `EXT_MOD_CTRL` |

So **Alt+a = `0x2000 | 'a'` = `0x2061`** (the "Alt-keys range" you wanted —
everything with bit `0x2000` set), **Ctrl+Right = `0x4000 | EXT_KEY_RIGHT`**, etc.
Base key space is `0x0000`–`0x0FFF` (4096 codes — plenty); max possible value
`0x7FFF` stays positive, so `-1` timeout is always unambiguous.

**Why flags over a flat per-modifier range:** they compose (Ctrl+Shift+key falls
out for free), match the wire encoding 1:1 (easy decode), and don't multiply the
named-key table. Your "range per group" intent is preserved for the *base* keys;
modifiers just live in the upper bits.

**Resolution (2026-06-20 🟢; amended for negative error block):** range map above +
orthogonal `EXT_MOD_SHIFT/ALT/CTRL` flag bits (`0x1000/0x2000/0x4000`). Base-key
space `0x000`–`0xFFF`; user-macro range `0x0E00`–`0x0EFF` reserved. **All
error/exception returns are small-magnitude negatives** (`0xFFF0`–`0xFFFF` =
`-16`..`-1`) so they never collide with valid key codes — enumerated in **S4**.
(Supersedes the earlier positive `EXT_KEY_UNKNOWN = 0x0FFF`.)

---

### D3 — Decode strategy
**Status:** 🟡 · **Needs user:** confirm

**Question:** Exact-string table match (sketch) or a small structured parser?

**Trade-off:** Pure table = dead simple, but explodes / can't generalize once you
want modifiers (`ESC [ 1 ; 5 C` = Ctrl-Right) or numeric `~` keys with params.
A lightweight CSI/SS3 parser (collect optional `;`-separated params, then the
final byte) handles families compactly and degrades gracefully.

**Leaning (strengthened by the Q1 findings):** **Hybrid parser + LUT.** A small
state machine splits a burst into `(intro, params[], final)`, then maps it via a
compact table. This is now the clear winner because Tera Term emits **multiple
encodings for the same key** (Home = `CSI H` *or* `CSI 1~`; End = `CSI F` *or*
`CSI 4~`; Delete = `CSI 3~` *or* `DEL`) — a rigid exact-string table would need
duplicate rows and still couldn't carry the `;<mod>` param. The parser also makes
the Alt-meta branch (`ESC <ch>`, **D7**) and modifier flags (**D2**) fall out
naturally. Falls back to `EXT_KEY_UNKNOWN` (**S4**).

**Extensibility (author requirement):** must decode **user-defined key macros**
(Tera Term keyboard-macro feature), not just the standard set. Design hooks:
- The decode table is **data-driven**; a second **user table** can be appended /
  registered (e.g. `v_term_register_keymap(const term_keymap_t *, count)`) that
  resolves into the reserved `0x0E00`–`0x0EFF` range (**D2**).
- **Lead-byte handling is not hard-wired to ESC.** v1 decodes ESC-led bursts, but
  the burst-collect entry should accept a small set of configurable **lead bytes**
  so custom macros using another unused control char — or an **8-bit (MSB-set)
  lead** — can trigger collection later (**W6**).

**Resolution (2026-06-20 🟢):** **Hybrid parser + data-driven LUT, as proposed.**
Plus an explicit API requirement: provide a function to **register a small set of
valid lead-in chars** (default `{ESC}`), e.g.
`v_term_set_lead_chars(const uint8_t *pu8_leads, uint8_t u8_count)`. Only a
registered lead byte triggers burst gathering; everything else returns as a bare
byte (preserves the **S1** near-zero-block contract). User keymap registration +
8-bit/alt-control leads remain **W6**.

---

### D4 — v1 key coverage
**Status:** 🟡 · **Needs user:** confirm

**Question:** Which keys must v1 decode?

**Candidate tiers:**
1. Arrows ↑↓→← (CSI `A/B/C/D`, SS3 `OA..OD`).
2. Nav cluster: Home, End, Insert, Delete, PgUp, PgDn (incl. the dual Home/End
   encodings).
3. Function keys F1–F12 (SS3 `OP..OS` + CSI `…~`).
4. **Alt-meta** of printable keys (`ESC <ch>`) — see **D7**.
5. Ctrl/Shift-modified specials (`CSI 1;<mod>X`) — **deferred** (parser leaves the
   door open; not in v1 since **Q2** only asked for Alt).

**Leaning:** v1 = tiers **1–4**. Tier 5 deferred to wishlist (the **D3** parser
already extracts the `<mod>` param, so adding it later is a table change, not a
rewrite).

**Resolution (2026-06-20 🟢):** v1 ships the editing/cursor cluster (arrows, Home,
End, Insert, Delete, PgUp, PgDn) with dual Home/End encodings via CSI + SS3.

**Amended (2026-06-20, post-HuIL):** **F-keys (tier 3) promoted into the shipped
keymap** — the HuIL report flagged F1–F12 as a gap (not marked "expected"), and
they're a clean 12-row add matching the user's **stock Tera Term KEYBOARD.CNF /
IBMKEYB.CNF** (F1–F4 = SS3 `OP/OQ/OR/OS`; F5–F12 = CSI `15/17/18/19/20/21/23/24 ~`).
Alt-meta (tier 4) and Ctrl/Shift-modified specials (tier 5) remain deferred
(**D7** / wishlist).

---

### D5 — Lead-in coverage
**Status:** 🟡 · **Needs user:** confirm

**Question:** Which second-byte branches after `ESC` do we handle?

**Context (confirmed for Tera Term):**
- `ESC [` → **CSI**: arrows (normal), nav cluster, F5–F12, modified keys.
- `ESC O` → **SS3**: arrows (app-cursor mode), F1–F4. Ignoring SS3 silently breaks
  arrows whenever app-cursor mode is on — must handle.
- `ESC <other printable>` → **Alt-meta** branch (**D7**): if exactly one byte
  follows and it isn't `[`/`O`, treat as `Alt+<ch>`.

**Leaning:** Handle all three branches (`[`, `O`, Alt-meta). Anything else (e.g.
`ESC` + a second byte that starts yet another multi-byte form we don't know) →
`EXT_KEY_UNKNOWN` per **S4**.

**Resolution (2026-06-20 🟢):** **v1 decodes `ESC [` (CSI) and `ESC O` (SS3)** — both
required for the tier-1/2 editing keys (SS3 covers app-cursor-mode arrows). The
Alt-meta branch (`ESC <ch>`) is **structurally present** via the configurable
lead-char/parser path but its **decode is deferred** (**D7/D4**). Unknown 2nd bytes
→ `EXT_KEY_UNKNOWN` (**S4**).

---

### D6 — Enum + table location
**Status:** 🟢 · **Needs user:** no

**Resolution (2026-06-20 🟢):** **`ANSI.h` stays standalone** — it's primarily
*terminal-control output* sequences (host→terminal) and the author uses it on its
own in other apps **without** the `term` library, so it must not gain a `term`
dependency. **All `term`-specific declarations live in `term.h`**: the `EXT_KEY_*`
enum, `EXT_MOD_*` flags, the negative error codes (**S4**), `term_keymap_t`, and the
prototypes. `term.h` *includes* `ANSI.h` (for `ESC` etc.); not vice-versa. Decode
table(s) file-private in `term.c`.

---

### D7 — Alt-meta handling
**Status:** 🟡 · **Needs user:** confirm (this is the "advise" ask)

**Question:** Do we decode Alt+key, and how, given it's ESC-led?

**Findings:** In Tera Term, Alt-meta is **off by default** — Alt drives TT/Windows
shortcuts (Alt+C copy, Alt+V paste, Alt+Enter maximize, …). Enabling **Setup →
Keyboard → "Meta key"** (`MetaKey=on` in `teraterm.ini`) makes `Alt+A` send
`ESC A`, and *disables* those Alt shortcuts. (`Meta8Bit` can alternatively send a
high-bit byte, but default/recommended is the ESC-prefix form.)

**Advice (my recommendation):** **Yes, support it — and no special exception is
needed**, exactly as you guessed. It's just the third branch of the burst decoder
(**D5**): `ESC` + one byte that isn't `[`/`O`, with nothing more inside the
inter-byte gap → `EXT_MOD_ALT | <ch>` (**D2**). Two caveats to record, not
blockers:
1. **Config dependency:** only works when the user enables TT "Meta key." Document
   it; the reader degrades gracefully (Alt does nothing special if disabled).
2. **Inherent ESC-vs-Alt ambiguity:** a lone ESC press and `Alt+<ch>` are
   distinguished *only* by whether a byte arrives within the inter-byte gap
   (**S2/S3**). This is the same trade-off `readline`/vim make; the timeout
   already handles it. Accept it.

**Resolution (2026-06-20 🔵 → 🟢 SHIPPED):** Implemented exactly as advised. In
`i16_gather_and_decode()`, the `ESC + <byte that is not '[' / 'O'>` branch now
returns **`EXT_MOD_ALT | <ch>`** (`0x2000 | ch`) instead of draining→UNKNOWN. No
look-ahead/drain is needed — a keyboard Alt-meta is exactly two bytes and a lone
ESC already returned earlier (it produced no intro byte). The two recorded caveats
stand and are non-blocking: (1) requires Tera Term **Meta key** enabled, else Alt
drives TT shortcuts and never reaches us (graceful no-op); (2) the lone-ESC vs Alt
ambiguity is bounded by the inter-byte gap (**S2/S3**), the same trade-off
readline/vim make. The `[k]` test renders a `CTRL+/ALT+/SHIFT+` prefix by masking
the `EXT_MOD_*` bits off the base code, so future Ctrl/Shift forms display for free.

---

### S1 — Timeout architecture
**Status:** 🟡 · **Needs user:** confirm

**Question:** One overall timeout, or the sketch's outer (wait-for-first-key) +
inner (inter-byte gap) pair?

**Leaning:** Keep the **two-timeout** model — it's the correct VT-reader shape:
`u32_timeout_ms` bounds how long we wait for *any* key; a separate fixed
inter-byte gap bounds the ESC burst. Define `u32_timeout_ms == 0` = non-blocking
single poll (consistent with `i_getchar_blocking_with_timeout`), but **once a key
arrives**, always allow the inter-byte window to complete a burst (see **S2**).

**Resolution (2026-06-20 🟢):** **Two-timeout model with a strict near-zero-block
contract.** `u32_timeout_ms` bounds the wait for the *first* byte; the inter-byte
gap is separate and only matters once a lead char arrives. **`timeout=0` typical
path blocks ~nothing:** empty queue → `-1` now; one non-lead char → return it now.
Only a lead char (e.g. ESC) authorizes the brief inter-byte gather. This is the
whole reason the `timeout` arg exists (single-threaded super-loop use).

---

### S2 — Inter-byte gap value + short/zero overall timeout
**Status:** 🟡 · **Needs user:** confirm

**Question:** What inter-byte gap, and how does it interact with a tiny/zero
overall timeout?

**Leaning:** Inter-byte gap ≈ **50 ms** (`EXTENDED_KEY_TIMEOUT_MS`); generous vs
the ~µs spacing of a real burst at 921600 baud, safe against chunking. The
inter-byte window is **independent of** the overall timeout: if the first byte was
ESC, we always give the burst its full inter-byte budget even if `u32_timeout_ms`
was 0 (fixes the sketch's tangled `u32_timeout >= TIMEOUT_MS` clause).

**Resolution (2026-06-20 🟢):** Inter-byte gap ≈ **50 ms**, engaged **only after a
lead char**. **Early-out optimization (important for latency):** the gap is an
*upper bound*, not a mandatory dwell — the parser returns the instant a sequence is
**complete** (e.g. a CSI final byte in `0x40`–`0x7E`, or an SS3 final). The full
gap is only spent in the genuinely ambiguous cases: bare ESC and (future) Alt-meta,
where we must wait to see whether another byte follows. So well-formed bursts add
near-zero latency; only a lone ESC costs the ~50 ms.

---

### S3 — Bare-ESC policy + latency
**Status:** 🟡 · **Needs user:** confirm

**Question:** What does a lone ESC press return, and is the latency acceptable?

**Leaning:** A bare ESC (no bytes within the inter-byte gap) returns **`ESC`
(0x1B)** like any other key. Inherent cost: a real ESC press is delayed by the
inter-byte gap (~50 ms) — universally accepted by terminal apps; fine here.

**Resolution (2026-06-20 🟢):** Bare ESC → returns `ESC` (0x1B) after the inter-byte
gap elapses with no follow byte. The ~50 ms latency on a lone ESC is accepted (same
trade-off readline/vim make).

---

### S4 — Error / corner-case return codes
**Status:** 🟢 · **Needs user:** no

**Resolution (2026-06-20 🟢):** A small set of **negative** error codes in
`0xFFF0`–`0xFFFF` (`-16`..`-1`), never colliding with positive key codes (**D2**):

| Code | Meaning |
|------|---------|
| `-1` | **No key** — nothing in the input queue before timeout |
| `-2` | **Unrecognized** multi-char burst sequence |
| `-3` | **Buffer overflow** (see **S5**) |
| `-4`..`-15` | Reserved / TBD trappable exceptions |

Suggest naming these `TERM_KEY_NONE` (`-1`), `TERM_KEY_UNKNOWN` (`-2`),
`TERM_KEY_OVERFLOW` (`-3`) in `term.h`. Reserved slots left for things like a
future "partial sequence / would-block" indication.

**Generalized bare-lead rule (author):** if a **registered lead char** arrives but
is *not* followed by a burst within the inter-byte gap, it is returned as its own
**bare single-byte char** — exactly how the original code returned a lone ESC.
This applies to *any* configured lead byte (**D3**), not just ESC.

---

### S5 — Overflow policy
**Status:** 🟢 · **Needs user:** no · *(the recursive footnote — see S4 — see S5)*

**Resolution (2026-06-20 🟢):** **Buffer overflow is an error → return `-3`**
(`TERM_KEY_OVERFLOW`, **S4**). On overflow, keep **draining** incoming bytes until
the inter-byte gap elapses (so the stream stays in sync / we don't leave half a
burst in the queue), stop storing past the buffer, then return `-3`. Size the
burst buffer for the **longest v1 sequence** (editing keys are ≤ ~3 bytes after the
lead; `EXTENDED_KEY_MAX_LEN = 5` gives comfortable headroom for the planned F-keys
without modifiers). Modified/`;mod` forms are deferred, so we don't need the 6+
byte budget yet.

---

### I1 — "No char" sentinel
**Status:** 🟢 · **Needs user:** no

**Resolution:** stdin is **non-blocking** (`Core/Src/main.c`: `setvbuf(…,_IONBF,0)`);
`__io_getchar()` (`app_main.c`) returns **`0` immediately** when the RX queue is
empty (`i16_uart_stream_rx_byte() < 0` → `0`). So **"no char" = `0`** — handle as
`<= 0` for safety in both loops (the sketch inconsistently used `< 0` / `> 0`). A
literal NUL byte is therefore indistinguishable from "empty" — acceptable, NUL
isn't a console key.

---

### I2 — Reuse helper vs purpose-built loop
**Status:** 🟢 · **Needs user:** no

**Resolution (2026-06-20 🟢):** **Hand-roll** the dual-timeout loop directly on the
non-blocking `getchar()` (**I1**) — do *not* build on `i_getchar_blocking_with_timeout()`
(single overall timeout only; the burst/inter-byte logic + early-out + lead
detection need finer control). The loop still pumps `v_app_polling_task()` each
spin per the cooperative invariant.

---

### I3 — Table / parser representation
**Status:** 🟢 · **Needs user:** no (ties to **D3**)

**Author intent:** keep it **lightweight and code-space efficient**. Original
reasoning: a pointer-per-entry costs 4 bytes + the string body; fixed short arrays
(< 8, since a typical sequence sans lead is ≤ 4 bytes) avoid the pointer. The
flexible/extensible design won't be *quite* as tight — accepted — but stay as lean
as reasonable.

**Resolution (2026-06-20 🟢):** Use a **compact, data-driven match table — no
per-entry pointers.** Because **D3** is a parser, entries don't store full strings;
they store a small fixed tuple, e.g.:

```c
typedef struct
{
    uint8_t  u8_intro;   /* 0 = CSI ('['), 1 = SS3 ('O'), … */
    uint8_t  u8_param;   /* leading numeric param, 0 if none (e.g. 15 for F5) */
    uint8_t  u8_final;   /* final byte: 'A','~','P', … */
    uint16_t u16_key_id; /* EXT_KEY_* */
}
term_keymap_t;           /* ~5 bytes/entry, no pointer, no NUL string */
```

The standard table is `const` (flash); count via `ARRAY_COUNT` (no sentinel row).
This is actually **tighter** than the original fixed-string idea (no stored lead
byte, no terminator) while handling the multi-encoding cases and `;param` forms.
The optional **user keymap** (**D3/W6**) is the same struct, registered separately.

---

### T1 — ANSI/xterm key-sequence reference (discovery deliverable)
**Status:** 🟡 · **Needs user:** confirm home

**Status note:** the reference content is **done** — see *Reference: Tera Term /
xterm key sequences* in LOCKED CONTEXT above (verified against TT5 manual + xterm
table + MS console docs). Remaining decision is only **where the canonical copy
lives**.

**Options:** (a) block comment beside the decode table in `ANSI.h`; (b) a standalone
`Docs/` reference note; (c) both (comment in code + a Docs pointer).

**Resolution (2026-06-20 🟢):** Reference comment block lives in **`term.h`** next to
the keymap table (not `ANSI.h`, which stays standalone — **D6**), plus the table
kept here in the plan as the discovery record. **Testing is dual:**
- **HuIL (Human-in-the-Loop 😄):** a **debug-menu** function that reads keys via
  `i16_term_get_key()` and prints the decoded `EXT_KEY_*` / byte / error — press
  keys, watch the decode.
- **Automated:** the *same* debug-menu hook is driven by a **`playstr`-style paced
  UART injection** of raw escape bursts vs. golden expected decodes (**T2**).

---

### T2 — Bench test method
**Status:** 🔵 → 🟢 (SHIPPED) · **Needs user:** no

**Approach (decided via T1):** one **debug-menu decode-echo** function serves both
roles — interactive HuIL testing *and* the target for **automated** `playstr`-style
paced UART injection (raw escape bursts in, compare printed decode to golden).

**Resolution (2026-06-20 🟢):** Built the `term.*` analogue of the PLAY harness,
then **superseded the menu path with a deterministic automation REPL** (see the
*Automation harness* section below — the host now reads to a framed terminator,
not a timer):
- **`scripts/term_golden/keys.json`** — golden vector set: `{name → {send (hex
  burst), expect (**exact** uint16 result code), desc}}`. Covers arrows (CSI **and**
  SS3), both Home/End encodings (PC `H`/`F`, VT220 `1~`/`4~`, SS3), Ins/Del/PgUp/
  PgDn, F1/F4 (SS3) + F5/F10/F12 (CSI `~`), **Alt-meta** (`ESC a`/`ESC Z` → `0x2061`/
  `0x205A`, D7), bare printable + bare control byte, **unknown** (`0xFFFE`: CSI `Z`
  back-tab and `;mod` Ctrl-Right), and **overflow** (`0xFFFD`).
- **`scripts/term_key_bench.py`** — harness client (same `bench.defaults.json` /
  ST-Link discipline as `play_bench.py`). Enters the REPL (`0xDA`), sends
  `K <hex>\r` per vector, reads to `<HRN K res=0xRRRR …>`, compares the **exact**
  code, quits (`0xA5`). Subcommands `run [names…]` / `list` / `send <hex>`.
  Run: `python scripts/term_key_bench.py run` (needs the COM port free — close any
  Tera Term session holding it first).

Follow-on (optional): register a `/keytest` skill mirroring `/playtest`, and fold
the vector run into a CI/roundtrip step. Tracked loosely; not a v1 blocker.

---

### Automation harness — deterministic REPL (built 2026-06-20, beyond v1)

Replaces the fragile *ESC×3 → menu-key → read-for-timeout* automation entry with
a resident, framed command REPL. Author-approved design (all points greenlit):

- **Entry/exit:** a high-bit sentinel `0xDA` (= `0x5A|0x80`, un-typeable / can't
  collide with an ASCII menu key) enters; `0xA5` (or `Q`) exits. Resident mode
  (many ops per session), **not** auto-return-after-one — kills the "next byte
  leaks into the menu" race.
- **Framing:** `<HRN v1 RDY>` on entry, a `<HRN …>` line per op, `<HRN BYE>` on
  exit — host reads to a terminator, fully deterministic.
- **Protocol:** line-oriented text (CR-terminated). Built-ins `V` (version/ping,
  pins firmware), `L`/`?` (list ops), `Q`/`0xA5` (quit). Domain ops come from a
  caller table (`harness_op_t`): **`K <hex>`** = inject a burst into the real
  decoder and report the exact result code; **`P`** = reuse the human/menu PLAY
  string entry verbatim (`v_debug_play_playstr`).
- **Safety:** 15 s inactivity timeout auto-exits (anti-wedge); cooperative
  `v_app_polling_task()` pumped every spin.
- **Module boundary (author directive, 2026-06-20):** *all* test logic lives in
  `App/Src/test_harness.c` (+ `App/Inc/test_harness.h`, gated `TEST_HARNESS_ENABLED`):
  the executive, the `K`/`P` op handlers + table, **and** the interactive `[k]`
  HuIL decode test (`v_test_harness_key_huil`). `debug_menu.c` keeps only thin
  hooks — the sentinel→`v_test_harness_run()` intercept and the `[k]` menu entry
  pointing at the exported HuIL fn — and **exports** `v_debug_play_playstr()` for
  the `P` op to reuse. The test module legitimately depends on the app modules it
  exercises (`term`, `debug_menu`/PLAY); other modules export the hooks it needs.
- **term test-only surface:** `v_term_inject` (+ `TERM_INJECT_MAX`) is grouped in
  `term.h` under a *"Testing / HIL hooks — NOT for normal app use"* banner.
  `pc_term_key_name` stays in the normal API (a display helper, cf. ncurses
  `keyname()`).
- **Known nit:** Tera Term `Meta8Bit` would make `Alt+Z` emit `0xDA` (the enter
  byte). Non-default; noted.
- **Follow-on:** migrate the *remaining* menu test entries (LED/I2S/quick tests)
  behind the harness as desired; ESP-IDF `console`/`linenoise` is a reference for
  a future richer REPL.

---

### Q1 — Target terminals
**Status:** 🟢 · **Needs user:** no

**Resolution (2026-06-20):** Target = **Tera Term v5.3+**, decoding whatever it
supports (VT100/VT220/xterm). We're not chasing PuTTY/Windows-Terminal quirks in
v1. In practice TT follows the standard xterm/VT sequences captured in the
LOCKED-CONTEXT reference table, so this is broadly portable anyway.

---

### Q2 — Modifier detection in v1?
**Status:** 🟢 · **Needs user:** no

**Resolved part (locked):** control characters (BS/DEL, CR, LF, Tab, …) are
**passed through bare** as their ASCII codes — never intercepted. The reader only
adds ESC-burst decoding.

**Open part:** the user wants **Alt-meta** support ("would be nice… advise") but no
explicit Ctrl/Shift matrix. Recommendation: **support Alt-meta** (see **D7**),
**defer** Ctrl/Shift-modified specials to the wishlist (tier 5 in **D4**). Confirm
and this goes 🟢.

**Resolution (2026-06-20 🟢):** Control chars pass bare (locked). **Alt-meta is
deferred past v1** (planned, not immediate — **D7**); Ctrl/Shift-modified specials
stay wishlist. So v1 has no modifier decoding at all — just bare keys + the tier-1/2
editing keys.

---

### Q3 — Terminal-size / cursor query scope
**Status:** 🔵 · **Needs user:** no (deferred by author)

**Resolution (2026-06-20):** **Deferred** — author wants the extended-key reader
in place and working first. Tracked as **W3**. Build the key reader so its
burst-collect + timeout core can be reused by a future `i16_get_terminal_size()` /
cursor-report parser (`ESC[6n`, `ESC[18t`, `ESC[999;999H` + save/restore).

---

### W5 — Terminal-function migration inventory (future sweep)
**Status:** 🔵 (backlog) · own effort/plan when scheduled

Candidate terminal funcs to gather into `term.*` (initial scan — not exhaustive):

| Current | Where | Likely fate |
|---------|-------|-------------|
| `i_getchar_blocking` | `utils.c` | keep (rename `i_term_get_char` or wrap) |
| `i_getchar_blocking_with_timeout` | `utils.c` | keep / fold into key reader's core |
| `i_getline` | `utils.c` | **replace** with editing+history version (**W1**) |
| `v_newline` / `v_conditional_newline` | `utils.c` | keep |
| `v_repeat_char` | `utils.c` | keep (used by banner) — keep a util alias if needed |
| `v_hexdump` | `utils.c` | borderline — probably stays in `utils` (data util, not terminal) |
| `__io_putchar` / `__io_getchar` | `app_main.c` / syscalls | **stay app-side** — newlib stdio hooks the app defines; NOT migrated into `term` |
| `v_app_polling_task` (weak stub) | `utils.c` → **`term.c`** | **move** the weak no-op into `term.c`; app keeps the strong override |
| lone `ANSI_*` use | `debug_menu.c` | migrate call sites to `term_*` helpers over time |

Naming convention from **D1** applies. Migrate incrementally (don't break the debug
menu); keep thin `utils` aliases where call sites are many, retire later.

---

## Terminal size / cursor query (Q3 → active 2026-06-20)

**Building block #2** of the terminal library — a cooperative query that asks the
terminal "where is the cursor / how big are you?" and parses the ESC-led reply.
**Reuses the key-reader core:** the reply is just an inbound CSI burst, so the same
non-blocking inject-aware getbyte + inter-byte-timeout discipline (S1/S2/I2) reads
it; only the *parse target* differs (a numeric report, not a keymap entry).

**Working mode:** resolve D8–D11 / S6 / I4 / T3 by ID in chat; no firmware until the
method rows (D8/D9) lock.

**Status: SHIPPED 2026-06-20** — all rows 🟢. Implemented in `App/Src/term.c` +
`App/Inc/term.h` (API per D9) using `ANSI.h`'s `ANSI_GET_CURSOR` / new
`ANSI_REPORT_TEXT_AREA` (`CSI 18t`) / save-restore macros. Build clean (0/0),
automated parser vectors **9/9** (`scripts/term_report_bench.py` +
`scripts/term_golden/reports.json`), key decoder unregressed (**30/30**). Live `[w]`
HuIL query against Tera Term is the remaining human check (resize window → re-run).

### Findings (verified 2026-06-20 — Tera Term 5 manual, *Supported control functions*)

Tera Term v5 supports **both** candidate mechanisms:

| Query | Send | TT reply | Notes |
|-------|------|----------|-------|
| **Cursor position (CPR / DSR 6)** | `CSI 6 n` | `CSI r ; c R` | ECMA-48 standard; universal. `ANSI_GET_CURSOR` already in `ANSI.h` |
| **Text-area size (XTWINOPS 18)** | `CSI 18 t` | `CSI 8 ; rows ; cols t` | xterm ext (not ECMA-48); TT supports it, but `allowWindowOps`-class gating disables it on some emulators |
| **Size via CPR corner-trick** | `ESC 7` · `CSI 999;999H` · `CSI 6 n` · `ESC 8` | `CSI r ; c R` (= size) | Most portable; reuses the **one** CPR parser; briefly moves+restores cursor |

### Mini-board

| ID | Status | Subject (one line) |
|----|--------|-------------------|
| **D8** | 🟢 | `get_size` method — **try direct `18t` first, fall back to CPR corner-trick**; first-choice = build option |
| **D9** | 🟢 | API = **(B) struct out-param + `bool` return** (`b_term_get_*` → fill `term_*_t{rows,cols,err}`, return success); bool ignorable |
| **D10** | 🟢 | No-reply → return error, **out-params untouched**; defaults via header `#define`s |
| **D11** | 🟢 | Cursor save/restore for corner-trick = **DECSC/DECRC** (`ESC 7`/`ESC 8`, already in `ANSI.h`) |
| **D12** | 🟢 | `err` doubles as a **success channel** — on success it reports WHICH method answered (`TERM_OK` / `TERM_OK_DIRECT` / `TERM_OK_CPR`) — **shipped** 2026-06-20 |
| **S6** | 🟢 | stdin interleaving — **caller's job** to drain unwanted RX first; reader keeps scan-to-lead + garbage rejection |
| **I4** | 🟢 | `x_term_read_csi_report()` on inject-aware `i_term_getbyte()` — **shipped** 2026-06-20 |
| **T3** | 🟢 | Harness ops `C`/`X`/`Z` + `reports.json` (9 vectors, 9/9) + `[w]` live HuIL — **shipped** 2026-06-20 |

### D8 — get_size method
**Resolution (2026-06-20 🟢, author):** Implement **both** methods and have
`x_term_get_size()` **try the direct XTWINOPS `CSI 18t` first, then fall back to the
CPR corner-trick** if the direct query doesn't answer (timeout / bad reply). Author
rationale: the direct get is cleaner and avoids cursor "flicker" (no jumping the
cursor to the far corner and back) when the cursor is visible — only pay the
corner-trick's cursor disturbance if `18t` is unsupported/disabled.

- **Which method runs first is a module build option** (e.g.
  `TERM_SIZE_PREFER_DIRECT` default on) so a deployment on a terminal lacking `18t`
  can flip to corner-trick-first and skip the dead `18t` round-trip + its timeout.
- Both underlying methods are also exposed as **separate API functions** (per the
  author's "provide both as separate API functions" note) for callers that want a
  specific one without the auto-fallback wrapper — `b_term_get_size_direct()`
  (XTWINOPS `18t`) / `b_term_get_size_cpr()` (corner-trick), with `b_term_get_size()`
  as the try-direct-then-fallback convenience. All three share the **D9** shape
  (`bool` return + `term_size_t *` out-param).
- The CPR corner-trick still reuses the **one** `CSI r;c R` parser that
  `x_term_get_cursor()` needs; the `18t` path adds a sibling parse of
  `CSI 8;rows;cols t` (three params, final `t`) — both via the **I4** report reader.

**Note (timeout budget):** the fallback means a terminal that silently ignores the
*first* method costs one timeout before the second is tried — size the per-method
`u32_timeout_ms` modestly so the worst case stays snappy.

### D9 — API shape + return type
**Question (elaborated 2026-06-20):** a size/cursor query must hand back **two
numbers** (row+col / rows+cols) **and** a success indicator, but C returns one value.
Pick the return idiom:

- **(A) status-return + pointer out-params** *(leaning)* — `return` carries
  OK/TIMEOUT/BAD_REPLY; dimensions come back through caller-supplied pointers:
  ```c
  typedef enum { TERM_ERR_OK = 0, TERM_ERR_TIMEOUT, TERM_ERR_BAD_REPLY } term_err_t;
  term_err_t x_term_get_cursor(uint16_t *pu16_row,  uint16_t *pu16_col,  uint32_t u32_timeout_ms);
  term_err_t x_term_get_size  (uint16_t *pu16_rows, uint16_t *pu16_cols, uint32_t u32_timeout_ms);
  ```
  Composes cleanly with **D10** (on error, leave the pointers untouched).
- **(B) return a small struct by value** — `term_size_t { u16_rows; u16_cols; err; }`;
  no out-params, reads nicely, copies a few bytes.
- **(C) packed int** (`(rows<<16)|cols`, negative = error) — compact, cryptic; not
  recommended.

**Resolution (2026-06-20 🟢, author):** **(C) is out.** Chose **(B) the result struct**
— with a variation: the struct **carries the `err` member**, *and* the function
**also returns a plain `bool`** (`true` = success). The bool is the convenience
"did it work?" channel; on `false` the caller may "look at `err` if interested."
Per common C idiom the **caller is free to ignore the bool** (or both, after
pre-seeding defaults). The struct travels by **pointer out-param** (not by value),
so D10's "don't touch the out-params on error" applies cleanly — see below.

```c
/* D12: success codes carry the method that answered; see TERM_STATUS_IS_OK(). */
typedef enum {
    TERM_OK = 0, TERM_OK_DIRECT, TERM_OK_CPR,      /* success (method) */
    TERM_ERR_TIMEOUT, TERM_ERR_BAD_REPLY           /* failure          */
} term_err_t;

typedef struct { uint16_t u16_row;  uint16_t u16_col;  term_err_t err; } term_pos_t;   /* cursor */
typedef struct { uint16_t u16_rows; uint16_t u16_cols; term_err_t err; } term_size_t;  /* window */

bool b_term_get_cursor(term_pos_t  *px_pos,  uint32_t u32_timeout_ms);
bool b_term_get_size  (term_size_t *px_size, uint32_t u32_timeout_ms);
```

**Fill contract (reconciles with D10):**
- **Success:** write `rows/cols` **and** `err = ` the success code for the method
  used (see **D12**: `TERM_OK_DIRECT` / `TERM_OK_CPR`, or `TERM_OK` for cursor);
  return `true`.
- **Failure:** set `px->err` to the specific code (the status channel), **leave
  `rows/cols` untouched** (D10), return `false`. So a caller that pre-seeds
  `px->u16_rows = TERM_DEFAULT_ROWS` keeps its default on failure.

**Caller patterns (all valid):**
```c
term_size_t sz;
if (b_term_get_size(&sz, 50)) { use sz.u16_rows, sz.u16_cols; }   /* quick bool */

term_size_t sz2 = { .u16_rows = TERM_DEFAULT_ROWS, .u16_cols = TERM_DEFAULT_COLS };
(void) b_term_get_size(&sz2, 50);                                 /* ignore bool; defaults survive failure */
switch (sz2.err) { ... }                                          /* or inspect detailed err */
```

**Naming:** `b_` prefix (bool return) per project Hungarian; distinct `term_pos_t` /
`term_size_t` for read clarity (same shape, different intent). Enum tag `term_err_t`
matches the project's `*_err_t` convention. Separate D8 method functions follow the
same shape (`b_term_get_size_direct` / `b_term_get_size_cpr`).

### D10 — no-reply fallback
**Resolution (2026-06-20 🟢, author):** on timeout/garbage, **return failure and do NOT
modify the caller's dimension members** (`u16_row/col` / `u16_rows/cols` keep
whatever the caller put there). Expose sane defaults as **header `#define`s** —
`TERM_DEFAULT_ROWS` (24) / `TERM_DEFAULT_COLS` (80) — for the caller to apply if it
wants. Policy stays with the consumer; the primitive never guesses dimensions.

**Scope of "don't touch" (author clarification 2026-06-20):** the **`err` member is
absolutely subject to modification** — it's the status channel and is *always*
written (the specific code on failure, a **success/method code** on success — see
**D12**). Only the **x/y dimension members are preserved on failure.** This exists to
support the author's intended call pattern:

1. **Instantiate** the result struct and **initialize the x/y members** with
   defaults of the caller's choosing — typically `TERM_DEFAULT_ROWS/COLS`, but the
   caller's free to pick others.
2. **Invoke** the function — and **probably ignore the `bool`/`err` return** (D9).
3. **Use whatever is in the struct's x/y** going forward.

Because failure leaves x/y untouched, step 3 transparently yields the caller's
pre-seeded defaults when the terminal didn't answer — no branch needed. (A caller
that *does* care still has the `bool` return and `err` member to inspect.)

```c
term_size_t sz = { .u16_rows = TERM_DEFAULT_ROWS, .u16_cols = TERM_DEFAULT_COLS };
(void) b_term_get_size(&sz, 50);   /* ignore result */
/* sz.u16_rows/cols = real size on success, my defaults on failure — either way usable */
```

### D11 — cursor save/restore
**Resolution (2026-06-20 🟢):** **DECSC/DECRC** (`ESC 7` / `ESC 8`) via the existing
`ANSI_SAVE_CURSOR` / `ANSI_RESTORE_CURSOR` macros — robust on TT, one byte cheaper
than SCO `CSI s`/`CSI u`. Used by the corner-trick fallback path (**D8**).

### D12 — status as a success channel (method reporting)
**Resolution (2026-06-20 🟢, author):** make `err` report **success as well as
failure** — specifically *which* acquisition method answered. The success range now
splits into method codes so a caller can tell how the size came back:

```c
TERM_OK = 0,        /* success, method n/a (cursor query — single method)        */
TERM_OK_DIRECT,     /* size answered by the direct XTWINOPS query  (CSI 18t)      */
TERM_OK_CPR,        /* size answered by the CPR cursor-move trick                 */
TERM_ERR_TIMEOUT, TERM_ERR_BAD_REPLY        /* failures                          */
#define TERM_STATUS_IS_OK(s)  ((int)(s) <= (int)TERM_OK_CPR)
```

- **Single-method APIs just seed the code they used:** `b_term_get_cursor` → `TERM_OK`,
  `b_term_get_size_direct` → `TERM_OK_DIRECT`, `b_term_get_size_cpr` → `TERM_OK_CPR`.
- **The combined `b_term_get_size`** fills `err` with the **first method that
  succeeded** (its sub-call already wrote the right code; the wrapper just returns it).
  So `err == TERM_OK_CPR` after `b_term_get_size` means the direct `18t` was ignored
  and the corner-trick carried the day — useful telemetry for spotting terminals
  that don't support XTWINOPS.
- Success codes are grouped low (0..2) so the `bool` return and `TERM_STATUS_IS_OK()`
  stay trivial; errors follow (3..4). `pc_term_status_name()` renders all five for
  the `[w]` HuIL readout. Golden vectors (`reports.json`) assert the exact method
  code per op (cursor=0, direct=1, cpr=2).

### S6 — input-stream interleaving
**Resolution (2026-06-20 🟢, author):** **It is the caller's responsibility to ensure
the input stream is quiet before invoking a query** — anything queued that the
consumer doesn't want lost must be drained first. The query cannot do this
deterministically without poking the device driver's RX buffer (e.g. `uart_stream`),
and a **hard no-go** is breaking the API's **platform/driver-agnostic** contract.
This does **not** drop the reader's own robustness: it still **scans past non-`ESC`
bytes** to the `CSI`-led report and rejects garbage, bounded by the timeout — a
stray byte arriving mid-window is simply discarded by that scan. A driver-aware
response-router stays out of scope (would couple `term` to a specific UART driver).

### I4 — reply parser reuse
**Leaning:** a focused `i16_term_read_csi_report(uint16_t *pu16_params, uint8_t
u8_max, char *pc_final, uint32_t u32_timeout_ms)` that waits (bounded) for `ESC [`,
collects `;`-separated decimal params, and stops at the final letter — built on the
**inject-aware** `i_term_getbyte()` + inter-byte timeout, so the **same
`v_term_inject()` harness path** can feed synthetic replies for deterministic
tests. `get_cursor`/`get_size` are thin wrappers that send the request then call it.
*Confirm shared-core approach.*

### T3 — test plan
**Leaning:** mirror the key-reader test stack — (a) a **harness op** that injects a
synthetic report burst (e.g. `\x1B[24;80R`) and prints parsed `rows/cols`, with
golden vectors in `scripts/term_golden/`; (b) a **HuIL menu entry** that performs a
*live* `x_term_get_size()` against the real Tera Term and prints the result (resize
the TT window, re-query, watch it track). *Confirm; pick the harness op letter at
implement.*

---

## Output primitive library (building block #3 — API roadmap)

**Author intent (2026-06-20):** beyond input (key reader, #1) and queries
(size/cursor, #2), build a **broad set of output primitives** wrapping the most
useful ANSI/VT operations under the `term_` API — cursor show/hide, cursor motion
(relative / absolute / home / save-restore), display attributes
(bold/dim/underline/reverse/blink/…), foreground & background colors (16 / 256 /
RGB), and erase / scroll / line-edit ops. **`ANSI.h`'s existing macro set is the
template** for what counts as "useful"; this section catalogs the candidate
primitive set and flags ANSI ops *not yet* in `ANSI.h` that a general-purpose,
ncurses-like API may want. **Author is seeking agent input on the complete set** —
this is a living catalog, not locked.

**Layering (preserves the `ANSI.h`-standalone rule, D6):**
- **L0 — raw escape macros:** `ANSI.h` (host→terminal byte strings; usable on its
  own with **no `term` dependency**). *Exists.*
- **L1 — output primitives:** thin `term_` functions emitting L0 strings to the
  app's stdout (e.g. `v_term_set_fg(color)`, `v_term_clear_eol()`,
  `v_term_move(row,col)`) — add runtime params, arg validation, uniform naming.
- **L2 — input + queries:** key reader (#1, done) + size/cursor (#2, this plan).
- **L3 — buffered "window" layer (aspirational):** the big ncurses idea — an
  off-screen cell buffer diffed against the screen on a `refresh()`. **Not** a
  near-term primitive; the eventual direction for flicker-free full-screen UIs (W).

### Candidate primitive catalog

Legend: **M** = `ANSI.h` macro already exists (just wrap it) · **+** = useful op
**not** in `ANSI.h` yet (consider adding macro + primitive) · **W** = higher-level /
later.

| Group | Primitives (proposed `term_` verbs) | ANSI.h | Notes |
|-------|-------------------------------------|:------:|-------|
| **Cursor visibility** | `show_cursor` / `hide_cursor` | M | `ANSI_SHOW/HIDE_CURSOR` |
| **Cursor motion (rel)** | `up/down/left/right(n)` · `next_line/prev_line(n)` | M | `ANSI_CURSOR_*`, `ANSI_NEXT/PREVIOUS_LINE` |
| **Cursor motion (abs)** | `move(row,col)` · `home` · `col(c)` HPA · `row(r)` VPA | M / + | have move/home/HPA; **VPA `CSI Ps d` = +gap** |
| **Cursor save/restore** | `save_pos` / `restore_pos` | M | DECSC/DECRC (`ESC 7/8`) |
| **Attributes (SGR)** | `attr_reset` · `bold/dim/underline/blink/reverse/hidden/strikeout` (+ each `_off`) | M | full set present; maybe a combined `set_attrs(mask)` |
| **Colors** | `fg/bg` (16) · `fg/bg_256(idx)` · `fg/bg_rgb(r,g,b)` · `color_default` | M | `ANSI_FG_*` + `_FMT` + RGB; maybe `color_pair(fg,bg)` (ncurses-ish) |
| **Erase** | `clear_eol/bol/line` · `clear_eos/bos/screen` · `clear_scrollback` · `clear_home` | M | full set present |
| **Scroll** | `scroll_up/down(n)` · `set_scroll_region(top,bot)` | M / + | have scroll; **DECSTBM `CSI t;b r` = +gap** (status-bar/pane layouts) |
| **Line/char edit** | `insert_line/delete_line(n)` · `delete_char(n)` · `insert_char(n)` ICH · `erase_char(n)` ECH | M / + | have IL/DL/DCH; **ICH `CSI @` / ECH `CSI X` = +gaps** (pairs with W1 line editor) |
| **Modes** | `insert/replace_mode` · `alt_screen_enter/leave` · `autowrap_on/off` · `app_cursor_keys_on/off` | M / + | have INS/OVR; **alt-screen (DECSET 1049) = big +gap**; **DECCKM ties to the key reader's CSI↔SS3 flip** |
| **Queries** | `get_cursor` · `get_size` (#2) · `device_attributes` (DA `CSI c`) | + | DA identifies the terminal; optional |
| **Misc** | `bell` (BEL 0x07) · `soft_reset` (DECSTR `CSI ! p`) · `full_reset` (RIS) · `set_title` (OSC 0/2) | M / + | `ANSI_RESET` = RIS; BEL / DECSTR / OSC = +gaps |

### Notable gaps worth adding to `ANSI.h` (ranked)

1. **Alternate screen buffer** — DECSET `?1049h` / DECRST `?1049l`. The single most
   valuable add for an ncurses-like full-screen mode: enter → draw → leave restores
   the user's prior screen + scrollback intact. **High value.**
2. **Scroll region** — DECSTBM `CSI <top>;<bot> r`. Enables status bars / split panes.
3. **VPA** row-absolute (`CSI Ps d`) — symmetry with the existing HPA column-absolute.
4. **ICH / ECH** (`CSI Ps @` / `CSI Ps X`) — char insert / erase for in-line editing
   (natural partner to the future `i_getline` editor, **W1**).
5. **App-cursor-keys mode (DECCKM)** — lets the app *choose* CSI vs SS3 arrows, both
   of which the key reader already decodes; closes the input/output loop.
6. **OSC window title**, **BEL**, **DECSTR soft reset** — small but occasionally handy.

### ncurses concept map (inspiration only — rolling our own)

*Author note: hasn't touched ncurses in decades — this is a quick orientation, not a
spec to copy. Worth a look at ncurses for ideas; our API and naming stay bespoke.*

| ncurses | Rough `term_` analogue | Status |
|---------|------------------------|--------|
| `initscr` / `endwin` | alt-screen enter/leave + setup/teardown | gap #1 |
| `move(y,x)` | `v_term_move(row,col)` | have (M) |
| `addch` / `addstr` / `printw` | `printf` to the retargeted stdout | have |
| `attron` / `attroff` / `attrset` | the SGR attribute primitives | have (M) |
| `init_pair` / `COLOR_PAIR` | `color_pair(fg,bg)` convenience | consider |
| `curs_set` | show / hide cursor | have (M) |
| `getyx` | `x_term_get_cursor` | this plan (#2) |
| `COLS` / `LINES` | `x_term_get_size` | this plan (#2) |
| `clear` / `erase` / `clrtoeol` / `clrtobot` | the erase primitives | have (M) |
| `getch` / `keypad` | `i16_term_get_key` | **done (#1)** |
| `refresh` + `WINDOW` double-buffer | L3 buffered layer | **far future (W)** |
| `box` / borders, `panel`s | higher-level draw helpers | later |

**The one big idea to borrow later:** ncurses' *off-screen buffer + diffed refresh*
(L3). Everything in the catalog above is stateless "fire a sequence" output; a
buffered layer that emits only the **changed** cells is the eventual leap for
flicker-free full-screen UIs. Tracked as future **W** work, not now.

---

## Global notes / footer

- **Plan status:** 🔴 ×0 · 🟡 ×0 · 🟢 ×26. Q3 size/cursor query **shipped**
  2026-06-20 (D8–D11/S6/D9/I4/T3 all 🟢); D7 + T2 shipped 2026-06-20.
- **Roadmap captured:** output-primitive library (building block #3) — see
  **§ Output primitive library**; awaiting author/agent pass on the full set.
- **Deferred (not v1 blockers):** W5 migration sweep · W6 user-macro decode · L3
  buffered window layer. Architecture leaves room for all.

---

## CODE ANCHORS / implementation phase (v1)

New module: **`App/Src/term.c`** + **`App/Inc/term.h`** (includes `ANSI.h`).
**Shipped 2026-06-20** — clean build (0 errors / 0 warnings), flashed + smoke-tested
on the bench (banner shows the new `[k]` menu item).

- [x] **P1 — `term.h` API surface.** `term_key_t` enum (ranges per **D2**),
  `EXT_MOD_*` flags, error codes `TERM_KEY_NONE/UNKNOWN/OVERFLOW` (**S4**),
  `term_keymap_t` (**I3**), prototypes `i16_term_get_key`,
  `v_term_set_lead_chars`, `v_term_register_keymap` (W6 hook).
- [x] **P2 — `v_app_polling_task()` weak no-op stub** moved into `term.c`; removed
  from `utils.c` (app keeps the strong override).
- [x] **P3 — Dual-timeout reader core** (**I2/S1/S2/S3**): non-blocking `getchar()`;
  outer wait bounded by `u32_timeout_ms` (0 ⇒ near-zero); inter-byte gather only
  after a lead byte; **early-out on complete sequence**; bare lead → lead byte.
- [x] **P4 — CSI/SS3 parser + compact keymap** (**D3/D5/I3**): `term_keymap_t`
  tuple table; editing/cursor cluster (both Home/End encodings) **+ F1–F12**
  (added post-HuIL, **D4** amended), CSI+SS3; unknown → `-2`; overflow drain →
  `-3`; modified (`;mod`) forms → UNKNOWN.
- [x] **P5 — Reference comment block** in `term.h` (**T1/D6**).
- [x] **P6a — Debug-menu decode-echo** test fn (`[k]` top menu) — **HuIL tool done**.
- [x] **P6b — Automated golden vectors** (**T2**): `scripts/term_key_bench.py` +
  `scripts/term_golden/keys.json` inject raw bursts into `[k]` and match the decode.
- [x] **P8 — Alt-meta decode** (**D7**): `ESC <ch>` → `EXT_MOD_ALT | ch`; `[k]`
  test prints `ALT+` (and any `CTRL+/SHIFT+`) prefix.
- [~] **P7 — Build/flash/smoke** done (boots, menu present). **Live key-decode
  verification is HuIL** — press arrows / nav cluster / bare ESC / a bad burst at
  Tera Term via `[k]` and confirm the decodes; or run the T2 harness on a free port.

Implemented in `App/`; **no Core/ edits**. Built/flashed/smoked via project skills.

**End of extended-key-input-plan.md**
