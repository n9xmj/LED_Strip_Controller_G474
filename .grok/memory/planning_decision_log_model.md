# Decision-log planning model

**Category:** workflow / agent handoff  
**Full doc:** [Docs/planning/decision-log-model.md](../../Docs/planning/decision-log-model.md)

## One-screen summary

For multi-session design (PLAY v1, etc.):

1. Create `Docs/planning/<topic>-plan.md` with **The Big Board** — the summary decision table at the top: ID · status · one-line subject. (*Dr. Strangelove* nickname; user may refer to it that way in chat.)
2. **§ MSG (Must-Ship Gap)** — optional scan table between Big Board and wish list: **`G1`…`Gn`** firmware gaps (append-only IDs); **`GP1`…`** peripheral; **Ord** = bring-up tier. (*Mine-Shaft-Gap* joke.)
3. **ID prefixes:** D=design, S=semantics, I=implementation, T=tooling/docs, Q=user question, W=wish, **G**=MSG firmware gap, **GP**=MSG peripheral.
4. **Status:** 🔴 open · 🟡 leaning · 🟢 resolved · 🔵 deferred.
5. Below the table: **detail section per ID** (question, options, leaning, resolution).
6. **LOCKED CONTEXT** section for decisions already made.
7. Footer: global notes + plan status.

User resolves in chat by ID (*"green D3"*, *"S2 option A"*). Agent updates the plan doc same session; sync the main spec when decisions lock.

**Active plan (PLAY):** [Docs/planning/play-v1-implementation-plan.md](../../Docs/planning/play-v1-implementation-plan.md)  
**Latest handoff:** [Docs/planning/play-v1-session-handoff-2026-06-14-g8.md](../../Docs/planning/play-v1-session-handoff-2026-06-14-g8.md)
**Account switch (2026-06-13):** [Docs/planning/play-v1-account-switch-handoff-2026-06-13.md](../../Docs/planning/play-v1-account-switch-handoff-2026-06-13.md)

**The cayuse project** (informal) = **vTree Mk 4** — [github.com/cayuse/color_organ](https://github.com/cayuse/color_organ). **vTree Mk 1–3** = author originals; **this G474 repo = Mk 5 (vTree+)**. See [Docs/PROJECT.md](../../Docs/PROJECT.md) § *Product lineage*. Future mic→DSP→LED; not PLAY.

Read `decision-log-model.md` + active plan (+ handoff if new session) when user mentions PLAY implementation, decision IDs, or planning mode.
