# PLAY v1 — Chatbot & author brief

**Purpose:** Copy-paste this file into ChatGPT, Grok, Google Gemini, Claude, etc. when you want a **correct, bounded** mental model of the PLAY music language — more detail than the [cheat sheet](play-lead-char-cheat-sheet.md), far less than the full [implementation plan](play-v1-implementation-plan.md) or [language design](../PLAY_language_design.md).

**Scope (G474 v1 ship):** Documents **what works today** + **v1/v1.1 target** on the current bench MCU. **v2+** (polyphony, loaders, richer synth) is planned for a likely **STM32H7** fork — out of scope here. After PLAY v1 ships (hobby milestone), project focus is expected to shift to mic input, DSP, and audio-reactive lighting per [PROJECT.md](../PROJECT.md).

**Living document:** Update this file whenever `App/Src/play.c` gains or loses behavior. **Firmware truth:** `App/Src/play.c` + bench presets in `App/Src/play_presets.c`.

**Last updated:** 2026-06-13 (audited against firmware — inheritance, order-flex, `%`, S7i, duty, `~`, `&` transpose, **`K"…"`** key LUT, **`G4`** label pre-parse, top-level **`S`** playstr hook)

---

## What PLAY is

PLAY is a **single-line, monophonic** music macro language for embedded firmware. One ASCII string → one note at a time → sine-wave output (today). There is **no AST** in RAM: a streaming parser walks the string and schedules notes on a 1 ms tick.

- **Notes:** uppercase **`A`–`G`** only at pitch start (not lowercase).
- **One voice** per interpreter instance (v1).
- **Whitespace** between tokens is optional and ignored.
- **String ends** at NUL; EOF behaves like **`*`** (END).

---

## How to read the status columns

| Status | Meaning |
|--------|---------|
| **YES** | Implemented and exercised on the STM32G474 bench (or trivial subset proven). |
| **PARTIAL** | Parsed or partially wired; spec behavior incomplete — see notes. |
| **NO** | Not in firmware — usually `PLAY warn: unsupported executive` (NORMAL) or fault (STRICT). |
| **DEFERRED** | Locked out of v1 scope by design — do not use in “v1-safe” scores. |

---

## Session defaults (apply at every `play_start`)

These seed **sticky note memory** before the first token (no need to spell them unless changing):

| Field | Default | Explicit override |
|-------|---------|-------------------|
| Tempo | **120 BPM** | `T120` |
| Beat unit | **Quarter = 1 beat** | `%Q` (also `%W` `%H` `%I`) |
| Octave | **4** | `O4` |
| Duration | **Quarter (`Q`)** | on first note or via memory |
| Duty | **Legato 8/8** | `_` (implicit in template) |
| Volume | **50%** | `V50` — **YES** (live update while sustaining) |
| Voice | **Sine (`P0`)** | `P0` sine · `P1` triangle — **YES** |
| Key | **C major (`K"C"`)** | **YES** — LUT on bare letters; default all naturals |

Template name in spec: **`Cn4Q_`** — first note may omit duration/octave and inherit **Q** + **O4**.

---

## Note & rest tokens (core grammar)

### Token boundary (critical for LLMs)

- A **note/rest token** starts with **`A`–`G`** or **`R`** (rest).
- After the **first pitch letter** of a note, the next **`A`–`G`** starts a **new** note — not a continuation of the same cluster.
- Example: `CQ4DEFGABC5` = C₄ quarter, then D E F G A B C₅ each inheriting duration/octave from the previous note.

### Inheritance (**YES**)

After each note or rest **commits**, these fields live in **note memory** and carry forward until overridden:

- Duration (`W` `H` `Q` `I` + optional dot)
- Octave digit (`0`–`8`)
- Duty (`_` `!` `;` `;n`)

**Letter-only** tokens inherit everything above: `DEFGAB` after `C4Q` plays six quarters at the current octave.

