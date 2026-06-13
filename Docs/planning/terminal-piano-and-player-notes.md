# Terminal piano UI & PLAY player bench notes

**Status:** Spec + wishlist captured — **PLAY v1 Phase 1 not started** in tree; terminal piano UI **after** PLAY skeleton + smoke path.

**When to load:** User says *terminal piano*, *virtual synthboard*, *PLAY player*, *I8*, *I9*, *player submenu*, *smoke scale*, or *TRS-80 keyboard*.

**Authoritative locked decisions:** [play-v1-implementation-plan.md](play-v1-implementation-plan.md) (**I8**, **I9**, handle API, feature fence). This doc is a **focused agent brief** — not a substitute for the full plan.

**Cross-refs:** [uart_stream-port-notes.md](uart_stream-port-notes.md) · [PLAY_language_design.md](../PLAY_language_design.md) · [play-lead-char-cheat-sheet.md](play-lead-char-cheat-sheet.md) · existing interactive player: `Docs/Interactive noteplayer spec.txt` · `App/` note player via debug menu **`p`**.

---

## Heritage

The PLAY meta-language and the **terminal piano display** trace to the author’s **TRS-80 Model I** commercial music player — musician-first UX, primitive top-down keyboard on a text terminal. Modern reprise: **ANSI + UTF-8** on the G474 debug UART (**USART2**, 921600).

Plan traceability: **T5** in [play-v1-implementation-plan.md](play-v1-implementation-plan.md).

---

## Three related “player” surfaces (do not conflate)

| Surface | Menu | API / mechanism | Uses PLAY? |
|---------|------|-----------------|------------|
| **Interactive note player** | Top **`p`** (and submenu dup) | `v_note_player_run()` — blocking terminal keys | **No** — CORDIC synth, monophonic |
| **PLAY interpreter bench** | Submenu **`m`** → **`1`** / **`s`** | `b_play_start(…)` + jobs + **I4** HW tick | **Yes** |
| **Terminal piano display** | *(none yet — I8 consumer)* | ANSI redraw on NOTE on/off | **Observes** PLAY via **I8** hook |

**Synth ownership:** Only one owner of **`synth_engine`** at a time. While PLAY is **RUNNING**, refuse submenu **`p`** or **`v_play_stop`** PLAY first (**I9**).

---

## PLAY v1 public API (locked sketch)

```c
typedef enum {
    PLAY_STATE_IDLE = 0,
    PLAY_STATE_LOADING,   /* v2+ — unused in v1 */
    PLAY_STATE_READY,     /* v2+ staging — unused in v1 */
    PLAY_STATE_RUNNING,
    PLAY_STATE_STOPPED,
    PLAY_STATE_ENDED,
    PLAY_STATE_FAULT
} play_state_t;

typedef struct play_instance play_instance_t;

struct play_instance {
    play_state_t  e_state;
    const char   *psz_src;
    uint32_t      u32_src_offset;
    /* tempo, voice, fault, title — TBD at implement */
};

typedef void *play_handle_t;

#define PLAY_HANDLE_NULL              ((play_handle_t)NULL)
#define PLAY_HANDLE_AS_INSTANCE(h)    ((play_instance_t *)(h))  /* bench read-only */

bool b_play_start(const char *psz_src, play_handle_t *px_out_handle);
void v_play_stop(play_handle_t px_handle);       /* NULL → no-op */
bool b_play_is_running(play_handle_t px_handle);
```

- **`play_handle_t`:** opaque to product code; underlying object is **`play_instance_t`** pool entry.
- **`PLAY_INSTANCE_MAX = 1`** at v1.
- Source: **`const char *`**, NUL-terminated, **on-chip only** (flash `.rodata` or static RAM). Parser **never mutates** score bytes.

---

## Debug menu — player tests submenu (**I9** 🟢)

| Location | Key | Action |
|----------|-----|--------|
| Top debug menu | **`m`** | Open **`--- Player tests and experiments ---`** |
| Submenu | **`1`** | **`b_play_start(psz_play_smoke_test, &px_active_play)`** |
| Top menu | **`S`** | **playstr hook** — same `PLAY>` / `i_getline()` as submenu **`s`**; scripts ESC×3 then **`S`** |
| Submenu | **`s`** | Prompt **`PLAY>`** · `i_getline()` · ≤ **`PLAY_DEBUG_LINE_MAX`** (4096, heap) → **`b_play_start(...)`** |
| Submenu | **`p`** | **`v_note_player_run()`** — duplicate of top **`p`**, **not** PLAY |
| Submenu | **ESC** | **`v_play_stop(px_active_play)`** then leave |
| Top menu | **`p`** | *(unchanged)* interactive note player |

**Config (`play_config.h`):**

```c
#define PLAY_DEBUG_MENU_HOOK_KEY ('S')
#define PLAY_DEBUG_LINE_MAX   (4096U)
#define PLAY_INSTANCE_MAX     (1U)
```

