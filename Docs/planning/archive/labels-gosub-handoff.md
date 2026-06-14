# Agent Handoff: G5 — Runtime Labels / goto / GOSUB / RETURN (D16–D19, S2)

**Date:** 2026-06-13 (S2 model revised 2026-06-14 — goto is a pure jump)  
**Status:** ✅ SHIPPED 2026-06-14 — archive via `/cleanup-docs`
**MSG:** **G5** · **Plan refs:** D16, D17, D19, **S2** (revised), S7a, S7d, S7e, I2 (🟢 — do not re-litigate)
**Bench:** COM9 @ 921600 · ST-Link SN from `scripts/bench.defaults.json` (`003C00193137510C39383538`)

**Authoritative plan:** [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — MSG **G5** row, **S2** detail (revised 2026-06-14), D19 detail
**Predecessor (what G4 left for you):** [archive/labels-preparse-handoff.md](archive/labels-preparse-handoff.md) **§10**
**Template:** [focused-implementation-handoff-template.md](focused-implementation-handoff-template.md)

---

## 1. Mission (one screen)

Make the label control-flow tokens **execute** at runtime. G4 already built the **pre-parse label table** (with resolved `u32_define_offset` per entry) and verified every `>`/`=` reference; today those tokens are still **zero-time skip stubs**. G5 turns them into real jumps:

- **`>` goto — pure PC jump (S2 revised 2026-06-14).** Resolve the target, set PC to its `<` define offset, **carry the current context unchanged**. Forward and backward are the **same code path** — no snapshot save, no restore, no offset comparison. A backward goto loops and **accumulates** whatever the body mutates (that's intended; reset-per-iteration is `[ ]:N`, which already exists).
- **`=` GOSUB.** Push a call frame (return PC + caller snapshot), jump to the target define. Callee **inherits** caller context on entry.
- **`/` RETURN.** Pop the frame, **restore** the caller snapshot, resume at return PC. **Empty stack → hard abort** (S7a fatal, not warn-and-continue).
- **`<` define at runtime.** Context **no-op** — just skip the token (no snapshot capture; the per-label snapshot idea is gone with the S2 revision).

**Exit:** new terminating goldens `labels_goto` + `labels_gosub` pass under STRICT, and the trimmed `grammar_torture` label block runs to `*` END under STRICT with zero fatals (see §5/§7).

**Out of scope (do NOT ship here):**
- `L"…"` library GOSUB (**D23**) / `b_stop_is_return` "`*` = return" path — **not v1**; `*` stays hard STOP; `L` stays the existing "library (L) not in v1" recoverable warn.
- **G8** key-LUT-in-snapshot (`ai8_key_lut` absent from `play_ctx_snapshot_t`) — leave the snapshot as-is.
- Pre-parse / resolver changes — G4 owns that and it is ✅.

---

## 2. Locked wire / behavior (no design debate)

| Token | Form | Runtime action |
| ----- | ---- | -------------- |
| `<n` / `<"name"` | define | **Context no-op** — skip the token (as today's stub, minus any snapshot) |
| `>n` / `>"name"` | goto | Lookup; set PC = target `u32_define_offset`; **carry ctx**. Same for forward + backward (**S2 revised**) |
| `=n` / `="name"` | GOSUB | Lookup; push {return PC after the ref token, caller snapshot}; jump to target define; callee inherits ctx (**D19**) |
| `/` | RETURN | Pop frame; restore caller snapshot; resume at return PC; **empty stack → FATAL** (**S7a**) |
| `*` | END | Unchanged — hard STOP (D19; `b_stop_is_return` false in v1) |

**Landing convention (from G4 §10):** jump targets land on the `<` char (`u32_define_offset`); the runtime `<` handler then skips the define. Idempotent.

**Stack:** the call stack shares the **S7e** depth cap `PLAY_STACK_MAX_DEPTH` (=10, `play_config.h`). Overflow → FATAL, same class as repeat-stack overflow.

**Snapshot fields:** GOSUB/RETURN save+restore exactly the fields `v_play_snapshot_save` captures today (tempo, octave, volume, beat-unit, dur, dot, duty, transpose, voice).

---

## 3. Code anchors (today — `App/Src/play.c`)

| Symbol / line | What exists before this session |
| ------------- | ------------------------------- |
| `play_label_entry_t` (~L92) | `e_kind`, `u16_num_id`, `ac_name`, `u32_define_offset`, `b_referenced`. **No change needed** (S2 revision = no per-label snapshot). |
| `play_ctx_snapshot_t` (~L51) | Snapshot struct — no key LUT (G8). Reuse for GOSUB frames. |
| `play_repeat_frame_t` + `ax_repeat` / `u8_repeat_depth` (~L78/L109) | Repeat stack — **model your call stack on this**. |
| `i8_play_label_find` (~L700) | Linear lookup by kind+id/name — reuse for runtime ref resolution. |
| `v_play_snapshot_save` (~L2008) | Saves ctx. **No restore fn exists** — add `v_play_snapshot_restore` for `/` RETURN. |
| `b_play_skip_label_define` (~L959) | Runtime `<` stub — already a context no-op skip; keep as-is (or inline). |
| `b_play_skip_label_ref` (~L993) | Runtime `>`/`=` stub — replace with goto / GOSUB logic. |
| dispatch `if (c_ch == '<')` / `'>' \|\| '='` (~L2911–2928); `/` falls to "unsupported executive" (~L2929) | **Split `>` (goto) from `=` (GOSUB); add a `/` RETURN case.** |
| `b_play_open_repeat` / `b_play_close_repeat` (~L2475–2560) | Repeat stack model + snapshot at `[` (see §8 note). |
| `v_play_end_sequence` (~L2561) | `*` / NUL termination — leave alone. |

**Hard rule:** edits in `App/` only. No `Core/` edits. Driver stays RTOS-agnostic.

---

## 4. Implementation checklist

- [x] Add `v_play_snapshot_restore(px_rt, const play_ctx_snapshot_t *)` mirroring `v_play_snapshot_save` (write back to `x_public` / `x_note_mem` / members) — used by `/` RETURN.
- [x] Runtime `<` define: context no-op, skip token (no snapshot).
- [x] Runtime `>` goto: lookup → set PC = `u32_define_offset`; carry ctx; **no fwd/bwd branch, no restore**. Missing (shouldn't happen post pre-parse) → FATAL (S7a).
- [x] Add a call-stack array + depth (reuse `PLAY_STACK_MAX_DEPTH`); frame = {return offset, caller snapshot}.
- [x] Runtime `=` GOSUB: push frame, jump to define offset; overflow → FATAL.
- [x] Add `/` RETURN dispatch case: empty stack → FATAL (S7a); else pop, restore caller snapshot, set PC to return offset.
- [x] Reset call-stack depth in session reset (alongside `u8_repeat_depth`).
- [x] Goldens green (see §5): `labels_goto`, `labels_gosub`, full `grammar_torture`; fatal vectors stay green.
- [x] MSG **G5** → ✅ in plan (§ MSG + **I10** audit); update [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) (runtime goto/GOSUB now YES; note goto = pure jump).
- [x] `/wrapup` → new session handoff; this file → archive candidate.

---

## 5. Golden / bench exit criteria

```text
python scripts/play_bench.py --reset --timeout 60  test labels_scan       # G4 still green
python scripts/play_bench.py --reset --timeout 60  test labels_goto       # NEW — §7a
python scripts/play_bench.py --reset --timeout 60  test labels_gosub      # NEW — §7b
python scripts/play_bench.py --reset --timeout 120 test grammar_torture   # full I1 fence, STRICT, must END
```

Pre-parse fatal vectors (`labels_fatal_missing`, `labels_fatal_quote`) must **stay** green.

---

## 6. Sub-task IDs (optional commit messages)

| ID | Deliverable |
| -- | ----------- |
| **G5a** | `v_play_snapshot_restore`; runtime `<` no-op skip; `labels_goto` golden |
| **G5b** | `>` goto pure PC jump (S2 revised — both directions carry ctx) |
| **G5c** | `=` GOSUB call stack + `/` RETURN + empty-stack hard abort (D19/S7a); `labels_gosub`; torture passes |

---

## 7. Golden vectors

### 7a. `labels_goto.play` — NEW (terminating goto model, user-specified)

Four blocks in source order B0, B1, B2, B3; control flow visits each **exactly once** in order **0 → 2 → 1 → 3**, then `*` stops:

```text
@ G5 goto exerciser — visits 0,2,1,3 once each, then ends (no loop) @
T120 O4 %Q
<0 C4Q >2
<1 E4Q >3
<2 D4Q >1
<3 G4Q *
```

Trace (PC starts at `<0`): `<0` C4 → `>2` (forward) → `<2` D4 → `>1` (backward) → `<1` E4 → `>3` (forward) → `<3` G4 → `*`. Audible order **C D E G** confirms the jump sequence. Mix in a string label (e.g. `<"end"` / `>"end"`) for extra ref-resolution coverage.

Covers: forward goto, backward goto, out-of-order PC movement, numeric/string ref resolution, clean termination. (With the S2 revision goto carries context and never restores — this golden just proves PC movement + termination.)

### 7b. `labels_gosub.play` — NEW (GOSUB/RETURN: forward + backward + nested)

Octave bounce proves caller-context restore on every `/`:

```text
@ G5 gosub exerciser — fwd + backward + nested; octave bounce proves restore @
T120 O4 %Q
C4Q ="up" E4Q ="down" G4Q
*
<"up"   O5 C5Q /
<"down" O3 C3Q ="up" /
```

Trace (watch the octave): main O4 → `C4` → call `up` (O5 `C5`) → `/` restore O4 → `E4` (proves restore) → call `down` (O3 `C3` → nested call `up` O5 `C5` → `/` restore O3 → `/` restore O4) → `G4` → `*`. Pitches **C4 C5 E4 C3 C5 G4**; octave pattern **4,5,4,3,5,4**. If restore breaks, octaves stick high. Covers forward call (main→up/down, defined below), backward call (down→up, defined above), 2-deep stack, caller-context restore on `/`.

Register both in `scripts/play_golden/tests.json` (tier `feature`, policy `strict`; aliases `G5`/`G5-goto`, `G5-gosub`).

### 7c. `grammar_torture.play` — fix the infinite-loop label block

Today the torture label block (~lines 32–43) has `<"bk" T100 O6` then `>"bk" …`, which under real goto loops forever and never reaches `*`. **Rewrite that block to a terminating form** that still exercises `<` `>` `=` `/` (keep at least one nested `=` inside a callee — the torture is the full-fence vector). Forward-only gotos + the `=`/`/` pattern from §7b is the simplest fix. **Do not** add loop caps to the interpreter — backward goto looping is by design (S2); the fix is in the score.

---

## 8. Notes / deferrals

- **G8 (key LUT in snapshots, 🟡):** `play_ctx_snapshot_t` omits `ai8_key_lut`, so a `/` RETURN across a `K"…"` key change won't restore the key. That's the existing G8 gap — **leave it for G8**, don't widen scope.
- **Repeat snapshot restore (S4):** `b_play_close_repeat` re-enters the body without calling a restore today, so per-pass reset isn't actually wired. If your new `v_play_snapshot_restore` is general, wiring it into repeat re-entry would close that S4 gap too — **optional**, only if cheap and it doesn't destabilize the torture. Confirm with the parent before expanding scope; otherwise note it for a follow-up.

---

## 9. Next session pointer

After **G5** ✅: only **G8** (key LUT in repeat/label snapshots) remains open in v1 MSG. Run `/cleanup-docs` to archive this file, then the next focused session reads the **G8** row.

---

**Lifecycle:** when **G5** is ✅ and the checklist is closed, run **`/cleanup-docs`** — archive or delete this file.

**End of labels-gosub-handoff.md**
