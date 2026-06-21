# PLAY — lead-char cheat sheet

Detail: [implementation plan](../planning/play-v1-implementation-plan.md) · **LLM brief:** [chatbot_brief.md](chatbot_brief.md) · **Suite hub:** [README.md](README.md) · firmware audit: **I10** in plan  
**Last updated:** 2026-06-21 (v1.2 **G12**/**G15**/**G13**/**G14**/**G20** — D25 `=`/`>` · quoted labels · signed repeat · GOSUB restore · multi-dot)

**Legend:** 🟢 spec locked · 🔵 deferred · **fw** = shipped in `App/Src/play.c` today · *(blank)* = spec only, not in firmware yet · **partial** = subset only

### v1.1 → v1.2 migration (breaking)

| Was (v1.1) | Now (v1.2) |
|------------|------------|
| `>` goto · `=` GOSUB | **`=`** goto · **`>`** GOSUB (**D25**) |
| `<n` / `>n` / `=n` numeric labels | **Quoted text only** — `<"name"` `="name"` `>"name"` (**D29**) |
| `]:N` always restores `[` snapshot | `]:N` / `]:+N` restore; `]:-N` **no restore**; count **max(1, N)** (**S12**/**S14**) |
| `/` always restores GOSUB caller | `>-"name"` skips restore; `>"name"` default restore (**S13**) |
| One `.` → ×1.5 | Chained `.` → factor **2 − 2⁻ⁿ** (**D26**) |

Archive: `grammar_torture_v11.play` stays on **v1.1** wire for **X**/**Y** regression only.

---

## Session defaults (**S10** 🟢 · **fw** via `play_config.h`)

| Field | Default | Implicit executive |
|-------|---------|-------------------|
| Duration | **Quarter** (`Q`) | sticky note memory · `Cn4Q_` template |
| Tempo | **120 BPM** | `T120` |
| Beat unit | **Quarter = 1 beat** | `%Q` (implicit) |
| Octave | **4** | `O4` |
| Key | **C major** | `K"C"` *(default; LUT applied on bare letters)* |
| Duty | **Legato 8/8** | `_` |
| Volume | **50%** | `V50` |
| Voice | **Sinewave** | `P0` |

Letter-only runs (`CDEF`, `DEFGAB`) inherit **Q** + **O4** from this seed. **NUL at EOF** = implicit `*` END (**fw**).

---

## Compact runs & inheritance (**fw**)

Omitted suffix fields **inherit from note memory** after each note/rest commits (order-flex within the first token).

| Pattern | Meaning |
|--------|---------|
| `CQ4` | C · quarter · octave **4** (any order: `C4Q`, `Q4C`, …) |
| `DEFGAB` | Letter-only tokens · inherit **Q** + **oct 4** from prior note |
| `C5` | C · octave **5** · inherit **Q** |
| `CQ4DEFGABC5` | Full C-major scale, quarter notes, oct 4 → top C oct 5 |

**Token boundary:** after the **first** letter of a note, the next **`A`–`G`** starts a **new** note (not part of the same cluster). Whitespace optional between letters (`C4Q D E F` ≡ `C4QDEF`).

**First note in a score** may omit duration/octave when **S10** seeds apply (default **Q** + **O4**). Explicit `Q`/`O`/`T`/`V` executives override as usual.

**Duty / dot:** `_` `!` `;` `;n` `;nn` inherit like duration/octave (**fw**). **K** LUT on bare letters; explicit `#`/`+`/`b`/`-`/`n` skips LUT.

---

## Fault policy (**S7i** 🟢 · **fw** API)

| Mode | Recoverable faults | Use |
|------|-------------------|-----|
| **NORMAL** | WARNING + continue (default) | Bench playback |
| **LAZY** | Silent skip/fix | Low-noise autoplay |
| **STRICT** | Promote to **stop** (GCC `-Werror`) | Golden strings / authoring |

**S7a** fatals (bad **T**, unclosed `@`, stack overflow, …) **always abort** in every mode.  
API: `b_play_start()` → NORMAL · `b_play_start_policy(src, PLAY_FAULT_POLICY_STRICT, &h)` for golden runs.

---

## Top-level leads (column 0 after whitespace)

| Char | Usage | fw |
|------|--------|-----|
| **A–G** | Start **note** — uppercase only 🟢; compact runs (**above**) | **fw** |
| **N** | **Absolute semitone** — `N60Q`, `N604Q` = sem **604** (**D22** 🟢) | **fw** |
| **R** | **Rest** — same suffix cluster as notes → memory + silence 🟢 | **fw** |
| **T** | **Tempo** BPM (`T120`) | **fw** |
| **O** | **Default octave** (`O4`) — sticky note memory | **fw** |
| **^** **v** | Octave up / down one 🟢 | **fw** |
| **K** | **Key** — **`K"…"` only** (e.g. `K"C"C4Q`) 🟢 | **fw** |
| **&** | **Transpose** — `&+nn` / `&-nn` / `&0` 🟢 | **fw** |
| **%** | **Beat unit** — `%W` `%H` `%Q` `%I` (which note = 1 beat; **not** a time signature) 🟢 | **fw** |
| **V** | **Volume** 0–100 (`V80`); clamps >100 🟢 | **fw** |
| **P** | **Voice/timbre** (`P0` = sine · `P1` = triangle) 🟢 | **fw** |
| **?** | **Print** — `?"…"` lyrics / text at playback time; C escapes; bare `?` → CRLF 🟢 | **fw** |
| **\\** | **Extension** — `\"cmd:args"`; **`ctx:`** = instant note-memory 🟢 | **fw** |
| **[** | **Repeat** open — `]:N` / `]:+N` restore **`[`** snapshot on re-entry (**S4**/**G8**); `]:-N` **no restore** (**S12**); passes = **max(1, N)** (**S14**) | **fw** |
| **<** | **Label** define — `<"name"` only (≤**16** chars, max **10** labels/**I2**) 🟢 | **fw** |
| **=** | **Goto** — `="name"`; pure PC jump, carries ctx (**S2**); optional `+`/`-` before `"` stripped; missing → **fatal** | **fw** |
| **~** | **Repeat last note** — top-level only 🟢 | **fw** |
| **>** | **GOSUB** — `>"name"` 🟢; `>-"name"` = no caller restore on `/` (**S13**); **`/`** returns; **`*`** in callee = hard END unless **`b_stop_is_return`** (**D23**) | **fw** |
| **/** | **RETURN** — pop **`>`** stack; **fatal** if empty at root 🟢 | **fw** |
| **L** | **Library** — `L"…"` (**D23** 🔵); sets **`b_stop_is_return`** for descendants | warn + skip |
| **\*** | **END** — hard STOP at root (incl. in-string **`=`** callee); **return** when **`b_stop_is_return`** (**D23**) | **fw** (root NUL = `*`) |
| **@** | **Comment** open/close only 🟢 (**D10** withdrawn — title = `?"…"`) | **fw** (`\@` literal **fw**) |
| **:** | **D12** optional top-level EOS 🟢 | **partial** — stray `:` warns only today |

Quoted metas (`"` ends token; **optional WS before `"`** per **D8b**): `K"C"C4Q` · `K "C"C4Q` · `?"hi"` · `\"ctx:4Q;6"` · `<"loop"` · `="loop"` · `>"TURN"`

**Lexical (D12 🟢):** WS = skip · **`:`** = optional top-level **EOS** *(spec; fw partial)* · **`;`** = duty in notes only (**D5**) · **except** **`]:N`** / **`]:±N`** (**S4**/**S12**) · abut OK (`K"C"T120`) · **`:`** alternative (`K"C":T120`) when EOS lands

**`*` purpose (D19 + D23):** at **root** (`**b_stop_is_return` false**), hard **play-stop** — including inside **`>"…"`** subroutine bodies; use **`/`** to return from **`>`**. Under **`L`**, flag **true** → **`*`** returns to **parent** (pop **`>`** or **L**). Main still ends with **`*`** (or NUL) at root.

**Bench smoke (fw):** `@ smoke scale @ CQ4DEFGABC5 *` — menu **`m` → `1`** (T120 is session default; explicit `T120` still OK)

**Golden regression (fw):** `python scripts/play_bench.py test smoke` · `grammar_torture` (v1.2 wire) · `grammar_torture_v11` (v1.1 wire archive) · `duty_percent` · `multi_dot` · `repeat_*` · `labels_*`

---

## Inside note / **`N`** suffix (after **A–G** or **`N` digits**)

| Char | Usage | fw |
|------|--------|-----|
| **#** **+** | Sharp | **fw** |
| **b** **-** | Flat | **fw** |
| **n** | Natural accidental — not top-level **`N`** | **fw** |
| **0–9** | Octave digit — inherit if omitted | **fw** |
| **W** **H** **Q** **I** | Whole / half / quarter / eighth | **fw** |
| **X** **Y** | 16th / 32nd — **D4** 🟢 v1.1 | **fw** |
| **.** | Dotted — one `.` → ×1.5; chained `.` → **2 − 2⁻ⁿ** on base (**D26** v1.2) | **fw** |
| **_** **!** **;** **;n** **;nn** | Duty: legato / staccato / normal / n-of-8 / percent (**G10**) | **fw** |

Any order after note letter; **one** duration per token (explicit or inherited). **S9:** duplicate modifier class in one token → **last wins**; **STRICT** may fatal. **`;6`** = 75% (6/8); **`;60`** = 60% (two digits).

---

## Comment-only · reserved

| Context | Char | Usage |
|---------|------|--------|
| inside `@…@` | **\\@** | Literal `@` |
| deferred | **\\|"name"** | Sync barrier (**S3** 🔵) |
| reserved | **L** | **Library** — `L"…"` (**D23** 🔵); **L** stack; sets **`b_stop_is_return`**; nested **L** inherits |
| retired | **M** | use **P** · **\*** was label define → **`<`**; now **END** |
| retired | **S** | was Shift/transpose → use **`&`** (**D21**) |

---

## Unallocated printable ASCII

No role as top-level lead, note/rest descriptor modifier, repeat/label/quote structure, **`K`/`&`/`\\`/`L` token syntax**, or deferred **`\\|"…"`** sync. **`@…@` / `"…"` content** may still use any character. **Exception:** **`:`** at top level = optional hard end-of-command (**D12**); **`]:N`** repeat tail (**S4**) is structural, not terminator.

` ' ( ) , ` { } J M S U Z a c d e f g h i j k l o p q r s t u w x y z`

---

_Bump when new leads land, caps change, or firmware catches up to spec._
