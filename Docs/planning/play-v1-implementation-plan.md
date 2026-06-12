# PLAY v1 — Implementation readiness plan

**Parent spec:** [Docs/PLAY_language_design.md](../PLAY_language_design.md)  
**Related:** [co5ths_key_signature_handoff.md](../co5ths_key_signature_handoff.md) · [tools/play_melody.py](../../tools/play_melody.py) · **[play-lead-char-cheat-sheet.md](play-lead-char-cheat-sheet.md)** (planning quick ref)  
**Branch:** `main` (planning on main until feature branch opened) · **Status:** PLANNING

> **Goal:** Move PLAY from "early preview" to an **implementation-ready v1 contract**
> — charset locked, semantics unambiguous, v1 scope fenced, host + on-device paths
> aligned — without boiling the ocean.

> **Working mode:** Living decision-log. Resolve items in chat by ID (`D3`, `S2`, …).
> Mark 🟢 and record outcome in detail sections + LOCKED CONTEXT. Sync
> `PLAY_language_design.md` when a batch of decisions lands. Agent proposes 🟡
> leanings; user locks 🟢 (or says *"your call on D4"*).

---

## Summary decision table

_D-items **D1–D22** listed in numeric order; detail sections below may still be out of order until T1 doc pass._

| ID | Status | Subject |
|----|--------|---------|
| D1 | 🟢 | Voice selection (`P<n>`, note memory); default = sine until more voices exist |
| D2 | 🟢 | Note-repeat — **`~`** top-level only (whole smash) |
| D3 | 🟢 | Octave pitch step (`^` up, `v` down) |
| D4 | 🔵 | Sixteenth / thirty-second durations (`X`, `Y`) — v1 or defer |
| D5 | 🟢 | Note duty — `_`/`!`/`;` shorthands + `;n` general (`;` = normal) |
| D5c | 🟢 | `;n` scale — `PLAY_DUTY_NUMERATOR` (default 8), `n`/N, clamp 0 and >N |
| D5d | 🔵 | Pizzicato shorthand — defer; add duty shorthands later |
| D6 | 🟢 | Volume `V` — range 0..100; overflow clamps to max |
| D7 | 🟢 | Case-sensitive **`A`–`G`**; flat **`b`/`-`**; sharp **`#`/`+`**; natural **`n`** (descriptor-only) |
| D8 | 🟢 | **`K"…"`** only; no opening `"` after **`K`** = WARNING; quote integrity = FATAL (**D8b**) |
| D8a | 🟢 | **Rejected** — no unquoted **`K`** or command abutment; use **`K"…"`** (**D8**) |
| D8b | 🟢 | Shared **`"…"`** delimiter + quote fault policy; **optional WS before `"`** (all string consumers) |
| D9 | 🟢 | `@ … @` comment blocks; `\@` escape; pre-parse unterminated check |
| D10 | 🟢 | Title = first `@ … @` block (pre-parse); no magic header |
| D11 | 🟢 | No separate `M` cmd — `P` is canonical voice selection |
| D12 | 🟢 | **Lexical boundaries** — WS readability; **`:`** = optional EOS (BASIC-like); **`;`** = duty only (not C-EOS) |
| D13 | 🔵 | Envelope / ADSR PLAY syntax — post-v1 (after synth + duty ship) |
| D14 | 🟢 | **`?"…"`** debug print — C escapes in string; no auto-CRLF; bare **`?`** → CRLF |
| D15 | 🔵 | **Tuplet / triplet timing** — syntax + duration math (v2+; not v1 blocker) |
| D16 | 🟢 | **String goto labels** — `<"name"` / `>"name"` (max **`PLAY_LABEL_MAX_LEN`**, default 16) |
| D17 | 🟢 | **Label define `<` / goto `>`** — replace `*` define; `<`/`>` symmetry (**S2** semantics unchanged) |
| D18 | 🟢 | **Expansion `\` — `\`"<cmd>:<args>"`** → dispatch; **`ctx:`** = zero-time note-memory load |
| D19 | 🟢 | **GOSUB / RETURN / END** — **`="name"`** / **`/`** / **`*`**; **`/`** underflow + **undefined label ref** = **hard abort** |
| D20 | 🟢 | **`R` rest** — full notation sub-parser → note memory + timed silence |
| D21 | 🟢 | **Transpose `&`** — **`&+n` / `&-n` / `&0`**; after **K**+accidentals; OOR → octave wrap + WARNING |
| D22 | 🟢 | **`N<n>`** absolute semitone — max **3** digits; suffix like notes; **K/&/acc** skip pitch + acc not stored |
| S1 | 🟢 | Polyphony — one monophonic PLAY string = one voice; sync post-v1 |
| S2 | 🟢 | Goto / label snapshots — forward keep ctx, backward restore; **undefined `>`/`=` ref → hard abort** |
| S3 | 🔵 | Sync barriers — **deferred** (post-v1 polyphony); leaning **`|"name"`** when scheduled |
| S11 | 🟡 | **v2+ headwind** — multi-instance + NVM/FS load **requires** explicit sync/staging model (observations; design open) |
| S4 | 🟢 | Repeat `[ … ]:N` — re-entry restores **`[` snapshot** (same model as S2 backward) |
| S5 | 🟢 | **Timing formula** + **`U`/`W/H/Q/I` tables**; **`PLAY_TEMPO_BPM_MAX=240`**; tick budget → **I4** |
| S6 | 🟢 | Duty constants — **`#define` only** for v1 (`PLAY_DUTY_*`, `PLAY_DUTY_NUMERATOR`) |
| S7 | 🟡 | **Error policy** — **S7c** 🟢; optional **S7f** / **S7g** still open |
| S7a | 🟢 | **Hard abort** — **`@`**, label faults (**S7d**), **`/`** underflow, bad **`T`**, stack overflow |
| S7b | 🟢 | **WARNING + continue** — **`K`/`&`/pitch/`~`/ `?` / optional `R`/ `\` faults |
| S7c | 🟢 | **Default** unlisted faults — **skip + WARNING log once + continue** |
| S7d | 🟢 | **Pre-parse** = sanity + **label resolver**; **missing label ref** = **FATAL**; not a LINTer |
| S7h | 🔵 | **Optional LINT scanner** (later phase) — strict syntax pass, invoke by flag/CLI |
| S7e | 🟢 | **Stack depth** — **`PLAY_STACK_MAX_DEPTH`** (default **10**); overflow → hard abort |
| S7f | 🟡 | **Strict mode** — debug **`playstr`** stop-on-first-fault (optional) |
| S7g | 🟡 | **I8 resolve hook** — no callback on rejected tokens (leaning) |
| S10 | 🟢 | **Session init defaults** — full note-memory struct + **`Cn4Q_`** template for first **`~`** |
| S8 | 🟢 | **Closed** — **`&0`** explicit transpose reset (**D21**); legacy **`S`** retired |
| S9 | 🟢 | Duty on one note — **last parsed modifier wins** |
| I1 | 🟢 | **PLAY v1 feature fence** (must-ship list) |
| I2 | 🟢 | Label table cap — **`PLAY_LABEL_MAX_LEN`**, **`PLAY_LABEL_TABLE_MAX`** |
| I3 | 🟢 | Stack depth cap → **S7e** (`PLAY_STACK_MAX_DEPTH`) |
| I4 | 🟢 | **Dedicated HW timer** @ **`PLAY_SCHED_TICK_US`**; shared tick counter; integer math; no 0-tick |
| I5 | 🔵 | Per-voice RAM budget line in spec |
| I6 | 🔵 | Binary compiled event format in v1 |
| I7 | 🔵 | **Module split** — deferred **v2+**; v1 = opaque **`play_handle_t`** + exposed **`play_instance_t`** (bench cast) + on-chip source (**I9**) |
| I8 | 🟢 | **Resolve hook** — callback on every completed parse (Release-safe; verbose / test / GUI / LEDs) |
| I9 | 🟢 | **Player tests submenu** — ROM smoke tune + **`playstr`** (<128) + terminal **`p`** duplicate |
| T1 | 🔴 | `PLAY_language_design.md` dedupe + implementer quick-ref (not user howto) |
| T2 | 🔴 | `play_melody.py` conformance phases vs v1 |
| T3 | 🔴 | Reference test strings + acceptance criteria |
| T4 | 🔴 | **Normative EBNF** — standalone formal grammar (post spec-lock) |
| T5 | 🔴 | **Musician howto** — user guide + tiered example repertoire (see T5) |
| Q1 | 🔵 | Star Wars / triplet feel — v1 approximate; real tuplets → **D15** |

_Status key: 🔴 unaddressed · 🟡 leaning / in discussion · 🟢 resolved · 🔵 deferred_

---

## LOCKED CONTEXT (do not re-litigate without user reopen)

These are **already chosen** in the spec or firmware; v1 implementation should assume them unless an OPEN row explicitly overrides.

- **Unified note memory** — one per-voice struct holds note attributes *and* command-driven state (tempo, key, volume, transpose, duty ratio, …).
- **Inheritance** — omitted note fields inherit from note memory; accidentals do **not** inherit implicitly (K LUT + explicit `#`/`b` override).
- **Lead vs metadata chars** — first char of token disambiguates note vs command; W/H/Q/I/X/Y, `.`, `_`, `!`, **`;` duty**, only valid **after** note letter **A–G** or after **`N` semitone digits** (**D22**). Lowercase **`n`** = natural accidental (**D7**) — not **`N`** command.
- **Order-flexible note descriptors** — after the lead letter, accidental / octave / duration / dot / duty modifier may appear in any order; exactly one duration required per note token.
- **Note duty (D5 🟢, D5c 🟢, S9 🟢)** — one **`duty_ratio`** in note memory (inherited). Shorthands **`_`**, **`!`**, bare **`;`**, **`;n`** (see D5 detail). More duty shorthands (e.g. pizzicato, D5d 🔵) are **non-blocking** — same modifier pattern. **Envelope / ADSR PLAY surface** deferred (**D13 🔵**); v1 uses existing `synth_engine` linear attack/decay only.
- **Command letters (current):** `R` rest · **`N` absolute semitone (**D22** 🟢) · `T` tempo · `O` octave · **`^` / `v` octave step (D3 🟢)** · `K` key · **`&` transpose (D21 🟢)** · `U` beat unit · **`V` volume (D6 🟢)** · **`P` voice selection (D1 🟢)** · **`?` debug print (D14 🟢)** · **`\` expansion hook (D18 🟢)** · `[ ]:` repeat · **`<` label (D17 🟢)** · **`>` goto** · **`=` GOSUB (D19 🟢)** · **`/` RETURN** · **`*` END** · **`~` note-repeat (D2 🟢)**. _(Legacy **`S`** retired; **`T`** = tempo only.)_
- **Expansion hook (D18 🟢)** — top-level **`\` + quoted string** only: **`\"cmd:args"`** (payload pattern **`cmd:args`**, colon-separated). Core parser extracts decoded payload → **`play_extension_fn_t`** dispatch table; **v1 default stub** echoes payload to debug UART (**same spirit as D14 `?"…"`**, for bench-test). Reserved **`ctx:`** cmd → **zero-time note-memory load** (see **D18** / **D20**). Unknown **`cmd`** → stub path (WARNING optional per **S7**). Not valid inside note descriptors. **`\@`** remains comment-escape only (**D9**).
- **`R` rest (D20 🟢)** — **`R`** accepts the **full notation sub-parser** (same postfix set as **`C4Q`**: octave, duration, dot, duty — order-flexible; **one duration required**). **All applicable fields update unified note memory**; **schedule timed silence** only (no pitch). **`#`/`b`/`+`/`-` on `R`:** parsed but **no rest audio effect** and **not stored** (no accidental-inheritance field). **Timed context bump** (octave + duration + duty in one token): use **`R4Q;6`**. **Zero-time context-only** (no wall-clock gap): use **`\"ctx:4Q;6"`** (**D18**) — **no dedicated SET lead** (rejected).
- **`~` note-repeat (D2 🟢, S10 🟢)** — **top-level only**. Replays **last completed note** snapshot (“whole smash”). **Before any completed note:** replay **session default note template** (**`Cn4Q_`**, **S10**) + **WARNING**. Distinct from labels / sync.
- **`V<n>` volume (D6 🟢)** — decimal **0..100** (human “percent loudness”; **0–127 MIDI-style rejected**). **`V` + one or more ASCII digits**; value **> 100** silently clamps to **100** (max volume). **`V0` = silence**. Updates sticky volume in note memory (inherits; label/repeat snapshots). Maps to synth as **`level = n / 100.0f`** (single conversion point in firmware).
- **`^` / `v` octave shorthands (D3 🟢)** — standalone executives (no digits). **`^`** increments **`current_octave`** by 1; **`v`** decrements by 1. Same field as `O<n>` and explicit note octaves. **`+` / `-` rejected** for octave step — collide with note accidentals (`+`/`#`, `-`/`b`) and with **`&+` / `&-` transpose** syntax; would require lookahead/context rules for marginal ergonomics gain.
- **`P<n>` voice selection (D1 🟢, D11 🟢)** — **canonical** command for synthesizable voice / timbre (not per-note; not polyphony routing). Updates **`u8_current_voice`** in unified note memory (inherits; label/repeat snapshots). **Default:** pure **sine** (CORDIC) — implicit **`P0`**. Range **0–255**; **`P1`, `P2`, …** map to future generators in the voice table. **No separate `M` command** — withdrawn from spec (D11). **v1 syntax:** **`P` + digits only**; **`P`-family modifier syntax may grow** as `synth_engine` gains params (ADSR per voice, detune, …) without adding parallel command letters.
- **Title metadata (D10 🟢)** — the **first `@ … @` comment block** in the string (after leading whitespace skip) is the **piece/part title**, captured at **pre-parse**. Stored in `play_session_t` (not note memory). If no comment block precedes music, title is **empty**. No mandatory magic/version header.
- **`@ … @` comment blocks (D9 🟢)** — bracketed skip regions; `\@` escape; unterminated block at EOF = load error. **First** block doubles as title (D10); later blocks are comments only.
- **K + & pitch pipeline (D8 🟢, D21 🟢)** — **Default key: C major** until valid **`K"…"`**. Per note: letter + explicit **`#`/`b`** → **key LUT** → add sticky **`&`** semitone offset (direct sum). **`K"…"` only**; **`K`** without opening **`"`** → **WARNING**, keep key; bad keyspec inside quotes → **WARNING**; quote integrity → **FATAL** (**D8b**). Out-of-range pitch after sum → pitch class **`% 12`**, clamp octave to playable min/max → **WARNING**, continue. Bad **`&`** → **WARNING**, keep offset. **`&0`** clears offset (**S8** closed).
- **`?"…"` debug print (D14 🟢)** — single-char **`?`** (BASIC **`PRINT`** shorthand). **`?"…"`** → emit **decoded** string (**C escapes**, no **`printf`** `%` formats); **no auto-CRLF** after quoted output (**`?""`** = emit nothing). **Bare `?`** alone → **CR/LF (`\r\n`)**. Quote integrity faults → **FATAL** (**D8b**); other **`?"…"`** faults → **WARNING**, continue.
- **Streaming-first parser (not a REPL / not a general language)** — music is interpreted **at runtime** by a char-at-a-time walk; **no AST**, no token list in RAM. **Single-char executives** dominate (`T120`, `P1`, …). **Quoted-string metas (D8b):** **`K"…"`** (D8 — **only** form), **`?"…"`**, **`\"…"`**, **`<"…"`** / **`>"…"`** / **`="…"`** — all allow **optional WS before opening `"`**. **Block meta:** **`@ … @`** (D9).
- **Error policy default (S7c 🟢)** — faults **not** in **S7a** (fatal) or **S7b** (explicit carve-outs): **skip** the bad token/executive, log **WARNING once** (not ERROR), **continue** playback. Silent ignore rejected.
- **Startup pre-parse (still runtime at load, not compile-time — S7d 🟢)** — **not** a full syntax scanner or LINTer. One **linear pass** for **hard failures that are unresolvable before playback can start**, plus **label table build + reference check**. Everything else is handled **during streaming interpret** (**S7c** default / **S7b** WARNINGs). Optional **strict LINT pass** deferred (**S7h** 🔵).
  1. **Comment integrity** — every `@` opener has a closing `@` before EOF (`\@` does not close); else **FATAL**, refuse to play (**D9**, **S7a**).
  2. **Title capture (D10)** — the **first** complete `@ … @` block’s inner text (trim) → sequence title; max **64 chars** (truncate + log). If none, title = empty.
  3. **Label table + resolver** (comment-aware) — record every **`<n`** / **`<"…"`** define; resolve every **`>…`** / **`=…`** reference. **Missing reference** (goto/GOSUB to undefined label) → **FATAL**, refuse to play (**S2**, **D19**, **S7d**). **Unreferenced define** → **WARNING** only (**S7b**). Required-string quote faults on label/goto/GOSUB tokens (**D8b**) → **FATAL** in this pass. Runtime undefined-ref hit → **S7a** safety net only.
  4. *(Not in v1 pre-parse)* repeat **`[` / `]`** balance, note-descriptor completeness, **`T`/`U`/`N` malformed** — caught at **runtime** (**S7c** / **S7b**) unless **S7h** LINT invoked later.
  Playback then **streams** from `pos=0`; `@` blocks skipped during interpret.