**Smoke preset (`play_presets.c`):**

```c
const char *psz_play_smoke_test =
    "@ smoke scale @ T120 O4 C4Q D4Q E4Q F4Q G4Q A4Q B4Q C5Q *";
```

Expose via **`play_presets.h`** (`extern const char *psz_play_smoke_test;`). Self-terminates with **`*` END**.

**Menu rules:**

- Handlers **return immediately** — PLAY runs async (jobs + **I4** tick), not inside menu callback.
- Static **`play_handle_t px_active_play = PLAY_HANDLE_NULL`**; clear after stop.
- ESC/RETURN auto-stops active PLAY (mirror **`i`** synth submenu behavior).

**Phase 1 deliverable (when coding starts):** `play.h` / `play.c` / `play_config.h`, jobs, **I4** tick, `play_presets.c`, submenu **`m`**, smoke through **`synth_engine`** — parser can grow incrementally; smoke path is the gate.

---

## Resolve hook (**I8** 🟢) — observer for piano + tests

Optional **`play_resolve_fn_t`** callback — **Release-safe**, default **NULL**.

**Fires:** immediately after parser commits a **valid, complete executive** (note, rest, meta, `?`, structural tokens — leaning yes on `[` `]` `=` `>`).

**Does not fire:** rejected tokens (**S7g** 🟡 leaning **no** on rejects); skipped `@ … @` comment regions.

**Consumers:**

1. **Verbose UART echo** — log source span + decoded summary (optional post-**I9** minimum).
2. **Golden trace tests** — compare resolve sequence without mocking I2S.
3. **Terminal piano UI** — **`PLAY_RESOLVE_NOTE`** → highlight key; META → update tempo/voice/key widgets from **`x_memory`**.
4. **LED animation** — NOTE → hue timeline; META → global brightness.

Payload struct **`play_resolve_event_t`** — finalized at implement; illustrative shape in plan **I8** section.

Hook body **must not block** on I2S; UART work should stay short or defer to a job (see threading below).

---

## ANSI terminal piano UI (“virtual synthboard”) — wishlist

**Not v1 Phase 1.** Implement after PLAY skeleton + smoke path. Depends on [uart_stream-port-notes.md](uart_stream-port-notes.md) for non-blocking TX.

### Layout (TBD on real terminal)

- **Two terminal rows** — full note range without absurd window width.
- Each **white key = 3 character cells** wide.
- **Black keys** — 2-column indents / UTF-8 block glyphs between naturals.
- Test glyphs in **Tera Term** / **Windows Terminal** @ **921600**.
- Project already has **`App/Inc/ANSI.h`** for escape sequences.

### Highlight behavior

- **ANSI SGR** (foreground/background/reverse) to redraw **only changed key(s)** on NOTE on/off.
- Avoid full-screen refresh every event.

### Event source

- Primary: **PLAY `I8`** on **`PLAY_RESOLVE_NOTE`** (and optionally NOTE-off / rest semantics at implement).
- Optional later: hook from **`p`** note player and **`?`** debug paths.

### Threading

| Era | Model |
|-----|--------|
| **Today (co-op)** | PLAY posts coalesced **`JOB_PIANO_DRAW`** (or ring of `{midi_note, on/off}`) — handler emits ANSI diffs via **`uart_stream`**. |
| **Future (RTOS)** | Low-priority **piano-draw task** + message queue — same events; do not block synth/audio jobs. |

Good motivating example when FreeRTOS lands (**AGENTS.md**).

---

## Implementation order (recommended)

1. **PLAY v1 skeleton** — handle API, **I4** tick, jobs, submenu **`m`**, smoke scale → **`synth_engine`** ([play-v1-implementation-plan.md](play-v1-implementation-plan.md) Phase 1).
2. **`uart_stream` on USART2** — [uart_stream-port-notes.md](uart_stream-port-notes.md).
3. **I8 hook** — register piano consumer (or stub job queue first).
4. **Terminal piano draw** — two-row layout + incremental ANSI diffs.
5. Post-v1: Star Wars preset, **`playverbose`**, LED strip consumer, etc.

---

## Open items (non-blocking Phase 1)

| ID | Topic | Status |
|----|-------|--------|
| **S7f** | Strict stop-on-first-fault vs continue | 🟡 |
| **S7g** | I8 on rejected tokens | 🟡 leaning **no** |
| **S11** | v2+ multi-instance load/sync | 🔵 deferred |

---

## Suggested session openers

**PLAY implementation:**

```
/read-the-docs PLAY v1 — play-v1-implementation-plan.md summary + this file
Start play.h/play.c skeleton, I9 menu m, smoke preset.
```

**Terminal piano (later):**

```
/read-the-docs terminal piano — terminal-piano-and-player-notes.md + uart_stream-port-notes.md
I8 consumer + JOB_PIANO_DRAW + ANSI two-row layout.
```

---

*Captured 2026-06-11. Update when PLAY Phase 1 lands or piano UI scope changes.*
