# Decision-log planning model (LED_Strip_Controller_G474)

**Purpose:** Living plan documents for multi-session design work — especially specs
moving toward implementation (PLAY meta-language, RTOS bring-up, storage, etc.).

Adapted from the Simplehuman project workflow (sidebar decision-log + one-at-a-time
resolution). Same mechanics; **no** Simplehuman process baggage (paired PRs, CRA,
submodule rules) unless this repo's standing docs explicitly require them.

---

## When to use

Create or extend a decision-log plan when:

- A spec has many open design points (syntax, semantics, scope).
- Work will span multiple chat sessions or agent handoffs.
- You want to resolve items **one at a time** (or in small batches) without
  scrolling through walls of chat history.

For small one-shot fixes, skip the plan — just do the work.

---

## Document layout

Each **feature plan** lives under `Docs/planning/` (or `Docs/issues/` for
issue-tied work). Name pattern: `<topic>-plan.md` (e.g.
`play-v1-implementation-plan.md`).

### 1. Header block

- Title, link to parent spec (`Docs/PLAY_language_design.md`, etc.)
- Branch / status (PLANNING · IN PROGRESS · IMPLEMENTING · DONE)
- **Working mode** one-liner: resolve OPEN items in chat; update table + detail
  sections as decisions land.

### 2. Summary decision table (the sidebar view)

Single table — easy to scan in the editor:

| ID | Status | Subject (one line) |
|----|--------|-------------------|

**ID prefixes** (short, typeable in chat):

| Prefix | Meaning |
|--------|---------|
| **D** | Design — syntax, charset, file format, command letters |
| **S** | Semantics — behavior, inheritance, timing, error policy |
| **I** | Implementation — v1 scope, RAM, modules, scheduler, limits |
| **T** | Tooling / docs — Python driver, spec cleanup, test vectors |
| **Q** | Question — explicitly needs **user** input to proceed |

Number sequentially within each prefix (`D1`, `D2`, … `S1`, …).

**Status column** (emoji indicators):

| Indicator | Meaning |
|-----------|---------|
| 🔴 | Unaddressed — no leaning recorded yet |
| 🟡 | In progress — discussion open or leaning documented |
| 🟢 | Resolved — decision locked; detail section updated |
| 🔵 | Deferred / out of v1 scope / explicitly TBD later |

When status changes, update **both** the summary table **and** the matching
detail section below.

### 3. LOCKED CONTEXT (optional but recommended)

Decisions already made — **do not re-litigate** unless the user reopens them.
Bullet list or short table. Keeps the OPEN table focused on what still moves.

### 4. Detail sections (one per ID)

For each row in the summary table (OPEN items first, then resolved for history):

```markdown
### D1 — <short title>

**Status:** 🔴 · **Needs user:** yes/no

**Question:** …

**Options considered:** …

**Leaning / recommendation:** …

**Resolution:** _(empty until 🟢; then the locked decision)_
```

Resolved items keep their history (options + rationale) so handoffs stay auditable.

### 5. Global notes (footer)

- Cross-cutting decisions that aren't one ID.
- Implementation phase sketch (after enough 🟢).
- Links to code anchors as they appear.
- **Plan status summary** — counts by color, next suggested ID to tackle.

---

## How agents and humans work the plan

1. **User** references IDs in chat: *"Let's do D3 and S2"*, *"D5 → green, pick option B"*.
2. **Agent** updates the plan doc in the same session (table + detail + LOCKED if needed).
3. **Agent** does **not** silently resolve 🔴 items — propose leaning in 🟡, lock only
   when the user confirms (or user says *"your call on D4"*).
4. When implementation starts, add **CODE ANCHORS** / phase checklist to the plan
   or a sibling impl doc.
5. After material spec changes, sync `Docs/PLAY_language_design.md` (or the relevant
   spec) from the plan — plan is the negotiation log; spec is the contract.
6. **Hedged user language** (*probably*, *maybe*, …) → brief pushback **only if** you think it's a bad call or has serious cost; otherwise close it out. Not pedantic. See
   [`.grok/memory/user_conversational_tone.md`](../../.grok/memory/user_conversational_tone.md).

---

## Bootstrap / read-the-docs / wrapup

**Session start** — agents read:

1. This file (`Docs/planning/decision-log-model.md`) — the mechanics.
2. Any **active** plan linked from `Docs/PROJECT.md` or `.grok/memory/MEMORY.md`.
3. For PLAY work: `Docs/planning/play-v1-implementation-plan.md`.
4. **Newest** `Docs/planning/*-session-handoff-*.md` if present — written by **`/wrapup`** at end of prior session; read **before** diving into the full plan.

**Session end** — run **`/wrapup`** (`.grok/skills/wrapup/SKILL.md`): sanity-check docs, refresh handoff, commit WIP (no push by default).

---

## Related project docs

| Doc | Role |
|-----|------|
| [AGENTS.md](../../AGENTS.md) | Agent contract; points here for planning workflow |
| [Docs/PLAY_language_design.md](../PLAY_language_design.md) | PLAY spec (contract once decisions land) |
| [.grok/memory/planning_decision_log_model.md](../../.grok/memory/planning_decision_log_model.md) | Memory index stub → this file |

**End of decision-log-model.md**