- **Case + accidentals (D7 🟢)** — note letters **`A`–`G` uppercase only**; flat **`b`** or **`-`**; sharp **`#`** / **`+`**; natural **`n`** (descriptor-only). Top-level **`N`** = absolute semitone (**D22**). **`=` not natural** (GOSUB).
- **Lexical boundaries (D12 🟢)** — Think **BASIC/C lexer**, not REPL lines. **Whitespace** = skipped readability (mostly), plus soft boundary between executives. **String consumers** (**D8b**): optional WS before **`"`**. **`:`** = optional **end-of-statement** at top level (BASIC mental model — **not required** in our metalanguage). After **`:`**, skip WS → next sig char = top-level lead. **`;`** is **not** PLAY EOS — it is **note duty** (**D5** `;` / **`;n`** inside descriptors only; C’s statement-terminator role **not adopted** at top level). **Exception:** **`]:N`** repeat tail (**S4**). **`:`** / **`;`** literal inside **`"…"`**, **`@ … @`**, note sub-FSM.
- **Polyphony (S1 🟢)** — **One PLAY string = one monophonic voice** (one note at a time per interpreter instance). **No inline chords** in a single string (e.g. no `C4Q E4Q G4Q` chord tuples in one stream). **Polyphony = multiple concurrent `play_instance`s**, each with its own string + note memory + scheduler — **conductor / sync deferred (S3 🔵)**; **load + readiness sync deferred (S11 🟡)**. v1 ships **one monophonic interpreter**; multi-session mixing is post-v1. **`P<n>` (D1)** = timbre within a voice, not a polyphony slot.
- **Goto / label context (S2 🟢)** — on each **`<n`** (or **`<"…"`**, **D16**/**D17**) parse during playback, **overwrite** stored label snapshot with current note memory. **`>n` forward** (target offset **>** current): move PC only — **keep** present context. **`>n` backward** (target offset **<** current): move PC **and restore** label snapshot at that define (**overwrites** present context). _(Wire: define **`<`**, goto **`>`** per **D17**; older text used `*` for define.)_
- **Repeat blocks (S4 🟢)** — same **backward restore** principle as S2, anchored at **`[`** instead of **`<n`**. On **`[`** parse: push stack frame + **overwrite** **`[` open snapshot** with current note memory. **First** entry into the body: continue **without** restore (forward entry). On **`]`** with iterations remaining: **restore `[` snapshot** and jump to after **`[`** (re-entry overwrites mutations from the prior pass). On **`]`** when count exhausted: pop stack, continue forward. Nested repeats: **one snapshot per stack frame**.
- **Synth path today** — CORDIC sine + linear attack/decay + fast retrigger release (`synth_engine`); monophonic output first.
- **Resolve hook (I8 🟢)** — every time the parser finishes one **complete executive** (note, rest, meta, `?`, structural token, …), invoke an optional **Release-safe callback** with source span + resolved semantics + schedule context. Default **NULL** (single branch, near-zero cost). Enables verbose console echo, golden trace tests, virtual synthboard GUI, LED animation — without duplicating the parser. Parameter struct TBD at implementation; see **I8** detail.
- **Storage direction (post-v1 loader)** — text `.play` on LittleFS / FAT; not blocking first on-device interpreter if strings live in flash/const for tests.
- **Tuplets / triplets (D15 🔵)** — **not in v1.** No syntax for “N notes in the time of M” (e.g. triplet eighths, Star Wars opening). **S5** v1 formula assumes **standard W/H/Q/I (+ dot)** durations only. v1 demos (**Q1**, **T5** Star Wars) use **approximation** (even eighths + author comment). Real tuplet support is **v2+** (or v1 only if **D15** closes early with a clean design).
- **PLAY v1 feature fence (I1 🟢)** — **Must ship:** notes **A–G** + **`N<n>`** (**D22**); accidentals **`#`/`-`/`n`**; octave digit; durations **W H Q I** + dot; duty **`_` `!` `;` `;n`** (**D5**); **`R`** rest (**D20**); executives **`T` `O` `^` `v` `K"…"` `&` `U` `V` `P` `?"…"` `~`**; expansion **`\"cmd:args"`** incl. **`ctx:`** stub (**D18**); repeat **`[ ]:N`**; labels **`<n`/`<"…"`/`>…`/`="…"`** (**D16**/**D17** 🟢); **GOSUB/RETURN/END** (**D19**); optional first **`@`** title (**D10**); startup **label pre-scan** (**S7d**); **`play_resolve_fn_t`** hook (**I8**, NULL default); **monophonic** interpreter → **`synth_engine`** sine output (**P0** default, **`P<n>`** stored); **const-string** input OK for first bench (debug **`playstr`**). **Out of v1:** **X/Y** (**D4**); tuplets (**D15**, **Q1** approx only); inline chords / multi-voice in one string; polyphony / **`|"`** sync (**S3**); **VIB/TRM/ADSR** PLAY syntax (**D13**); binary compile (**I6**); LittleFS loader / ESP upload path; mandatory magic / **`VER:`** header. **Delivery:** one **`play_session`**, dedicated HW tick (**I4**), bypass terminal **`p`** keys for first on-device audio path.
- **Label table limits (I2 🟢, D16/D17 🟢)** — **`PLAY_LABEL_MAX_LEN`** (default **16**) caps quoted name length; **`PLAY_LABEL_TABLE_MAX`** (default **10**) caps **define** count per sequence (**`<"…"`** and **`<n`** share one sparse table). Both are **`#define`** build-time invariants in **`play_config.h`**. **11th define** or name **> max len** → **FATAL** at pre-parse. **Duplicate define** (same name or same numeric id) → **last wins** + **WARNING**. **Missing ref** unchanged (**FATAL**). Numeric **`<n` / `>n` / `=n`** use the same table — no dense **`label_pos[256]`** array.
- **PLAY C constants policy (I2 🟢)** — avoid magic-number literals in firmware for invariants and build-time limits; prefer **`#define`** in **`play_config.h`** (or documented headers). Runtime-mutable defaults only where a decision explicitly allows them (case-by-case).
- **Player bench menu (I9 🟢)** — debug top menu **`m`** → submenu banner **`--- Player tests and experiments ---`**. **v1 minimum:** **`1`** = in-flash PLAY smoke tune (C major scale ascending, quarter notes — interpreter smoke-test); **`s`** = prompt + **`i_getline()`** → dispatch **≤ `PLAY_DEBUG_LINE_MAX`** chars to PLAY API (async); **`p`** = same blocking terminal note player as top menu (**`v_note_player_run`**, **not** PLAY). Top-level **`p`** **kept** (duplicate entry). Menu handlers non-blocking; PLAY runs from jobs + **I4** tick. **Star Wars** intro ROM tune = follow-on preset, not v1 smoke gate.
- **PLAY v1 input scope (I7 🔵 / v1 implement)** — one start path: **`const char *`** to a **NUL-terminated** sequence in **core-accessible on-chip memory** (flash `.rodata` **`const`** literals or RAM buffers — same to the core). **No** separate ROM vs RAM API. **No** LittleFS / SD **`playfile`**, **no** NVM loader in v1. **Immutable source:** **`play_instance_t`** holds **`const char *psz_src`**; parser **never writes** the PLAY string (read-only walk via offset). Caller keeps storage valid for the session lifetime. **Handles:** **`play_handle_t`** = opaque token (**`void *`** in v1 sketch) returned by **`b_play_start`**, passed to **`v_play_stop`** / future APIs — product code treats it as opaque. **`play_instance_t`** struct is **declared in `play.h`** so bench/debug can **`PLAY_HANDLE_AS_INSTANCE(px_handle)`** and **read** status fields — **not** for general mutation. v1 **`PLAY_INSTANCE_MAX = 1`**. Module file split deferred **v2+**.

---

## Design decisions (D)

### D1 — Voice selection command

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; amended same session)

**Question:** How is **synthesizable voice** (timbre/generator) selected mid-sequence (persistent, not per-note)?

**User direction (initial):** Command at any point in PLAY string; stored in **unified note memory**; v1 stub.

**User direction (amendment):** **Default voice is pure sine** (current CORDIC path) until a `P` command provides another selection — and in practice until additional synthesizable voices exist in firmware.

**Locked syntax:**

```
P<n>    ; n = 0..255
        ; default (no P yet): sine  (implicit P0)
        ; P0 = sine (always)
        ; P1, P2, … = future voices when synth_engine grows them
```

- **Lead char:** **`P`**. Requires ≥1 digit (`P0`, `P1`, …).
- **Semantics:** Sets `note_memory.u8_voice_index = n`. Inherits until next `P`. Snapshots at labels/repeats include this field.
- **Audio (v1):** **Always sine** regardless of stored index — only one generator today. Parser/scheduler **must** call existing sine `set_tone` path; `P` updates state + debug display only until voice table has >1 entry.
- **Audio (future):** `P` index selects row in **voice table** (generator + default params). **`P0` remains sine.**
- **vs D11 (🟢):** **`P` alone owns voice/timbre.** No **`M`**. Extend voice table + optional **`P` modifiers** when synth grows — do not reopen a second instrument opcode.

**Resolution:** **`P<n>` selects synthesizable voice; default / `P0` = sine; v1 stores index + always renders sine until more voices ship.**

---

### D11 — Instrument / waveform command (`M` — withdrawn)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** Separate lead char **`M`** for sine / tri / saw?

**User direction:** **`M` goes away.** **`P`** is the **canonical** voice-selection command. As synth capabilities grow, **`P`-command modifier syntax will likely expand** (still under the **`P` lead char** — not a parallel opcode).

**Locked outcome:**

- **Do not implement or document `M<n>`** in PLAY v1 or future unless user explicitly reopens.
- **Voice index:** **`P<n>`** (D1) — `P0` = sine, `P1+` = future timbres in firmware voice table.
- **Future params:** Prefer **`P` family extensions** (e.g. index + optional suffix tokens when needed) over allocating a new command letter. Exact modifier grammar is **TBD per synth feature** — not v1-blocking.
- **Character `M`:** **Unallocated** — not reserved for instrument; available only if a future unrelated need arises (unlikely; prefer not to reuse).

**Resolution:** **`P` is canonical voice selection; `M` withdrawn; extend `P` syntax + voice table as synth grows.**

---

### D2 — Note-repeat token

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** Single punctuation = **repeat previous note** (not command repeat).

**User decision:** **`~`** (confirmed; message said “D8” but context is D2 note-repeat).

**Locked syntax:**

```
~    ; top-level only — replay last *completed* note snapshot
     ; before any completed note: replay S10 default Cn4Q_ + WARNING
     ; forbidden inside note descriptors (parse error)
```

**Rejected:** **`*`** (labels) · **`.`** (dotted duration) · **`|`** (S3 sync candidate) — see candidate table in prior revision.

**Cross-ref S3:** **`~`** note-repeat · **`<` / `>`** labels (S2, **D16**/**D17**) · **`|"…"`** sync (**S3** 🔵 deferred).

**Resolution:** **`~` = note-repeat; top-level only.**

---

### D3 — Octave pitch step (`^` / `v`)

**Status:** 🟢 · **Needs user:** no

**Question:** Lock octave-up/down shorthands? (`+`/`-` considered but rejected.)

**Locked syntax:**

```
^    ; current_octave += 1  (standalone executive, no digits)
v    ; current_octave -= 1
```

- **Pair:** **`^`** up · **`v`** down — supplements (does not replace) **`O<n>`** and explicit octave digits on notes.
- **Semantics:** Updates **`current_octave`** in unified note memory; inherits until next `O`, `^`, `v`, or note with explicit octave digit. Included in label/repeat snapshots (S2 🟢, S4).
- **Rejected alternatives:** **`+` / `-`** — intuitive for “up/down” but **`+`/`#` and `-`/`b`** are note accidentals inside descriptors; **`-`** is also the flat accidental at command-adjacent positions; **`&+` / `&-`** owns signed transpose (**D21**). Using **`+`/`-`** for octave would force fragile context/lookahead in a streaming single-char parser.
- **Prior art:** Lowercase **`v`** was used successfully in the earlier live **`p`** player; keep it.

**Resolution:** **`^` = octave up, `v` = octave down — locked for v1.**

---

### D4 — `X` / `Y` durations (sixteenth / thirty-second)

**Status:** 🔵 · **Needs user:** optional

**Question:** Include `X` and `Y` in v1 or defer?

**Leaning:** **Defer from v1 interpreter** (🔵). Spec keeps letters reserved; v1 accepts W/H/Q/I only (+ dot). Reduces parser + test surface for first ship. Add X/Y in v1.1 when monophonic playback is boring.

**Resolution:** _(treat as deferred unless user wants v1)_

---

### D5 — Note duty cycle (general + shorthands)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; amended same session)

**Concept:** **`_` / `!` / `;` / `;n` are one duty system** — note-on fraction vs silence within the nominal duration (after dot). Not a separate “articulation layer.” All targets **`#define`-able** and **runtime-settable**.

**Locked syntax (inside note descriptor only):**

| Form | Meaning |
|------|---------|
| **`_`** | **100%** duty → `PLAY_DUTY_LEGATO` (default **1.0**) |
| **`!`** | **~20–25%** staccato → `PLAY_DUTY_STACCATO` (tune by ear) |
| **`;`** (no digits) | **“Normal”** duty → `PLAY_DUTY_NORMAL` (~**80%**, tunable) |
| **`;n`** | **General case** — read digit **`n`**, then **`duty = clamp(n) / PLAY_DUTY_NUMERATOR`** (D5c). Default **`PLAY_DUTY_NUMERATOR = 8`**. Example: **`AN5Q;6`** → **75%** on (6/8). Order-flexible. |

**Encoding:** **`;n`** uses **n/N** scale per **D5c 🟢**. Named shorthands use separate tunable constants. Raw **`;nn` percent** deferred (D5b 🔵).

**Precedence (S9 🟢):** Last duty modifier wins. Examples: `AN5Q!;6` → **75%** (`;6` last); `AN5Q;6!` → staccato constant.

**Rejected (legacy spec drafts):** leading **`D`** · trailing **`n/8` / `nn%` suffixes** — replaced by **`;` / `;n`** (semantic eighths via D5c).

**Resolution:** **`;` family + `_` / `!` locked; n/N scale locked (D5c).**

---

### D5b — Raw-percent `;nn` encoding (optional, deferred)

**Status:** 🔵 · **Needs user:** only if 10× scale is too limiting

**Question:** Allow **`;60`** meaning 60% directly (two-digit percent)?

**Leaning:** **Defer** — **`;6` vs `;60`** ambiguous without a fixed max digit count. v1 uses **`;n` with n/N** only (D5c).

**Resolution:** **Deferred (v1.1+).**

---

### D5c — `;n` numeric scale (`PLAY_DUTY_NUMERATOR`)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Locked formula:**

```c
/* play_config.h — default; runtime/debug overridable */
#define PLAY_DUTY_NUMERATOR  (8U)

/* n from ;n digit run (0 if bare ; — separate normal shortcut) */
duty = (float)clamp_n / (float)PLAY_DUTY_NUMERATOR;

/* clamp_n: if n == 0 OR n > PLAY_DUTY_NUMERATOR → n = PLAY_DUTY_NUMERATOR */
```

- **`;n` means n parts of an N-part window** — default **N=8** → musical eighths (**`;4` = 50%**, **`;6` = 75%**, **`;8` = 100%**).
- **`PLAY_DUTY_NUMERATOR`** is **`#define`-able** and **runtime-settable** (same policy as other duty constants).
- **Out-of-range:** **`n > N` → treat as `n = N`** (100% duty).
- **`n == 0`:** **treat as `n = N`** (100%) — avoids div/0; **`;0` is not “silence”** (use **`R`** rest for that).
- **Base-10 (n×10%) rejected** for v1 — see prior D5c discussion.

**Resolution:** **`;n` duty = clamp(n)/PLAY_DUTY_NUMERATOR; default N=8; 0 and >N clamp to N.**

---

### D5d — Pizzicato shorthand (vs staccato `!`)

**Status:** 🔵 · **Needs user:** no (deferred 2026-06-11)

**User direction:** **Not needed for v1.** Easy to add more duty/articulation **shorthands later** — same extensible modifier pattern; **not an implementation blocker.**

**Context:** Pizzicato (Sugar-Plum feel) ≠ staccato (`!`) — likely needs **envelope shape**, not duty alone (**D13 🔵**).

**Resolution:** **Deferred post-v1.** Revisit when envelope PLAY syntax exists and/or a concrete transcription target appears.

---

### D13 — Envelope / ADSR in PLAY

**Status:** 🔵 · **Needs user:** no (deferred — post-v1 design pass)

**Question:** How does PLAY expose per-note or per-voice envelope (attack/decay/sustain/release, pizzicato pluck shape, etc.) beyond **`duty_ratio`** gate timing?

**User direction (2026-06-11):** **Not discussed yet** — separate conversation **after** v1 interpreter + current `synth_engine` path ship. Today: fixed linear attack/decay in firmware; PLAY only schedules note-on/off from duty + duration.

**Leaning:** **`P`-family growth** (D11) and/or future note-descriptor modifiers once envelope model is defined. **`VIB`/`TRM`/`ADSR` command blocks** in old spec prose stay **out of v1 fence**. Do not block v1 on this.

**Resolution:** **Deferred — design pass after monophonic PLAY + duty interpreter land.**

---

### D6 — Volume range

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** `V` command numeric range — decimal 0..100 vs 0..127 (7-bit / “computer-natural”)?

**User direction:** **Decimal 0..100** — more human-intuitive than base-2-ish / MIDI-scaled ranges. **Overflow silently = max volume** (no parse error).

**Locked syntax:**

```
V<n>    ; n = 0..100 (logical range)
        ; parse one or more ASCII digits after V
        ; if parsed value > 100 → clamp to 100 (silent)
        ; V0 = silence, V100 = full scale
```

- **Semantics:** Sets **`current_volume`** (0..100) in unified note memory; inherits until next **`V`**. Included in label/repeat snapshots.
- **Audio:** Single firmware mapping **`level = clamp_u8(n, 0, 100) / 100.0f`** → `synth_engine` / mixer.
- **Rejected:** **0..127** — no MIDI legacy in PLAY v1; avoids mental math at authoring time.

**Resolution:** **`V` uses 0..100; overflow clamps to 100 silently.**

---

### D7 — Case + `b` / `B` ambiguity

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Problem:** Compact, order-flexible descriptors + case-insensitivity make **`ABbbC`** unparseable without mandatory separators.

**User decision:** **Case-sensitive notes** (agent recommendation accepted). Rationale includes a **broader namespace** — lowercase letters largely available for future metas/commands without colliding with pitch names.

**Locked rules (`.play` + on-device interpreter):**

| Context | Rule |
|---------|------|
| **Note letter** | **`A`–`G` uppercase only** starts a note (top-level, or after prior descriptor is **complete**). |
| **Flat** | Lowercase **`b`** or **`-`** (single-char accidentals, anywhere in descriptor cluster). **`Bb4Q` = B♭.** |
| **Sharp** | **`#`** or **`+`** |
| **Natural** | Lowercase **`n`** only — explicit natural; pitch from letter + **K** LUT (cancels earlier **`#`/`b`/`+`/`-`** in same cluster). **Descriptor-only** — distinct from top-level **`N`** semitone cmd (**D22**). |
| **Incomplete descriptor** | Uppercase **`A`–`G`** before current note has its duration → **parse error** (not next note). |
| **Whitespace** | Optional (**D12**); readability between executives — not required when abut/`:` disambiguates. |

**Out of scope (looser):** live terminal **`p` player** may remain case-insensitive for hand entry.

**Rejected:** Case-insensitive note letters · flat-only-`-` (dropping **`b`**) · mandatory separators · **`=` as natural** (GOSUB conflict).

**Resolution:** **Case-sensitive `A`–`G`; flat = `b` or `-`; natural = `n` only (`Cn4Q`); `N` free for expansion.**

---

### D8 — `K` key signature command

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; **quoted-only** amendment same session)

**Goal:** One consistent key syntax — same **`"…"`** delimiter as labels, print, and expansion metas (**D8b**). No unquoted **`K<root>…`** path; **D8a** rejected.

**User direction:**

1. Valid form: **`K"`** + keyspec + **`"`** — opening **`"`** may follow **`K`** immediately or after optional whitespace (**D8b** general rule).
2. **`K` without opening `"`** (e.g. **`KDb`**, **`KC`**) → **WARNING**, ignore command, **keep current key** (**D8b** optional-string class).
3. **`K"` opened but payload invalid** (bad keyspec inside closed quotes) → **WARNING**, keep key.
4. **Quote integrity faults** on any executive — unterminated close, missing required open, misaligned/nested quotes (e.g. **`KDm"`**) → **FATAL** (**D8b**).
5. **Default key signature:** **C major** at sequence start until first valid **`K"…"`**.

**Locked syntax:**

```
K"<keyspec>"
    keyspec = <root>[<acc>][m]   ; inside quotes
    root    = A|B|C|D|E|F|G      (uppercase, D7)
    acc     = # | + | - | b      (optional)
    m       = minor marker       (optional literal m)
```

Closing **`"`** ends the command (music may abut: **`K"Db"C4Q`**).

**Examples:**

```
K"C"C4Q          ; C major (explicit; same as default)
K"Db"D4Q         ; D-flat major
K"F#m"A4Q        ; F-sharp minor
K"Db"C4Q         ; abut note after closing "
K "C"C4Q         ; space before " OK (D8b)
K"C" T120        ; space optional after closing "
KDb D4Q          ; no opening " after K → WARNING, key unchanged
KDbC4Q           ; same
KC C4Q           ; same
K"Bad"C4Q       ; bad keyspec inside valid quotes → WARNING, key unchanged
KDm"             ; quote misalignment → FATAL (D8b)
K"Dm             ; missing closing " → FATAL (D8b)
```

**Parse flow:** Apply **D8b** WS-before-**`"`** rule after **`K`**. If next sig char is not **`"`** → **WARNING**, do not update LUT, resume at next top-level lead-in. Else parse quoted payload; on **FATAL** quote fault refuse/stop; validate keyspec; on success update LUT.

