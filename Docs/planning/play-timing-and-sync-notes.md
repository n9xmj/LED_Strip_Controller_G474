# PLAY timing accuracy & multi-instance sync — design notes

**Status:** Design rationale captured 2026-07-18. **§3 single-instance drift fix IMPLEMENTED &
bench-verified 2026-07-19** (see §3 acceptance result). Multi-instance sync (§6), external-device /
MIDI resync (§7), and the scheduler/generator split (§8) remain **forward-looking** — recorded here
so the eventual polyphony refactor and the PLAY→MIDI fork don't re-derive this reasoning from scratch.

**When to load:** *PLAY timing*, *note drift*, *fixed-point tick*, *32.32*, *polyphony sync*,
*multi-instance*, *transport clock*, *MIDI fork*, *SAM2695*, *synth engine design*, *OPL3/FM*.

**Cross-refs:** [play-v1-implementation-plan.md](play-v1-implementation-plan.md) ·
[terminal-piano-and-player-notes.md](terminal-piano-and-player-notes.md) ·
[../PLAY_language_design.md](../PLAY_language_design.md) · code:
[`App/Src/play.c`](../../App/Src/play.c) (scheduler), [`App/Inc/synth_engine.h`](../../App/Inc/synth_engine.h),
[`App/Inc/platform.h`](../../App/Inc/platform.h) (`ELAPSED_TIME`, wrap-safe interval idiom).

> Line numbers below are point-in-time (2026-07-18); verify against current source before editing.

---

## 1. TL;DR

- The PLAY scheduler runs on the shared **1 ms SysTick** counter (`su32_sched_tick`). Note
  durations are computed to 1 ms and **floored** — a systematic, one-directional quantization
  error that **accumulates** over a long run of short notes. Left alone, tracks audibly
  de-synchronize. This is the problem.
- **Fix (this session):** carry the sub-tick remainder in a per-instance **32.32 fixed-point**
  accumulator (Bresenham / error-diffusion), and anchor each note's deadline **cumulatively**
  against the ideal timeline instead of re-reading "now" every note. Result: total timing error
  vs. ideal is bounded to **±1 tick, non-accumulating, forever**.
- **Key insight for polyphony:** with that fix, multiple PLAY instances on the *same MCU*
  sharing `su32_sched_tick` **cannot drift relative to each other**, given a common origin. There
  is nothing to periodically resync — and resetting the fraction would *inject* error, not remove
  it. Absolute sync is achieved by an **architecture** (one shared transport clock + absolute
  note positioning), not by runtime correction.
- Periodic re-anchoring **is** legitimate — but only at the boundary to a device with its **own
  clock** (the SAM2695 over MIDI, or a future second MCU), where two oscillators genuinely drift.

---

## 2. The problem — two distinct drift sources

The current scheduler ([`play.c`](../../App/Src/play.c)) has **two** independent drift sources.
Naming both matters, because the obvious "fixed-point" fix only addresses one of them.

### A — per-note quantization (systematic, always loses)

`u32_play_calc_note_ticks` (~:1595) computes the ideal note length as an integer:

```c
uint32_t u32_ms    = (uint32_t)(u64_num / u64_den);          // floor — discards < 1 ms
uint32_t u32_ticks = u32_ms / (PLAY_SCHED_TICK_US / 1000U);  // TICK_US = 1000 → /1 (a 2nd floor)
```

At `PLAY_SCHED_TICK_US == 1000` the two floors collapse to **one floor per note**. Every note is
rounded *down* by up to ~1 tick. Because it is always a floor, the error is **systematic and
one-directional**: the track consistently runs short → speeds up vs. ideal → a long sequence of
short notes drifts steadily out of sync.

### B — the scheduler re-anchors to real "now" every note (structural)

`v_play_schedule_note` (~:2175) sets the note-end from the *observed* current tick:

```c
uint32_t u32_now = su32_sched_tick;               // reads REAL current time
px_rt->u32_note_end_tick = u32_now + u32_note_ticks;
```

Between notes there is a `PLAY_SCHED_PARSE` phase that runs on a later poll, so note N+1 doesn't
start exactly at note N's ideal end — it starts whenever the parser gets to it (+0…N ticks of
latency), and that latency is **baked into the new anchor and never given back**. This is a
second accumulator of lateness, present even with perfect per-note rounding, and it is the one
that matters most for polyphony because it is per-track and depends on each track's own token
lengths and poll phase.

---

## 3. The fix (this session's scope) — 32.32 accumulator + cumulative anchor

### Locked decisions