### Order-flex (**YES**)

Within one note/rest token, suffix pieces may appear in **any order** after the lead letter:

- `C4Q` = `CQ4` = `Q4C` = `4QC` (same semantics)

### Rests (**YES**)

- **`R`** uses the **same suffix grammar** as notes (duration, dot, duty, octave inheritance).
- Examples: `RQ` `RI.` `R4Q` `RI` (eighth rest if prior note had `I`).

### Pitch & accidentals

| Feature | Status | Notes |
|---------|--------|-------|
| White-key **`A`–`G`** + octave | **YES** | Equal-temperament Hz from letter + octave |
| **`#` `+` `b` `-` `n`** in note | **YES** — explicit accidentals shift pitch; skip key LUT |
| **`K"C"`** key signature | **YES** — `K"…"` only; Co5ths LUT on bare letters |
| **`&+n` / `&-n` / `&0`** transpose | **YES** | Sticky linear semitone offset (D21) |
| **`N60`** absolute MIDI-like semitone | **NO** | Distinct from natural **`n`** accidental |

**Bare vs explicit accidental (locked policy — see implementation plan *Pitch resolve pipeline*):**

- **Bare letter** (no `#`/`+`/`b`/`-`/`n` in the note cluster): apply **`K"…"` key LUT** per letter (default C major = all naturals).
- **Explicit accidental** in the cluster: **ignore key signature**; use only the requested sharp/flat/natural for that token.
- **`n`** = natural pitch class of the letter (e.g. in B♭ major: bare `E` → E♭; `En` → E natural).
- Accidentals **do not inherit** from the previous note.

**Math note for implementers:** `% 12` applies only while **normalizing pitch class** (with octave borrow/carry) and in **out-of-range salvage** — **not** after `&` transpose on in-range notes (linear absolute semitone must preserve octave jumps).

**Authoring:** use **`K"Db"`** / **`K"F#m"`** etc. for scored keys; spell accidentals explicitly when you want to override the key (e.g. `Ab` in F major).

### Durations inside notes

| Symbol | Meaning | Status |
|--------|---------|--------|
| `W` | Whole | **YES** |
| `H` | Half | **YES** |
| `Q` | Quarter | **YES** |
| `I` | Eighth | **YES** |
| `.` | Dotted (×1.5) | **YES** |
| `X` | Sixteenth | **DEFERRED** (D4 — not v1) |
| `Y` | Thirty-second | **DEFERRED** (D4 — not v1) |

### Duty inside notes (**YES**)

| Symbol | Meaning |
|--------|---------|
| `_` | Legato (full note sounding) |
| `!` | Staccato |
| `;` | Normal (6/8 of note time) |
| `;n` | n of 8 quanta sounding (n = 0…8) |

Duty affects **sound vs gap** within the note’s time slot; default legato **8/8**.

---

## Top-level executives (column 0)

These start a new statement at the top level (after whitespace), not inside a note suffix.