**Cross-ref S7:** **`K` without `"`** and bad keyspec = **WARNING + retain key**; quote integrity = **FATAL** (**D8b**).

**Rejected:** Unquoted **`K<root>[acc][m]<WS>`** · **D8a** abutted compaction · hard-error on bad key.

**Resolution:** **`K"…"` only; missing opening `"` = WARNING + keep key; bad keyspec = WARNING; quote integrity faults = FATAL; default C major.**

---

### D8a — `K` without whitespace (optional compaction)

**Status:** 🟢 · **Rejected** (2026-06-11 — user: consistent syntax, **`K"…"`** only)

**Was:** Allow unquoted **`K<root>[acc][m]`** to abut the next command when unambiguous (**`KDmT100`**) or require mandatory whitespace before notes.

**Why closed:** **D8** now mandates **`K"<keyspec>"`** only — same **`"…"`** pattern as **`?"…"`**, labels, and **`\`"…"`**. Removes dual parse paths and the terminator-alphabet problem entirely.

**Resolution:** **Rejected.** Use **`K"F#m"T120`**, **`K"Db"C4Q`**, etc. No revival planned.

---

### D8b — Quoted string arguments + quote fault policy

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; quote policy amended same session)

**User direction:** Enclose **string-type meta arguments** in **`"…"`**. Closing **`"`** ends the token (music may abut). **Delimiter shared; escape rules differ:** **`K"…"`** minimal (**`\"`**, **`\\`** only); **`?"…"`** and **`\`"…"`** full **C string escapes** (D14).

**General rule — optional whitespace before opening quote (locked 2026-06-11):**

Every **string-consuming** executive skips **optional whitespace** (**D12**: space, CR, LF, TAB) between its **command lead** and the opening **`"`**. Compact and spaced forms are **equivalent**:

```
?"printme"       ; same as ? "printme"
K"Db"C4Q         ; same as K "Db"C4Q
>"repeat"         ; same as > "repeat"
<"repeat"        ; same as < "repeat"
\ "echo:hi"      ; same as \"echo:hi"   (D18 expansion lead)
```

Applies to **all** quoted metas: **`K"`**, **`?"`**, **`\"`**, **`<"`**, **`>"`**, **`="`**. Does **not** allow whitespace **inside** the **`"…"`** payload (except literal spaces in print strings). After the closing **`"`**, the next executive may follow via **abut**, **whitespace**, or **`:`** (**D12**).

**Bare `?` disambiguation:** After **`?`**, apply WS-before-quote. If the next sig char is **`"`** → quoted print. Otherwise → **bare `?`** (CR/LF), e.g. **`?C4Q`** or **`? C4Q`** (WS skipped, **`C`** is not **`"`**).

**Locked syntax:**

```
K "<keyspec>"    ; WS before " OK
K"Db"C4Q         ; key then note — no space after closing "
? "hello"C4Q     ; same as ?"hello"C4Q
? "printme"      ; same as ?"printme"
\"echo:bench"C4Q ; expansion then note (D18)
< "repeat" … > "repeat"   ; required-string class (labels)
K"C" T120        ; WS after closing " starts next executive
K"C":T120        ; : hard end-of-command (D12) — same semantics
```

**Quote fault policy (locked 2026-06-11):**

| Class | Executives | Fault | Policy |
|-------|------------|-------|--------|
| **Optional string** | **`K"`**, **`?"`**, **`\"`** *(when lead expects quote)* | Next sig char after **WS-before-`"`** skip is **not `"`** | **WARNING**, ignore executive, retain sticky state |
| **Optional string** | Same | Valid **`"…"`** but **bad payload** (e.g. **`K"Bad"`**) | **WARNING**, ignore update, retain state |
| **Required string** | **`<"`**, **`>"`**, **`="`** (labels, goto, GOSUB) | No opening **`"`** after **WS-before-`"`** skip | **FATAL** |
| **Required string** | Same | Missing closing **`"`** before token end / EOF | **FATAL** |
| **Any quoted executive** | All **`"…"`** metas above | **Quote misalignment / nesting** — stray **`"`**, close without open, **`KDm"`**-style mismatch | **FATAL** |
| **Comment blocks** | **`@ … @`** | Unterminated before EOF | **FATAL** (**D9**) — same integrity class as broken quotes |

**Rationale:** Missing **opening** quote on **`K`** is a soft author mistake (ignore + warn). Missing **closing** quote or **misaligned** quotes break the streaming scanner — same severity as an unterminated **`@`** comment. **Goto/GOSUB/label define** **require** a string operand; absence is **FATAL**, not ignorable.

**Implementation notes:**

- **WS-before-`"`:** See general rule above — one code path for all string consumers.
- **Escape (v1):** **`K"…"`** — **`\"`**, **`\\`** only inside payload.
- **Pre-parse:** Label resolver pass applies **required-string** + **quote integrity** rules to **`<"…"`**, **`>"…"`**, **`="…"`** tokens; **missing label ref** → **FATAL** (**S7d**).
- **Runtime:** Optional-string executives use the same quote-integrity **FATAL** rules once **`"`** is opened.

**Resolution:** **Shared `"` delimiter; WS-before-`"` on all string consumers; optional-string missing open = WARNING; required-string + quote integrity = FATAL.**

---

### D14 — `?"…"` debug print (BASIC `PRINT` shorthand)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; amended — `?` not `print`; C escapes; bare-`?`-only CRLF)

**User direction:** Debug string emit for sequence tracing (vs silent **`@` comments**). Formally part of v1 spec. **Single-char lead `?`**. **Not `printf`** — no **`%`** conversion specifiers; runtime data insertion deferred to a later PLAY version.

**Locked syntax:**

```
?"Starting verse 2"     ; same as ? "Starting verse 2"
? "printme"             ; spaced form OK (D8b)
?"line\n"               ; \n in source → LF byte
?"tab:\there"           ; \t → tab
?"hex:\x41"             ; optional \xHH hex escape
?                       ; bare ? only → CR/LF (\r\n)
?""                     ; empty quoted → emit nothing (no CR/LF)
C4Q ?"after C4" E4Q
```

| Rule | Detail |
|------|--------|
| **Forms** | **`?"…"`** / **`? "…"`** (quoted; **D8b** WS-before-**`"`**) · **bare `?`** (after WS skip, next sig char is **not `"`**) · **`?""`** (empty quoted — **no output**). |
| **Quoted output** | Emit **expanded bytes only** — **does not** auto-append CR/LF after **`?"…"`** (including **`?""`**). Use `\n` / `\r` inside the string, or a separate bare **`?`**, for line breaks. |
| **Bare `?`** | After **`?`**, **WS-before-`"`** skip. Next sig char **not `"`** → emit **CR/LF (`\r\n`)** (e.g. **`?`**, **`? C4Q`**, **`?C4Q`**) — not a **WARNING**. |
| **C escapes (v1)** | Inside **`?"…"`** payload, after `\`: `\` `"` `'` `?` `n` `r` `t` `0` `a` `b` `f` `v` — standard C character escapes. **Optional v1:** `\xHH` (hex, 1–2 digits) and `\ooo` (octal, 1–3 digits). Unknown `\X` → **WARNING**, emit `\` + `X` literally (fallback). |
| **Not in v1** | **`printf`-style `%d` / `%s` / field width** — explicitly out of scope; add in a later version if needed. |
| **Source cap** | Max **64 chars** inside quotes (source length before escape expansion) — truncate source + **WARNING** if exceeded (D10 cap precedent). |
| **When** | **Runtime** — interpreter reaches token during playback; emits then continues (no audio side effect). |
| **Sink** | Debug UART via project logging. Host **`play_melody.py`** should apply the same escape expansion when simulating. |
| **vs `@` comments** | Comments are **skipped** (never executed). **`?`** is **executed** at parse time in the stream. |
| **Errors** | **Quote integrity** (unterminated **`?"…`**, misaligned quotes) → **FATAL** (**D8b**). Garbage after closing **`"`**, unknown `\X`, source truncate → **WARNING**, continue. |
| **Charset** | **`?`** is a **top-level command lead only** — never valid inside note descriptors. |

**Rejected:** Multi-char **`print"…"`** · auto-CRLF after every **`?"…"`** · **`?""`** → CRLF (superseded) · **`printf`** formatting in v1.

**Resolution:** **`?"…"`** with C escapes, no auto-CRLF; **`?""`** silent; bare **`?`** → `\r\n`; **`%` formats deferred.**

---

### D15 — Tuplet / triplet timing (syntax + duration math)

**Status:** 🔵 · **Needs user:** yes (when triplet work is scheduled — **not** a v1 ship blocker)

**Problem:** Many scores divide a beat into **3, 5, 6, …** equal parts (triplets, quintuplets, …). v1 durations (**W/H/Q/I** + dot) and the **S5** timing formula assume **power-of-two** subdivisions only. The **Star Wars** main-theme opening (**Q1**) is the canonical pain point — written as triplet-feel eighths, not straight `IQ IQ IQ`.

**v1 stance (locked for planning):**

| In v1 | Out of v1 (→ D15) |
|-------|-------------------|
| W/H/Q/I + dot + **S5** formula | Triplet / general **N-in-the-time-of-M** tuplets |
| **Approximate** transcriptions + `@` / `?"…"` author notes | Exact tuplet timing math |
| **T5** Star Wars example with documented compromise | Blocking v1 ship on tuplet syntax |

**Not a v1 implementation blocker.** Firmware, **T4** grammar, and **T5** howto proceed without **D15**. If a **simple** design emerges during v1 bring-up, **D15** may land early — treat as scope add, not a prerequisite.

**Open design space (candidates only — do not implement until chosen):**

| Approach | Sketch | Pros / cons |
|----------|--------|-------------|
| **A — Bracket group** | `[3:2] IQ IQ IQ` or `(3)IQ IQ IQ` — modifier scopes N notes | Musically familiar; needs bracket/scope rules + interaction with **S4** repeats |
| **B — Tuplet command** | `N3` / `T3` executive opens triplet mode until cancelled | Single-char executive friendly; mode state + snapshots (**S2/S4**) |
| **C — Per-note ratio** | suffix `/3` on duration (`IQ/3`) | Local; verbose on long runs |
| **D — Rational duration type** | extend duration map beyond powers of 2 | Heavy parser + **S5** extension |

**Semantics to resolve with syntax (when D15 opens):**

- How tuplets interact with **`U`** beat unit and **`T`** tempo (**S5** extension).
- Whether tuplet ratio is **sticky** (inherits) or **per-group** only.
- Snapshot behavior at labels/repeats (**S2/S4**) — likely same marker-restore model.
- Dotting inside a tuplet (defer or define explicitly).

**Cross-refs:** **Q1** (Star Wars v1 approximation) · **D4** (X/Y finer subdivisions — orthogonal but often scheduled together) · **T5** Star Wars chapter · **T3** optional `star_wars_approx.play` golden string.

**Resolution:** **Deferred post-v1 (v2+ default). Track in D15; v1 uses approximation. Re-open when scheduling tuplet syntax or if early design wins during v1.**

---

### D16 — String goto labels (`<"…"` / `>"…"`)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; caps locked **I2** same session)

**User direction:** Label **define** and **goto** use the **same quoted name** — not different strings. **`>`** jumps to the offset recorded for **`<"`** with that name. Closing **`"`** ends the label token (D8b / **`K"Db"C4Q`** pattern) — music may **abut immediately** after the quote.

**Canonical loop (user example — infinite ascending scale):**

```
<"repeat"C4QDEFGABC5R>"repeat"
```

| Token | Meaning |
|-------|---------|
| **`<"repeat"`** | Define label **`repeat`** at this offset; capture **S2** snapshot on each pass |
| **`C4QDEFGABC5R`** | **C4Q** · **D–B** inherit quarter + oct 4 · **C5** top C (oct 5, **Q** inherited) · **R** quarter rest |
| **`>"repeat"`** | Goto label **`repeat`** — backward jump → **S2** restore snapshot, replay forever |

**More readable equivalent (optional whitespace):**

```
<"repeat" C4QDEFGABC5R >"repeat"
```

| Rule | Detail |
|------|--------|
| **Define** | **`<"`** + name + **`"`** — max **`PLAY_LABEL_MAX_LEN`** (default **16**; **I2**) |
| **Goto** | **`>"`** + **same name** + **`"`** — lookup in label table; **missing target → FATAL** at pre-parse (**S7d**) |
| **Not `><n>`** | Goto is **one** **`>`** lead — legacy design-doc typo **`><n>`** is wrong |
| **Numeric alt** | **`<1` … `>1`**, **`=1`** — same table; undefined id → **hard abort** |

**Label graph faults (locked 2026-06-11; pre-parse required):**

| Fault | Policy |
|-------|--------|
| **Missing reference** — **`>"…"`**, **`>n`**, **`="…"`**, **`=n`** with no matching **`<…`** define | **FATAL** at pre-parse (**S7d**) |
| **Unreferenced define** — **`<"…"`** / **`<n`** never targeted by **`>`** or **`=`** | **WARNING** at pre-parse end (**S7b**) — playback still allowed |
| **Required-string / quote integrity** on label/goto/GOSUB tokens | **FATAL** (**D8b**) |

Runtime hit on undefined ref (should not occur if pre-parse ran) → **hard abort** (**S7a**).

**Corner cases (unreferenced = WARNING is safe):** orphan subroutine labels, typo’d define name (companion **missing ref** on the **`>`** side still **FATAL**), dead code after **`*` END**, numeric **`<1`** never jumped to — all **WARN-only** dead weight, not run-start blockers. **S7h** LINT may elevate unreferenced to a stricter author-facing report later.

**Agent leaning:** **String labels in v1** — this is the natural authoring idiom; numeric optional.

**Resolution:** **Locked 2026-06-11.** **`<"name"` / `>"name"`** in v1; same quoted name on both sides; **S2** snapshot rules unchanged. Name length capped by **`PLAY_LABEL_MAX_LEN`** (default **16**); table size **`PLAY_LABEL_TABLE_MAX`** (default **10**) — see **I2**. Name **> max** → **FATAL** at pre-parse. Missing ref → **FATAL**; unreferenced define → **WARNING**.

---

### D17 — Label define `<` / goto `>` (replace `*`)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; caps **I2** same session)

**Wire shape (locked intent):**

```
<"name" …music… >"name"     ; same name both sides; > is single-char goto lead
<1 … >1                     ; numeric alternate
```

**Not:** different define/goto names in the normal case · **`><n>`** double-bracket goto · **`*`** as label define (legacy — now **END**, **D19**)

| Aspect | Detail |
|--------|--------|
| **S2 semantics** | Unchanged — only define lead moves `*` → `<` |
| **`*` repurposed** | **END** hard STOP (**D19** 🟢) — no longer label define |
| **Sync (S3 🔵)** | Leaning **`|"name"`** when polyphony is scheduled — deferred |

**Agent leaning:** **Adopt for v1** — see **D16** canonical loop example.

**Resolution:** **Locked 2026-06-11.** Define **`<`**, goto **`>`** (single char); **`*`** = **END** only (**D19**). Numeric **`<n` / `>n` / `=n`** alternate shares **I2** sparse table. **S2** forward/backward snapshot semantics unchanged.

---

### D18 — Expansion hook (`\`"<cmd>:<args>"` → sub-parser)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Purpose:** Generic **top-level** extension surface for obscure / rarely used features without allocating a new single-char executive per opcode. Core parser stays thin; handlers live in a **dispatch table**.

**Locked wire syntax:**

```
\"tuplet:3:2"
\"echo:hello"C4Q          ; closing " ends token — music may abut (D8b pattern)
```

| Rule | Detail |
|------|--------|
| **Lead** | **`\`** — **top-level lead-in only** (after whitespace / lead-in reset). **Not** inside note descriptors. Distinct from **`\@`** (comment escape, **D9** only). |
| **Payload** | **`"` … `"`** — **C string escapes** (same rules as **D14 `?"…"`**): `\n`, `\"`, `\\`, etc. |
| **Payload shape** | **`cmd:args`** — first colon splits **command name** (sub-parser route) from **argument tail** (opaque to core parser). Examples: **`tuplet:3:2`**, **`echo:hello`**, **`ctx:4Q;6`**. |
| **`ctx:` handler** | **Zero-time note-memory load** — **`args`** parsed with the **same rules as a note/rest descriptor suffix** (octave, duration, dot, duty; **no pitch letter**). Updates note memory; **does not schedule** silence or tone. Alternative to **`R`** when author wants context without a rest gap (**D20**). |
| **Dispatch** | Core calls **`play_extension_fn_t(cmd, args, ctx)`** (names TBD at v1 implement). Table of handlers; **NULL / unknown `cmd` → default stub**. |
| **v1 stub** | **Default handler echoes decoded payload** to debug UART — **bench-test parity with `?"…"`**. Real **`ctx:`** handler (apply note memory, no schedule) ships when extension table is wired — can land same release as **`R`** parser reuse. |
| **I8** | Emit **`PLAY_RESOLVE_EXTENSION`** (or **DEBUG_PRINT** kind with tag) on successful dispatch. |

**Why `\` (not `X`):** **`X`** reserved for sixteenth duration inside note descriptors (**D4** 🔵). **`\`** is unused at top-level today (note-repeat **`~`** won over `\`/`$`/`&` candidates).

**Rejected:** **`X"<data>"`** at top level — collides with **D4** ergonomics even if FSM-splittable. **Dedicated top-level SET / pseudo-note** for instant context — use **`\"ctx:…"`** instead (**D20**).

**Resolution:** **`\` + quoted `cmd:args` string; v1 ships parser + default echo stub + hookable dispatch table; `ctx:` reserved for zero-time note-memory load.**

---

### D20 — `R` rest — full descriptor, note memory + silence

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** What does **`R`** accept, and how does it interact with unified note memory?

**Locked behavior:**

| Aspect | Rule |
|--------|------|
| **Parse** | **`R`** uses the **same notation sub-parser** as note descriptors (reuse collector — no duplicate grammar). |
| **Postfix set** | Octave digit, **W/H/Q/I** (+ dot), duty **`_` `!` `;` `;n`** — order-flexible; **exactly one duration required** (may inherit duration only if grammar already allows omission on `R` — **leaning: required**, same as notes). |
| **Note memory** | **Write all applicable fields** to unified note memory (duration, octave, duty, dot, …) — same inheritance path as a played note. |
| **Audio** | **Silence only** for the resolved rest length — no pitch, no tone. |
| **Accidentals on `R` / `N`** | **Ignore for output and storage** — no accidental field in note memory; optional **S7** WARNING if present (**D22**). |

**Author patterns:**

```
R4Q           ; quarter rest + set default oct 4 / quarter / inherited duty
R4Q;6         ; rest + duty template 6/8 for following notes
\"ctx:4Q;6"   ; same memory effect, zero timeline cost (D18 ctx: handler)
T120 C4Q      ; plays — inherits oct 4, Q, duty from either path above
```

**Instant context without dedicated SET lead (user 2026-06-11):**

| Need | Mechanism |
|------|-----------|
| **Context + musical rest gap** | **`R`** + descriptor suffix |
| **Context only, no time advance** | **`\"ctx:<descriptor-suffix>"`** via **D18** (e.g. **`\"ctx:4Q;6"`**) |
| Single-field executives | Existing **`T` `O` `^` `v` `K` `V` `P` `&` `U`** — still preferred when one field suffices |

**Rejected:** Top-level **SET pseudo-note** or **duration-less WHQI meta** — avoids overloading **`R`** and keeps “one duration per descriptor” invariant.

**Implementation:** Single **`v_parse_note_descriptor_suffix()`** (or equivalent) called from **`R`** and from **`ctx:`** handler; only **`R`** calls scheduler.

**Resolution:** **`R` = full suffix parse → note memory + timed silence; zero-time context = `\"ctx:…"` extension, not a new lead char.**

---

### D21 — Transpose (`&` semitone shift)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Was:** Legacy design used **`S+nn` / `S-nn` / `S0`** (“Shift”) and left pitch-clamp policy open (**S8** 🟡). **`T`** is **tempo** — not transposition.

**User direction:** After resolving pitch from **note letter**, **explicit accidentals**, and **`K"…"` key LUT**, apply a **sticky global semitone offset** as a **direct addition**. Syntax like **`T+7` / `T-4`** in spirit, but **`T` is taken** — adopt **`&`** as lead char (**`t`** rejected: lowercase breaks command convention; **`T`** = tempo).

**Locked syntax:**

```
&+n     ; n = one or more digits — semitones up   (e.g. &+7)
&-n     ; semitones down                         (e.g. &-4)
&0      ; explicit reset to 0 semitones (closes S8)
```

**Semantics:**

| Step | Rule |
|------|------|
| **Order** | Per played note: **letter** → **explicit `#`/`b`/`+`/`-`** (if any) → **`K` LUT accidental** → **add `i8_transpose_semitones`** from last valid **`&`** |
| **Sticky** | Stored in unified note memory; inherits until next **`&`**; included in label/repeat/GOSUB snapshots |
| **Default** | **0** at sequence start |
| **Invalid `&`** | Malformed token → **WARNING**, **keep current offset** (same class as bad **`K`**) |

