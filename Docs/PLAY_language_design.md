# PLAY Meta-Language Design (Player-Piano / Sequencer)

> **Implementation readiness:** open decisions are tracked in the living plan
> [`Docs/planning/play-v1-implementation-plan.md`](planning/play-v1-implementation-plan.md)
> (resolve by ID in chat: D1, S2, I1, …). Planning workflow:
> [`Docs/planning/decision-log-model.md`](planning/decision-log-model.md).

**Status:** Early preview / evolving → **v1 contract in progress** (see plan above).  
**Owner:** User (full spec coming).  
**Current driver:** `tools/play_melody.py` (host-side, drives the terminal note player over serial).  
**Recent progress (as of this session):** CORDIC-based sine synth now has linear attack (~7 ms) + decay (~4 ms) envelope + special fast transition release logic (3 ms) for clean note changes in the live 'p' player (no more audible pops on key-to-key or octave shifts). Hot-swap of tones without stopping the I2S stream. Linear envelopes noted as acceptable for de-popping; proper ADSR will want exponential/log ramps later.  
**Future target:** On-device interpreter that can load strings from LittleFS (SPI NOR flash) or FAT32 (SD card) and drive the `synth_engine` autonomously (extending it for multiple waveforms).

---

**Handoff / Current State Summary (as of late June 2026)**

This document is the single source of truth for the PLAY meta-language. All major design decisions from iterative sessions are captured here with "current thinking + wiggle room" language.

