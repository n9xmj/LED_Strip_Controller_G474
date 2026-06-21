# PLAY v1 — Chatbot & author brief

**Purpose:** Copy-paste this file into ChatGPT, Grok, Google Gemini, Claude, etc. when you want a **correct, bounded** mental model of the PLAY music language — more detail than the [cheat sheet](cheat_sheet.md), far less than the [implementation plan](../planning/play-v1-implementation-plan.md) or [legacy design notebook](../PLAY_language_design.md).

**Scope (G474):** Documents **what works today** on the bench MCU. **v1 + v1.1 required firmware (G1–G10) is complete** (2026-06-14). **v2+** (polyphony, loaders, richer synth) is planned for a likely **STM32H7** fork — out of scope here.

**Living document:** Update whenever `App/Src/play.c` gains or loses behavior. **Firmware truth:** `App/Src/play.c` + bench presets in `App/Src/play_presets.c`.

**Last updated:** 2026-06-14 (audited against firmware — **G10** `;nn` percent duty; **G9** X/Y; **G8** key LUT in snapshots; **G5** labels/GOSUB; **G4** pre-parse)

**Suite hub:** [README.md](README.md)

---

## What PLAY is

PLAY is a **single-line, monophonic** music macro language for embedded firmware. One ASCII string → one note at a time → sine/triangle output (today). There is **no AST** in RAM: a streaming parser walks the string and schedules notes on a 1 ms tick.

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

- Duration (`W` `H` `Q` `I` `X` `Y` + optional dot)
- Octave digit (`0`–`8`)
- Duty (`_` `!` `;` `;n` `;nn`)

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
| **`N60`** absolute MIDI-like semitone | **YES** — `N604Q` = semitone 604 + suffix order-flex (**D22**) |

**Bare vs explicit accidental (locked policy — see implementation plan *Pitch resolve pipeline*):**

- **Bare letter** (no `#`/`+`/`b`/`-`/`n` in the note cluster): apply **`K"…"` key LUT** per letter (default C major = all naturals).
- **Explicit accidental** in the cluster: **ignore key signature**; use only the requested sharp/flat/natural for that token.
- **`n`** = natural pitch class of the letter (e.g. in B♭ major: bare `E` → E♭; `En` → E natural).
- Accidentals **do not inherit** from the previous note.

**Authoring:** use **`K"Db"`** / **`K"F#m"`** etc. for scored keys; spell accidentals explicitly when you want to override the key (e.g. `Ab` in F major).

### Durations inside notes

| Symbol | Meaning | Status |
|--------|---------|--------|
| `W` | Whole | **YES** |
| `H` | Half | **YES** |
| `Q` | Quarter | **YES** |
| `I` | Eighth | **YES** |
| `.` | Dotted (×1.5) | **YES** |
| `X` | Sixteenth | **YES** (v1.1 **G9**) |
| `Y` | Thirty-second | **YES** (v1.1 **G9**) |

### Duty inside notes (**YES**)

| Symbol | Meaning |
|--------|---------|
| `_` | Legato (full note sounding) |
| `!` | Staccato |
| `;` | Normal (6/8 of note time) |
| `;n` | n of 8 quanta sounding (n = 0…8) — **D5c** (`;6` = 75%) |
| `;nn` | Raw percent 0–100 (two digits) — **D5b** / **G10** (`;60` = 60%, distinct from `;6`) |

Duty affects **sound vs gap** within the note’s time slot; default legato **8/8**.

---

## Top-level executives (column 0)

These start a new statement at the top level (after whitespace), not inside a note suffix.

| Lead | Syntax | Purpose | Status |
|------|--------|---------|--------|
| **`A`–`G`** | note cluster | Play pitch | **YES** |
| **`R`** | rest cluster | Silence | **YES** |
| **`N`** | `N60Q` … | Absolute semitone pitch | **YES** |
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
| **`<` / `>`** | `<"lbl"` `>"lbl"` / `<n` `>n` | Label define (no-op skip) / goto (pure PC jump, carries ctx) | **YES** |
| **`=` / `/`** | `="name"` `/` | GOSUB / RETURN (caller snapshot restore) | **YES** — empty `/` stack → **fatal** |
| **`:`** | `T120:C4Q` | Optional statement terminator (**D12**) | **PARTIAL** — stray `:` warns; full EOS not wired |

