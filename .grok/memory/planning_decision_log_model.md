# Decision-log planning model

**Category:** workflow / agent handoff  
**Full doc:** [Docs/planning/decision-log-model.md](../../Docs/planning/decision-log-model.md)

## One-screen summary

For multi-session design (PLAY v1, etc.):

1. Create `Docs/planning/<topic>-plan.md` with **The Big Board** — the summary decision table at the top: ID · status · one-line subject. (*Dr. Strangelove* nickname; user may refer to it that way in chat.)
2. **ID prefixes:** D=design, S=semantics, I=implementation, T=tooling/docs, Q=user question.
3. **Status:** 🔴 open · 🟡 leaning · 🟢 resolved · 🔵 deferred.
4. Below the table: **detail section per ID** (question, options, leaning, resolution).
5. **LOCKED CONTEXT** section for decisions already made.
6. Footer: global notes + plan status.

User resolves in chat by ID (*"green D3"*, *"S2 option A"*). Agent updates the plan doc same session; sync the main spec when decisions lock.

**Active plan (PLAY):** [Docs/planning/play-v1-implementation-plan.md](../../Docs/planning/play-v1-implementation-plan.md)  
**Latest handoff:** [Docs/planning/play-v1-session-handoff-2026-06-11.md](../../Docs/planning/play-v1-session-handoff-2026-06-11.md)

**Audio-reactive lighting (future, not PLAY):** [cayuse/color_organ](https://github.com/cayuse/color_organ) — institutional memory; see [Docs/PROJECT.md](../../Docs/PROJECT.md) § *Institutional memory*.

Read `decision-log-model.md` + active plan (+ handoff if new session) when user mentions PLAY implementation, decision IDs, or planning mode.