**Out-of-range pitch (locked):**

After the full semitone sum, if the result lies **outside playable boundaries** (product constants — e.g. min/max MIDI note for `synth_engine`):

1. Preserve **pitch class**: `pc = semitone mod 12` (positive modulo).
2. **Clamp octave**: map `pc` to the **lowest** playable octave if below range, **highest** if above.
3. **WARNING** once per offending note (or coalesced log); **continue playback** — not hard abort.

**Examples:**

```
K"C"C4Q        ; C4
&+7            ; global +7 semitones
C4Q            ; sounds ~G4 (letter still C in source — offset applied at pitch calc)
&0             ; clear transpose
&-4
```

**Rejected:** **`T±n`** (tempo collision) · legacy **`S±n`** (retired — **`S`** freed) · **`t±n`** at top level · hard abort on out-of-range pitch · silent clamp without WARNING.

**Cross-ref:** **S8** closed via **`&0`**. **D3** octave **`^`/`v`/`O`** applies to **written octave digit**, before or after transpose in implementation — **lock at implement:** octave digit sets written octave, then LUT+**`&`** shift applied to absolute pitch (document in **S5** / freq helper).

**Resolution:** **`&+n` / `&-n` / `&0`**; post-key direct semitone add; out-of-range → **pc wrap + octave clamp + WARNING**.

---

### D22 — Absolute semitone note (`N<n>`)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**User intent:** **`N`** is a **note substitute** with **absolute** sounding pitch. Same temporal/articulation surface as letter notes; **same suffix sub-parser** after the semitone digits. **Tone-shifting modifiers affect sticky memory where applicable but do not change the frequency produced by this token** — except **V** / **P** (session output path).

**Case (locked):** top-level **`N`** (command) vs descriptor **`n`** (natural accidental, **D7**) — no conflict under case-sensitive rules.

**Locked syntax (top-level only):**

```
N<sss>…   ; sss = 1..3 ASCII digits → semitone index (primary field)
          ; … = shared suffix cluster (D7/D5/S9) — duration required (specify or inherit)
```

**Parse order (D22-1 + D22-2):**

1. **`N`** + read **1..3 consecutive digits** → **`u8_semitone`** (primary; no further digits consumed into this field).
2. Invoke **`v_parse_note_descriptor_suffix()`** on the remainder (order-flexible W/H/Q/I, `.`, `_`/`!`/`;`, optional octave digit).

**Octave disambiguation (D22-1 — author contract):** there is **no** syntactic way to embed an octave digit inside the semitone digit run. If the author wants an octave digit in **memory**, place it **after at least one non-digit suffix character** (typically **after duration**), or **omit** it.

| Author wrote | Parses as | Pitch |
|--------------|-----------|-------|
| **`N604Q`** | semitone **604** (3 digits) + **`Q`** | **604** → OOR → **D21** clamp + WARNING |
| **`N60Q4`** | semitone **60** + suffix **`Q4`** | **60**; oct **4** → memory only |
| **`N60Q`** | semitone **60** + **`Q`** | **60**; oct unchanged in memory |

**Examples:**

```
N60Q           ; MIDI 60, quarter — middle C
N60Q_          ; + legato duty to memory
N48I;4         ; semitone 48, eighth, 50% duty
N60Q4          ; semitone 60; oct 4 written to memory (not added to pitch)
&+12 N60Q      ; & sticky — N60 still sounds 60
K"Db" N60Q     ; key sticky — N60 still sounds 60
\"ctx:60Q"     ; zero-time memory load (D18) — no schedule
```

**Two-path pitch model:**

| Path | Token | Pitch calculation |
|------|-------|-------------------|
| **Relative (letter)** | **`C4Q`**, **`Bb3H`**, … | letter → explicit **`#`/`b`/`n`** → **K LUT** → written octave → **`&`** → freq |
| **Absolute (`N`)** | **`N60Q`**, … | **digit run only** → semitone → freq (**D22-3** if OOR) |

**Ignored for sounding pitch on this token:**

| Mechanism | Applied to freq? |
|-----------|------------------|
| **`K"…"` LUT** | **No** |
| Sticky **`&` transpose** | **No** |
| Sticky **`O`/`^`/`v`** and suffix **octave digit** | **No** |
| Suffix **accidentals** **`#`/`b`/`+`/`-`/`n`** | **No** |

**Applied to timing / articulation:**

| Mechanism | Applied? |
|-----------|----------|
| Duration + dot + duty | **Yes** — inherit or explicit; **S9** on duty |
| **`T` / `U`** | **Yes** — **S5** |
| **`V` / `P`** | **Yes** — output |

**Note memory after completed `N`:**

| Field | Stored? |
|-------|---------|
| **`u8_absolute_semitone`** + **`b_last_note_was_absolute = true`** | **Yes** — for **`~`** absolute replay |
| Duration, dot, duty, suffix octave digit | **Yes** — sticky / last-note (oct does **not** shift **`N`** pitch) |
| Accidentals on **`N`** | **No** — same as **`R`** (**D20**); **optional WARNING** (**D22-4**, **S7b**) |

**`~` replay:** absolute snapshot if last completed note was **`N`**; else letter path (**D2**).

**Out-of-range semitone (D22-3):** same as **D21** — pc **`% 12`**, octave clamp, **WARNING**, continue (**S7b**).

**Malformed `N` (D22-2):** bare **`N`**, **`NQ`** (no digits before suffix) → **parse error** (**S7d** leaning hard abort at parse).

**S10 default (D22-5):** keep letter template **`Cn4Q_`**; implementer note: **`N60Q_`** is pitch-equivalent in C major / **`&0`**.

**Parser reuse:** **`N`** → digits (1..3) → **`v_parse_note_descriptor_suffix()`** — shared with **`A`–`G`**, **`R`**, **`ctx:`**.

**Rejected:** **`&`/`K`/oct/acc on `N` pitch** · storing acc on **`N`** in memory · **`=` natural** · lowercase **`n`** as semitone lead.

**Resolution:** **`N` + 1..3 digit semitone + shared suffix; oct after non-digit suffix or omit; OOR = D21 WARNING; acc not stored; ctx mirror OK.**

---

### D19 — Subroutine / GOSUB, RETURN, and END (hard STOP)

**Status:** 🟢 · **Needs user:** no (wire syntax + stack error policy resolved 2026-06-11)

**User idea:** BASIC **GOSUB** / **RETURN** / **END** trio — reusable labeled **sections**, **call** with return to caller, and a **hard STOP** so **main** and **subroutine libraries** can live in one PLAY string without linear fall-through.

**Locked wire syntax (top-level only):**

| Char | Executive | Syntax | Semantics |
|------|-----------|--------|-----------|
| **`=`** | **GOSUB** | **`="name"`** (quoted; max **`PLAY_LABEL_MAX_LEN`**; same rules as **D16**) | Resolve label via pre-scan table; push **return PC** + **caller note-memory snapshot**; jump to **`<"name"`** target. Callee **inherits** caller context on entry (**no** callee-local reset). Optional numeric **`=n`** *(same table as **`<n` / `>n`*, **I2**)*. |
| **`/`** | **RETURN** | bare **`/`** | Pop return PC; **restore caller snapshot** from matching **GOSUB**. **Hard abort** (refuse / stop playback) if call stack already empty — not WARNING-and-continue. |
| **`*`** | **END** | bare **`*`** | **Hard STOP** — cease parser/scheduler immediately; no snapshot restore. Equivalent to BASIC **END**. *(Repurposes **`*`** from retired label-define role — **D17** uses **`<`**.) |

**Subroutine marking:** Every subroutine begins with a normal **goto label define** (**D16**/**D17**) — e.g. **`<"TURN"`** — so the existing **pre-scan label table** (forward references OK) resolves the target offset. **GOSUB** uses the **same label name** as goto would (**`="TURN"`** → **`<"TURN"`**).

**String layout (why END matters):**

Subroutine bodies may live **before or after** the main program in the same buffer. Without **END**, linear playback would **fall through** into subroutine definitions (or run past main into dead code).

```
@ main @
T120 C4Q
… ="TURN" …              ; GOSUB
C4Q
*                         ; END — never reaches <"TURN" below

<"TURN" F4Q G4Q /        ; library at end of file
```

Or subroutine library **first**:

```
<"TURN" F4Q G4Q /
*                         ; barrier — main starts below

T120 ="TURN" C4Q … *
```

**Context model (user-locked for D19 planning):**

| Event | Note memory |
|-------|-------------|
| **GOSUB** | Callee **inherits** caller context as-is (tempo, key, octave, duty, …) |
| **RETURN** | **Restore** caller snapshot from call site (BASIC-like — mutations inside callee that should stick globally still behave like today’s unified note memory unless we add LOCAL later) |
| **END** | *(n/a — playback stops)* |

**Distinct from existing control flow:**

| Construct | Behavior |
|-----------|----------|
| **`<"name"` / `>"name"`** (**S2**) | **Goto** — no return stack; backward goto restores **label** snapshot, not caller |
| **`[ … ]:N`** (**S4**) | **Counted loop** — repeat stack, not call/return |
| **END** | **Terminate sequence** — not a jump |
| **GOSUB/RETURN** | **Call stack** — return PC + **caller** snapshot |

**Error policy (locked):**

| Fault | Policy |
|-------|--------|
| **`/` with empty call stack** | **Hard abort** — stop playback |
| **Missing label reference** | **`>"…"`**, **`>n`**, **`="…"`**, **`=n`** with no matching **`<…`** define → **FATAL** at pre-parse (**S7d**); runtime safety net → **S7a** |
| **Required-string / quote fault** on **`<"`**, **`>"`**, **`="`** | **FATAL** (**D8b**) |
| **`*` END** | Normal termination — not an error |

**Implementation notes:**

- **Return stack depth cap** — **S7e** (`PLAY_STACK_MAX_DEPTH`, shared with repeat stack).
- **Pre-scan** — label table unchanged; optionally detect **unreachable** code after **`*`** (debug only).
- **I8 resolve hook** — emit structural events on **`=`**, **`/`**, **`*`**.
- **D18** — **`\"gosub:…"`** extension prototype **superseded** by native **`="name"`**.

**v1 scheduling:** Wire syntax + firmware path **🟢** (**I1**). **GOSUB/RETURN/END** ship in v1; stray **`/`** with empty stack → **hard abort** (**S7a**).

**Resolution:** **`="name"` / `/` / `*`**; callee inherits, **RETURN** restores caller snapshot; **`/`** underflow or **undefined label ref** = **hard abort**; **`*`** = hard STOP.

---

### D9 — Comment blocks (`@ … @`)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** How do comments work in `.play` strings?

**User direction:** Streaming model; comments are an exception to single-char executives. Bracket with **`@`**; **`\@`** escapes `@` inside a block. Content is **not parsed** — skip as fast as possible. **Pre-parse** must detect **unterminated** comment ( `@` with no closing `@` before EOF) and **error**.

**Locked syntax:**

```
@ any text, not interpreted, may span lines @
C4Q @ rehearsal @ E4Q
@ line one
  line two @
@ use \@ to write a literal at-sign @
```

**Rules:**

| Rule | Detail |
|------|--------|
| Open / close | **`@`** starts a comment block; next **unescaped `@`** ends it |
| Escape | **`\@`** → literal `@` inside comment (backslash only meaningful inside an open comment) |
| Outside comments | Normal lead-char / note grammar; **`@` is not a command lead** |
| Performance | Byte scan / state machine only — no allocation, no nested interpretation |
| Pre-parse | On sequence start: validate all blocks terminate; **first** block → title (D10); label scan is comment-aware |
| First block | Inner text = **title** (pre-parse) **and** skipped as comment during playback |
| Later blocks | Comments only (skipped; not titles) |

**Rejected for v1:** `;` to EOL, `#` lines. Legacy examples using `-` **between** executives (not as flat accidental) should use whitespace instead (D12).

**Resolution:** **`@ … @` comment blocks with `\@` escape; startup pre-parse validates termination; fail closed on error.**

---

### D12 — Lexical boundaries (whitespace + `:` command terminator)

**Status:** 🟢 · **Needs user:** no (amended 2026-06-11 — parsing model reinforcement)

**Question:** Is whitespace a formal part of the streaming parser contract?

**Parsing model (locked):** PLAY is a **streaming executive** interpreter — implement like a **BASIC interpreter** or **C lexer** walking a buffer: one **statement** (executive) at a time, no AST in RAM. **Boundaries** come from **grammar completion**, **optional whitespace**, and **optional `:`** (our metalanguage’s **end-of-statement** marker).

#### Lexer mental model (BASIC / C)

| Familiar | PLAY v1 |
|----------|---------|
| **BASIC** statement end (newline / implicit break between statements) | Top-level **executive** complete → next lead-in. **`:`** is the **optional explicit end-of-statement** when authors want absolute separation (`T120:C4Q`). |
| **C** **`;`** end-of-statement | **Not used at top level.** **`;`** is **reclaimed for note duty** (**D5** bare **`;`**, **`;n`**) inside note/rest/**`N`** suffix clusters only. |
| **C** whitespace between tokens | **D12** — skip; readability + soft boundary. |
| **String literal `"…"`** | **D8b** — shared across **`K`**, **`?`**, labels, **`\`**. |

**Implementation hint:** one scanner loop — **skip WS** → **read executive** → on **`:`** at top level (except **`]:N`**) **commit executive** and return to lead-in; do **not** treat **`;`** as EOS outside note sub-FSM.

#### Whitespace — readability first

**User direction:** **` `**, **`\r`**, **`\n`**, **`\t`** are valid whitespace; **all equivalent**. Skipped during scan.

| Principle | Detail |
|-----------|--------|
| **Primary role** | **Readability convenience** — authors may space executives apart (`T120 C4Q`, `K "Db" C4Q`). **Not** a syntactic construct in the grammar sense. |
| **Secondary role** | **Disambiguation** — when grammar alone might leave end-of-command ambiguous, a whitespace run **forces** top-level lead-in reset: next non-WS sig char starts a **new** note or command executive. |
| **Flexibility** | **String consumers** (**D8b**) — optional WS before opening **`"`** on **`K`**, **`?`**, **`\`**, **`<`**, **`>`**, **`=`**. **Abut** after closing **`"`** remains valid (`K"Db"C4Q`). |
| **Inside executives** | **No** whitespace inside compact note/rest tokens (`C4Q`, `A5!Q`) or inside **`"…"`** / **`@ … @`** (except literal spaces in print payloads). |

#### **`:` — optional end-of-statement (EOS)**

**User direction (2026-06-11):** **`:`** is **not** whitespace. At **top-level** executive parse, **`:`** **hard-stops** the current statement immediately — the **optional explicit EOS** in our metalanguage (BASIC-like: authors may write **`T120:C4Q`** for clarity). After **`:`**, skip whitespace (**D12**); the next **non-whitespace** character **must** be a valid **top-level lead** — note letter **`A`–`G`**, **`R`**, **`N`**, or any command executive lead (**`T`**, **`K`**, **`[`**, **`<`**, **`>`**, **`=`**, **`?`**, **`\`**, …).

**Optional for authors** — never required when abut or whitespace already separates tokens:

```
T120:C4Q          ; tempo, then note
T120: C4Q         ; WS after : OK
K"C":D4Q          ; key, then note (same as K"C"D4Q)
? "hi":E4Q        ; print, then note
P1:V80:T120       ; explicit separators (readable scores)
```

| Topic | Rule |
|-------|------|
| **Not whitespace** | **`:`** does not participate in WS-skip loops; it **terminates** the active executive (optional EOS). |
| **`;` not EOS** | Top-level **`;`** is **not** statement terminator — only **duty** inside note/rest/**`N`** descriptors (**D5**). Rejected: **`;` to EOL** comments. |
| **After `:`** | Skip WS → require top-level lead-in. Garbage (e.g. **`T120:99`**) → **S7c** (WARNING + skip + continue). |
| **Abut still valid** | **`T120C4Q`** is **not** legal (`C` would be consumed as tempo digit) — use WS, **`:`**, or separate tokens (`T120 C4Q`). |
| **Cross-ref D8b** | Closing **`"`** may be followed by **`:`**, WS, or abut before next executive. |

#### **`:` exceptions — do not treat as end-of-command**

| Context | Rule |
|---------|------|
| **`[ … ]:N`** (**S4**) | **`:`** immediately after **`]`** begins **repeat count** — structural, not terminator. |
| **`"…"`** quoted payloads | **`:`** literal (**D8b**, **D14**, **D18** `cmd:args`, label names if ever used). |
| **`@ … @`** comments | **`:`** literal comment body. |
| **Note / rest / `N` suffix sub-FSM** | **`:`** not an end-of-command marker inside descriptor clusters (keeps note parser simple). |

**Implications (summary table):**

| Topic | Rule |
|-------|------|
| Disambiguation | **WS** (convenience + soft boundary) · **`:`** (optional hard boundary) · **grammar completion** (closing **`"`**, digits done, etc.) |
| **String executives** | **Optional WS before opening `"`** (**D8b**) |
| Compact notes | No WS inside **`C4Q`** / **`A5!Q`** |
| `-` | Flat accidental inside note grammar (**D7**) — not WS |
| `@` comments | Whitespace inside **`@ … @`** is literal body; no lead-in reset until closing **`@`** |
| Pre-parse | Label/comment scan uses same WS + **`@`** rules; **`:`** visible but does not affect label matching |
| **Rejected for v1** | **`;` to EOL** / **`#` lines** as comments · top-level **`;`** as EOS (conflicts with **D5** duty) |

**Leaning:** Matches streaming executive model — authors choose compact abut, spaced, or **`:`**-separated styles without changing semantics.

**Resolution:** **Lexer-style scan; WS = skip + readability; `:` = optional top-level EOS (BASIC-like); `;` = duty only (not C-EOS); S4 `]:N` exception; D8b flexible WS-before-`"`.**

---

### D10 — Title = first `@ … @` block (pre-parse)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; amended same session)

**Question:** How is piece/part title metadata carried?

**Prior resolution:** Optional first-line `TITLE:` keyword — **withdrawn.**

**User direction (amendment):** The **first `@ … @` comment block** encountered in the string is the **title**, determined at **pre-parse** time.

**Agent feedback — magic header:** Still **not required** (see prior analysis). Title-as-first-comment unifies metadata with D9 — no second syntax.

**Locked rules:**

```
@ Star Wars Intro @
T120
CH GH FQ EQ DQ C5Q ...
```

| Rule | Detail |
|------|--------|
| Which block | **First** complete `@ … @` in the buffer (leading whitespace skipped per D12) |
| Capture | Inner text, **trim** leading/trailing SP/LF/CR/TAB; store in **`play_session_t.title`** |
| Max length | **64 chars** — truncate with log if longer |
| No title block | Title string **empty** (legal — e.g. bare `C4Q` test fragments) |
| During playback | Title block is still a **comment** — skipped, never interpreted as music |
| Second `@` block | Ordinary comment only (e.g. `@ rehearsal @`) |
| Pre-parse order | Comment termination check → **title capture** → label table (single pass possible) |

**Rejected:** `TITLE:` keyword line; mandatory `#PLAY v1` magic.

**Resolution:** **Title = first `@ … @` block, captured at pre-parse; no magic header; `TITLE:` withdrawn.**

---

## Semantics (S)

### S1 — Polyphony model

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** Inline chords in one string vs one-voice-per-string + conductor?

**User direction:** **Each PLAY string is an individual voice.** **Multiple play sessions may run concurrently.** **Synchronization is unresolved** (acceptable for v1) — matches prior agent leaning.

**Locked model:**

| Layer | v1 |
|-------|-----|
| **One PLAY string** | **Monophonic** — at most one sounding note per interpreter instance |
| **Inline chords** | **Rejected** — not `C4Q E4Q G4Q` in one stream |
| **Polyphony** | **N concurrent `play_session`s**, each bound to its own string + note memory |
| **Conductor / sync** | **Deferred (S3 🔵)** — post-v1 multi-session |
| **`P<n>` (D1)** | **Timbre** within a voice — **not** “voice 2 of a chord” |

**v1 implementation:** Ship **one** monophonic interpreter + one active instance; architecture should not preclude a **`play_instance_t`** pool later — but **must** leave room for per-instance **LOADING / READY** states when storage lands (**S11**).

**Resolution:** **Conductor model; monophonic per string; multi-session polyphony later; score sync deferred (S3 🔵); load/readiness sync TBD (S11 🟡).**

---

### S2 — Goto / label context snapshots

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11; wire tokens amended **D17** / **D16** 2026-06-11)

**Question:** When is note memory restored from a saved snapshot?

**Wire (D17/D16):** **`<"name"`** or **`<n`** defines a label · **`>"name"`** or **`>n`** gotos to it — **same name/id on both sides** for loops. Goto lead is **single `>`** (not legacy **`><n>`**).

**User direction (locked):**

| Jump | Context rule |
|------|----------------|
| **Forward goto** (label **ahead** of current position) | **Present context in effect** — goto moves PC only; tempo/key/volume/transpose/voice/duty **unchanged**. |
| **Backward goto** (label **behind** current position) | **Context at the label marker** — restore note memory snapshot captured when **`<"`** / **`<n`** was parsed; **overwrites** present context. |

**Musician rationale:** Forward jumps (D.S. al coda, skip ahead) carry your current interpretation. Backward jumps (D.C., **`>"repeat"`** loops) re-enter the section as it was **marked**. Authors who want different behavior should make the **first note and metas explicit** immediately after the backward target.

**Implementation rules:**

1. **Pre-scan** records **`<"name"`** / **`<n` → offset** only. No context snapshot at pre-scan. **Same pass:** every **`>…`** / **`=…`** must resolve (**missing → FATAL**); **unreferenced `<…`** → **WARNING** at pass end (**S7b**); required-string quote faults → **FATAL** (**D8b**) — refuse to play on **FATAL** only (**S7d**).
2. **On each label define during playback:** `label_snapshot[key] = current_note_memory` (**overwrite** on every pass over the marker).
3. **On goto (`>"…"` / `>n`) or GOSUB (`="…"` / `=n`):** resolve key in label table; if **missing** *(pre-parse should have refused)* → **hard abort** (stop playback). Compare **target offset** vs **current offset** *(goto only)*:
   - **Forward** (`target > current`): set `pos = target`; **do not** restore snapshot.
   - **Backward** (`target < current`): set `pos = target`; **`note_memory = label_snapshot[key]`**.
   - **Same offset** (degenerate): treat as **forward** (PC only).
4. **PC landing:** position at **first char of `<`** — label token is **re-parsed** on continued execution (snapshot refreshed per rule 2).
5. **Repeat blocks (S4 🟢):** same backward-restore model at **`[`** (distinct storage, identical semantics).

**Examples:**

**Infinite loop (user canonical — D16/D17):**

```
<"repeat"C4QDEFGABC5R>"repeat"
```

Backward **`>"repeat"`** each time → restore snapshot from **`<"repeat"`** → scale replays forever.

**Forward vs backward (numeric):**

```
T120 <1 C4Q D4Q E4Q >2    ; <1 snapshot = T120; forward to >2 keeps T120
<2 F4Q G4Q >1             ; backward to >1 → restore T120, replay from <1
```

```
T120 <1 C4Q
T140 D4Q >2               ; snapshot at <1 was T120; forward to >2 keeps T140
<2 E4Q                    ; E4 plays at T140 (not T120)
```

**Author escape hatch:** After a backward jump, set **`T` / `K` / `V` / explicit note accidentals** if the restored marker context isn't what you want for this re-entry.

**Rejected for v1:** Always-restore on any goto · never-restore (breaks D.C./loop idioms).

**Resolution:** **Forward = PC only; backward = restore snapshot at label define; snapshot updated on every `<` parse; repeats use same restore-on-re-entry at `[` (S4). Wire: `<` define, `>` goto, quoted or numeric id.**

---

### S3 — Sync barriers vs goto labels

**Status:** 🔵 · **Deferred** (user 2026-06-11) — post-v1 polyphony / multi-session conductor

**Question:** How do multi-voice **sync barriers** work, and how are they **syntactically distinct** from goto labels?

**User direction (2026-06-11):** **Sync barriers must use a different token/syntax than goto labels** — do not overload label navigation. Goto labels use **`<"…"` / `>"…"`** (**D16**/**D17**). Sync is wait/rendezvous, not PC navigation.