| # | Decision | Choice |
|---|----------|--------|
| D1 | Sub-tick representation | **32.32 fixed-point** (32-bit integer tick "mantissa" == `sizeof(su32_sched_tick)`; 32-bit fraction). M4 is a 32-bit datapath — 32-bit ops are as cheap as 16-bit; take the headroom. |
| D2 | Storage shape | **Split struct**: integer tick stays a normal `uint32_t` (full 49.7-day range, matches the SysTick wrap); a separate 32-bit fraction accumulator rides alongside. *Not* a packed 24.8 word — packing would shrink the integer range to ~4.66 h for no benefit. |
| D3 | Anchoring | **Cumulative**: advance the note-end deadline by the *ideal* duration each note; never re-read `su32_sched_tick` mid-stream. |
| D4 | Where the fraction applies | **Note-to-note boundary only.** The intra-note duty/gap split does *not* accumulate (each note re-derives its gate from its own start), so it stays integer. One accumulator per instance. |
| D5 | Fire comparison | **Wrap-safe** `(int32_t)(su32_sched_tick - deadline_tick) >= 0` (the compare-form cousin of `ELAPSED_TIME` in [`platform.h`](../../App/Inc/platform.h)). Survives the 32-bit tick wrap *and* makes best-effort catch-up fall out for free. |
| D6 | Scope fence | Single-instance drift fix only. **No** synth refactor, **no** polyphony, **no** event producer/sink seam this session. |

### Mechanism

```c
/* per note, at the note-to-note boundary: */
frac          += dur_frac;                    /* 32.32 fractional part of ideal duration */
deadline_tick += dur_int + (frac >> 32);      /* carry rolls whole ticks into the integer part */
frac          &= 0xFFFFFFFFu;                 /* keep only the fraction */
/* fire when: (int32_t)(su32_sched_tick - deadline_tick) >= 0 */
```

`u32_play_calc_note_ticks` is reworked to emit `{int, frac}` (compute the ideal duration directly
in fixed-point via a shifted division `(numerator << 32) / denominator`, keeping the remainder
rather than flooring to integer ms). The fraction **never** participates in the fire test — it
only decides when a whole tick rolls into the integer deadline, so the comparison stays a plain
32-bit op.

### Seed points

`now` legitimately re-enters at exactly three places; everywhere else the timeline free-runs
cumulatively: **start**, **resume/seek**, **restart-after-stop** → `deadline_tick = now; frac = 0`.

### Tempo changes are clean

The accumulator is in **tick units** (tempo-independent), so a mid-piece tempo change just alters
future `dur` values; the carried fraction sails across untouched. This is the payoff over a raw
numerator/denominator remainder, which would need renormalizing on every tempo change.

### Acceptance criterion — MET (bench, 2026-07-19)

Torture: **T175, 200 quarter-notes** (ideal 60000/175 = **342.857 ms/note**, non-integer ticks so
the carry fires almost every note). Each note's fire-tick captured via the `play_resolve_fn_t` hook
(the ` t=<tick>` field added to the NOTE/REST resolve trace in `debug_menu.c`) against
`su32_sched_tick`. Results:
- **Detrended per-note drift span: 0.857 tick over 199 notes** — bounded, non-accumulating (the
  342/343 quantization band). Old floor-per-note code would have finished **170 ticks short**.
- **Aggregate elapsed 68572 ms vs 68571 ms ideal (+1 ms / 200 notes)** — independent cross-check
  from `[PLAY]` start/ended LOG timestamps.
- A **one-time ~2.6-tick offset on note 0** (`v_synth_engine_set_tone` I2S/DMA spin-up burns ~3 ms
  between the timeline seed and the trace emit) is a fixed *phase* offset, **not drift** — the
  note's `note_end` is anchored to the pre-setup seed, so the offset never enters the timeline.
  (Future nicety: pre-warm I2S before the first note if first-note onset latency ever matters.)

Test harness: `play_drift_test.py` (drives the harness `P` op via `PlayBenchClient`, parses fire
ticks, checks detrended span + aggregate). Not yet promoted to `/playtest` golden infra — see §10 T5.

---

## 4. Why the accumulator does not drift (the guarantee)

The Bresenham carry is **exact**. The running fraction is not an error that grows — it is *credit
owed to the timeline*, and the carry pays it back in full the instant it reaches a whole tick.
Instantaneous quantization error is bounded to ±1 tick and **never accumulates**.

