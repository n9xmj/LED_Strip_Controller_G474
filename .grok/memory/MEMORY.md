# Project-local memory index — LED_Strip_Controller_G474

One-line index. Each entry is a **topic deep-dive** — read its body **on demand** when that topic
is the session focus (see AGENTS.md *Session start — no assumed focus* + Topic Map). Don't
bulk-read all bodies at session start; the always-read exception is `user_conversational_tone.md`.

- [Author Haskell gap](author_haskell_gap.md) — no FP literacy; agents must translate mokus0 Haskell / vTree into plain + embedded C when Mk 5 DSP starts
- [Preferred conversational tone + workflow lightness](user_conversational_tone.md) — casual techno-geeky chat; pop-culture refs OK; personal hobby archival git; hedged planning language = brief pushback if bad call, else close out.
- [Decision-log planning model](planning_decision_log_model.md) — **The Big Board** = summary table (D/S/I/T/Q, 🔴🟡🟢🔵) + detail sections; resolve in chat by label (*"green D3"*). Full doc: `Docs/planning/decision-log-model.md`. **Active plan (authoritative continuity):** `Docs/planning/play-v1-implementation-plan.md`. **Handoff:** newest `Docs/planning/play-v1-session-handoff-*.md` if one exists (via `/wrapup`). **Focused sessions:** `Docs/planning/focused-implementation-handoff-template.md`; `/cleanup-docs` after MSG ✅.
- [`uart_stream` port brief](../Docs/planning/uart_stream-port-notes.md) — USART2 register ISR, HAL out of IRQ path, LED DMA coexistence; **v1.1 stretch**; reference in `not-in-project/`.
- [Terminal piano + PLAY player bench](../Docs/planning/terminal-piano-and-player-notes.md) — I8/I9, smoke preset, virtual synthboard wishlist, implementation order.
- [PLAY deadline-driven scheduler (W30)](../Docs/planning/play-v1-implementation-plan.md#w30--deadline-driven-play-service-v2--optimization) — v1 polls main loop + 1 ms tick counter; future: wake only at sound-off / rest-end (author 2026-06-14).
- [PLAY RTOS migration (NVIC + dual clock)](../Docs/planning/play-v1-implementation-plan.md#rtos-migration--play-timer-nvic-and-freertos-tick) — FreeRTOS tick vs PLAY HW timer, syscall ceiling, priority stack (2026-06-14).
- **The cayuse project** (vTree Mk 4 color organ, post-PLAY) — [github.com/cayuse/color_organ](https://github.com/cayuse/color_organ); author’s **vTree Mk 1–5** lineage [Docs/PROJECT.md](../Docs/PROJECT.md) § *Product lineage*
- [INMP441 I2S wiring (left channel only)](inmp441_i2s_wiring.md) — L/R→GND on bench; right slot tri-states — handlers must not use R samples (garbage); stereo = shared SD + opposite L/R strap
- [Session handoff 2026-06-20 — mic VU + LED bargraph](session-handoff-2026-06-20-mic-led-vu.md) — I2S2 DMA→job-queue stream, `m` dBFS meter + 10-LED bargraph (LED_CHANNEL_2), LED driver repeat-TX completion fix; next: FFT/beat detection in chunk handler (vTree Mk 5)