**Deferred rationale:** v1 is **monophonic** (**S1**); no multi-session conductor to rendezvous. Design notes kept for when polyphony lands; **not a v1 blocker**.

**Principle (for future work):**

| Mechanism | Purpose | v1 |
|-----------|---------|-----|
| **Goto labels** | **PC navigation** + **S2** context snapshots | **`<"…"` / `>"…"`** or **`<n` / `>n`** |
| **Sync barriers** | **Multi-voice rendezvous** — hold until all sessions reach the same barrier ID | **None** — **S3 deferred** |

**Sync semantics (post-v1, when **S1** multi-session lands):**

- Each **`play_session`** runs its own parser/scheduler.
- A **barrier** marks a point in the score where that voice **blocks** until every active voice in the **conductor group** has reached the **same barrier ID**.
- On release, all blocked voices **resume** from the next parse position — **no** automatic context restore (unlike backward **S2** goto).

**Syntax options (agent — pick one when scheduling polyphony):**

| ID | Form | Notes |
|----|------|-------|
| **S3-A (recommended)** | **`\|"name"`** — barrier marker only | **Pipe + quoted string** — visually distinct from **`*"…"`** / **`>"…"`**. Same name cap as **`PLAY_LABEL_MAX_LEN`** (default 16) or shared **`PLAY_SYNC_NAME_MAX`**. Example: `\|"verseEnd"` in each voice’s string; all must hit before any continues. |
| **S3-B** | **`|n`** numeric | Simple but **easy to confuse** with **`*n`** at a glance; user rejected shared token family |
| **S3-C** | **`&"name"`** or **`%` lead** | Unused leads today; **`&`** was note-repeat history — avoid |

**Leaning:** **S3-A `|"name"`** pairs cleanly with **D16 `<"name"` / `>"name"`** — three roles, three leads:

```
<"coda"     navigation define
>"coda"     navigation goto (same name)
|"coda"     sync rendezvous (post-v1 polyphony only)
```

**v1:** No barrier opcodes — **I1** monophonic. **Do not** reserve **`|"`** in v1 grammar unless needed; leaning **`|"name"`** documented here for future polyphony pass only.

**Open (when polyphony work starts):**

- Barrier ID namespace: **same strings as labels allowed?** **Leaning:** **separate tables**.
- Timeout / missing voice → **S7** policy.
- Conductor scope: all sessions in a **group** vs global — TBD with **S1** multi-session API.
- **Load / readiness (S11 🟡):** barriers are meaningless if one voice is still blocked on LittleFS/NVM while another is already parsing — see **S11** before locking **S3** semantics.

**Resolution:** **Deferred (🔵).** Split from goto labels is settled in principle; **`|"name"`** leaning when multi-session work is scheduled. Not v1. **Hard dependency:** **S11** load/staging model must exist before **S3** barriers can be implemented safely.

---

### S11 — Multi-instance load + synchronization (v2+ headwinds)

**Status:** 🟡 · **Needs user:** no (observations captured 2026-06-11 — design **open**)

**Context:** v1 deliberately avoids this pain: **one** instance, **`const char *`** already in core memory, synchronous pre-parse, shared **I4** tick. **v2+ polyphony (S1)** plus **pull-from-NVM / filesystem (I7)** reopens a much harder problem than “mix N sine voices on one timer.”

**User observation (locked as planning constraint):** **Synchronization is going to be a bitch** — especially once sequences are loaded from NVM or a filesystem. **Non-deterministic blocking delays** (path lookup, open/read, index search, wear-leveling pauses, pre-parse CPU time on large files) **absolutely require an explicit synchronization model**. Hoping “everyone starts at `b_play_start` and stays aligned on **I4**” will fail the moment load latency differs per voice.

**What shared **I4** does and does *not* solve:**

| **I4** shared HW tick | **S11** load + readiness |
|-------------------------|---------------------------|
| Keeps **scheduled note deadlines** comparable once all voices are **RUNNING** on the same timeline | Does **not** align **when each voice’s source buffer exists** or when pre-parse completes |
| Integer tick math avoids float drift between instances (**I4** note) | Does **not** prevent voice A from reaching **S3** barrier while voice B is still **`LOADING`** |

**Headaches to plan for (non-exhaustive):**

1. **Variable start skew** — Voice 1 starts from flash instantly; voice 2 **`playfile`** on SD takes 40–400 ms. Without staging, “measure 1 downbeat” is undefined.
2. **Blocking on the wrong thread** — FS/NVM reads **must not** run inside menu callbacks, **I4** tick context, or parser hot paths. Loader work belongs in **jobs** (or dedicated task when RTOS lands) with completion posted back to the instance state machine.
3. **Pre-parse vs play start** — **S7d** label resolver may scan the whole file. On slow media that scan is itself a **non-deterministic delay**; cannot gate “group play” on synchronous **`b_play_start(path)`** returning only when audio-ready.
4. **Partial group failure** — Voice 3 missing file / corrupt NVM entry while voices 1–2 are **READY** — conductor must define **abort-all**, **start subset**, or **timeout → S7** policy before **S3** barriers mean anything.
5. **Mid-performance reload** — Streaming next movement from FS while others continue — needs **double-buffer / swap** semantics; not the v1 “immutable **`psz_src`** for life of session” rule.
6. **Deterministic test (T2/T3)** — Golden traces assume repeatable “tick 0.” Host tests need **RAM-staged strings** or **injected FS timing**; cannot rely on real SD latency in CI.

**Leaning architecture directions (not locked — reopen when scheduling v2+):**

| Layer | Leaning rule |
|-------|----------------|
| **Instance state machine** | Extend **`play_instance_t`** / internal FSM: **`IDLE` → `LOADING` → `READY` → `RUNNING` → (`WAIT_BARRIER` post-**S3**) → `STOPPED`/`ENDED`/`FAULT`**. **`b_play_start`** on a path returns a handle immediately; audio/parser **RUNNING** only after **`READY`**. |
| **Staging** | **Option A:** conductor **`b_play_group_arm(handles[], n)`** — no voice enters **RUNNING** until **all** handles **READY** (shared tick zero). **Option B:** per-voice async start with explicit **`|"start"`** barrier in score (author-controlled). Likely need **both**. |
| **Storage API** | **`b_play_start(const char *psz_src, …)`** stays for RAM/flash. **`b_play_start_file(path, …)`** or loader job is **async** — never blocks until bytes are in a **core buffer pool**. |
| **Barriers (S3)** | Evaluate barrier reach **only** in **RUNNING** state; **`LOADING`** voices do not participate in rendezvous counts. |
| **Timeouts** | Every wait (barrier, load, group arm) needs **S7**-classified timeout → **FATAL** or **WARNING+skip** — TBD with user when **S3** closes. |

**v1 carry-forward (do not paint into a corner):**

- Keep **`play_handle_t`** + instance pool design (**I7**).
- Keep **read-only parse** on a **`const char *`** once **READY** — loader copies/stages into RAM; parser still never mutates source bytes in place.
- Document in **`play_instance_t`** bench fields at least **`e_state`** + load fault code so **S11** pain is visible on UART before audio lies.

**Cross-refs:** **S1** (N instances) · **S3** (score barriers — blocked on **S11**) · **I4** (audio timeline) · **I7** (storage adapter) · **S7d** (pre-parse gate) · **T2/T3** (determinism)

**Resolution:** **Observations captured (🟡).** No v1 action. **Do not implement **S3** sync barriers or multi-instance **`playfile`** without an **S11** design pass.** User reopen welcome when v2+ polyphony is scheduled.

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** On `[ … ]:N` second iteration, restore note memory from `[` open or keep mutations from prior pass?

**User direction (locked):** For **consistency with S2**, repeat re-entry follows the **backward-reference context restore** model — re-entering the loop body restores context **as it was when the `[` marker was parsed**, overwriting mutations accumulated during the prior pass.

**Unified principle (S2 + S4):**

| Construct | Marker | Snapshot captured | Restore when |
|-----------|--------|-------------------|--------------|
| **Label `<n` / `<"…"`** | `<n` / `<"…"` | Each time label define is parsed | **Backward goto** |
| **Repeat `[ … ]:N`** | **`[`** | Each time **`[`** is parsed (per stack frame) | **`]`** with iterations **remaining** (re-entry) |

**Implementation rules:**

1. **On `[`:** parse repeat count from `]:N` (after matching `]` — see grammar); **push** repeat stack frame `{ return_pos_after_], u16_remaining, x_open_snapshot }`; set **`x_open_snapshot = current_note_memory`** (overwrite on each **`[`** parse for this frame).
2. **First entry** (immediately after `[`): **no restore** — play forward into the body with context in effect at **`[`** (same as entering a label forward in S2).
3. **On `]`:** if **`u16_remaining > 1`**: decrement remaining; **`note_memory = x_open_snapshot`**; **`pos = after [`** (re-entry). If **`u16_remaining == 1`**: last iteration complete — **pop** frame; continue after closing **`]`** without restore.
4. **Nested `[`:** each inner **`[`** pushes its own frame + snapshot; inner **`]`** re-entry restores **that frame’s** `[` snapshot only.
5. **`:0`:** zero iterations — **push/pop** or skip body per S7 lean (**leaning:** parse `]:0` as valid, body never entered — confirm in S7).
6. **Author escape hatch:** same as S2 — after re-entry lands, use explicit **`T` / `K` / `V` / note spellings** if restored context isn’t desired.

**Example:**

```
T120 [ C4Q T140 D4Q ]:2
; Pass 1: [ snapshot = T120; C4@120; T140; D4@140
; ]:2 → restore T120, re-enter; Pass 2: C4@120 again (T140 inside body ignored for pass 2 start)
```

**Cross-ref S2:** Label backward restore and repeat re-entry restore are the **same semantic** (“back to marker context”); only the **trigger** differs (goto vs `]` loop).

**Cross-ref I3:** Stack depth max (10) caps nested `[` depth.

**Resolution:** **Re-entry restores `[` open snapshot; first entry does not; consistent with S2 backward restore.**

---

### S5 — Normative timing formula

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11 — **`PLAY_TEMPO_BPM_MAX=240`**, tick → **I4**)

**Locked formula (v1):**

```
beat_ms       = 60000 / tempo_bpm
unit_ms       = beat_ms * (duration_beats / beat_unit_beats)   // U sets beat_unit
note_ms       = unit_ms * dot_factor                           // dot_factor = 1.5 or 1.0
active_ms     = note_ms * duty_ratio                          // 0.0..1.0 (D5/S6)
rest_ms       = note_ms - active_ms
```

**On device:** the interpreter **does not** run this float path in the hot loop — it uses the **integer tick** equivalent in **I4** (`play_calc_*` shared with host preview). The ms formulas above are the **spec contract** and test oracle.

**Duration map** (in quarter-note fractions; v1 **W/H/Q/I** only — **X/Y** deferred **D4**):

| Letter | `duration_beats` |
|--------|------------------|
| W | 4.0 |
| H | 2.0 |
| Q | 1.0 |
| I | 0.5 |

**Beat unit (`U`) map** — “which written note value = **one beat**”:

| Command | `beat_unit_beats` |
|---------|-------------------|
| `UW` | 4.0 |
| `UH` | 2.0 |
| `UQ` | 1.0 *(default)* |
| `UI` | 0.5 |

**Defaults at sequence start:** see **S10** (`T90`, **`UQ`**, **`V33`**, **`P0`**, **`&0`**, **`K"C"`**, **`O4`**, default **`~`/`inherit` note template `Cn4Q_`**). Former plan text **`T120`** superseded by user lock 2026-06-11.

**Tempo limit:** **`T<n>`** must satisfy **`1 ≤ n ≤ PLAY_TEMPO_BPM_MAX`**. **`n > PLAY_TEMPO_BPM_MAX`** → **hard abort** (parse-time pre-scan preferred; runtime **`T`** command must also enforce). **`PLAY_TEMPO_BPM_MAX = 240`** — user has not encountered scores above ~240 BPM; compile-time **`#define`** in `play_config.h` (see **I4**).

**`T` semantics:** BPM = **beats per minute**, where **one beat** = the note value selected by current **`U`** (not always a quarter — that is only the **`UQ`** default).

**Shortest possible note (v1):** **`UW`** + **`I`** (undotted) at **`tempo_bpm = PLAY_TEMPO_BPM_MAX`**.

> **Beat-unit direction:** worst case is **`UW` (whole note gets one beat)** — the **largest** `beat_unit_beats` (4.0), **not** the smallest. That makes each beat long in ms, but a written **eighth** is only **1/8** of that beat → smallest `note_ms`. **`UI`** (eighth = beat) makes an **`I`** note last a **full beat** — much longer.

```
note_ms_min = (60000 / PLAY_TEMPO_BPM_MAX) * (0.5 / 4.0)
            = 60000 / (PLAY_TEMPO_BPM_MAX * 8)
            /* at T_max=240 → 31.25 ms */
```

**Scheduler tick budget (→ I4):** dedicated HW timer period must resolve **`;n`** duty on that shortest note — at least **`PLAY_DUTY_NUMERATOR`** (8) quanta per **`note_ms`**:

```
PLAY_SCHED_TICK_US <= (note_ms_min * 1000) / PLAY_DUTY_NUMERATOR
                   = 60000000 / (PLAY_TEMPO_BPM_MAX * 64)    /* µs; N=8, UW, I */
                   /* at T_max=240 → 3906 µs max period */
```

**Worked example (`T120`, `UQ`, `C4Q`, duty 50%):**

```
beat_ms   = 60000/120 = 500 ms
unit_ms   = 500 * (1.0/1.0) = 500 ms
note_ms   = 500 ms
active_ms = 250 ms
rest_ms   = 250 ms
```

