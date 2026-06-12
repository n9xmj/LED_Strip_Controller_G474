# PLAY — lead-char cheat sheet

Planning snapshot · detail: [play-v1-implementation-plan.md](play-v1-implementation-plan.md) · 🟢 locked · 🟡 pending · 🔵 deferred

## Top-level leads (column 0 after whitespace)

| Char | Usage |
|------|--------|
| **A–G** | Start **note** (`C4Q`) — uppercase only 🟢 |
| **N** | **Absolute semitone** — `N60Q`, `N60Q4` … (**D22** 🟢); **1..3** digits then suffix; **`N604Q` = sem 604** |
| **R** | **Rest** — full descriptor suffix → **memory + silence** 🟢; use **`\"ctx:…"`** for zero-time load |
| **T** | **Tempo** BPM (`T120`) |
| **O** | **Default octave** (`O4`) |
| **^** **v** | Octave up / down one 🟢 |
| **K** | **Key** — **`K"…"` only** (e.g. `K"Db"C4Q`) 🟢 |
| **&** | **Transpose** — `&+nn` / `&-nn` / `&0` (after **K**+accidentals) 🟢; OOR → octave clamp + WARNING |
| **U** | **Beat unit** (`UQ`, `UE`, …) |
| **V** | **Volume** 0–100 (`V80`); clamps >100 🟢 |
| **P** | **Voice/timbre** (`P0` = sine) 🟢 |
| **?** | **Print** — `?"…"` C escapes; bare `?` → CRLF 🟢 |
| **\\** | **Extension** — `\"cmd:args"`; **`ctx:`** = instant note-memory 🟢; v1 stub echoes |
| **[** | **Repeat** open — `]:N` close 🟢 |
| **<** | **Label** define — `<n` or `<"name"` (≤**16**, max **10** labels/**I2**) 🟢 |
| **>** | **Goto** — `>n` / `>"name"`; fwd keep ctx, back restore 🟢; **undefined label → hard abort** 🟢 |
| **~** | **Repeat last note** — top-level only 🟢 |
| **=** | **GOSUB** — `="name"` (≤**16**; same label table as **`<`**) 🟢; **undefined label → hard abort** 🟢 |
| **/** | **RETURN** — pop call stack; **hard abort** if stack empty 🟢 |
| **\*** | **END** — hard **STOP** playback 🟢 |
| **@** | **Comment** open/close; first block = title 🟢 |

Quoted metas (`"` ends token; may abut; **optional WS before opening `"`** per **D8b**): `K"Db"C4Q` · `K "Db"C4Q` · `? "hi"` · `?"hi"` · `\"ctx:4Q;6"` · `< "loop"` · `>"loop"` · `="TURN"`  
**Label refs** (`>"…"`, `>n`, `="…"`, `=n`): undefined define → **hard abort** (pre-scan check recommended) 🟢  
**Lexical (D12 🟢):** WS = skip/readability · **`:`** = optional **EOS** (BASIC-like) · **`;`** = duty in notes only (**D5**, not C-EOS) · after **`:`** skip WS → top-level lead · **except** **`]:N`** (**S4**)  
Abut after closing **`"`** still OK (`K"C"T120`); **`:`** alternative (`K"C":T120`)

## Inside note / **`N`** suffix (after **A–G** letter or **`N` digits**)

| Char | Usage |
|------|--------|
| **#** **+** | Sharp |
| **b** **-** | Flat |
| **n** | Natural (explicit; letter + **K** LUT only) 🟢 — not top-level **`N`** |
| **0–9** | Octave digit (inherited) |
| **W** **H** **Q** **I** | Whole / half / quarter / eighth duration |
| **X** **Y** | 16th / 32nd — 🔵 **D4** deferred |
| **.** | Dotted (×1.5) |
| **_** **!** **;** **;n** | Duty: legato / staccato / normal / n-of-8 🟢 |

Any order after note letter; **one** duration required. Fields inherit; accidentals from **K** unless `#`/`b`/`n`.

## Comment-only · reserved

| Context | Char | Usage |
|---------|------|--------|
| inside `@…@` | **\\@** | Literal `@` |
| deferred | **\\|"name"** | Sync barrier (**S3** 🔵) |
| retired | **M** | use **P** · **\*** was label define → **`<`**; now **END** |
| retired | **S** | was Shift/transpose → use **`&`** (**D21**) |

## Unallocated printable ASCII

No role as top-level lead, note/rest descriptor modifier, repeat/label/quote structure, **`K`/`&`/`U`/`\\` token syntax**, or deferred **`\\|"…"`** sync. **`@…@` / `"…"` content** may still use any character. **Exception:** **`:`** at top level = optional hard end-of-command (**D12**); **`]:N`** repeat tail (**S4**) is structural, not terminator.

$ % ' ( ) , ` { } J L M S Z a c d e f g h i j k l o p q r s t u w x y z

_Bump when new leads land or caps change._