An instance's k-th boundary is `B(k) = seed + Σ dur(1..k)`, summed exactly in 32.32, and it fires
at `floor(B(k))`. That sum is a deterministic, exact function of the **score** (notes + tempo) —
not of real time except at the seed. The only rounding is quantizing each note's ideal duration to
32.32 (≤ 2⁻³² tick per note, deterministic), and it does **not** compound because the carry
recovers it. Over a 4-hour piece the total error vs. ideal stays ±1 tick, flat.

---

## 5. Why NOT to periodically reset the fraction

A tempting idea: every N beats, snap the fraction back to the integer tick, all instances at once.
**Don't.** Because the accumulator is exact (§4), zeroing the fraction *throws away* up to ~0.9999
tick of credit that was about to become a whole tick → the timeline jumps up to ~1 tick early,
**permanently**, from that point. Do it every 50 beats and you have *manufactured* up to ~1 tick
of error per 50 beats — the very drift the accumulator prevents, injected by hand.

Self-defeating corollary: to fire a coordinated reset "at the same real time" across instances you
already need a shared clock good enough to align them — but if you *have* that clock (you do), you
never needed the reset.

Zeroing is only harmless when the bar length is an exact integer number of ticks (fraction is
already ~0 at the barline — a no-op). Triplets / odd tempi break that. **Rule: never reset the
fraction. Let it run.**

---

## 6. Multi-instance sync (future polyphony) — architecture, not runtime correction

Two fundamentally different sync regimes; do not conflate them:

**(a) Multiple PLAY instances on the *same* MCU, sharing `su32_sched_tick`.** They physically read
one clock. Given a **common origin**, they *cannot* drift relative to each other — each quantizes
the *same* shared real tick against an *exact* function of its score. No second oscillator exists
to disagree. Resync is unnecessary and counterproductive.

**(b) PLAY driving a device with its *own* clock** (SAM2695 / future second MCU). Genuine
two-oscillator drift. Periodic re-anchoring is legitimate — see §7.

### The airtight on-chip answer: a single shared transport clock

Rather than N per-instance accumulators trying to stay in lockstep, maintain **one** master
transport:

- A single 32.32 "song position" accumulator, advanced once per tick against `su32_sched_tick`.
- Each instance expresses note boundaries as **absolute positions on that shared transport**, not
  as cumulative sums from its own private start.
- "Simultaneous" is then true *by construction*: two notes at the same transport position quantize
  the same shared value against the same real tick → same integer tick, always. N streams, **one
  clock, one accumulator** → nothing to drift relative to.

This also erases a second-order gremlin: independent per-instance floors of "4 quarters" vs "8
eighths" across the same bar could differ by a few 2⁻³² LSBs/bar (negligible — ~10⁹ bars to reach
one tick — but nonzero); with a shared transport both floor the *same* master position → exactly
zero.

**Forward-compat:** the per-instance 32.32 accumulator built in §3 is the **degenerate one-instance
case** of exactly this transport. Polyphony hoists the accumulator out of `play_runtime_t` into a
shared transport and has instances reference it — relocation, not re-derivation. Corollary for §3
implementation: **do not bake a per-instance origin in so hard that a future shared transport can't
seed all instances from one common song-origin.**

### Coherent multi-instance startup: pre-init + starting-gun barrier

Instances get created asynchronously, in any order, over whatever real-time interval setup takes.
To make them sample-aligned they must NOT each seed from their own first-note tick — they arm, then
release together on a **starting gun**. The barrier is how instances *join* the shared transport (§6)
coherently; the four rules that make it exact:

1. **The gun hands out the origin — instances don't grab it.** The gun handler captures
   `transport_origin = su32_sched_tick` **once** into shared state, *then* releases all instances;
   each seeds `ideal_tick = transport_origin` (reads the shared value, never its own clock). One
   capture, N readers → bit-identical origins regardless of release order. If instances each read
   `su32_sched_tick` at release, a SysTick boundary mid-release skews them by a tick.
2. **Hold at the first *timed event*, not the first token.** All leading meta (T/O/key/transpose)
   is processed *during* ARM so the gun fires directly into each instance's first note/rest;
   otherwise an instance with a long preamble fires note 0 several polls late. (This is the same
   seam as §3's lazy seed-on-first-note — just gated behind the gun instead of firing immediately.)
3. **Engine-ready is a gun precondition** — the synth/generator engine spins up during ARM, before
   the gun. The gun fires into a hot engine, so the one-time I2S-onset offset measured in §3
   (~2.6 tk on note 0) simply doesn't exist on the polyphony path. (Pre-warming the engine on the
   mono path would erase it there too.)