**Worst-case tick count (`T240`, `UW`, `I`, `PLAY_SCHED_TICK_US=1000`):**

```
note_ms_min = 31.25 ms → 31 ticks (integer path; see I4 partition rules)
duty step   ≈ 4 ticks per ;n quantum on that note
```

**Rest (`R`):** same **`note_ms`** path (dot applies); **full `note_ms` silence** on the timeline (duty fields may update memory per **D20** but do not split the rest gap).

**Resolution:** **Formula + tables + `PLAY_TEMPO_BPM_MAX=240` locked. Integer tick math + HW timer → I4. Shared `play_calc_*` helper name TBD at implement.**

---

### S6 — Duty shorthand constants (tunable)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11 — **`#define` only for v1**)

**Question:** Numeric values for `_` / `!` / normal duty?

**Locked for v1:** compile-time **`#define`** only (no runtime setter until a debug-menu or shadow hook is needed):

```c
#define PLAY_DUTY_NUMERATOR   (8U)    /* ;n scale (D5c) */
#define PLAY_DUTY_LEGATO      (1.0f)  /* _ */
#define PLAY_DUTY_STACCATO    (0.25f) /* ! — tune on bench; start here */
#define PLAY_DUTY_NORMAL      (0.80f) /* bare ; */

#define PLAY_TEMPO_BPM_MAX    (240U)  /* S5 — hard cap on T<n> */
```

**Cross-ref I4:** **`PLAY_SCHED_TICK_US`** lives in the same header; compile-time `#if` guard verifies tick ≤ S5 worst-case budget.

**Semantics:** **`;n`** still uses **`clamp(n) / PLAY_DUTY_NUMERATOR`** (not the named floats). **`!`** uses **`PLAY_DUTY_STACCATO`** directly (may differ from **`;2`** on the N=8 scale). Bench ear-tune by editing headers and rebuilding — runtime tunable deferred.

**Resolution:** **`#define` constants for v1; values above are defaults until hardware tune.**

---

### S7 — Error policy

**Status:** 🟡 · **Needs user:** optional **S7f**, **S7g** only · **S7a–S7e**, **S7c** 🟢

**Parent question:** On parse or runtime fault, does PLAY **stop**, **warn and continue**, or **skip one token and continue**?

Sub-items (same pattern as **D5** / **D5c**):

| ID | Subject | Status |
|----|---------|--------|
| **S7a** | Hard abort (fatal) | 🟢 |
| **S7b** | WARNING + continue (explicit list) | 🟢 |
| **S7c** | Default for *unlisted* faults | 🟢 |
| **S7d** | Pre-parse scope + runtime split | 🟢 |
| **S7h** | Optional LINT scanner (post-v1) | 🔵 |
| **S7e** | Stack depth cap | 🟢 |
| **S7f** | Strict mode (debug authoring) | 🟡 |
| **S7g** | Resolve hook on rejected tokens (**I8**) | 🟡 |

Cross-ref: session init / **`~`** seed → **S10** (not an S7 sub-item).

---

#### S7a — Hard abort (fatal stop)

**Status:** 🟢 · **Needs user:** no (resolved)

**Rule:** **Refuse to play** (pre-parse) or **stop playback immediately** (runtime). Not WARNING-and-continue.

| Fault | When detected | Cross-ref |
|-------|---------------|-----------|
| **Missing** label reference — **`>"…"`**, **`>n`**, **`="…"`**, **`=n`** with no **`<…`** define | Pre-parse *(required)*; runtime safety net if bypassed | **D16**, **S2**, **D19**, **S7d** |
| **Quote integrity** — missing close, misaligned/nested **`"`** on **any** executive once **`"`** is opened; missing required open on **`<"`**, **`>"`**, **`="`** | Pre-parse + runtime | **D8b**, **S7a** |
| Unterminated **`@ … @`** | Pre-parse *(required)* | **D9** |
| **`/` RETURN** with empty call stack | Runtime | **D19** |
| **`T<n>`** out of range — **`n < 1`** or **`n > PLAY_TEMPO_BPM_MAX`** | Pre-scan *(recommended)* + runtime **`T`** | **S5** |
| **Repeat or call stack overflow** | Runtime push | **S7e** |

**Resolution:** **Fatal stop for comment integrity, quote integrity on required strings, missing label refs, bad tempo, `/` underflow, stack overflow.**

---

#### S7b — WARNING + continue (explicit carve-outs)

**Status:** 🟢 · **Needs user:** no (resolved)

**Rule:** Log **WARNING** (once or coalesced); **retain** relevant sticky state; **continue** playback. These faults are **not** promoted to hard abort in v1.

| Fault | Policy | Cross-ref |
|-------|--------|-----------|
| Bad / unquoted **`K`** (no opening `"` after **WS-before-`"`** skip) | Keep current key | **D8**, **D8b** |
| **Unreferenced label define** — **`<"…"`** / **`<n`** never targeted by **`>`** or **`=`** | Log **WARNING** at pre-parse end; continue | **D16**, **I2**, **S7d** |
| Bad **`&`** token | Keep current transpose offset | **D21** |
| Out-of-range pitch after **`&`** | pc **`% 12`**, octave clamp, continue | **D21** |
| **`~`** before any **completed** note/rest | Play **S10** template **`Cn4Q_`** + WARNING | **D2**, **S10** |
| **`?"…"`** faults (garbage after closing `"`, unknown `\X`, source truncate) | Continue | **D14** |
| Accidentals on **`R` / `N`** | Ignore; optional WARNING | **D20**, **D22** |
| Unknown **`\\` extension `cmd`** | Default stub / optional WARNING | **D18** |

**Resolution:** **Listed faults = WARNING + continue only; do not escalate to S7a without user reopen.**

---

#### S7c — Default policy (unlisted faults)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Rule:** Everything **not** in **S7a** (fatal) or **S7b** (explicit WARNING list): **skip** the offending token/executive, log **WARNING once** (coalesce duplicates if cheap), **continue** playback. **Not** ERROR level. **Not** silent ignore.

**Applies to (examples):** malformed note executives · unknown top-level lead char · unbalanced **`[` / `]`** at runtime · incomplete note descriptor · garbage after **`:`** EOS (e.g. **`T120:99`**) · **`T`/`U`/`N`** malformed executives not covered elsewhere.

**Rejected:** hard abort as production default · silent skip without log.

**Resolution:** **Skip + WARNING log once + continue for all unlisted parse/runtime faults.**

---

#### S7d — Pre-parse scope vs runtime errors

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Principle:** **Pre-parse is not a strict syntax scanner or LINTer.** It is a **basic sanity pass** for errors that are **unresolvable at run-start**, plus the **label resolver**. **Most** faults are detected and handled **during streaming playback** (**S7c** skip+log default, **S7b** WARNING carve-outs). A **full LINT-like strict scan** is a **later-phase optional tool** (**S7h** 🔵) — not v1 firmware requirement.

**Pre-parse (load / `play_start`) — FATAL, refuse to play:**

| Check | Policy | Cross-ref |
|-------|--------|-----------|
| Unterminated **`@ … @`** | **FATAL** | **D9**, **S7a** |
| **Label defines** — build **`<n`** / **`<"…"`** table (comment-aware) | Required | **D16**, **D17**, **I2** |
| **Missing reference** — **`>"…"`**, **`>n`**, **`="…"`**, **`=n`** with no matching **`<…`** define | **FATAL** | **S2**, **D19**, **S7a** |
| **Quote integrity** on label/goto/GOSUB string tokens | **FATAL** | **D8b**, **S7a** |
| **Unreferenced define** — **`<"…"`** / **`<n`** never referenced | **WARNING** (end of pass) | **S7b** |
| Title capture (first **`@ … @`**) | Non-fatal truncate | **D10** |

**Runtime streaming interpret — default (not pre-parse):**

| Fault class | Policy | Cross-ref |
|-------------|--------|-----------|
| Malformed note / **`N`** / executives | **S7c** — skip + WARNING once + continue | **S7b** where listed |
| Unbalanced **`[` / `]`** | Runtime (not v1 pre-parse) | **S7c** |
| Incomplete descriptor, forbidden **`~`** in descriptor | Runtime parse error | **S7c** |
| Duplicate **`<"name"`** / **`<n`** | **Last wins** + **WARNING** at pre-parse | **I2** 🟢 |
| Undefined label if pre-parse skipped/bypassed | **S7a** hard abort | Safety net |

**Later phase (S7h 🔵):** optional **strict LINT** pass (host CLI, IDE hook, or `play lint` menu) — full grammar, bracket balance, unreachable labels, duplicate defines, etc. Invoked **by author choice**; does not replace minimal v1 pre-parse.

**Resolution:** **Pre-parse = `@` integrity + label table + missing-ref FATAL + unreferenced WARN; quote integrity FATAL; not a LINTer; runtime owns most other errors.**

---

#### S7h — Optional strict LINT scanner (deferred)

**Status:** 🔵 · **Needs user:** no (tracked for post-v1)

**Idea:** Separate from v1 **pre-parse** (**S7d**). A **strict, optional** scanner (host tool and/or firmware menu flag) that reports syntax/style issues **before** or **without** playback — bracket nesting, duplicate labels, **unreferenced defines** (elevated from **S7b** WARNING), suspicious tokens, strict mode aggregation of **S7b** warnings, etc.

**Not v1-blocking.** Implement when authoring workflow demands it; may share parser FSM with **T2** host preview.

**Resolution:** **Deferred post-v1; optional invoke.**

---

#### S7e — Stack depth cap

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Locked:**

```c
/* play_config.h */
#define PLAY_STACK_MAX_DEPTH   (10U)
```

| Stack | Cap | On overflow |
|-------|-----|-------------|
| **Repeat** **`[ … ]:N`** nested depth | **`PLAY_STACK_MAX_DEPTH`** | **Hard abort** (**S7a**) |
| **GOSUB / RETURN** call stack | **same cap** | **Hard abort** (**S7a**) |

Separate stacks, **shared numeric limit**. Depth **10** is **`#define`-able** only for v1 (no runtime setter).

**Cross-ref:** closes **I3**. **D19** return underflow remains distinct from overflow (empty vs full).

**Resolution:** **`PLAY_STACK_MAX_DEPTH = 10`; overflow = hard abort.**

---

#### S7f — Strict mode (debug authoring)

**Status:** 🟡 · **Needs user:** ship in v1?

**Leaning:** Debug-menu **`playstr`** (and host preview flag) may enable **strict mode**: first **S7b-class** WARNING or **S7c** skip-class fault → **stop** as if **S7a**. Production / SD playback uses tolerant **S7c** default.

**Resolution:** _(optional v1 — pending)_

---

#### S7g — Resolve hook on rejected tokens

**Status:** 🟡 · **Needs user:** confirm

**Question:** Does **`play_resolve_fn_t` (**I8**)** fire when the parser **rejects** a token?

**Leaning:** **No** — hook runs only on **successful** executive resolution; failures log via **S7** path separately (keeps golden traces clean).

**Resolution:** _(pending)_

---

**S7 parent resolution:** **S7a + S7b + S7c + S7d + S7e locked.** Optional **S7f** / **S7g** may close later; **S7** parent may mark 🟢 once those default or defer. **S7h** LINT deferred.

---

### S10 — Session init defaults (runtime note memory)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Rule:** On **`play_session` start** (and after **`*` END** if session is reused), **`v_play_session_init_defaults()`** zero-fills / loads the **unified note-memory struct** from **`play_config.h`** `#define`s. All inheritance, **`~`**, and label/repeat snapshot baselines read from this state until executives mutate it.

**Locked defaults** (user 2026-06-11 — adjust only via headers):

| Field | Default | Wire / struct |
|-------|---------|----------------|
| **Last-note template** (for **`~`** + pitch inheritance seed) | **`Cn4Q_`** | C · **natural** · oct **4** · **Q** · **legato `_`** |
| **Tempo** | **90 BPM** | implicit **`T90`** |
| **Volume** | **33%** | implicit **`V33`** |
| **Beat unit** | quarter = one beat | implicit **`UQ`** *(not “BQ” — beat executive is **`U`*)* |
| **Voice** | sine | implicit **`P0`** |
| **Transpose** | none | implicit **`&0`** *( **`T`** = tempo only)* |
| **Key signature** | C major | implicit **`K"C"`** (**D8**) |
| **Default octave** | 4 | implicit **`O4`** (**D3** — sticky until **`O`/`^`/`v`/note digit**) |
| **Duty (sticky)** | legato | **`PLAY_DUTY_LEGATO`** / **`_`** — matches template **`_`** |
| **Dot** | off | undotted |
| **Completed note flag** | **`b_has_completed_note = false`** | first real note/rest completion sets **true**; **`~`** before that → template + **WARNING** |

**`~` behavior (closes S7/D2):**

1. **`b_has_completed_note == false`:** emit **WARNING** (`~` before any note`); schedule **`Cn4Q_`** at current **`T`/`U`/`V`/`K`/`&`** (same as replaying seeded template).
2. **`true`:** replay **last completed note** snapshot — letter path or **`N`** absolute path per **`b_last_note_was_absolute`** (**D22**); duration, dot, duty; letter-path explicit accidentals only (**D7** — not stored on **`N`/`R`**).

**`play_config.h` sketch:**

```c
#define PLAY_DEFAULT_TEMPO_BPM     (90U)
#define PLAY_DEFAULT_VOLUME        (33U)   /* V0..100 */
#define PLAY_DEFAULT_OCTAVE        (4U)
#define PLAY_DEFAULT_VOICE         (0U)    /* P0 sine */
#define PLAY_DEFAULT_TRANSPOSE     (0)     /* &0 */
/* Key = C major (K"C") — LUT row or enum in play_key.c */
/* Beat unit = UQ — beat_unit_beats = 1.0 */
/* ~ / inherit template: Cn4Q_ — encode as constants or small initializer */
```

**Cross-refs:** **S5** timing uses **`tempo_bpm`** from memory (starts **90**). **S2/S4** snapshots include full struct. **`R`** / **`N`** / **`\"ctx:…"`** overwrite applicable fields. **`N60Q_`** ≈ **`Cn4Q_`** pitch under default **K"/&0** (**D22-5**).

**Not session defaults (do not init here):** **`play_session_t` title** (D10, empty until pre-parse) · **PC / call stack / repeat stack** · **label table** (built at load) · **`g_u32_play_sched_tick`**.

**Resolution:** **Full struct init at session start; `T90` `V33` `UQ` `P0` `&0` `K"C"` `O4`; `~` seed `Cn4Q_`; first `~` → WARNING + template.**

---

### S8 — Transpose reset (`&0`)

**Status:** 🟢 · **Closed** (merged into **D21** 2026-06-11)

**Was:** Whether **`S0`** explicitly clears transpose.

**Resolution:** **`&0`** clears **`i8_transpose_semitones`** to **0**. Omitting **`&`** does not auto-clear. Legacy **`S0`** / **`S±n`** — **retired**; use **`&`**.

---

### S9 — Duty modifiers on one note (precedence)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**User direction:** **Whatever is parsed last rules.**

**Locked rule:** While collecting one note descriptor (order-flexible), each of **`_`**, **`!`**, **`;`**, **`;n`** updates **`duty_ratio`** when seen; **the last such modifier in parse order** sets both **this note’s** duty and **sticky** note memory for following notes. No “forbid combo” rule. Whitespace ends the note token — duty modifiers after duration cluster only, same as other descriptor parts.

**Examples:** `AN5Q!;6` → **75%** (`;6` last). `AN5Q;6!` → staccato constant. `AN5Q;` → normal (~80%). `AN5Q;0` → **100%** (0 clamps to N).

**Resolution:** **Last parsed duty modifier wins (current note + inherit).**

---

## Implementation (I)

### I1 — PLAY v1 feature fence

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** What is the **minimum shippable v1** on-device interpreter — syntax surface, runtime modules, and explicit deferrals?

**Proposed v1 must-ship:**

| In | Out (v1) |
|----|----------|
| Notes **A–G** + **`N<n>`** absolute semitone (**D22** 🟢), accidentals `#` `-` `n`, octave digit | `X`/`Y` durations (D4 🔵) |
| Durations W H Q I + dot | **Tuplets / triplets (D15 🔵, Q1)** |
| `_` `!` `;` `;n` duty (D5 🟢) | **Inline chords / multi-voice in one string (S1 🟢)** |
| **`R` rest — full descriptor → memory + silence (D20 🟢)** | Polyphony / conductor sync (**S3** 🔵 deferred) |
| T O ^ v **`K"…"`** **`&+`/ `&-`/ `&0` (D21 🟢)** S U **`V` 0..100 (D6 🟢)** **`?"…"` (D14 🟢)** **`\"cmd:args"` (D18 🟢)** incl. **`ctx:`** handler (zero-time memory) | VIB/TRM/ADSR PLAY syntax (**D13** 🔵) |
| **`P` voice index** — stored; **audible output = sine only** until voice table grows | Non-sine timbre generators |
| `[ ]:N` **`<n` / `>n` / `<"…"` / `>"…"` (D16/D17 🟢)** **`~` note-repeat (D2 🟢)** **`="…"` / `/` / `*` GOSUB/RETURN/END (D19 🟢)** | Binary compile (I6 🔵) |
| Label **pre-scan** (**S7d** 🟢) | Mandatory magic / `VER:` header |
| **`play_resolve_fn_t` hook (I8 🟢)** — NULL default | ESP32 / host upload transport |
| Optional **first `@` title block** (D10 🟢) | LittleFS **`playfile`** loader (const string OK for first bench) |

**Leaning:** Ship **monophonic interpreter** driving `synth_engine` directly (bypass terminal `p` keys). **Default voice = sine (D1);** `P` stored, audible sine until more voices exist.

**Resolution:** **Locked 2026-06-11.** v1 delivers **one monophonic `play_session`** with the **In** column above — full streaming parser + **I4** scheduler + **`synth_engine`** sine path. **Input:** **`const char *`** in on-chip memory only (**I7** / **I9**); **no** filesystem loader in v1. Parser **read-only** on source bytes. **GOSUB/RETURN/END** in v1 firmware. **String + numeric labels** (**D16**/**D17**/**I2** 🟢). **Out** column hard deferral. Unblocks **T4/T5** and v1 coding.

---

### I2 — Label table cap

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Question:** How big is the label table, how long can names be, and how are limits expressed in firmware?

**User direction:** **16** chars max name; **10** table entries; both **`#define`-able** at build time. General rule: **no magic-number literals** in PLAY C code for invariants — **`#define`** preferred in **`play_config.h`**; runtime-mutable values only when explicitly specified per feature.

**Locked `play_config.h` constants:**

```c
#define PLAY_LABEL_MAX_LEN       (16U)   /* max chars in quoted label name (excl. quotes) */
#define PLAY_LABEL_TABLE_MAX     (10U)   /* max label defines per sequence (<"…" + <n) */
```

**Table model (replaces dense `label_pos[256]` leaning):**

| Aspect | Rule |
|--------|------|
| **Structure** | One **sparse** table per **`play_session`**, sized **`PLAY_LABEL_TABLE_MAX`** at compile time |
| **String keys** | **`<"name"`** / **`>"name"`** / **`="name"`** — name length **≤ `PLAY_LABEL_MAX_LEN`** |
| **Numeric keys** | **`<n` / `>n` / `=n`** — same table; **`n`** is the lookup id (not an array index) |
| **Pre-scan** | Linear pass builds table before playback (**S7d**); **I8** may emit binds |
| **11th define** | **FATAL** — table full |
| **Name too long** | **FATAL** at pre-parse (or at define parse) |
| **Duplicate define** | Same string or same numeric id seen twice → **last wins**, log **WARNING** |
| **Missing reference** | **`>"…"`**, **`>n`**, **`="…"`**, **`=n`** with no matching **`<…`** → **FATAL** (**S7d**) |
| **Unreferenced define** | **WARNING** only (**S7b**) |

**Why sparse:** v1 pieces need few labels; **`PLAY_LABEL_TABLE_MAX=10`** keeps RAM predictable (~few hundred bytes including names + offsets + snapshot refs) vs a 256-slot dense map.

**Resolution:** **`PLAY_LABEL_MAX_LEN=16`**, **`PLAY_LABEL_TABLE_MAX=10`**, both overridable **`#define`s**; sparse unified table; duplicate **last wins + WARNING**; overflow/name-too-long **FATAL**. **D16/D17** wire locked to same caps.

---

### I3 — Repeat stack depth

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11 — moved to **S7e**)