### Repeat blocks (**YES** — S4 snapshot restore)

Syntax: `[ body ]:N` (e.g. `[CQDQEQ]:4`).

- **YES:** open `[` saves ctx snapshot (incl. key LUT); close `]:N`, loop body, nested depth limit.
- **S4:** on `]` re-entry when iterations remain, **`[` snapshot is restored** then PC jumps to body start — mutations from the prior pass are undone (**G8**).
- **Goto (`>`):** pure PC jump — **no** snapshot save/restore (S2 revised 2026-06-14). Backward goto loops accumulate context; use `[ ]:N` for per-iteration reset.

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
- Inside **`=`** subroutines: **`*`** = hard stop at root; callee **`/`** returns to caller (**YES**, **G5**). **`b_stop_is_return`** (NUL/`*` = return in library tunes) — **NO** (D23 deferred).

---

## Error handling (**YES** — S7i)

| Policy | Recoverable error | Use |
|--------|-------------------|-----|
| **NORMAL** (default) | `PLAY warn:` + continue | Bench |
| **STRICT** | warn → **fatal stop** | Golden tests |
| **LAZY** | silent skip | Low-noise |

**Fatals always stop** (bad tempo, unclosed `@`, repeat stack overflow, etc.) in every mode.

**Multi-digit cap (S7j):** executives reading numeric runs (`T`, `V`, `P`, `&±n`, `N`, `[ ]:N`, …) accept at most **5** ASCII digits, stored as **uint16/int16**. Duty `;` suffix uses a **2-digit cap** (**G10** / **D5b**). More than 5 (executive) or 3rd digit after `;nn` → **WARN** (skip excess digits, use first 5) in NORMAL/LAZY; **fatal** in STRICT. Per-command range limits apply after that (e.g. `T≤240`, `V≤100`).

---

## Copy-paste examples that work TODAY

**Smoke scale (bench preset):**

```text
@ smoke scale @ CQ4DEFGABC5 *
```

**Duty disambiguation (G10):**

```text
@ duty @ T120 O4 %Q C4Q;6 C4Q;60 *
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

---

## Do NOT generate these (common LLM mistakes)

| Bad pattern | Why |
|-------------|-----|
| `R I.` with spaces | Use **`RI.`** — one rest token |
| `;6` same as `;60` | **`;6`** = 75% (6/8); **`;60`** = 60% — two digits = percent |
| `C#4` / `Fb3` without key context | Use **`K"…"`** or explicit accidentals in each note cluster |
| lowercase `c4q` | Invalid — **uppercase letters only** |
| MML / ABC / JSON / MIDI | Wrong language — PLAY only |
| Spaces required between notes | Optional — `CDEFGAB` is valid |

---

## Gaps (do not rely on these yet)

| Feature | Status | Notes |
|---------|--------|-------|
| **`:` as D12 EOS** | **PARTIAL** | Stray `:` → warn; `T120:C4Q` full EOS not implemented |
| **`L"…"` library** | **PARTIAL** | Warn + skip payload (**D23** deferred) |
| **`b_stop_is_return`** | **NO** | `*` always hard END at root in **`=`** callees (**D23**) |

---

## DEFERRED (not v1 — not “missing firmware bugs”)

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
| [README.md](README.md) | Doc suite hub |
| [cheat_sheet.md](cheat_sheet.md) | One-screen lead-char reference |
| [play-v1-implementation-plan.md](../planning/play-v1-implementation-plan.md) | Full decision log + **The Big Board** + **§ MSG** |
| [PLAY_language_design.md](../PLAY_language_design.md) | Legacy design notebook (implementer history) |
| `v1_grammar.md` | Normative EBNF *(planned T4)* |
| `howto.md` | Musician howto *(planned T5)* |

---

**Maintenance rule:** When merging PLAY firmware work, update the **Status** columns and **Last updated** stamp here in the **same PR** as `play.c` changes. When in doubt, grep `App/Src/play.c` for the lead character in `b_play_exec_next` / `b_play_parse_pitch_token`.