**Core model (solid, low risk of change):**
- Unified per-voice "note memory" (static struct) holding both note attributes and command-driven state (tempo, key, volume, transposition, instrument, note/rest ratio, etc.).
- Inheritance/default propagation is a deliberate win for compactness and parser simplicity.
- Lead characters (notes A–G and command leads) must be unique for immediate top-level dispatch. Metadata/suffix characters (durations W H Q I X Y, dot, `_` `!`, duty) may overlap because they only appear after a note lead.
- Note descriptors are deliberately order-flexible after the lead note letter (A5Q == AQ5 == A5!Q, etc.). Only the note letter is positionally invariant.
- Repeat-last-note is a **pure note** operation (punctuation char like `\`, `$`, `&`, etc. TBD). It copies the full prior note state ("the whole smash"), including accidentals. It is **not** a command repeater.
- Context snapshots of the full note memory (plus related state) are required at labels and repeat markers to handle jumps/loops without breaking inheritance.
- Low-level timing: A reasonably high-resolution shared tick is still assumed for multi-voice drift control, even in cooperative scheduling.

**Key syntax elements locked or strongly preferred:**
- K<root>[#|b][m] — Circle-of-Fifths key signature (with LUT generation).
- S{+|-}<semitones> — transposition (sign only meaningful here).
- U<duration> — beat unit (which note value = one beat).
- ^ / v (proposed) — octave inc/dec shorthands (supplemental to O<n>, valuable for transcription despite some redundancy with inheritance + direct octave-in-note).
- `_` / `!` — legato (100% duty) / staccato (very short duty, silence dominates, e.g. Nutcracker style). Ratio is inheritable; exact staccato value to be tuned by ear and made a defined constant (or runtime-settable).
- Note-repeat punctuation (TBD char) — full prior note replay including ratio/accidental.

**Open / high-wiggle items (explicitly called out in the doc):**
- Exact instrument command lead character (still TBD; must be a non-colliding lead char).
- Down character for octave shorthand (v proposed; alternatives acceptable).
- Precise staccato duty ratio (tune by ear; constant or runtime).
- Full tuplet syntax, ties, more articulation, ornaments, rit/accel curves, etc. (listed in Future expansions).
- Exact multi-voice conductor implementation details and low-level tick fencing.
- Compiled binary event format (text remains primary for authoring/storage).

**Related artifacts for hand-off:**
- This file (PLAY_language_design.md) — primary.
- co5ths_key_signature_handoff.md — theory/LUT details for K command (already incorporated).
- PROJECT.md — only high-level references + pointer to this doc (per explicit prior instruction).
- Interactive noteplayer spec.txt — legacy terminal piano (precursor).
- Current synth_engine + note_player implementation (CORDIC direct sine + linear envelope + note memory usage).

The document is intentionally written so another agent (or human) can pick it up with minimal re-derivation. All character decisions, parser constraints from prior constrained-platform experience, and the unified note-memory + snapshot model are here.

---

**Status:** Early preview / evolving.  
**Owner:** User (full spec coming).  
**Current driver:** `tools/play_melody.py` (host-side, drives the terminal note player over serial).  
**Recent progress (as of this session):** CORDIC-based sine synth now has linear attack (~7 ms) + decay (~4 ms) envelope + special fast transition release logic (3 ms) for clean note changes in the live 'p' player (no more audible pops on key-to-key or octave shifts). Hot-swap of tones without stopping the I2S stream. Linear envelopes noted as acceptable for de-popping; proper ADSR will want exponential/log ramps later.  
**Future target:** On-device interpreter that can load strings from LittleFS (SPI NOR flash) or FAT32 (SD card) and drive the `synth_engine` autonomously (extending it for multiple waveforms).

## Goals
- Compact, human-readable / human-writable notation for melodies and sequences.
- Easy to store in flash/SD (text or lightly compiled form).
- Low RAM footprint on the STM32 (especially important on G474 with 128 KB SRAM).
- Extensible for tempo, articulation (staccato/legato), ADSR, repeats, key signatures, etc.
- Bridge to the existing CORDIC-based synth engine and the interactive note player.
- Synergizes with:
  - External storage (SPI flash + SD) for large pattern/sequence libraries.
  - Future ESP32 coprocessor (networked pattern upload / MQTT control).
  - USB device (CDC/HID for uploading .play files).
  - RTOS jobs (non-blocking playback).

## Current Syntax (Preview)
```
<note|command>{<accidental>}{<octave>}{<duration>{<dot>}}[{<modifier>}{<duty>}]
```

**Note on note descriptors**: After the required note letter, the remaining elements (accidental, octave, duration, dot, modifier, duty) may appear in **any order**. Only the note letter itself is invariant and must come first. Examples: `A5Q`, `AQ5`, `A#5Q`, `A5Q!` are all equivalent (A, octave 5, quarter, staccato). A standard suggested order is note + accidental + octave + duration + dot + modifier + duty, but the parser must accept any permutation for convenience (especially when transcribing sheet music).

**Notes**
- Letters: C D E F G A B (case-insensitive for parsing; see aliases and disambiguation below).
- Octave digit (optional): e.g. `C5` means C in octave 5. If omitted, use current/default octave (default 4, or set via `O` command).
- Accidentals (optional): `#` or `+` (sharp), `b` or `-` (flat).

**Durations** (current mapping)
- `W` Whole (4 beats)
- `H` Half (2)
- `Q` Quarter (1)
- `I` Eighth / 1/8 (0.5)   ← chosen because `E` conflicts with note E
- `X` Sixteenth / 1/16 (0.25) — tentative
- `Y` 1/32 (0.125) — tentative

**Dot**
- `.` after a duration specifies note duration * 0.5 (i.e. adds half the base duration, total 1.5×).
  - Example: `CQ.` = dotted quarter-note = 1.5 × duration of quarter note.

**Articulation Modifiers (note descriptors)**
- These are note modifiers attached after the duration (and dot if present).
- `_` legato (play a longer portion of the duration).
- `!` staccato (play a shorter portion of the duration).

**Duty Cycle / Note-Rest Duration (note descriptors)**
- Direct specification of the "pulse width" or duty (note play time vs. rest/silence time within the nominal duration).
- Specified most likely as a percentage or n/8 value, where n is the note play time over 8, modified by the duration spec.
- Example: `C4Q!4/8` (staccato quarter playing only 4/8 of the beat) or `C4Q_75%` (legato quarter playing 75% of the duration).
- The duty applies to the effective duration (after dot).
- If omitted for a note, fall back to the current global note/rest ratio (which can be set by a future global command like `DUTY <n/8>` or similar, or a default such as 80/20 for legato-ish playing).
- This provides precise control for the sequencer beyond the simple `_` / `!` modifiers.
- In the on-device interpreter, this directly controls how long `set_tone` is active vs. silence within each note's allocated time (scaled by tempo). For the current terminal note player (host-driven), it would map to how long to hold the key vs. send space.

**Note on Global vs. Per-Note**
- The modifiers `_` (legato) and `!` (staccato) update the current inheritable note/rest ratio in the note memory (legato → 100% duty, staccato → very short duty so silence occupies most of the timing window, as in the Nutcracker). They are "per-note" in that they are attached to a specific note descriptor, but the resulting ratio becomes the new default (sticky) for subsequent notes.
- A future global duty command can be added to set the default ratio for the voice/sequence without attaching it to a particular note.
- The exact staccato duty ratio will be tuned by listening on the hardware and will be implemented as a defined constant (or exposed as runtime-settable).

**Character Allocation Guidelines (Living — Subject to Refinement)**

The design aims to strike a practical balance between:
- Compact, readable notation that feels natural to how musicians think and write (small note designators, inheritance of common attributes).
- Ease of implementation for a streaming, low-RAM, char-by-char parser on the G474 (minimal lookahead, clear token boundaries, low ambiguity).

**Refined uniqueness rule (one-degree relaxation)**:
- **Lead characters** (the first character of a top-level token — either a note or a command) must be unique. When the parser sees the first character, it must immediately know whether it is starting a note descriptor or a command. This is the critical disambiguation point.
- **Metadata / suffix characters** (duration letters W H Q I X Y, dot, articulation modifiers `_` `!`, duty specifiers, etc.) may overlap with lead characters of other categories. These characters are only ever encountered *after* a note lead character (A–G) has already been seen, so the parser is already in "note descriptor" context. They never appear standalone at the start of a token.

This distinction keeps the common case (dense sequences of notes) clean and fast to parse while still allowing the overall alphabet to be used efficiently.

Current command lead characters: R (rest), T (tempo), O (octave), ^ (octave up), v (octave down), K (key), S (shift/transpose), U (beat unit), V (volume), P (voice/timbre), **? (print — lyrics + trace)**. **Multi-char metas (exceptions):** `@ … @` comment blocks and **`K"…"`** quoted key args (D8b).

The instrument/waveform command lead character remains TBD (user has not specified one). It must be a lead character that does not collide with existing note leads (C D E F G A B) or current command leads. Punctuation characters are also viable for future features such as "repeat last note" (see Inheritance Model below). Do not use 'A' for the instrument command (conflicts with note A).

## Character Allocation Summary (Living)

This living table summarizes all character selections made so far in the PLAY meta-language. It is intended as a quick reference and will be kept up to date as the spec evolves. For any command whose character is not yet finalized, the table marks it TBD. Punctuation characters are deliberately allocated for features that benefit from visual distinction from letter-based tokens.

**Note on categories**: "Lead" characters appear at the start of a token (notes or commands) and must be disjoint. "Metadata" characters (durations, modifiers, etc.) only appear after a note lead character and may overlap with lead characters of other categories per the guidelines above.

| Character(s)    | Category             | Brief Usage |
|-----------------|----------------------|-------------|
| C D E F G A B   | Notes                | Play the named pitch (case-insensitive) |
| W H Q I X Y     | Durations            | Whole (4 beats), Half (2), Quarter (1), ⅛ (I), ¹⁄₁₆ (X), ¹⁄₃₂ (Y) |
| .               | Dot                  | Dotted note (multiplies duration by 1.5) |
| _               | Articulation modifier| Legato: sets note/rest ratio to 100% duty (full note). Updates the current inheritable ratio. |
| !               | Articulation modifier| Staccato: sets note/rest ratio to a very short duty (~25% or less; most of the window is silence, as in the Nutcracker). Updates the current inheritable ratio. Exact value will be a tunable constant (or runtime-settable). |
| # +             | Accidentals          | Sharp (raise by one semitone) |
| b -             | Accidentals          | Flat (lower by one semitone) |
| R               | Command              | Rest using the duration syntax |
| T               | Command              | Set tempo in BPM (quarter note gets the beat) |
| O               | Command              | Set default octave for subsequent notes |
| ^               | Command (Octave Up)  | Increment current octave by 1. Slightly redundant with direct octave-in-note (C5) + inheritance, but highly valuable for visual/intuitive transcription of real sheet music. Supplements (does not replace) `O<n>`. |
| v (proposed)    | Command (Octave Down)| Decrement current octave by 1 (down character TBD; 'v' used successfully in prior implementation). Slightly redundant with inheritance + explicit octave in note specs, but very intuitive for transcription. Supplements `O<n>`. |
| K               | Command              | Set key signature — **`K<root>[#\|b\|+\|-][m]<WS>`** (unquoted + mandatory whitespace) **or** **`K"<keyspec>"`** (quoted; closing `"` ends token). Circle of Fifths LUT. Invalid `K` → WARNING, keep current key. Default C major. |
| S               | Command (Transposition / Shift) | Transpose subsequent pitches by semitones (S+1, S-2, S0). 'S' chosen to avoid conflict with duration 'X'. |
| V               | Command              | Set volume for the current voice (0–100, v1 plan D6) |
| U               | Command (Beat Unit)  | Set which note value gets one beat (e.g. UQ, UE). Affects duration interpretation relative to tempo. |
| ?               | Command              | **Print (v1):** **`?"…"`** — **lyrics / spoken text** at playback time (UART); also trace. C string escapes, no auto-CRLF; **`?""`** silent; **bare `?`** → CR/LF. No `printf` `%` formats. |
| P               | Command              | Voice / timbre selection (`P0` = sine default). See v1 plan D1/D11. |
| [ ] :           | Structural (Repeat)  | `[ sequence ]:N` — repeat the block N times (nestable) |
| *               | Structural (Label)   | `*<n>` — define a label for goto |
| >               | Structural (Goto)    | `><n>` — jump to the given label |
| + -             | Aliases              | Alternate forms for sharp/flat (inside note descriptors) |
| (space)         | Whitespace           | Optional separator for token disambiguation |
| \ $ & ... (TBD punctuation) | Note Repeat Trigger | (Proposed) Pure **note** repeat (not a command repeat). Repeats the immediately prior note, inheriting the full note memory including accidental + current legato/staccato ratio (100% or ~25% duty), etc. ("the whole smash"). Example characters: `\`, `$`, `&`, or similar punctuation. |

**Sub-syntax notes**
- Inside note descriptors: `D<n>/8` or `<n>%` for explicit duty cycle (after modifier). These are metadata characters and may overlap with command lead characters (see Character Allocation Guidelines).
- Future commands (dynamics, fades, ADSR, effects, time signature details) may use additional characters or multi-character tokens; any new single-char commands must use a lead character that does not collide with existing note leads or command leads.

**Rests**
- `R<duration-spec>` — uses the same duration syntax as notes (W/H/Q/I/X/Y + optional dot). The parser may accept a full note descriptor and simply ignore the note/accidental/octave parts (code reuse). Rests are always full silence for the duration (no articulation applies).

**Commands**
- `T<1-3 digit decimal>` — set tempo in BPM. Assume 4/4 timing for now (quarter note gets one beat). Affects all subsequent duration calculations.
- `O<1-digit>` — set default octave (1-8 or 0-7 per convention). Subsequent notes without an explicit octave use this. (Note that the `^` / `v` shorthands and the ability to put an octave digit directly on a note also affect this value; see octave shorthands below.)
- `^` — octave up shorthand: increment the current octave by 1. Slightly redundant given the ability to specify octave directly in a note (C5Q) and the improved inheritance/defaults model, but the visual/intuitive aspect makes it very useful when transcribing real-world sheet music. Supplements (does not replace) the full `O<n>` command. Directly updates the current octave in the note memory.
- `v` (proposed) — octave down shorthand: decrement the current octave by 1 (down character is TBD; lowercase 'v' was used successfully in a prior implementation). Slightly redundant with inheritance + explicit octave-in-note, but highly intuitive for transcription work. Supplements `O<n>`. Directly updates the current octave in the note memory.
- `K<root>[#|b|+|-][m]<WS>` — set key signature using Circle of Fifths (e.g. `KC `, `KG `, `KDb `, `KD-m `, `KF#m `). **Unquoted form requires mandatory whitespace** after the key token (space, CR, LF, or TAB). Sets the default accidentals for subsequent bare notes. Minor keys derive the relative major signature (root + 3 semitones). Invalid syntax → **WARNING**, current key unchanged; default at sequence start is **C major**.
- `K"<keyspec>"` — **quoted alternate** for the same key grammar inside ASCII double quotes (e.g. `K"C"`, `K"Db"`, `K"F#m"`). The closing **`"`** is an unambiguous token boundary — **`K"Db"C4Q`** is valid without a space after the key. **`\"`** escapes a literal quote inside the payload. Unquoted and quoted forms coexist.
- `?"<text>"` — **print (v1):** at runtime, emit the **expanded** string to the debug UART (host tools should apply the same rules). **Primary use:** **spoken lyrics** interleaved with notes — text appears in near-real-time as playback reaches each token. **Also:** author trace. Single-char lead **`?`** (BASIC **`PRINT`** shorthand). Payload uses **C character escapes** (`\n`, `\r`, `\t`, `\"`, `\\`, `\0`, `\a`, `\b`, `\f`, `\v`; optional `\xHH` hex and `\ooo` octal). **Not `printf`** — no `%` conversion specifiers (deferred). **No auto-CRLF** after quoted output; **`?""`** emits nothing. **Bare `?`** (not followed by `"`) emits **CR/LF (`\r\n`)**. Unterminated quote or garbage after closing `"` → **WARNING**, continue. Max **64 source chars** inside quotes (before expansion).
- `S{+|-}<semitones>` — transpose/shift (e.g. `S+1`, `S-2`, `S0`). Applies a global semitone shift to all subsequent pitches after key-signature accidentals have been applied. Can be changed mid-sequence. 'S' (Shift) chosen instead of 'X' to avoid collision with the duration specifier 'X' (sixteenth note).
- `U<duration>` — set beat unit (e.g. `UQ` = quarter note gets one beat, `UE` = eighth note gets one beat). This tells the player which duration value corresponds to one beat for the current tempo. Affects all subsequent duration calculations. Full time signatures (e.g. 4/4, 3/8) are primarily for human readability or authoring tools; the runtime primarily cares about the beat unit + tempo.
- Clef is not required as a runtime command (the combination of explicit octaves + note letters is sufficient). It may be useful only for human-readable score export or authoring tools.
- `V<n>` — set volume (0–127 or 0–100 scale TBD; maps to the `level` passed to the synth engine).
- `P<n>` — voice / timbre selection (`P0` = sine default). See v1 plan D1/D11. Replaces earlier TBD instrument command.
- `R<duration-spec>` — rest (see above).
- `[ <sequence> ]:<repeat-count>` — repeat the enclosed sequence `<repeat-count>` times (0 = no repeat, 1 = one repeat, etc.). Nestable (reasonable depth limit ~10 stack levels for implementation).
- `*<n>` defines a goto-label (n may be a number, perhaps > 9; some limit imposed on upper value, TBD; stored in a small label table).
- `><n>` Goto label <n>. Supports loops, codas, da capo-style constructs, jumping out of or into repeats, etc.
- `+` and `-` — aliases for `#` (sharp) and `b` (flat) inside note descriptors.
- Future expansions:
  - Dynamics and fades (e.g. `D<level>` or `F<in/out>` commands, or per-note modifiers). Common sheet-music markings: p pp mp mf f ff, sfz, fp, crescendo, diminuendo.
  - Effects: vibrato (`VIB<rate><depth>`), tremolo (`TRM<rate><depth>`), etc. These would modulate frequency (vibrato) or amplitude (tremolo) over time in the synth engine.
  - ADSR envelope configuration (e.g. `ADSR a d s r` with times or rates; applied per-note or as current voice state when calling the tone generator).
  - Per-voice instrument / waveform changes via the instrument command (see above; character TBD per the Character Allocation Guidelines). Note-descriptor extensions for per-note articulation/velocity/etc. are still TBD.
  - Additional common sheet-music elements: ties (e.g. `C4Q~` or duration carry), fermata, ritardando/accelerando (tempo curves), tuplets, basic ornaments/grace notes. Measure/bar lines are primarily for source readability and may be ignored at runtime.

**Inheritance Model / Default Propagation (Current Thinking — Wiggle Room Explicitly Reserved)**

The on-device player maintains a unified "note memory" (a small per-voice struct, sometimes called the static note characteristics struct). This single struct holds **both**:

- Traditional note attributes (duration, octave, accidental state, legato/staccato, note/rest ratio, etc.)
- Command-driven characteristics that musicians naturally expect to persist across subsequent notes (tempo, key signature, volume, transposition, instrument/waveform selection, etc.)

When a note descriptor is successfully parsed, any attributes or modifiers it explicitly provides update the note memory. Subsequent notes inherit from the current note memory for any characteristic not explicitly overridden.

**Rationale for a single unified note memory**:
A musician reasonably assumes that once a setting like key signature, tempo, volume, or voice/instrument has been established by a command, it applies to all following notes until an explicit command changes it. Keeping all this metadata together in one note memory struct (with copied snapshots as needed for jumps/loops) produces a simple, intuitive model for both authoring and implementation.

**Currently envisioned inheritable characteristics** (subject to change as the parser and player are implemented):
- Duration (W/H/Q/I/X/Y + dot)
- Octave (set via `O<n>`, `^` / `v` shorthands, or directly in a note like `C5`; all update the same current value)
- Volume (from V or future dynamics)
- Key signature (from K — affects default accidentals for bare notes)
- Transposition (from S)
- Note/rest ratio (from global duty command or per-note `_` / `!` modifiers). This is a core inheritable characteristic:
  - Legato (`_`) → 100% note duty (full duration, no rest)
  - Staccato (`!`) → very short note duty (on the order of 25% or less; silence occupies most of the timing window). A good musical example is the staccato writing in the Nutcracker suite. The exact note/silence ratio will be tuned by ear on the actual hardware and will be implemented as a defined constant (or made runtime-settable).
- Instrument/waveform selection (from future command)
- And other command-driven state as it is added (the exact list will be refined during implementation)

**Accidentals do not inherit by default.** Per note token:

- **Bare letter** (no `#` / `+` / `b` / `-` / `n` in the descriptor cluster): apply the current **K** key-signature LUT for that letter.
- **Explicit accidental** (any of `#` `+` `b` `-` `n` present in the cluster): **ignore key signature entirely**; resolve from letter + explicit accidental only. **`n`** requests the natural pitch class of the letter (e.g. bare `E` in B♭ major → E♭; `En` → E natural).

Full resolve order, linear absolute semitone, and **mod-12 vs transpose** rules: [play-v1-implementation-plan.md](planning/play-v1-implementation-plan.md) section **Pitch resolve pipeline** (after **D21**).

**Note-repeat idea (under consideration)**: A special punctuation character (examples: `\`, `$`, `&`, or other TBD punctuation — not a letter) means "play the immediately previous note again". This is strictly a **note** repeat — it does not repeat the last *command*. The token inherits almost all (or all) note context from the prior note, including:
- Duration, octave, accidental
- Legato/staccato (`_` / `!`), note/rest ratio, explicit duty
- Volume, transposition, key signature effects (as they apply to the note), instrument/waveform, etc.
("the whole smash" — any characteristic that makes sense to carry forward for a repeated note, drawn from the unified note memory).

Any inheritable characteristic that turns out not to make sense for a repeated note can be resolved at implementation planning time.

**Side effects with jumps/gotos/loops**: Because notes inherit from the current note memory, jumps ( `><n>` ), repeats (`[ ... ]:N`), and labels (`*<n>`) can have side effects on inherited context. To handle this cleanly, the implementation may need to associate **saved snapshots** of the full note memory (including both note-specific attributes and command-driven characteristics such as tempo, volume, key signature, etc.) with label definitions and repeat markers. When a jump or loop return occurs, the parser/player restores the note memory that was in effect at the point the label or repeat block was defined. This naturally leads to multiple "copied instances" of the note memory struct rather than relying on a single mutable global current state.

This model supports very compact writing while still allowing explicit overrides. Prior experience showed that a clean inheritance/defaults model was one of the highest-leverage decisions: it not only made the notation far more pleasant for the human (or script) author, it also significantly reduced work for the parser by letting it emit only what was explicitly present and rely on the maintained note memory for the rest.

The exact set of inheritable attributes, the scope (per-voice vs. global), the precise behavior of the repeat-last-note token, and the details of context saving/restoration for jumps are expected to be refined during implementation. The goal is to match "how musicians think" without making the streaming parser overly complex.

**For hand-off / next agent:** Start here. The "Handoff / Current State Summary" block at the very top + the Character Allocation Summary table + the Inheritance Model section give the fastest on-ramp. All prior design sessions (character rules, note memory + snapshots, flexible note-descriptor ordering, K/S/U commands, octave shorthands, staccato duty tuning, multi-voice timing concerns, etc.) are consolidated in this document. Related theory (e.g. Circle-of-Fifths LUT for K) lives in co5ths_key_signature_handoff.md and has already been folded in.

Example (from user):
```
CH G FQ E D C5H G4 ...
```
- C and G are both half-notes (duration propagates).
- FQ E D are all quarters (duration propagates).
- The final `G4` requires an explicit octave because `C5H` changed the default octave to 5.

**Whitespace / Disambiguation**
- Ideally, no spaces are required between note descriptors and commands (compact storage).
- Optional `<space>` (or other separators) can be inserted to disambiguate parsing, especially around single-letter items that could be confused (e.g., a flat 'b' vs. note 'B' when case-insensitive, or after a duration before the next letter).
- The "-" separator in examples is treated as a comment/ignorable separator.
- **K (key signature) — resolved (v1 plan D8/D8b):** use **mandatory whitespace** after unquoted key tokens (`KDb C4Q`), or the **quoted form** `K"Db"C4Q` when abutting the next token. Invalid `K` → WARNING + retain current key. See [play-v1-implementation-plan.md](planning/play-v1-implementation-plan.md) for full locked rules.

**Example (Star Wars intro theme, C major)**
```
CH GH FQ EQ DQ C5Q GH FQ EQ DQ C5H GH FQ EQ FQ DH
```

This is a tied-triplet / dotted-quarter feel on the FQ EQ DQ group (not pure quarters). Exact values can be adjusted with dots or future triplet syntax once defined.

### EBNF Grammar (Draft for Current Syntax)
This is a starting point for a formal grammar (using standard EBNF notation). It will be refined as the full spec (triplets, note-descriptor expansions for articulation/ADSR, exact key-sig and transposition encoding, etc.) is written.

```
play_sequence   = { element } ;
element         = note_descriptor | command | repeat_block | label_def | goto | comment_block | debug_print_cmd | whitespace ;
comment_block   = "@" { comment_char } "@" ;  (* skipped during playback except first block → title; see v1 plan D9/D10 *)
comment_char    = ? any char except unescaped "@" ? | "\\@" ;
debug_print_cmd = "?" [ c_quoted_string ] ;  (* absent → bare-? CRLF; empty "" → no output; else expanded C-string payload — D14 *)
c_quoted_string   = '"' { c_quoted_char } '"' ;
c_quoted_char     = ? any char except '"' and '\\' ? | "\\" escape_seq ;
escape_seq        = ( "n" | "r" | "t" | "0" | "a" | "b" | "f" | "v" | "\\" | "\"" | "'" | "?" | "x" hex_digit { hex_digit } | octal_digit { octal_digit } ) ;
quoted_string     = '"' { k_quoted_char } '"' ;  (* K key payload — minimal escapes only, D8b *)
k_quoted_char     = ? any char except '"' and '\\' ? | "\\" ( "\\" | "\"" ) ;
note_descriptor = note , { ( accidental | octave | duration [ dot ] [ modifier ] [ duty ] ) } ;  (* After the note letter, accidental, octave, duration+attachments may appear in any order. Only the note letter is required and must come first. *)
modifier        = "_" | "!" ;
duty            = ( "D" digits "/" "8" ) | ( digits "%" ) ;  (* proposed; e.g. D4/8 or 75%; TBD exact syntax per user: percentage or n/8 value, n=note play time over 8, modified by duration spec *)
note            = "C" | "D" | "E" | "F" | "G" | "A" | "B" ;
accidental      = "#" | "+" | "b" | "-" ;
octave          = digit ;
duration        = "W" | "H" | "Q" | "I" | "X" | "Y" ;
dot             = "." ;
modifier        = "_" | "!" ;
duty            = ( "D" digits "/" "8" ) | ( digits "%" ) ;  (* e.g. D4/8 or 50%; TBD exact prefix/syntax *)
command         = tempo_cmd | octave_cmd | octave_up_cmd | octave_down_cmd | key_cmd | transpose_cmd | beat_cmd | volume_cmd | voice_cmd | instrument_cmd | rest_cmd ;
tempo_cmd       = "T" digits ;
octave_cmd      = "O" digit ;  (* single-digit parser is sufficient and efficient *)
octave_up_cmd   = "^" ;        (* increment current octave by 1; shorthand for transcription *)
octave_down_cmd = "v" ;        (* decrement current octave by 1; supplements O<n> *)
key_cmd         = "K" root [ accidental ] [ "m" ] whitespace
                | "K" quoted_string ;  (* quoted payload: same root/acc/m grammar inside quotes *)
voice_cmd       = "P" digits ;  (* voice/timbre index; v1 plan D1 — default sine P0 *)
transpose_cmd   = "S" signed_digits ;  (* e.g. S+1, S-2, S0; semitone shift applied after key sig accidentals. 'S' for Shift, avoiding duration 'X' conflict. Only command where the sign is semantically meaningful. *)
beat_cmd        = "U" duration ;  (* set which note value gets one beat, e.g. UQ, UE. Affects duration-to-time mapping. *)
volume_cmd      = "V" number ;  (* full numeric (0-100 or 0-127 range); multi-digit parser required *)
instrument_cmd  = <TBD-char> ( digits | identifier ) ;  (* e.g. 0/sine, 1/tri, 2/saw, ... ; sets waveform for the voice. TBD-char must be unique per the Character Allocation Guidelines. *)
rest_cmd        = "R" duration [ dot ] ;
repeat_block    = "[" play_sequence "]" ":" repeat_count ;
repeat_count    = digits ;
label_def       = "*" label_id ;
goto            = ">" label_id ;
label_id        = digits ;   (* limit TBD, e.g. 0-255 or higher *)
digits          = digit { digit } ;
digit           = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
number          = digits [ "." digits ] ;   (* for volume etc. – variable-width numeric parser *)
signed_digits   = ( "+" | "-" )? digits ;   (* only semantically used by transposition; other commands may accept but ignore sign for robustness *)
whitespace      = { " " | "\t" | "\n" } ;   (* optional for compactness, useful for disambiguation *)
```

Notes on the grammar:
- The top-level is a sequence of elements (notes, commands, structural constructs).
- Note descriptors can appear standalone or inside rests (parser reuses the rule and discards non-duration fields).
- Label IDs are numeric for simplicity and compact storage; alphanumeric can be considered later.
- This grammar is intentionally loose on whitespace to support both compact (no spaces) and readable forms.
- Lead characters determine top-level token type (note vs. command). Metadata characters inside note descriptors (durations, dot, modifiers, duty) are only valid after a note lead character.
- Inside a note descriptor, after the leading note letter, the optional parts (accidental, octave, duration+dot+modifier+duty) may appear in any order. The parser for the "note sub-parser" must support this flexibility. A conventional order (note + accidental + octave + duration + dot + modifier + duty) is recommended for readability, but not required.

Future extensions (dynamics, effects, ADSR, triplets, full measure/time signature) will add productions under `command` or as modifiers inside `note_descriptor`.

### Expression, Dynamics, Effects, and Envelopes (Proposed Extensions)
These build on the existing volume command and note-descriptor expansion point:

- **Volume**: `V<n>` sets the current voice volume (0–127 or 0–100 scale; maps directly to the `level` argument of `set_tone`). Can be used mid-sequence for crescendos/diminuendos when combined with short notes or explicit timing.
- **Dynamics and Fades**: Commands such as `D<level>` (set dynamic, e.g. p, mp, mf, f, ff or numeric) or `F<direction><rate>` (fade in/out, e.g. crescendo, diminuendo). Common sheet-music markings to support in the future: p pp mp mf f ff, sfz (sforzando), fp. Or per-note modifiers inside the descriptor (e.g. `C4Q> ` for crescendo into the note). These adjust the effective volume over time or per event.
- **Effects**:
  - Vibrato: `VIB<rate><depth>` (modulates frequency around the base note; rate in Hz or cycles per beat, depth in cents or semitones). Applied by the tone generator (CORDIC phase or FM parameters in future expansions).
  - Tremolo: `TRM<rate><depth>` (modulates amplitude; similar parameters).
  - These can be global per voice or attached to specific notes/phrases.
- **ADSR Envelope Configuration**: `ADSR <attack> <decay> <sustain> <release>` (values in ms or normalized 0–1, or rates). Sets the current envelope for the voice. When a note starts, the synth engine applies the envelope to the level over the note's duration (with release on note-off or end of duration for rests). Can be changed mid-sequence for different articulations or instrument changes.
- These features will require corresponding support in the `synth_engine` (modulation paths for vibrato/tremolo, envelope generator in the fill callback or a service job). They fit the "future expansion inside the note descriptor" and global command model.

All of these are state updates in `play_state_t` (current_volume, vibrato_params, adsr_params, etc.) and affect how/when `set_tone` is called or how the fill callback generates samples for the current voice. They are "not-too-real-time-demanding" when pre-computed or updated at note boundaries, which aligns with streaming from storage.

**Parsing Rules (Important for Implementation)**
- Case-insensitive note letters for now (C/c both mean C), but watch the 'b'/'B' ambiguity when flats are in use — optional space or explicit context resolves it.
- Lead characters (first char of a token) immediately tell the parser whether it is starting a note descriptor or a command (see Character Allocation Guidelines). Metadata characters (durations W H Q I X Y, dot, `_` `!`, duty, etc.) only appear after a note lead character, so they are parsed in a different context and may overlap with command leads.
- Inside a note descriptor (after the lead note letter), the sub-parser must accept the optional elements (accidental, octave, duration, dot, modifier, duty) in **any order**. Only the note letter is required and positionally fixed as the first character of the descriptor. This flexibility is intentional for convenient transcription of sheet music (e.g. `A5Q` and `AQ5` and `A5!Q` must all be accepted and produce identical results). The note sub-parser should collect the parts and validate that exactly one duration appears.

**Numeric parsing strategies** (drawn from prior hand-written constrained implementations):
- Use a simple, fast "single-digit" parser for commands that are guaranteed to need only one digit (O for octave; historically V when hardware only supported a few volume levels).
- Use a full variable-width numeric parser (with optional decimal) for commands that need a wider range (T for tempo, V for volume in this project).
- A signed numeric parser is only required for transposition (S). Other numeric commands may accept an optional leading + or - for parser simplicity / robustness, but the sign should be ignored (or treated as an error) except for S.
- Reusing a common decimal-numeric subroutine across commands that need numbers is effective for code size. Reusing note-descriptor parsing logic for command constructs that only look superficially similar (e.g., key signature) is usually a source of complexity and should be avoided.
- The player maintains a small set of current note characteristics (duration, octave, volume, key signature, transposition, instrument, note/rest ratio, etc.). After parsing a note, any explicitly provided attributes or modifiers (including `_` / `!` for legato/staccato ratio) update the current set. Subsequent notes inherit current values for omitted attributes (see Inheritance Model section above for current list and wiggle room). Legato/staccato ratios (100% / ~25% duty) are explicitly part of the inheritable state.
- Accidentals never inherit implicitly. Bare notes use the current K key signature LUT; explicit accidentals in the cluster override key signature entirely (**Pitch resolve pipeline**).
- A future "repeat last note" punctuation token (TBD char such as `\`, `$`, etc.) repeats the immediately prior **note** (not a command). It inherits almost all note context from the prior note, including accidental, legato/staccato, note/rest ratio, and other inheritable characteristics ("the whole smash").
- Commands (T, O, K, S, R, [, *, >, etc.) are processed immediately and update parser/player state (including the current characteristics struct).
- Because jumps and loops can affect inherited note state, the implementation may associate saved snapshots of the note characteristics (plus related state such as tempo, volume, key signature) with labels and repeat markers. On jump/return the appropriate snapshot is restored.
- When parsing a bare note letter (no explicit accidental in the cluster), the current K key signature LUT supplies the default alteration for that letter. Any explicit `#`/`+`/`b`/`-`/`n` in the cluster **fully supersedes** the key signature for that note. Transposition (`&` per v1 plan; legacy `S` retired) is a **linear** semitone add on the resolved absolute pitch — **not** a `% 12` on the normal path. See **Pitch resolve pipeline** in the implementation plan.

**Code-reuse lesson from prior implementation**: Reusing the core note-descriptor parser for command constructs that look superficially similar (e.g., key signature sharing "note name + accidental" traits) created unnecessary complexity. Numeric parsing for commands (T, O, V, etc.) reused successfully. The current design deliberately keeps note descriptors and most commands syntactically distinct at the lead-character level to make clean reuse easier without forcing awkward shared sub-parsers.
- `*<n>` defines a goto-label: record the current source position (offset) in a small label table. n can be >9; a reasonable upper limit will be chosen (e.g. 256 or 1024) to keep the table small.
- `><n>` Goto label <n>: look up n in the label table and set the read position to the recorded offset (direct jump). Supports forward and backward jumps, and can be used to exit/enter repeat blocks.
- Repeats (`[ ... ]:N`) are structural: use a small runtime stack of (return position, remaining count). The parser does not need to fully unroll.
- The language should remain simple enough for a streaming, low-RAM interpreter on the G474 (no full AST in RAM). Label table is small and fixed-size (e.g. 256 entries). A cheap linear pre-scan on load populates the table so forward gotos work without backtracking.

## Host-Side Driver (Current)
`tools/play_melody.py` now includes `parse_play_string()` that:
- Tokenizes space-separated items.
- Parses note + optional accidental + optional octave digit + duration + dot.
- Emits events for the serial driver:
  - Octave changes via the note player's direct octave keys (`!@#$%^&*` for 1-8).
  - Note keys using the 1-8 whole-tone mapping (C=1 ... B=7, 8 = C next octave).
  - Proper timing via `time.sleep(duration * tempo)`.
- Supports the built-in `"star_wars"` melody using the exact string above.
- `--custom "CH GH ..."` or `--melody star_wars` work.

The driver still drives the **existing terminal note player** (sends key characters, the player sustains until the next key or space). This is perfect for rapid experimentation on the G474 while the on-device interpreter is developed.

Run examples:
```bash
python tools/play_melody.py --melody star_wars --tempo 0.85
python tools/play_melody.py --custom "CH GH FQ EQ DQ C5Q GH FQ EQ DQ C5H GH FQ EQ FQ DH"
```

## On-Device Interpretation Plan (C Side)

### High-Level Architecture
- A **sequence player** (job or cooperative task) that can:
  1. Load a PLAY string (from LittleFS file on SPI flash, FAT32 file on SD card, or in-memory buffer).
  2. Parse/interpret it on the fly (streaming parser to minimize RAM — important on G474's 128 KB).
  3. Drive `v_synth_engine_set_tone(freq_hz, level)` + schedule note-off / rest using a tick counter (existing 1 ms jobs or cooperative polling in `v_app_polling_task`).
  4. Handle global + per-sequence state: current tempo (BPM, quarter-note = beat), current octave (including ^ / v inc/dec shorthands), note/rest ratio (for staccato/legato), key signature (K / Circle of Fifths LUT), transposition (S), beat unit (U), ADSR envelope parameters (future). Note memory (for inheritance and repeat-last-note) plus saved context snapshots at labels/repeats may also be required.
- Non-blocking and cooperative: integrate with the existing job system (e.g. new `JOB_PLAY_TICK` or reuse `JOB_SYNTH_SERVICE`). The player yields frequently so LED strips, debug menu, and future RTOS tasks are not starved.
- A reasonably high-resolution shared tick (ideally sub-millisecond or at least a reliable 1 ms fence) is still assumed so that note submission times to the synth can be aligned across voices. Variable per-voice parse time plus tick uncertainty will otherwise cause gradual drift in the actual audio start times even when musical events are perfectly synchronized at the conductor level.
- Multiple voices later (polyphony / chords) by tracking several active notes + a simple mixer or round-robin in the synth fill callback.
- Storage synergy: PLAY strings (or a compiled compact event form) live on SPI NOR flash (via LittleFS — power-loss safe, low RAM) or SD card (via FatFs for easy PC authoring and drag-and-drop of .play files). The ESP32 coprocessor can push new sequences over UART/MQTT.

### Streaming Parser (low RAM + execution speed)
A simple state machine / recursive-descent parser that walks the string char-by-char without building a full token list or AST in RAM. This is critical for G474's 128 KB.

Past experience on far more constrained platforms (8-bit AVR, parsing + playback inside a ~1 kHz ISR) showed that the parser must also be *fast in execution*, not just small. At fast tempos, short notes (1/32, triplets), and with modifiers (staccato, legato, dotted, selectable note/rest ratio), the time to parse + resolve a note into a frequency + duration + hand-off to the synth can easily approach or exceed a 1 ms window. "Flight of the Bumblebee" territory quickly becomes the stress test. Even with the much more capable G474 and cooperative (non-ISR) scheduling, we still want the parser path to be lightweight and predictable so that multiple simultaneous voices do not accumulate noticeable timing skew.

1. Skip optional whitespace / separators (space, the '-' used as comment in examples). Spaces are *optional* for compactness but can be used to disambiguate (e.g. after a duration before a flat 'b' vs. note 'B').
2. Parse next token:
   - Note letter (C-G, A, B) + optional accidental (# / + / b / -) + optional octave digit + duration letter (W/H/Q/I/X/Y) + optional dot.
   - Or command: `R<dur>`, `T<1-3 digits>`, `O<1 digit>`, `^`, `v`, `V<number>`, `<inst-cmd><n>`, `K<root>[acc][m]`, `S{+|-}<n>`, `U<dur>`, `*<n>` (define label), `><n>` (goto), `VIB<rate><depth>`, `TRM<rate><depth>`, `ADSR ...`, or `[` (begin repeat block).
3. On `*n`: record current src position in the label table (array or small map of label_id → pos). Continue.
4. On `><n>`: look up n in the label table; if found, set pos to that position (jump). If forward goto and label not yet seen, the initial load pass should have pre-scanned all labels.
5. For repeats: on `[`, push (current position, repeat-count from `]:N`) onto a small fixed stack (max depth 10). On `]`, decrement and either jump back or pop.
6. Apply defaults / inheritance: Consult the player's current note characteristics struct. If the current token is a normal note descriptor, use inherited values for any omitted attributes (duration, octave, etc.). See the Inheritance Model section for the current list of inheritable characteristics and the proposed "repeat last note" punctuation token.
   - The repeat token is strictly for repeating a prior **note** (it does not repeat commands). It copies almost all note context from the previous note (the "whole smash"), including accidental + the current legato/staccato ratio (100% duty or very short/~25% or less so silence dominates, as in the Nutcracker), note/rest ratio, etc. The exact staccato ratio will be a defined constant (or runtime-settable).
7. Emit / execute action immediately (or record for the scheduler):
   - Compute frequency (reuse `f_noteplayer_calc_freq` + CORDIC path, or direct `set_tone`).
   - For volume/effects/ADSR/instrument commands: update the corresponding fields in `play_state_t` (e.g. current_instrument; the scheduler or tone generator will use it on the next set_tone / fill).
   - Schedule note-on now + note-off / rest after scaled duration (tempo * note/rest ratio for staccato/legato). Vibrato/tremolo are applied continuously by the tone generator while the note is sounding.
   - Update the current characteristics struct for T (tempo), O (default octave), ^ / v (octave inc/dec shorthands), K (key sig — build LUT), S (shift/transpose), R, V (volume), future instrument command, and any per-note modifiers.
   - Because of jumps/gotos and loops, saved snapshots of the note characteristics (plus related state such as tempo, volume, key signature) may need to be restored from labels or repeat markers so that inheritance reflects the context at the jump point. A repeat-last-note token copies the prior note's full resolved state.
8. For rests (`R...`): schedule silence for the duration; still propagate duration default to the next event.
9. Error handling: skip malformed tokens gracefully (log via debug printf or status) so a bad file doesn't hard-crash playback.

**Label handling note**: Because gotos can be forward, perform a cheap linear pre-scan of the entire string once when loading/starting a sequence to populate the full label table before any playback. Since music strings are small, this costs almost nothing and enables clean forward references. Limit on n (TBD by you, e.g. 0-255 or higher) keeps the table tiny (e.g. 256 * sizeof(size_t) ≈ 2 KB worst-case, or less with a sparse list). During normal playback `><n>` is just an O(1) position assignment.

This keeps RAM usage to a few dozen bytes of state (plus small label table and repeat stack) + the current read position in the (flash-mapped or buffered) string. No full expansion of repeats or gotos in memory. Volume, vibrato, tremolo, and ADSR parameters are just fields in the per-voice state; they do not increase parser complexity much.

## Polyphony and Synchronization

**Proposed model (orchestral / conductor style, per your input)**:
- Each independent PLAY string/sequence represents **one voice** (like one instrument's part in an orchestral score).
- Multiple voices can be "queued" and advanced in parallel by a top-level multi-voice player (the "conductor").
- **Synchro-markers**: Reuse the existing label mechanism (`*<n>`) or introduce a dedicated construct (e.g. `*SYNC<n>` or a convention around numeric labels) as synchronization points.
  - When a voice reaches a synchro-marker, it pauses (does not advance further) until the conductor releases the barrier.
  - The multi-voice scheduler checks at each global tick: "Are all active voices (or a defined group) waiting at matching synchro points?" If yes, release all of them to continue.
- This mechanism is the current proposed synchronization for multiple simultaneous PLAY sequences (polyphony / multi-voice). Each sequence is an independent voice with its own note memory. The conductor (top-level scheduler) coordinates at the synchro points. Context snapshots (note memory + tempo/volume/key/etc.) are restored per-voice on jumps within a sequence. The design is intentionally lightweight for the G474 (small active list + waiting flags). It will be refined during implementation.

**Low-level timing synchronization note** (from prior constrained implementation experience): Even with per-voice note memory and cooperative scheduling, independent parser instances will have slightly variable execution time. The exact moment a resolved note is handed to the synth engine (the "starting gun") can therefore drift between voices over time, due to parse-time variation + any uncertainty in the shared tick. A fairly high-resolution tick (ideally << 1 ms, or at least a reliable 1 ms fence that the parser can synchronize against) is still desirable so that all voices can align their actual audio start times, not just their musical events at synchro markers. Without this, even perfectly synchronized musical events can audibly smear at fast tempos with short notes. The conductor model handles musical alignment; a shared high-res time base helps keep the low-level "when the synth actually starts ringing" aligned across voices.
- This keeps voices completely independent (each string is self-contained, easy to store/load separately on SD/LittleFS) while providing global timing alignment for chords, rhythmic hits, or section changes.
- Benefits:
  - Simple to author: write each voice's part as its own .play file/string (with its own labels for internal structure + shared synchro labels for coordination).
  - Low RAM: each voice has its own tiny `play_state_t`; the conductor only needs a small list of active voices + their current sync status.
  - Scalable: start with 2-4 voices on G474; more on H7 with external RAM.
  - Natural fit for your MIDI keyboard input (one voice per "track" or real-time injection).
  - Works with repeats/gotos: a voice can have its own internal loops while still respecting global sync points.
- Example sketch:
  ```
  *SYNC1
  C4Q E4Q G4Q   ; voice 1 chord hit
  *SYNC1
  ```
  Voice 2 could have a different rhythm but also reach *SYNC1 at the same "conductor beat".
- Implementation notes:
  - The top-level player maintains a list of active sequences.
  - At each scheduler tick, advance each voice that is *not* waiting at a sync.
  - When a voice executes a synchro label, set a "waiting_at_sync" flag for that voice.
  - When the barrier condition is met, clear the flags and allow all waiting voices to continue (they may immediately process the next events).
  - Labels can serve dual purpose (internal jumps + global sync) or use a reserved range / special prefix to distinguish.
- This is much lighter than embedding multi-voice syntax inside a single string.

This model aligns beautifully with the rest of the design (independent storable sequences + conductor-like global state in the multi-voice scheduler). It also plays well with the ESP32 coprocessor (ESP can coordinate or even act as a "remote conductor" sending sync signals).

Error handling: skip malformed tokens (log via debug or status) so partial files don't crash playback.

### Data Structures (G474-friendly, low-RAM)
```c
typedef struct {
    const char *src;          // pointer into flash-mapped or SD-buffered string (read-only)
    size_t      pos;
    uint16_t    tempo_bpm;        // quarter note = 1 beat; default 120
    uint8_t     current_octave;   // 1-8 (or 0-7 convention)
    uint8_t     current_volume;   // 0-127 (or 0-100); from V<n> and dynamics
    uint8_t     note_rest_ratio;  // 0-255; 128 = 50/50 (staccato/legato)
    int8_t      current_key_fifths;   // from K command: signed steps around Circle of Fifths (+ = sharps, - = flats). Used to build the 12-note accidental LUT.
    int8_t      transpose_semitones;  // from S command: global semitone shift applied after key-signature accidentals.
    uint8_t     current_instrument; // from the instrument/waveform command (char TBD); 0 = sine (CORDIC today), 1 = triangle, etc. Affects tone generation for this voice.

    // Note memory (the unified "static struct" of inheritable state for this voice/sequence).
    // Holds both traditional note attributes (duration, octave, accidental state, legato/staccato / note-rest ratio, etc.)
    // *and* command-driven characteristics that a musician expects to persist until explicitly changed
    // (tempo, key signature, volume, transposition, instrument/waveform, etc.).
    //
    // Note/rest ratio is explicitly inheritable:
    //   - Legato (`_`) sets 100% duty (full note duration)
    //   - Staccato (`!`) sets a very short duty (~25% or less; silence occupies most of the window, as in the Nutcracker suite)
    //     The exact ratio will be tuned by ear and implemented as a defined constant (or made runtime-settable).
    //
    // Updated by explicit attributes in note descriptors and by commands (K, S, V, future instrument, etc.).
    // A "repeat last note" punctuation token would copy the prior note's full state (the "whole smash", including current ratio) here.
    //
    // Because of jumps/gotos and loops, the implementation may need to maintain *multiple saved snapshots* of the
    // full note memory associated with label definitions and repeat markers. On jump/loop return, the appropriate
    // snapshot is restored so that note inheritance reflects the context that was active when the label or repeat
    // was originally encountered.
    //
    // Current thinking only — the exact fields, what belongs in the unified note memory, and the inheritance rules
    // have wiggle room (see Inheritance Model section).
    // Effect state (updated by VIB/TRM etc.; applied in the tone generator or a service job)
    struct {
        uint8_t rate;   // Hz or cycles per beat
        uint8_t depth;  // cents or amplitude units
    } vibrato, tremolo;
    // ADSR (set by ADSR command; applied on note start/stop)
    struct {
        uint16_t attack;   // ms or rate
        uint16_t decay;
        uint8_t  sustain;  // level
        uint16_t release;
    } adsr;
    // Small fixed-size repeat stack for [ <seq> ]:N  (max depth ~10)
    struct {
        size_t   pos;         // return position in src
        uint16_t remaining;   // counts left (0 = done)
    } repeat_stack[10];
    uint8_t repeat_depth;
    // Label table: built in a cheap linear pre-scan pass when starting a sequence.
    // Use a fixed array sized to the chosen max label value (e.g. 256 for 0-255).
    // If labels can be larger/sparse, switch to a small list of {uint16_t id; size_t pos;} + linear lookup on jump.
    size_t      label_pos[256];   // position in src for each label ID; 0 or sentinel if undefined
    // future: pending note-off tick, active voice list for polyphony, etc.
} play_state_t;
```

Keep the struct tiny. The unified "note memory" holds both note-specific attributes (including the current legato/staccato ratio: 100% duty for legato, very short/~25% or less for staccato so that silence occupies most of the window) and the command-driven characteristics (tempo, key signature, volume, transposition, instrument, etc.) that a musician expects to be inherited by subsequent notes until changed. A repeat-last-note token copies the prior note's full state (the "whole smash") from this memory. Because of jumps/gotos/loops, saved snapshots of the full note memory are associated with labels and repeat markers for context restoration. Volume, effects, ADSR, and the note memory are the main pieces of per-voice runtime state. The label table size is the main variable cost — choose a sensible max n (e.g. 256 or 1024) based on expected music complexity and available RAM. The table is populated once per sequence load.

### Integration with Existing Code
- **synth_engine**: Reuse `v_synth_engine_set_tone(float freq, float level)` (and `stop`). Compute freq with the existing `f_noteplayer_calc_freq` + CORDIC path (which will need to incorporate key signature accidentals from the K LUT and then apply S transposition). Modulate level for future velocity/ADSR. The instrument/waveform command (char TBD per the Character Allocation Guidelines) will select the generator; current implementation is CORDIC direct sine only. Recent work (linear ~7 ms attack + ~4 ms decay envelope + 3 ms fast transition release on retrigger) already provides clean note on/off and key-to-key transitions for the monophonic case — this will be the foundation for per-instrument envelopes and timbre switching. Extending beyond sine will require additional generation paths in the fill (software triangle/saw, FMAC assistance, tables, etc.).
- **note_player**: The current terminal key-driven mode ('p' menu) stays for live play. Add a sequencer mode or debug command: `playstr "CH GH..."` or `playfile tune1.play` that runs the interpreter non-interactively (bypasses the key handler, drives the synth engine directly with timing).
- **Jobs / cooperative loop** (critical for G474): Drive the parser + scheduler from a new `JOB_PLAY_TICK` (or reuse `JOB_SYNTH_SERVICE` + `v_app_polling_task`). Yield frequently so LED USART/DMA, audio fill, debug menu, etc. are unaffected. Use the existing 1 ms tick for duration scheduling.
- **Storage** (directly solves the 128 KB RAM concern + uses the hardware you have today):
  - LittleFS on SPI/QSPI NOR flash (your Mikroe 8 Mbit board + bare chips on TSOP carriers you can solder to carriers). Power-loss safe, wear-leveled, tiny RAM footprint. Perfect for internal .play strings or compiled event streams.
  - FatFs/FAT32 on your SDCard breakouts — for user content (format on PC, drop text .play files; the future ESP32 coprocessor can also write them over WiFi/MQTT).
  - Hybrid: LittleFS on NOR (fast/reliable for core sequences), FAT on SD (capacity + easy authoring).
  - The G474 QSPI (already enabled in your .ioc) is great for fast sequential reads / streaming.
- **Future ESP32 coprocessor**: The ESP owns networking and can deliver new PLAY strings/files over the bidirectional UART. The STM32 core stays lean and real-time.
- **Current G474 constraints**: Streaming parser + tiny state + external storage is the key. On the H723/H743 boards you just got this becomes luxurious (more RAM + native QSPI/SDMMC + external SDRAM on the 743).

### Storage & Filesystem Notes
- Strings (or compiled events) are tiny — one 1 MB flash can hold dozens of tunes + samples.
- For audio samples (future poly/sample synth): stream raw data from flash/SD with small DMA buffers into SAI.
- On G474 prototype: wire one of your SPI flash chips (or the Mikroe board) + SD breakout to a free SPI. QSPI is already clocked.
- The player-piano sequencer can load a file and play it autonomously — no host Python script required once the interpreter + FS are in.

This approach keeps everything cooperative, low-RAM, and directly leverages the storage hardware you described plus the CORDIC synth engine we've already built.

## High-Level Feedback and Suggestions

**Overall assessment**: This is a thoughtful, pragmatic domain-specific language (DSL) tailored for an embedded real-time music sequencer on resource-constrained hardware like the STM32G474 (and future H7 boards). It cleverly balances:
- Human writability/readability (text strings, defaults propagation, optional spaces).
- Storage friendliness (compact text or pre-compiled binary events on LittleFS/FAT32 SD/SPI flash; easy to author on PC or push via ESP32/MQTT/USB).
- Low-RAM execution (streaming parser, no full AST, small state machine + stacks/tables, cooperative scheduling via existing jobs).
- Musical expressiveness (control flow via repeats + labels/gotos for codas/da capos/loops, articulation hooks, key/tempo/octave state).
- Project synergy (drives the CORDIC synth_engine directly, extends the interactive note_player, pairs with storage hardware you have, future ESP32 coprocessor for "smart" networked music, ties into player-piano wishlist modeled on GWBASIC PLAY).

The incremental rebuilding from memory is working well; the design doc is capturing a solid foundation. The "not-too-real-time" nature of sequences (load from storage, schedule events) fits the cooperative super-loop + jobs model perfectly, avoiding heavy RTOS needs initially.

**Strengths**:
- Defaults + state (octave, duration, tempo) reduce verbosity dramatically while remaining predictable.
- Labels/gotos + nested repeats give real power for musical forms (repeats, codas, variations) without turning into a full scripting language.
- Clear separation: text for humans/storage, events for runtime.
- Practical constraints acknowledged (RAM, flash endurance, streaming from SD/SPI, non-blocking playback).

**Suggestions for additions / changes** (high-level; prioritize based on your lost original spec):

1. **Triplets, tuplets, and rhythmic precision** (high priority, given your Star Wars triplet note):
   - Add explicit syntax, e.g. `T3{ Q E D }` or `(3:QE D)` or duration suffix like `Q3` for triplet.
   - Or a global `TU<num>` tuplet command.
   - This avoids hacks like approximating with shorter durations.

2. **Polyphony and multiple voices** (key for "player-piano" ambitions):
   - Support simultaneous notes/chords or parallel tracks.
   - Examples: `C4Q E4Q G4Q` (chord) or `V1: seq1 | V2: seq2` (voices) or `P1: ... P2: ...`.
   - State per voice (current octave/dur per voice) or a mixer in synth fill.
   - Ties into future FM/ADSR/poly synth.

3. **Articulation, dynamics, and expression** (builds on your "note descriptor expansion"):
   - Per-note or phrase: staccato (.), legato (-), accent (>), etc. as modifiers on duration/note.
   - Dynamics: `v<0-127>` or `pp p mp mf f ff` (map to level/velocity for set_tone).
   - Global or local ADSR: `ADSR a d s r` (attack/decay/sustain/release times or rates) applied to envelopes.
   - Ties: `C4Q~D4Q` or explicit tie command.
   - This directly feeds the synth_engine's level and future envelope params.

4. **More structural / musical commands**:
   - Time signature / bars: `TS4/4` or `|` bar lines (for counting, but optional since timing is duration-based).
   - Relative tempo: `T+10` or accelerando commands (in addition to absolute T).
   - Sections/markers beyond labels: `||: ... :||` (repeat with volta 1/2 variants).
   - Variables/macros for reuse: `DEF motif1 = [C Q D Q]` then `motif1`.
   - Comments: full-line `;` or `#` (in addition to inline -).
   - Metadata/header: `TITLE="Star Wars" TEMPO=120 KEY=C` at top of file (parsed separately, useful for storage/display).
   - Includes: `INCLUDE "fanfare.play"` for modular songs on SD.

5. **Control flow refinements**:
   - Conditionals? (e.g. based on a "random" or input, but keep minimal for embedded).
   - While-style or conditional gotos (once more state like counters exist).
   - Subroutines: `CALL label` / `RET` in addition to raw gotos (cleaner than manual stack management).
   - Limit on label n early (recommend 0-127 or 0-255 for tiny table).

6. **MIDI / standard interop and tooling**:
   - Bridge to MIDI: commands to import simple MIDI (note/dur/vel) or export.
   - Velocity per note: extend descriptor with `v80` or number after duration.
   - Quantization or swing commands.
   - Since you have ESP32 coprocessor plans: the ESP can do heavy lifting (MIDI file parsing, complex composition) and send PLAY strings or events over UART.

7. **Error handling, robustness, versioning**:
   - Define behavior for errors: undefined label, invalid note, unknown command, mismatched brackets (e.g. skip bad token + log, or stop with silence).
   - Versioning: optional header `#PLAY v1` at start of file for future incompatible changes.
   - Validation: the host Python script (even if secondary) can be a reference validator + "compiler" to compact binary form.

8. **On-device / implementation considerations** (to keep in spec):
   - Timing model: durations in "ticks" derived from tempo (quarter = beat). Use existing 1ms jobs or synth fill callbacks for scheduling. Support fractional for dots/tuplets.
   - State machine: current defaults (octave, dur, tempo, key_sig, note_rest_ratio), small stacks for repeats/labels (already planned), pending events queue (tiny, 4-8 entries).
   - Pre-processing: on load from storage, optional pass to resolve labels + validate structure (cheap).
   - Compiled form (strongly recommended for production): text .play -> binary events (smaller on flash, faster parse, less RAM during play). Define opcode set (e.g. 0x01=NOTE_ON freq16 dur16, 0x02=REST dur16, 0x03=SET_TEMPO, 0x10=GOTO label, etc.).
   - Resource limits: document max labels, repeat depth, simultaneous voices, string length per sequence (to fit G474 RAM + flash).
   - Non-blocking: must yield; no long parses or blocking waits. Perfect fit for cooperative jobs.
   - With storage: load string on demand from LittleFS file or SD sector. For samples: separate "trigger sample ID" commands that stream from flash.
   - Future RTOS: easy to promote player to its own task.

9. **Usability / ecosystem**:
   - Authoring tools: enhance Python script (or new host tool) to support full syntax, simulate playback (print notes + timing), convert to/from MIDI, generate binary.
   - Editing on-device? (via terminal or future ESP32 web UI?).
   - Examples: expand the Star Wars one with repeats, labels for structure, tempo change, dynamics once syntax is there.
   - Documentation: BNF/EBNF grammar for the language (prevents ambiguity in optional spaces, case, etc.). Semantics section (state transitions, timing, defaults).
   - Testing: unit tests for parser (edge cases like bare R, forward gotos, nested repeats, defaults across jumps).

**Potential changes / refinements**:
- Make rests always `R<dur>` (no bare space in files) for clarity in storage, but allow space in live terminal mode.
- Decide case sensitivity definitively (recommend upper for notes/commands, or force specific casing).
- For key signature: specify exactly how it auto-applies accidentals (e.g. in C major, F is natural; in F major, B is flat by default unless overridden).
- Limit on label n: suggest picking 0-127 or 0-255 now for the table size; document it.
- Make the note descriptor expansion (for articulation) use a clear prefix/suffix, e.g. `C4Q.stac` or `C4Q.80` (vel) or dedicated letter.
- Consider a "compiled" vs "source" distinction early in the spec.
- For the lost original spec: this incremental approach + the design doc is a great way to reconstruct. If you remember any other commands (e.g. from GWBASIC PLAY like MN/ML for music normal/legato, or N for note number), add them.

**Overall recommendation**: This is already a strong, focused design that avoids over-engineering (no full language runtime, keeps embedded constraints front-and-center). Prioritize getting a working monophonic interpreter + basic storage (LittleFS on one of your SPI flash chips) on the G474 first—it will let you test real sequences end-to-end quickly. The labels/gotos + repeats already give you 80% of the "complex musical forms" power.

The design doc is the right place to keep evolving this. I can help:
- Add a formal (E)BNF grammar section.
- Flesh out more complete examples (full song using new features).
- Sketch C pseudocode for the parser/state machine (streaming, label table, repeat stack).
- Update the Python driver to be a more complete reference implementation/validator for the full syntax (even if secondary).
- Add this language description to PROJECT.md or the MCU guide for visibility.
- Plan the binary event format or integration with your SD/SPI flash hardware.

What would you like to tackle next? Or any specific part of the feedback you'd like to discuss/deepen? This is exciting—it's turning the note player into a real embedded music platform.

## Polyphony and Synchronization

**Proposed model (orchestral / conductor style, per your input)**:
- Each independent PLAY string/sequence represents **one voice** (like one instrument's part in an orchestral score).
- Multiple voices can be "queued" and advanced in parallel by a top-level multi-voice player (the "conductor").
- **Synchro-markers**: Reuse the existing label mechanism (`*<n>`) or introduce a dedicated construct (e.g. `*SYNC<n>` or a convention around numeric labels) as synchronization points.
  - When a voice reaches a synchro-marker, it pauses (does not advance further) until the conductor releases the barrier.
  - The multi-voice scheduler checks at each global tick: "Are all active voices (or a defined group) waiting at matching synchro points?" If yes, release all of them to continue.
- This mechanism is the current proposed synchronization for multiple simultaneous PLAY sequences (polyphony / multi-voice). Each sequence is an independent voice with its own note memory. The conductor (top-level scheduler) coordinates at the synchro points. Context snapshots (note memory + tempo/volume/key/etc.) are restored per-voice on jumps within a sequence. The design is intentionally lightweight for the G474 (small active list + waiting flags). It will be refined during implementation.

**Low-level timing synchronization note** (from prior constrained implementation experience): Even with per-voice note memory and cooperative scheduling, independent parser instances will have slightly variable execution time. The exact moment a resolved note is handed to the synth engine (the "starting gun") can therefore drift between voices over time, due to parse-time variation + any uncertainty in the shared tick. A fairly high-resolution tick (ideally << 1 ms, or at least a reliable 1 ms fence that the parser can synchronize against) is still desirable so that all voices can align their actual audio start times, not just their musical events at synchro markers. Without this, even perfectly synchronized musical events can audibly smear at fast tempos with short notes. The conductor model handles musical alignment; a shared high-res time base helps keep the low-level "when the synth actually starts ringing" aligned across voices.
- This keeps voices completely independent (each string is self-contained, easy to store/load separately on SD/LittleFS) while providing global timing alignment for chords, rhythmic hits, or section changes.
- Benefits:
  - Simple to author: write each voice's part as its own .play file/string (with its own labels for internal structure + shared synchro labels for coordination).
  - Low RAM: each voice has its own tiny `play_state_t`; the conductor only needs a small list of active voices + their current sync status.
  - Scalable: start with 2-4 voices on G474; more on H7 with external RAM.
  - Natural fit for your MIDI keyboard input (one voice per "track" or real-time injection).
  - Works with repeats/gotos: a voice can have its own internal loops while still respecting global sync points.
- Example sketch:
  ```
  *SYNC1
  C4Q E4Q G4Q   ; voice 1 chord hit
  *SYNC1
  ```
  Voice 2 could have a different rhythm but also reach *SYNC1 at the same "conductor beat".
- Implementation notes:
  - The top-level player maintains a list of active sequences.
  - At each scheduler tick, advance each voice that is *not* waiting at a sync.
  - When a voice executes a synchro label, set a "waiting_at_sync" flag for that voice.
  - When the barrier condition is met, clear the flags and allow all waiting voices to continue (they may immediately process the next events).
  - Labels can serve dual purpose (internal jumps + global sync) or use a reserved range / special prefix to distinguish.
- This is much lighter than embedding multi-voice syntax inside a single string.

This model aligns beautifully with the rest of the design (independent storable sequences + conductor-like global state in the multi-voice scheduler). It also plays well with the ESP32 coprocessor (ESP can coordinate or even act as a "remote conductor" sending sync signals).

**Next for this doc**: We can add a formal grammar section, more complete worked examples (full piece using labels for structure + synchro-markers for polyphony), and a first C sketch for the multi-voice scheduler + label/gotos if you want.

Storing the raw text string is simplest for authoring; compile to events only for performance-critical use.

### High-Level Feedback and Suggestions

**Overall assessment**: This is a thoughtful, pragmatic domain-specific language (DSL) tailored for an embedded real-time music sequencer on resource-constrained hardware like the STM32G474 (and future H7 boards). It cleverly balances:
- Human writability/readability (text strings, defaults propagation, optional spaces).
- Storage friendliness (compact text or pre-compiled binary events on LittleFS/FAT32 SD/SPI flash; easy to author on PC or push via ESP32/MQTT/USB).
- Low-RAM execution (streaming parser, no full AST, small state machine + stacks/tables, cooperative scheduling via existing jobs).
- Musical expressiveness (control flow via repeats + labels/gotos for codas/da capos/loops, articulation hooks, key/tempo/octave state).
- Project synergy (drives the CORDIC synth_engine directly, extends the interactive note_player, pairs with storage hardware you have, future ESP32 coprocessor for "smart" networked music, ties into player-piano wishlist modeled on GWBASIC PLAY).

The incremental rebuilding from memory is working well; the design doc is capturing a solid foundation. The "not-too-real-time" nature of sequences (load from storage, schedule events) fits the cooperative super-loop + jobs model perfectly, avoiding heavy RTOS needs initially.

**Strengths**:
- Defaults + state (octave, duration, tempo) reduce verbosity dramatically while remaining predictable.
- Labels/gotos + nested repeats give real power for musical forms (repeats, codas, variations) without turning into a full scripting language.
- Clear separation: text for humans/storage, events for runtime.
- Practical constraints acknowledged (RAM, flash endurance, streaming from SD/SPI, non-blocking playback).

**Suggestions for additions / changes** (high-level; prioritize based on your lost original spec):

1. **Triplets, tuplets, and rhythmic precision** (high priority, given your Star Wars triplet note):
   - Add explicit syntax, e.g. `T3{ Q E D }` or `(3:QE D)` or duration suffix like `Q3` for triplet.
   - Or a global `TU<num>` tuplet command.
   - This avoids hacks like approximating with shorter durations.

2. **Polyphony and multiple voices** (key for "player-piano" ambitions):
   - Support simultaneous notes/chords or parallel tracks.
   - Examples: `C4Q E4Q G4Q` (chord) or `V1: seq1 | V2: seq2` (voices) or `P1: ... P2: ...`.
   - State per voice (current octave/dur per voice) or a mixer in synth fill.
   - Ties into future FM/ADSR/poly synth.

3. **Articulation, dynamics, and expression** (builds on your "note descriptor expansion"):
   - Per-note or phrase: staccato (.), legato (-), accent (>), etc. as modifiers on duration/note.
   - Dynamics: `v<0-127>` or `pp p mp mf f ff` (map to level/velocity for set_tone).
   - Global or local ADSR: `ADSR a d s r` (attack/decay/sustain/release times or rates) applied to envelopes.
   - Ties: `C4Q~D4Q` or explicit tie command.
   - This directly feeds the synth_engine's level and future envelope params.

4. **More structural / musical commands**:
   - Time signature / bars: `TS4/4` or `|` bar lines (for counting, but optional since timing is duration-based).
   - Relative tempo: `T+10` or accelerando commands (in addition to absolute T).
   - Sections/markers beyond labels: `||: ... :||` (repeat with volta 1/2 variants).
   - Variables/macros for reuse: `DEF motif1 = [C Q D Q]` then `motif1`.
   - Comments: full-line `;` or `#` (in addition to inline -).
   - Metadata/header: `TITLE="Star Wars" TEMPO=120 KEY=C` at top of file (parsed separately, useful for storage/display).
   - Includes: `INCLUDE "fanfare.play"` for modular songs on SD.

5. **Control flow refinements**:
   - Conditionals? (e.g. based on a "random" or input, but keep minimal for embedded).
   - While-style or conditional gotos (once more state like counters exist).
   - Subroutines: `CALL label` / `RET` in addition to raw gotos (cleaner than manual stack management).
   - Limit on label n early (recommend 0-127 or 0-255 for tiny table).

6. **MIDI / standard interop and tooling**:
   - Bridge to MIDI: commands to import simple MIDI (note/dur/vel) or export.
   - Velocity per note: extend descriptor with `v80` or number after duration.
   - Quantization or swing commands.
   - Since you have ESP32 coprocessor plans: the ESP can do heavy lifting (MIDI file parsing, complex composition) and send PLAY strings or events over UART.

7. **Error handling, robustness, versioning**:
   - Define behavior for errors: undefined label, invalid note, unknown command, mismatched brackets (e.g. skip bad token + log, or stop with silence).
   - Versioning: optional header `#PLAY v1` at start of file for future incompatible changes.
   - Validation: the host Python script (even if secondary) can be a reference validator + "compiler" to compact binary form.

8. **On-device / implementation considerations** (to keep in spec):
   - Timing model: durations in "ticks" derived from tempo (quarter = beat). Use existing 1ms jobs or synth fill callbacks for scheduling. Support fractional for dots/tuplets.
   - State machine: current defaults (octave, dur, tempo, key_sig, note_rest_ratio), small stacks for repeats/labels (already planned), pending events queue (tiny, 4-8 entries).
   - Pre-processing: on load from storage, optional pass to resolve labels + validate structure (cheap).
   - Compiled form (strongly recommended for production): text .play -> binary events (smaller on flash, faster parse, less RAM during play). Define opcode set (e.g. 0x01=NOTE_ON freq16 dur16, 0x02=REST dur16, 0x03=SET_TEMPO, 0x10=GOTO label, etc.).
   - Resource limits: document max labels, repeat depth, simultaneous voices, string length per sequence (to fit G474 RAM + flash).
   - Non-blocking: must yield; no long parses or blocking waits. Perfect fit for cooperative jobs.
   - With storage: load string on demand from LittleFS file or SD sector. For samples: separate "trigger sample ID" commands that stream from flash.
   - Future RTOS: easy to promote player to its own task.

9. **Usability / ecosystem**:
   - Authoring tools: enhance Python script (or new host tool) to support full syntax, simulate playback (print notes + timing), convert to/from MIDI, generate binary.
   - Editing on-device? (via terminal or future ESP32 web UI?).
   - Examples: expand the Star Wars one with repeats, labels for structure, tempo change, dynamics once syntax is there.
   - Documentation: BNF/EBNF grammar for the language (prevents ambiguity in optional spaces, case, etc.). Semantics section (state transitions, timing, defaults).
   - Testing: unit tests for parser (edge cases like bare R, forward gotos, nested repeats, defaults across jumps).

**Potential changes / refinements**:
- Make rests always `R<dur>` (no bare space in files) for clarity in storage, but allow space in live terminal mode.
- Decide case sensitivity definitively (recommend upper for notes/commands, or force specific casing).
- For key signature: specify exactly how it auto-applies accidentals (e.g. in C major, F is natural; in F major, B is flat by default unless overridden).
- Limit on label n: suggest picking 0-127 or 0-255 now for the table size; document it.
- Make the note descriptor expansion (for articulation) use a clear prefix/suffix, e.g. `C4Q.stac` or `C4Q.80` (vel) or dedicated letter.
- Consider a "compiled" vs "source" distinction early in the spec.
- For the lost original spec: this incremental approach + the design doc is a great way to reconstruct. If you remember any other commands (e.g. from GWBASIC PLAY like MN/ML for music normal/legato, or N for note number), add them.

**Overall recommendation**: This is already a strong, focused design that avoids over-engineering (no full language runtime, keeps embedded constraints front-and-center). Prioritize getting a working monophonic interpreter + basic storage (LittleFS on one of your SPI flash chips) on the G474 first—it will let you test real sequences end-to-end quickly. The labels/gotos + repeats already give you 80% of the "complex musical forms" power.

The design doc is the right place to keep evolving this.

### Polyphony and Synchronization (new section based on your latest input)

**Proposed model (orchestral / conductor style)**:
- Each independent PLAY string/sequence represents **one voice** (like one instrument's part in an orchestral score).
- Multiple voices can be "queued" and advanced in parallel by a top-level multi-voice player (conductor).
- **Synchro-markers**: Use the existing label mechanism (`*<n>`) or introduce a dedicated construct (e.g. `*SYNC<n>` or just convention around numeric labels) as synchronization points.
  - When a voice reaches a synchro-marker, it pauses (does not advance further) until the conductor releases the barrier.
  - The multi-voice scheduler checks at each global tick: "Are all active voices (or a defined group) waiting at matching synchro points?" If yes, release all of them to continue.
- This keeps voices completely independent (each string is self-contained, easy to store/load separately) while providing global timing alignment for chords, rhythmic hits, or section changes.
- Benefits:
  - Simple to author: write each voice's part as its own .play file/string (with its own labels for internal structure + shared synchro labels for coordination).
  - Low RAM: each voice has its own tiny `play_state_t`; the conductor only needs a small list of active voices + their current sync status.
  - Scalable: start with 2-4 voices on G474; more on H7 with external RAM.
  - Natural fit for your MIDI keyboard input (one voice per "track" or real-time injection).
  - Works with repeats/gotos: a voice can have its own internal loops while still respecting global sync points.
- Example sketch:
  ```
  *SYNC1
  C4Q E4Q G4Q   ; voice 1 chord hit
  *SYNC1
  ```
  Voice 2 could have a different rhythm but also reach *SYNC1 at the same "conductor beat".
- Implementation notes:
  - The top-level player maintains a list of active sequences.
  - At each scheduler tick, advance each voice that is *not* waiting at a sync.
  - When a voice executes a synchro label, set a "waiting_at_sync" flag for that voice.
  - When the barrier condition is met, clear the flags and allow all waiting voices to continue (they may immediately process the next events).
  - Labels can serve dual purpose (internal jumps + global sync) or use a reserved range / special prefix to distinguish.
- This is much lighter than embedding multi-voice syntax inside a single string.

This model aligns beautifully with the rest of the design (independent storable sequences + conductor-like global state in the multi-voice scheduler). It also plays well with the ESP32 coprocessor (ESP can coordinate or even act as a "remote conductor" sending sync signals).

**Next for this doc**: We can add a formal grammar section, more complete worked examples (full piece using labels for structure + synchro-markers for polyphony), and a first C sketch for the multi-voice scheduler + label/gotos if you want.

The doc is now updated with the consolidated high-level feedback + your polyphony conductor model as a new dedicated section.

Let me know what to tackle next (grammar, C pseudocode for the synchro barrier, storage file format examples, Python driver updates for polyphony simulation, or anything else). This is shaping up into a really nice embedded music system.