**Resolution:** **`PLAY_STACK_MAX_DEPTH` (default 10) caps repeat + GOSUB stacks; overflow = hard abort. See S7e.**

---

### I4 — Scheduler tick resolution (dedicated HW timer)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11)

**Problem:** Beat timing must stay **synchronized across all `play_session` instances** (S1 — post-v1 polyphony uses multiple sessions on one timeline). **`HAL_GetTick()`** (1 ms SysTick) is **not** the PLAY clock — rounding and drift would desync voices. A **dedicated periodic HW timer** owns the PLAY schedule; **one global tick counter** is shared by every interpreter instance.

**Locked `play_config.h` constants:**

```c
#define PLAY_TEMPO_BPM_MAX     (240U)     /* S5 — hard cap on T<n> */
#define PLAY_SCHED_TICK_US     (1000U)    /* dedicated timer period (1 kHz) */
#define PLAY_SCHED_TICK_HZ     (1000000U / PLAY_SCHED_TICK_US)

/* Compile-time guard — tick must resolve duty on worst-case note (S5): */
#if (PLAY_SCHED_TICK_US * PLAY_DUTY_NUMERATOR * 64U) > (60000000U / PLAY_TEMPO_BPM_MAX)
#error PLAY_SCHED_TICK_US too coarse for PLAY_TEMPO_BPM_MAX + duty resolution
#endif
```

**Budget check at defaults:** worst case **`T240` + `UW` + `I`** → `note_ms_min = 31.25 ms` → max tick **3906 µs** for 8 duty steps; **`PLAY_SCHED_TICK_US = 1000`** gives **~31 note ticks** and **~4 ticks per duty quantum** — sufficient margin without sub-ms timer.

**Architecture (v1):**

| Piece | Rule |
|-------|------|
| **Timer** | One **`TIMx`** update interrupt at **`PLAY_SCHED_TICK_US`** (CubeMX + `App/` glue — not SysTick) |
| **Global counter** | **`g_u32_play_sched_tick`** — incremented **once** per ISR; monotonic for the power-on session |
| **Per-instance state** | Each **`play_session`** stores **`u32_next_deadline_tick`** (note off, rest end, etc.) compared against the **same** global counter |
| **ISR work** | **Minimal** — increment counter; optional coalesced **`sh_jobs`** post; **no** float math, **no** logging |
| **Main loop** | Scan sessions / advance interpreter when **`g_u32_play_sched_tick >= deadline`** |

**Integer tick math (sync-critical — no float in scheduler):**

- Shared helper **`play_calc_note_ticks()`** (host **`play_melody.py`** + firmware) computes **`note_ticks`** from **`tempo_bpm`**, **`U`**, duration letter, dot — using **64-bit rational intermediates**, then **integer divide**.
- **`note_ticks = max(1, computed)`** for any event with non-zero semantic duration (note or **`R`** rest). **Never schedule a 0-tick note/rest** — that would collapse timing and break multi-instance alignment.
- **Duty partition:** **`active_ticks + gap_ticks = note_ticks`** exactly. **`active_ticks = (note_ticks * duty_num) / duty_den`** (integer); if pitch sounds and **`duty_num > 0`**: **`active_ticks = max(1, active_ticks)`** when **`note_ticks ≥ 1`**; **`gap_ticks = note_ticks - active_ticks`**.
- **Tempo changes** affect **subsequent** notes only; in-flight deadlines stay in absolute tick space (no rescaling mid-note).

**Why this matters for sync (S3 future):** when polyphony lands, every voice must compare against the **identical** tick stream. Per-instance soft timers or ms floats that round differently would produce audible phasing; integer deadlines on one HW counter avoid that class of bug.

**Test seam:** unit tests inject tick advances without audio — **`g_u32_play_sched_tick`** (or test double) is the deterministic “when” (golden traces).

**Resolution:** **`PLAY_TEMPO_BPM_MAX=240`**, **`PLAY_SCHED_TICK_US=1000`**, dedicated shared HW timer, integer tick math, **`max(1, …)`** anti-zero rule locked.

---

### I5 — RAM budget per voice

**Status:** 🔵

**Leaning:** Document target **≤ 2 KB/voice** including label table. At **I2** defaults: **`PLAY_LABEL_TABLE_MAX × (PLAY_LABEL_MAX_LEN + overhead)`** ≈ **< 512 B** for labels alone (vs retired **256×4 B** dense-map sketch). Track full budget in spec after implementation skeleton lands.

**Resolution:** _(defer detailed line-item budget to T1 / impl)_

---

### I6 — Binary compiled format

**Status:** 🔵

**Leaning:** Text-only v1; sketch opcodes in appendix only. Revisit after T3 torture strings pass on device.

**Resolution:** _(deferred)_

---

### I7 — Module split + string input (storage)

**Status:** 🔵 · **Needs user:** no (resolved 2026-06-11 — **v2+**; v1 scope locked below)

**Question:** How is the on-device PLAY code organized, and where do sequences come from?

**User direction:** **Defer formal module split until v2+.** v1 player uses **core-accessible on-chip memory** only — no filesystem, NVM, or loader pass.

**Design note (immutable source — locked):**

- `play_instance_t` holds **`const char *psz_src`** (and read cursor / length derived from scan — **no** mutable copy of the score in the instance).
- The parser **must not write** through **`psz_src`** — streaming interpret is **read-only** on the source buffer (flash **`const`** literal or RAM buffer filled by **`playstr`**).
- On STM32G474, flash `.rodata` and RAM are both ordinary core-addressable; **one** start API — no **`start_rom` / `start_ram`** split.

**v1 (implement now — minimal surface):**

| Aspect | Rule |
|--------|------|
| **Input** | **`const char *psz`** — NUL-terminated PLAY text; storage = flash **`const`** array (**I9** **`1`**) **or** static/menu RAM buffer (**I9** **`s`**) |
| **Lifetime** | Caller guarantees **`psz`** valid until **`v_play_stop(px_handle)`** / session end. Caller holds **`play_handle_t`** (pointer) from **`b_play_start`** until stop or natural **`*` END** |
| **Instances** | v1: **`PLAY_INSTANCE_MAX = 1`** — second **`b_play_start`** fails until stop. v2+: raise cap; **same pointer handle** for N concurrent voices (**S1**) |
| **Out of v1** | **`playfile`**, LittleFS/FAT, SD, NVM sequence slots |
| **Code shape** | Cohesive module OK; **`play.h`** + **`play_config.h`** publish API + limits; **`play_instance_t`** body in **`play.h`** (bench-readable); mutation only in **`play.c`** |
| **Minimum API (v1 — `play.h`)** | **`b_play_start(…, &px_handle)`** · **`v_play_stop(px_handle)`** · opaque **`play_handle_t`**; bench cast **`PLAY_HANDLE_AS_INSTANCE`** → **`play_instance_t *`** |
| **Bench** | **I9** 🟢 owns debug-menu wiring |

**v2+ (deferred — reopen I7 + S11 then):**

| Module | Role |
|--------|------|
| `App/Src/play_parser.c` | Streaming parse + state (optional extract) |
| `App/Src/play_scheduler.c` | Tick-driven note on/off (optional extract) |
| Storage adapter | LittleFS / `.play` loader, optional NVM index — **async jobs only** (**S11**) |
| Loader / staging | Copy or mmap score into **RAM buffer pool**; pre-parse in job context; **`READY`** gate before **RUNNING** |
| Conductor (optional) | Group arm, barrier orchestration — depends on **S11** then **S3** |
| **`play_pitch.c`** | Shared K LUT if promoted from `note_player` |

**v2+ warning (S11 🟡):** **`playfile` / NVM pull** introduces **non-deterministic blocking**. A **`b_play_start(path)`** that synchronously opens and reads will desync multi-instance starts and fight the **I4** model. Plan **explicit load + readiness synchronization** before score-level **S3** barriers.

Reuse pitch math from **`note_player`** / shared helper in v1; extract to **`play_pitch.c`** when a second consumer lands (v2+ leaning).

**Handle model (locked — forward-compatible with v2+ polyphony):**

- **`play_handle_t`** — **opaque token** (`void *` in the v1 sketch). Product / menu code passes handles to **`b_play_start`**, **`v_play_stop`**, and future per-instance APIs **without dereferencing**.
- **`play_instance_t`** — **exposed struct** in **`play.h`** (not hidden in **`play.c`**). Holds interpreter + scheduler status fields for **bench-test monitoring** (run state, source offset, title, tempo snapshot, … — finalized at implement). **Read-only** for callers outside **`play.c`**; only the PLAY module mutates instance fields.
- **Bench cast (debug / HIL / menu status — not general API):**
  ```c
  play_instance_t *px_inst = PLAY_HANDLE_AS_INSTANCE(px_active_play);
  /* e.g. px_inst->e_state, px_inst->u32_src_offset — read only */
  ```
- **`b_play_start`** stores a handle whose underlying object is a **`play_instance_t`** pool entry.
- v1 menu: **`play_handle_t px_active_play = PLAY_HANDLE_NULL`**; clear after **`v_play_stop`**.

**Resolution:** **Locked 2026-06-11 (amended same session).** **I7** module/storage architecture deferred **v2+.** v1: opaque **`play_handle_t`** + exposed **`play_instance_t`** for bench reads; **`b_play_start` / `v_play_stop`**; on-chip source only; parser never mutates score bytes. **`PLAY_INSTANCE_MAX = 1`**. No filesystem/NVM.

---

### I8 — Resolve hook (post-parse observer callback)

**Status:** 🟢 · **Needs user:** no (architecture locked 2026-06-11; payload struct TBD at implement)

**User direction:** A hook invoked whenever the parser **resolves a complete command** — note, rest, meta, debug `?`, structural executive, etc. Must exist in **Release** builds (not `#ifdef DEBUG`). Default **NULL**; consumers register at runtime (verbose mode, tests, GUI, LED animation).

**Why (not redundant with `?"…"`):**

| Mechanism | Who controls it | Purpose |
|-----------|-----------------|---------|
| **`?"…"` / bare `?` (D14)** | **Author** embeds debug text in the `.play` string | Intentional trace lines in the score |
| **Resolve hook (I8)** | **Firmware / host tool** registers a callback | Observe **every** executive the parser commits — including notes and metas the author never explicitly printed |

**Fire point:** Synchronous call **immediately after** the parser has a **fully resolved, valid executive** and has applied sticky state updates to note memory (for metas) or built the resolved note/rest descriptor (for pitch events). **Before** or **as** the scheduler enqueues the audio side-effect — exact ordering pinned at v1 implement; hook must see **final resolved values** (freq, duration_ms, level, voice, tempo, key, …).

**Must NOT fire for:** skipped `@ … @` comment regions (never parsed as music). **Should fire for:** **`<`** label defs, **`>`** goto, **`=`** GOSUB, repeat **`[`** / **`]`** boundaries? **Leaning yes** for animation/test visibility — mark **structural** kind so GUI can ignore or consume.

**Release / cost model:**

- One function pointer per `play_session_t` (or global default + per-session override).
- **`if (px_cb != NULL) px_cb(...)`** at resolve sites — no heap, no format strings in the hook path unless the consumer chooses.
- Hook body may **not** block on hardware (no I2S wait); verbose UART echo is OK if consumer keeps it short.

**Payload (TBD — design targets from user examples):**

```c
/* Illustrative — names/types finalized in play.h at v1 implement */
typedef enum {
    PLAY_RESOLVE_NOTE,
    PLAY_RESOLVE_REST,
    PLAY_RESOLVE_META,       /* T O ^ v K & U V P … */
    PLAY_RESOLVE_DEBUG,      /* ?"…" / bare ? / ?"" */
    PLAY_RESOLVE_STRUCTURAL, /* *n >n [ ] repeat edges — leaning */
    PLAY_RESOLVE_UNKNOWN
} play_resolve_kind_t;

typedef struct {
    play_resolve_kind_t e_kind;
    const char         *psz_src;      /* pointer into original PLAY buffer */
    uint16_t            u16_src_len;  /* exact span executed (for verbose echo) */
    uint32_t            u32_src_off;    /* optional: offset for re-fetch / golden files */
    uint32_t            u32_resolve_ms; /* HAL_GetTick or injected clock (test seam) */
    uint32_t            u32_schedule_ms;/* when audio/animation should act (note on); 0 if N/A */
    /* Resolved semantics — use union or parallel fields; consumer reads per e_kind */
    play_note_memory_t    x_memory;     /* post-apply snapshot (synthboard: voice/vol/key/tempo/…) */
    play_note_event_t     x_note;       /* valid if NOTE/REST — freq, dur, duty, level */
} play_resolve_event_t;

typedef void (*play_resolve_fn_t)(play_session_t *px_session,
                                  const play_resolve_event_t *px_ev,
                                  void *pv_user);
```

**Consumer examples (all via same hook):**

1. **Verbose mode** — register cb that `printf`/`LOG` the **`psz_src` span** (and optionally one-line decoded summary). Human or CI log diff sees *what executed, in order*.
2. **Automated test** — cb appends `(kind, src_span, key fields)` to a ring buffer or host capture; compare to golden trace **without mocking synth or I2S**.
3. **Virtual synthboard (display)** — NOTE → highlight piano key; META → update tempo/voice/volume/key widgets from **`x_memory`**.
4. **LED strip animation** — NOTE → push hue/brightness timeline; META → global brightness or palette from **`V` / `P`**.

**Testing tie-in:** Host-side parser (T2) and on-device interpreter should share **the same resolve points** so golden traces match. Injected clock (**I4** / test seam) makes “when” deterministic without audio hardware.

**Cross-refs:** D14 (`?` is both a resolve event **and** may emit UART itself — hook still fires so tests see it); **T3** reference strings should include expected resolve traces; **S7** WARNING path — **leaning:** hook does **not** fire on rejected tokens (only successful resolves); failed parse logs separately.

**Resolution:** **Ship Release-safe resolve callback; NULL default; fire on every successful complete executive; payload carries source span + resolved semantics + schedule context; struct finalized at v1 implement.** _(UART verbose consumer optional post-**I9** minimum.)_

---

### I9 — Player tests submenu (debug menu)

**Status:** 🟢 · **Needs user:** no (resolved 2026-06-11 — v1 minimum)

**Question:** What debug-menu surface is required for v1 bench-testing the PLAY interpreter — while keeping the existing terminal note player?

**User direction (v1 minimum):**

1. **In-ROM (flash) smoke tune** — one-shot trigger; start with a **simple C major scale** (interpreter smoke-test). **Star Wars** main-theme intro is a natural next preset but **not** the v1 gate.
2. **Short typed PLAY string** — enter **< 128 characters**, dispatch to the PLAY interpreter after entry.
3. **Keep** the existing keyboard/terminal note player — **does not** use the PLAY API.
4. **Consolidate** all player-related bench items in one submenu titled **`--- Player tests and experiments ---`**. **Duplicate** the terminal **`p`** entry here (same handler as top menu); **do not remove** top-level **`p`**.

**Architecture rules (unchanged from prior draft):**

- Menu **`pfn_function`** handlers **return immediately** — PLAY runs async via jobs + **I4** HW tick (not inside the menu callback).
- While a **`play_session`** is **RUNNING**, **refuse** submenu **`p`** (terminal piano) or **auto-stop** PLAY first — one **`synth_engine`** owner.
- Submenu **ESC / RETURN** **auto-stops** an active PLAY session (mirror **`i`** submenu synth stop).
- Limits are **`#define`** in **`play_config.h`** (**I2** policy — no magic numbers).

**Locked menu layout:**

| Location | Key | Label / action |
|----------|-----|----------------|
| Debug **top** menu | **`m`** | **CALL_MENU** → **`x_player_tests_submenu`** |
| Top menu | **`p`** | *(unchanged)* Interactive note player |
| Submenu banner | — | **`--- Player tests and experiments ---`** |
| Submenu | **`?`** | Help (menu-api) |
| Submenu | **`1`** | **PLAY smoke** — **`b_play_start(psz_play_smoke_test, &px_active_play)`** |
| Submenu | **`s`** | **playstr** — prompt **`PLAY>`** · **`i_getline()`** into static buffer · NUL-terminate · **`b_play_start(ac_play_debug_line, &px_active_play)`** |
| Submenu | **`p`** | **Terminal note player** — **`v_note_player_run()`** (duplicate of top menu; **not** PLAY) |
| Submenu | **ESC** | Return — **`v_play_stop(px_active_play)`** if non-**`NULL`**, then leave submenu |

**Locked smoke-test string (v1 — `play_presets.c`; pointer + literal, not `#define`, not `[]` array name):**

```c
/* play_presets.c — v1 interpreter smoke-test */
const char *psz_play_smoke_test =
    "@ smoke scale @ T120 O4 C4Q D4Q E4Q F4Q G4Q A4Q B4Q C5Q *";
```

Exposed via **`play_presets.h`** (`extern const char *psz_play_smoke_test;`) for menu + tests. Self-terminates with **`*` END**; ascending C major oct 4→5.

**Locked `play_config.h`:**

```c
#define PLAY_DEBUG_LINE_MAX      (128U)   /* max chars for playstr UART entry (incl. NUL room in buffer) */
#define PLAY_INSTANCE_MAX        (1U)     /* v1: one voice; raise for v2+ polyphony (S1) */
```

**Public API sketch (`play.h` — v1 minimum):**

```c
typedef enum {
    PLAY_STATE_IDLE = 0,
    PLAY_STATE_LOADING,   /* v2+ async FS/NVM staging (S11) — unused in v1 */
    PLAY_STATE_READY,     /* staged + pre-parse OK; not yet on I4 timeline */
    PLAY_STATE_RUNNING,
    PLAY_STATE_STOPPED,
    PLAY_STATE_ENDED,
    PLAY_STATE_FAULT
} play_state_t;

typedef struct play_instance play_instance_t;

/* Exposed for bench status reads — fields grow at implement; do not mutate outside play.c */
struct play_instance {
    play_state_t  e_state;
    const char   *psz_src;        /* read-only score pointer */
    uint32_t      u32_src_offset; /* parser cursor — bench monitor */
    /* title, tempo, voice, fault code, … TBD */
};

typedef void *play_handle_t;      /* opaque — underlying object is play_instance_t */

#define PLAY_HANDLE_NULL              ((play_handle_t)NULL)
#define PLAY_HANDLE_AS_INSTANCE(h)    ((play_instance_t *)(h))  /* bench/debug read-only */

bool b_play_start(const char *psz_src, play_handle_t *px_out_handle);
void v_play_stop(play_handle_t px_handle);       /* NULL → no-op */
bool b_play_is_running(play_handle_t px_handle);
```

**Bench usage (I9 extended status / HIL — not product API):** after **`b_play_start`**, **`PLAY_HANDLE_AS_INSTANCE(px_active_play)`** yields a **`play_instance_t *`** for UART status dumps, automated tests, or submenu **`playstatus`** (post-v1). Callers **read** fields only; control stays on **`v_play_stop`** / public start API.