4. **RTOS-clean.** The barrier is a latch/event-group (cooperative: an `ARMED→RUNNING` flag flipped
   by the gun; FreeRTOS: event group + transport-origin write). `transport_origin` is
   write-once-at-gun, read-only during the run → no per-task duplication needed (unlike the Berry
   single-globals). Safe to hoist to a true global.

This same barrier IS the §7 "reconcile to master" primitive: hot-starting a track mid-song aligned
to the current bar is the gun fired at an arbitrary transport position instead of at origin.

> Caveat: a shared transport advances at one tempo. Tracks that *want* independent tempi
> (polyrhythm / tempo canon) are by definition not meant to stay tick-locked, so the shared-transport
> guarantee simply doesn't apply to them — which is correct, not a limitation.

---

## 7. External-device / MIDI resync (future PLAY→SAM2695 fork)

At the boundary to a device with its own clock, §5's prohibition flips: re-anchoring is **required**.
Two crystals + transport jitter genuinely drift. The mechanism is standard MIDI:

- **MIDI real-time clock** (`0xF8`, 24 PPQN) or a SysEx song-position message: periodically tell the
  external device "we are *here* on the transport."
- A correct resync **reconciles to the master, carrying the fraction** — it re-derives the next
  boundary from the shared transport position (fraction included). It **never** discards sub-tick
  state (that's the §5 mistake). This same "reconcile to master" primitive is also how you'd
  **hot-start a new track mid-song** aligned to the current bar.

MIDI has **no note-length field** — you schedule note-on and note-off as two *separately-timed*
events, so the drift-free scheduler matters *more* here than for the internal synth: any scheduler
jitter is directly audible as sloppy timing.

---

## 8. Scheduler ↔ generator layering (future synth engine)

The current [`synth_engine`](../../App/Inc/synth_engine.h) is already MIDI-shaped:
`v_synth_engine_set_tone` = **note-on** (hot-swap retrigger + light attack to de-pop),
`v_synth_engine_stop` = **note-off** (short decay/release then drain). The header already flags
future FM / full ADSR / polyphony.

The clean division of responsibility:

- **Scheduler (PLAY front-end):** owns **note-on/off timing** — *when* events fire on the transport.
  It should be an **event producer**: "note-on at ideal tick T, note-off at tick T+n."
- **Generator (synth voice):** owns **real-time evolution** — ADSR envelope, LFO, FM operator
  state, advanced per-sample in the fill ISR. It takes real time into account for *how* a note
  sounds, but not for on/off *timing*.

Making the scheduler an event **producer** and the sound source a **sink** lets the internal
FM/CORDIC voices and the external SAM2695 (MIDI) become interchangeable sinks of the *same* timed
event stream. The existing `play_resolve_fn_t` hook is today's event-observation seam and can serve
for §3 testing without building the real producer/sink yet.

**Planned synth architecture (author, high-level, own session):** one engine enclosing multiple
allocatable **generator** instances → **mixer** block → I2S streamer (GM / modern-polysynth voice
allocation). Roll-your-own **FM chiptune** as the learning target (not competing with the SAM2695);
CORDIC for operator sines, FMAC for modulation/summing.

---

## 9. Hardware / migration context

- Upward-migration target is an **STM32H5xx** or **H723** — both carry **CORDIC + FMAC** (this
  firmware uses both), giving headroom for a handful of 2–4-operator FM voices. See the private
  `h723-migration-target` note for flash/bus specifics.
- Aspirational: emulate slices of the **Yamaha OPL3** (2-op/4-op FM) on the H-series — CORDIC for
  the operator sines, FMAC for modulation summing/feedback.
- Alternate synthesis source under consideration: **M5Stack Unit MIDI (SAM2695)** over UART-MIDI —
  drives the §7 external-clock/resync path and the §8 producer/sink seam.

---

## 10. Open items deferred (not this session)

- **T1** Hoist the §3 accumulator into a shared transport clock (polyphony session).
- **T2** Global song-origin seeding so tied instances share bar1/beat1 (vs. per-instance origin).
- **T3** Synth engine refactor: generator instances → mixer → I2S; FM chiptune voices.
- **T4** PLAY→MIDI event producer + SAM2695 sink + MIDI-clock resync (possible fork/side-project).
- **T5** Promote `play_drift_test.py` to a permanent `/playtest` golden (needs a machine-readable
  PASS/FAIL witness the device or host can assert); decide whether the ` t=<tick>` resolve-trace
  field stays always-on (useful timing observability) or gets gated.