| Lead | Syntax | Purpose | Status |
|------|--------|---------|--------|
| **`A`–`G`** | note cluster | Play pitch | **YES** |
| **`R`** | rest cluster | Silence | **YES** |
| **`T`** | `T120` | Tempo BPM (1…240) | **YES** |
| **`%`** | `%Q` `%W` `%H` `%I` | Which note value = **one beat** (not a time signature) | **YES** |
| **`O`** | `O4` | Default octave in note memory | **YES** |
| **`^` / `v`** | | Octave up / down one (clamped 1…8) | **YES** |
| **`?`** | `?"text"` or bare `?` | **Lyrics / spoken text** at playback time (UART); also trace. C escapes; bare `?` → CRLF | **YES** |
| **`[` / `]`** | `[ … ]:N` | Repeat block **N** times | **YES** (see caveat below) |
| **`*`** | | END — stop playback | **YES** (NUL at EOF = implicit `*`) |
| **`@`** | `@ … @` | Comment block (skipped) | **YES** |
| **`L`** | `L"…"` | External library (future) | **PARTIAL** — warn + skip quoted payload |
| **`~`** | | Repeat **last completed note** | **YES** |
| **`K`** | `K"C"` | Key signature | **YES** |
| **`&`** | `&+2` `&-3` `&0` | Transpose semitones | **YES** |
| **`V`** | `V80` | Volume 0…100 | **YES** |
| **`P`** | `P0` / `P1` | Voice/timbre: **0** sine · **1** triangle | **YES** |
| **`\`** | `\"ctx:4Q"` | Extension / context load | **YES** (`ctx:` memory · `noop:`/unknown echo) |
| **`<` / `>`** | `<"lbl"` `>"lbl"` | Label define / goto | **PARTIAL** — **G4** pre-parse + table ✅; runtime PC jump **NO** (**G5**) |
| **`=` / `/`** | `="name"` `/` | GOSUB / RETURN | **NO** (**G5**) |
| **`:`** | | Optional statement terminator (D12) | **NO** — stray `:` warns |

### Repeat blocks — caveat (**YES** with spec drift)

Syntax: `[ body ]:N` (e.g. `[CQDQEQ]:4`).

- **YES:** open `[`, close `]:N`, loop body, nested depth limit.
- **Spec drift:** on `]` re-entry, the **`[` snapshot is not restored** — mutations inside the loop **persist** across iterations (plan amended 2026-06-13). Put **`^`** / **`O`** changes where you intend them.

---

## Comments & optional title

```text
?"My Song Title\r\n"
@ rehearsal note — not shown @
T132
CQ4DEFGABC5 *
```

- **`@ … @`**: skipped during playback (**YES**) — comments only, never executed.
- **Score title:** optional **`?"Song Title\r\n"`** at the start (**YES** — same as lyrics/trace, **D14**). No special `@` title capture (**D10** withdrawn).
- **Literal `@` inside comment (`\@`):** **YES** — escaped `@` does not close the block.

---

## END (`*`) semantics

- At **root**: **`*`** = hard stop (ends sequence).
- **NUL** at end of string = implicit **`*`** (**YES**).
- Inside future **`=`** subroutines: **`*`** may mean RETURN when `b_stop_is_return` — **NO** (GOSUB not shipped).

---

## Error handling (**YES** — S7i)

| Policy | Recoverable error | Use |
|--------|-------------------|-----|
| **NORMAL** (default) | `PLAY warn:` + continue | Bench |
| **STRICT** | warn → **fatal stop** | Golden tests |
| **LAZY** | silent skip | Low-noise |

**Fatals always stop** (bad tempo, unclosed `@`, repeat stack overflow, etc.) in every mode.

**Multi-digit cap (S7j):** executives reading numeric runs (`T`, `V`, `P`, `&±n`, `N`, `[ ]:N`, `;n`, …) accept at most **5** ASCII digits, stored as **uint16/int16**. More than 5 → **WARN** (skip excess digits, use first 5) in NORMAL/LAZY; **fatal** in STRICT. Per-command range limits apply after that (e.g. `T≤240`, `V≤100`).

**Player verbosity (I11 — not yet in firmware):** cumulative log level `SILENT` → `ERROR` → `WARN` → `INFO` → `DEBUG`. Controls interpreter diagnostics (`PLAY warn`/`fault`, lifecycle lines) — **not** score-directed `?"…"` output (lyrics always print, even in SILENT).

---

## Copy-paste examples that work TODAY

**Smoke scale (bench preset):**

```text
@ smoke scale @ CQ4DEFGABC5 *
```

**Compact run + tempo + beat unit:**

```text
@ demo @ T120 O4 %Q C4Q DEFGAB C5Q *
```

**Loop with octave step (from bench loop preset):**

```text
@ loop @ T120 O1 [CQDQEQFQGQAQBQ ?"Next octave\r\n" ^]:8 *
```

**Eighth-note figure with rests and ~ (v1-safe):**

```text
@ rhythm @ T132 O4 %Q RI. C4I E G C4Q G4I E G A4Q ~
```

**Tilde smoke:**

```text
@ tilde @ C4Q ~ ~ *
```

**Explicit everything (always safe):**

```text
T120 O4
C4Q D4Q E4Q F4Q G4Q A4Q B4Q C5Q *
```

---

## Do NOT generate these (common LLM mistakes)

| Bad pattern | Why |
|-------------|-----|
| `R I.` with spaces | Use **`RI.`** — one rest token |
| `X` / `Y` durations | **DEFERRED** — use `I` or `Q` |
| `C#4` / `Fb3` without key context | Use **`K"…"`** or explicit accidentals in each note cluster |
| `K"C"` then bare letters in that key | **YES** — LUT applies until next valid `K"…"` |
| lowercase `c4q` | Invalid — **uppercase letters only** |
| MML / ABC / JSON / MIDI | Wrong language — PLAY only |
| Spaces required between notes | Optional — `CDEFGAB` is valid |

---

## NOT IMPLEMENTED (v1 spec exists; firmware missing)

Group checklist for authors and chatbots — **do not rely on these in scores meant to run on current firmware:**

**Pitch & memory meta**

- ~~`N<n>` absolute semitone~~ — **shipped** (OOR salvage; `~` replays absolute path)

**Mix & timbre**

- ~~`V<n>` volume executive~~ — **shipped**  
- ~~`P<n>` voice selection~~ — **shipped** (`P0` sine, `P1` triangle)  

**Structure**

- `<` / `>` labels and gotos — **pre-parse table + ref resolve shipped (G4)**; **runtime PC jump still NO (G5)**  
- `=` GOSUB / `/` RETURN — **still NO (G5)**  
- ~~Startup label pre-scan (S7d)~~ — **shipped (G4)**  
- `:` as statement terminator  

**Extensions**

- ~~`\"cmd:args"` expansion dispatch~~ — **shipped** (`ctx:` zero-time suffix · default echo for `noop:` / unknown)
- `L"…"` library call (reserved; warn + skip)  

**Comment polish**

- ~~`\@` inside comments~~ — **shipped**

---

## DEFERRED (not v1 — not “missing firmware bugs”)

- **`X` / `Y`** sixteenth / thirty-second note durations (D4)  
- **Tuplets** / triplet syntax (D15) — approximate with even `I`/`Q`  
- **Polyphony** / inline chords / `|"` sync (S3)  
- **LittleFS / file loader** on device (I7) — host feeds string via UART today  
- **VIB / TRM / ADSR** PLAY syntax (D13)  

---

## Bench / host feeding (optional context)

On the G474 debug build, a **4096-character** PLAY string can be fed from the host:

1. Unwind to main menu (ESC×3).  
2. Top-level **`S`** → `PLAY>` prompt.  
3. Send body in **short UART bursts** (16 chars, ~20 ms gap) until CR.  

See `scripts/play_bench.py` and `/playstr` skill. This is **transport**, not part of the language grammar.

---

## Related documents

| Doc | Role |
|-----|------|
| [play-lead-char-cheat-sheet.md](play-lead-char-cheat-sheet.md) | One-screen lead-char reference |
| [play-v1-implementation-plan.md](play-v1-implementation-plan.md) | Full decision log + **The Big Board** + **§ MSG** (Must-Ship Gap) |
| [PLAY_language_design.md](../PLAY_language_design.md) | Original language design (being trimmed for v1) |

---

**Maintenance rule:** When merging PLAY firmware work, update the **Status** columns and **Last updated** stamp here in the **same PR** as `play.c` changes. When in doubt, grep `App/Src/play.c` for the lead character in `b_play_exec_next` / `b_play_parse_pitch_token`.