**Implementation (`debug_menu.c` + `play_presets.c`):**

- **`x_player_tests_submenu[]`** + top-menu **`m`** entry.
- Static **`ac_play_debug_line[PLAY_DEBUG_LINE_MAX + 1U]`** for **`s`** entry.
- Static **`play_handle_t px_active_play = PLAY_HANDLE_NULL`** — set by **`b_play_start`**, cleared after **`v_play_stop`**.
- **`1`** → **`b_play_start(psz_play_smoke_test, &px_active_play)`**.
- **`s`** → fill static **`ac_play_debug_line[]`**, then **`b_play_start(ac_play_debug_line, &px_active_play)`** — buffer must stay valid for session; parser does **not** modify it.
- **ESC / RETURN** → **`v_play_stop(px_active_play)`** · **`px_active_play = PLAY_HANDLE_NULL`** before leaving submenu.
- **`p`** → **`v_note_player_run()`** — refuse or **`v_play_stop(px_active_play)`** first if **`b_play_is_running(px_active_play)`**.

**Post-v1 extensions (not v1 menu requirements — track separately):**

| Item | Notes |
|------|-------|
| ROM **Star Wars** / richer **T3** presets | Submenu **`2`…`** or flash table |
| **`playstop` / `playstatus` / `playverbose`** | Extended bench; **I8** trace toggle |
| **`playparse` dry-run** | **S7d** label dump without audio |
| **`playfile`** | LittleFS (**I1** out) |
| Host **`play_melody.py`** retarget | T2 phase 4 — script **`m`** + **`1`** / **`s`** |

**Cross-refs:** **I1** (const-string + **`playstr`** bench) · minimal **`play.h`** at v1 implement · **I8** (optional verbose later) · **T2/T3** (Star Wars + golden strings)

**Resolution:** **Locked 2026-06-11 (amended).** v1 submenu under top **`m`**: **`1`** = **`psz_play_smoke_test`**, **`s`** = **`playstr`**, **`p`** = terminal player dup. **`play_handle_t`** opaque; **`play_instance_t`** exposed for bench cast/read. Top **`p`** retained.

---

## Tooling / docs (T)

> **Post-lock documentation bundle (user request, pre-implementation):** Once **D/S/I 🟢 rows needed for v1** are locked (minimum: **I1**, **S5**, **S7**, **I2**), produce **two separate user-facing / formal docs** — not buried in `PLAY_language_design.md`. **T4** = machine-readable grammar; **T5** = human howto. **T1** trims the legacy design doc for implementers and **links** T4/T5; **T3** golden strings feed **T5** examples and **T2** tests.

### T1 — Spec cleanup + implementer quick reference

**Status:** 🔴 · **Needs user:** no (agent task after spec-lock gate)

**Scope:** Remove duplicate sections in `PLAY_language_design.md`; add a short **implementer** quick-ref (1 page) at top; link this plan + **T4** + **T5**. **Do not** put the full EBNF or musician tutorial here — those are **T4** / **T5**.

**Gate:** Start after **T4/T5** outline approved, or in parallel once **S5 + S7 + I1** are 🟢.

**Resolution:** _(pending spec-lock gate)_

---

### T2 — `play_melody.py` conformance phases

**Status:** 🔴

**Proposed phases:**

1. Inheritance + order-flex + K/S/U/T/O/^/v
2. Duty + R + errors
3. Dry-run timing print (S5 formula) without hardware
4. Optional: serial RPC — script enters submenu **`m`**, preset **`1`** or **`s`** + line (**I9** 🟢)

**Resolution:** _(pending spec-lock — **S7f/g** optional; **I1/I2/D16/D17** 🟢)_

---

### T3 — Reference test strings

**Status:** 🔴

**Candidates:** Twinkle / Chopsticks (smoke) · feature micro-examples · Star Wars / Raiders excerpts · Sousa / Elgar demo excerpts · Chopin op. 66 / Bumblebee stress (post-v1 hardening) · inheritance chain · repeat + goto · malformed recovery · resolve-hook golden traces (I8)

**Note:** Full **T5 repertoire** (below) is **not** an implementation gate — grows as parser + arranger time allow. **T3** may reuse the same `.play` text with machine-oriented expected traces.

**Feeds:** **T2** regression · **T5** example source material (same strings, different docs).

**Resolution:** _(pending S5, S7)_

---

### T4 — Normative EBNF grammar (standalone)

**Status:** 🔴 · **Needs user:** no (agent task; formal review welcome)

**Deliverable:** New file **`Docs/PLAY_v1_grammar.md`** (name TBD) — **complete, v1-only** syntax in standard EBNF (or BNF + EBNF where clarity needs both). Replaces the draft grammar fragment in `PLAY_language_design.md`.

**Must include:**

| Section | Content |
|---------|---------|
| **Lexical** | Whitespace (**D12**); **`:`** optional hard end-of-command (**D12**); `@ … @` comments (**D9**); title block (**D10**) |
| **Notes** | Order-flexible descriptors (D7); duty `_` `!` `;` `;n` (D5); durations W H Q I + dot (D4 🔵 excluded) |
| **Metas** | `T` `O` `^` `v` **`K"…"`** **`&±n` / `&0`** (D8/D21) `U` `V` `P` (D1/D6) |
| **Debug** | `?"…"` C escapes; bare `?`; `?""` (D14) |
| **Control** | `R` · `[ ]:N` · `*n` · `>n` · `~` (D2) |
| **Semantics cross-refs** | Pointers only — timing (S5), goto/repeat snapshots (S2/S4), errors (S7) live in plan/spec prose, not duplicated as semantics in EBNF |

**Non-goals:** v1.1+ deferred syntax (**X/Y D4**, **D5d**, **D13**, **S3** sync, **D15** tuplets). Mark **v1 grammar subset** explicitly. *(**D19** wire is 🟢 — **`=` / `/` / `*`** in v1 fence leaning.)*

**Consumers:** firmware parser implementer · **T2** host parser · future codegen. Should be diffable when v1.1 adds productions.

**Resolution:** _(pending spec-lock gate)_

---

### T5 — Musician-facing howto (user reference)

**Status:** 🔴 · **Needs user:** review examples (especially Star Wars / Q1)

**Deliverable:** New file **`Docs/PLAY_howto.md`** (name TBD) — **non-programmer**, musician-friendly. This is the **authoring manual**, not the implementer spec.

**Pedagogy (locked 2026-06-11 — user direction):**

Introduce syntax **gradually**. Musicians learn by **playing short familiar music**, not by reading a command alphabet.

| Layer | What to teach | How |
|-------|----------------|-----|
| **Early** | Common, simple — notes **`C4Q`**, tempo **`T`**, rests **`R`**, comments **`@ … @`** | **Small complete snippets** (4–8 bars); Tier 0 tunes |
| **Middle** | Sticky inheritance, key **`K"…"`**, volume **`V`**, duty **`_`/`!`/`;`**, repeats **`[ … ]:N`** | One concept per section + **fragment** that isolates the feature |
| **Late** | Lesser-used — **`N60Q`**, **`&`**, **`~`**, **`^`/`v`**, GOSUB **`=`/`/`**, **`U`**, **`P`**, **`?"…"`**, **`\\` extensions** | **Code fragments** (not full pieces) that illustrate the tricky idea; cross-refs to Tier 0/1 |
| **Appendix / heritage** | Full arrangements (Sousa, Elgar, Chopin stress, film themes) | Tier 2–4 — assume reader already knows core syntax |

**Rules for T5 prose:**

- **Never** dump the full command table in chapter 1.
- **Complex concepts** → short **code fragment** in a callout, then one sentence of “what you’ll hear.”
- **Full scores** only after the reader has seen the building blocks in isolation.
- Pitfalls (backward goto restores context, **`N604Q` vs `N60Q4`**, **`>"name"`** must have matching **`<"name"`** — **FATAL** at load; orphan **`<"name"`** → **WARNING** only) appear **when the feature is introduced**, not in a front-loaded error appendix.
- **T4** = legality for implementers; **T5** = “how to write music”; **`play lint`** (**S7h**) mentioned only when it exists — v1 authors rely on device load errors + runtime logs.

**Historical note & rationale (author, 2026-06-11):**

PLAY is not a greenfield notation experiment. The author’s **first commercial software sale** — at the end of **high school senior year** — was a **music player on a TRS-80 Model I** that drove a **simple square-wave synth** using a **subset of this same language**. Shipping it required **end-user documentation**: how to enter tunes, what the commands mean, worked examples — not an implementer spec or grammar proof.

**T5 exists for the same reason that sale required docs:** someone with a keyboard and a melody should be able to **program the synth without reading firmware**. The G474 path (CORDIC sine, duty, inheritance, goto/repeat) is richer than the TRS-80 square wave, but the **audience for the howto is the same** — the person writing music, not the person writing the parser.

Implications for **T5** tone and scope:

- **Musician-first**, like the original TRS-80 user manual — not “API reference for embedded developers.”
- **Examples are the product** — from Twinkle through the TRS-80 demo marches, Atari-inspired Chopin, and theatre-era film themes — the way the early docs taught by showing **complete tunes**, not single commands in isolation.
- **T4** (EBNF) serves implementers; **T5** serves the historical end-user role that made the language shippable once before.

**Tone & shape:**

- Plain language first; grammar terms explained when needed.
- **Practical, copy-paste examples** — as many as needed to cover **every v1 capability** (not one demo and done).
- Each major feature: *what it does* → *minimal example* → *common pitfall* (e.g. backward goto restores context — explicit meta after label if you want something else).

**Proposed example catalog (v1 fence):**

Teach features first with **small** excerpts; grow into the **repertoire roadmap** as the interpreter matures. None of the listed pieces block v1 ship — they are **fun**, **historical**, and **stress targets**.

**Tier 0 — Start simple (childhood favorites, feature smoke):**

| Piece / fragment | Role |
|------------------|------|
| **Twinkle, Twinkle, Little Star** | First notes, inheritance, tempo |
| **Chopsticks** | Short duet-feel on **monophonic** arr. (melody line only) |
| Other nursery / one-line drills | Per-command micro-examples in early chapters |

**Tier 1 — Feature chapters (howto structure — one chapter ≈ one concept + fragment):**

| Chapter (order) | Demonstrates | Fragment style |
|-----------------|--------------|----------------|
| 1 Inheritance | Omit octave/duration; sticky defaults | 2-bar **`C Q D E`** |
| 2 Tempo | **`T`** | **`T90`** + short motif |
| 3 Key | **`K"…"`** | **`K"G"`** scale fragment |
| 4 Accidentals & **`n`** natural | **`#` `b` `n`** | **`F#4Q Bb4Q`** |
| 5 Articulation / duty | **`_` `!` `;` `;n`** | same pitch, different duty |
| 6 Rests & **`~`** | **`R`**, note-repeat | call/response 2 bars |
| 7 Comments & title | **`@ … @`** | title block + tune |
| 8 Repeats | **`[ … ]:N`** (**S4**) | 4-bar loop fragment |
| 9 Labels & jumps | **`<"…"` / `>"…"`** (**D16/D17**) — goto target must exist; unused labels OK | infinite loop example (small) |
| 10 Transpose | **`&+` / `&-` / `&0`** | same line, shifted |
| 11 Beat unit **`U`** | **`UQ` vs `UH`** | same notes, different feel |
| 12 Volume & voice | **`V`**, **`P`** | crescendo fragment |
| 13 Absolute pitch **`N`** | **`N60Q`**, **`N60Q4`** pitfall | **fragment only** |
| 14 Subroutines | **`=` / `/` / `*`** | tiny main + **`END`** |
| 15 Debug & extensions | **`?"…"`**, **`\\`** stub | bench only |

*(Order may shift slightly when drafting; **principle** is fixed: common → rare, snippet → fragment → full piece.)*

**Tier 2 — Author heritage demos (TRS-80 Model I player, commercial sale):**

| Piece (excerpt) | Notes |
|-----------------|-------|
| **Sousa — *Stars and Stripes Forever*** | Original product demo; arrange **single-voice** (piccolo/oboe line or simplified melody) |
| **Elgar — *Pomp and Circumstance* March #1** | Original product demo; famous tunestack excerpt |

**Tier 3 — Stress & aspiration (motivation + torture tests):**

| Piece (excerpt) | Notes |
|-----------------|-------|
| **Chopin — *Fantaisie-Impromptu* op. 66** | Atari **400** demo that sparked musical interest; fast figuration — **parser/scheduler stress** + arranger challenge on **S1** monophonic line |
| **Rimsky-Korsakov — *Flight of the Bumblebee*** | Already cited in design doc as timing torture; dense short notes |

**Tier 4 — Generational film themes (author, theatre-release era):**

| Piece (excerpt) | Notes |
|-----------------|-------|
| **Star Wars — main-title opening** | **Q1** / **D15** — author WIP `CH GH F? E? D? CH5 G4 …`; durations TBD under **S5** |
| **Raiders of the Lost Ark — *Raider’s March*** | Same era; march rhythm + range; monophonic arr. |
| **Star Trek (TOS) — Alexander Courage main theme** | Opening fanfare; broadcast-era TV (author: infant in ’60s — grew up with reruns/later releases) |

**Arranger / player constraints (all tiers):**

- **S1 🟢:** one **monophonic** voice per string — polyphonic scores need **manual reduction** (melody-only, top note, etc.). Refactoring is part of the project’s **fun**, not a spec failure.
- **D15 🔵:** triplet-heavy passages (Star Wars, parts of Chopin) may need **v1 approximation** + `@` author notes until tuplets exist.
- **Order of publication in T5:** Tier 0 → Tier 1 chapters → Tier 2–4 as `.play` files prove out on hardware/host — **no deadline tied to v1 firmware freeze**.

**Relationship to other artifacts:**

- **T3** — machine-oriented golden strings + expected traces (can reuse text, different presentation).
- **T4** — formal “what is legal”; **T5** — “how to write music in it.”
- **`play_melody.py`** — link as optional host playback tool once **T2** exists.

**Gate:** Draft after **I1 + S5 + S7** locked. **Tier 0 + Tier 1** first; heritage/stress/film excerpts land incrementally (not spec-lock blockers).

**Resolution:** _(pending spec-lock gate; repertoire grows with author arranging time)_

---

## Questions (Q)

### Q1 — Star Wars triplet passage (v1 approximation)

**Status:** 🔵 · **Needs user:** no for v1 (resolved for planning; exact tuplets → **D15**)

**Question:** Ship Star Wars demo with **approximated** eighth durations for triplet feel, or block demo until tuplet syntax exists?

**v1 resolution (locked for planning):** **Do not block v1.** Ship Star Wars (and **T5** example) with **straight eighths** (or other best-effort even subdivision) and an **`@` / `?"…"` note** that triplet feel is approximate. Exact timing waits for **D15** (v2+ default).

**Author WIP (2026-06-11):** Opening bars pitch contour roughly **`CH GH F? E? D? CH5 G4 …`** — **`?` = duration still being worked** (triplet-eighth vs dotted-quarter group under **S5**/**D15**). v1 starting point: try **`IQ`** per note in the triplet run (even eighths, ~75% of triplet feel) or **`FQ EQ DQ`** for the F–E–D pickup with **`HQ`** on anchors — tune by ear once **S5** formula is in **`play_melody.py`**.

**Example authoring note (v1):**

```
@ Star Wars main theme — triplet feel approximate until D15 tuplets @
T120 K"C" … IQ IQ IQ …   ; FQ EQ DQ groups as even eighths — not mathematically 3:2
```

**Cross-ref:** **D15** — when scheduled, re-transcribe Star Wars with real tuplet syntax and update **T3** golden timing.

**Resolution:** **v1 = approximate + documented; D15 = exact tuplet path (post-v1 unless pulled in early).**

---

## Global notes

### Suggested resolution order (first pass)

1. ~~**I1** — fence v1 scope~~ **🟢 2026-06-11**
2. ~~**I2** — label table cap (+ **D16**/**D17** wire)~~ **🟢 2026-06-11**
3. ~~**S1**~~ — monophonic + conductor-only polyphony **🟢**
4. ~~**D8**~~ charset/parse rules (D3, D6, D7, D8 🟢)
5. ~~**S5**~~ — timing formula **🟢**
6. ~~**S2, S4**~~ — control-flow semantics **🟢**
7. ~~**D2**~~ charset (D1, D2, D3, D5, D5c, D6, D7, D8, D9, D11, D12 🟢)
8. **S7f/g**, ~~**I2**~~, ~~**I3, I4**~~ — engineering limits (**I2/I3/I4 🟢**)
9. ~~**D16/D17**~~ — string label wire **🟢**
10. **T4** — normative EBNF grammar doc (`Docs/PLAY_v1_grammar.md`)
11. **T5** — musician howto (`Docs/PLAY_howto.md`) — tiered repertoire
12. ~~**I9** — player tests submenu~~ **🟢 2026-06-11**
13. **T1** — trim `PLAY_language_design.md`; link T4/T5
14. **T3 → T2** — golden strings then host parser phases

### Spec-lock gate (start T4/T5 / implementation)

Minimum 🟢 before formal grammar + howto + coding:

| Area | IDs |
|------|-----|
| Syntax | D1–D14 except 🔵 deferrals (**D4, D13, D15**) · **D16/D17/D18/D20 🟢** |
| Semantics | **S1–S2, S4–S5, S7, S9** (+ **D21** transpose / **S8** **`&0`**) |
| Implementation fence | **I1 🟢**, **I2 🟢**, **I3 🟢**, **I4 🟢**, **I8 🟢**, **I9 🟢** |
| Open but non-blocking | **S7f**/**S7g** 🟡 |

### Plan status (2026-06-11, I7 deferred)

| Status | Count (approx.) |
|--------|----------------:|
| 🔴 Open | 6 |
| 🟡 Leaning | 4 |
| 🟢 Resolved | 27+ |
| 🔵 Deferred | 9 |
| 🟡 Observations / open design | **S11** (+ **S7f/g**) |

**Next suggested chat prompt:** *"Draft T4 EBNF outline or start v1 `play.h` / `play.c` skeleton (I9 + handle API)."*

**Session handoff:** [play-v1-session-handoff-2026-06-11.md](play-v1-session-handoff-2026-06-11.md) — **S7f/S7g** still 🟡 (user resolves later).

### Cross-reference: punctuator roles (D2, S3, D16, D17, D18)

| Lead | Role | Status |
|------|------|--------|
| **`~`** | Note-repeat (D2 🟢) | v1 |
| **`<` / `>`** | Label define / goto (**D16**/**D17** 🟢) | v1 |
| **`\` `"`** | Expansion **`cmd:args`** → dispatch (**D18** 🟢) | v1 stub |
| **`|"`** | Sync barrier (**S3** 🔵) | deferred |
| **`@`** | Comments (D9 🟢) | v1 |
| **`:`** | Optional **EOS** at top level (**D12** 🟢); **not** inside note sub-FSM; **except** **`]:N`** (**S4**) | v1 |
| **`;`** | Note duty only (**D5** 🟢) — **not** top-level EOS | v1 |
| **`&`** | Transpose **`&+n` / `&-n` / `&0`** (**D21** 🟢) | v1 |
| **`= `"`** | GOSUB **`="name"`** (**D19** 🟢) | v1 leaning |
| **`/`** | RETURN — **hard abort** if call stack empty | v1 leaning |
| **`*`** | END hard STOP (**D19** 🟢) | v1 leaning |

Do **not** overload **`<`/`>`** for sync. **`@`** reserved for comments only.

---

**End of play-v1-implementation-plan.md** — update this file as IDs resolve; then sync [PLAY_language_design.md](../PLAY_language_design.md).
