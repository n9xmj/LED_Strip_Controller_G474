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

### 1a. Brief (executive summary) — **first thing in the doc**

A **one-to-two paragraph** plain-language overview at the very top: (sub)project
title, the big-picture of what it provides, and the scope of the current version.
This is the reader's orientation before any table. Keep it tight — it is *not* the
place for detail; that lives in the detail sections at the bottom.

### At-a-glance ordering (load-bearing)

The plan is the user's **at-a-glance** document — *where are we, where are we going,
what must I decide* — visible without scrolling/searching. So the **three key tables
sit together near the top, in this order, with minimal prose between them**:

1. **The Big Board** (decisions) → 2. **§ MSG** (must-ship gap) → 3. **Wish list**.

Row descriptions follow a **one-or-two-sentence soft rule**: bias to brevity, but
favor clarity when a row genuinely needs a few more words. **Longer descriptive
text comes *after* the three tables** (LOCKED CONTEXT, detail sections, notes).

### 2. Summary decision table — **The Big Board**

Single table at the top of each plan — easy to scan in the editor (nickname from
*Dr. Strangelove*: the war-room status board). In chat the user may say *"The Big
Board"*, *"what's still red?"*, or reference a row by ID (*"green S7g"*).

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
| **W** | Wish — v2+ backlog item (companion wish-list table only) |
| **E** | Expression — musical performance / timbre beyond v1 (often v2+) |

Number sequentially within each prefix (`D1`, `D2`, … `S1`, … `W1`, …).

**Status column** (emoji indicators):

| Indicator | Meaning |
|-----------|---------|
| 🔴 | Unaddressed — no leaning recorded yet |
| 🟡 | In progress — discussion open or leaning documented |
| 🟢 | Resolved — decision locked; detail section updated |
| 🔵 | Deferred / out of v1 scope / explicitly TBD later |

When status changes, update **both** the summary table **and** the matching
detail section below.

**Resolution:** *Living audit log — mark **§ MSG** rows ✅ when they land; bump **Last audited** in both **MSG** and here.*

---

### 2a. Must-Ship Gap (MSG) — scan table between Big Board and Wish list

For implementation plans with a locked **I1** (or equivalent) feature fence, add **§ MSG**
immediately after **The Big Board** and before the **Wish list**:

- **`G1`…`Gn`** — unique **firmware** gap row IDs (append-only; mark **FW** ✅ when shipped — **do not renumber**). **Ord** column = bring-up order tier (1 before 2) — not PLAY **`P<n>`** voice indices.
- **`GP1`…`** — peripheral rows (**§ MSG-GP**); cross-ref **T** IDs where applicable
- **Ref** column links existing **D** / **S** / **I** decision IDs

**Do not** put MSG rows on the wish list. **I10** (or similar) in the Big Board points here for detail/audit notes.

When a firmware row ships: mark ✅ on **G*n***, sync **I10** detail, living docs, and relevant goldens.

### 2b. Wish list (v2+ backlog) — optional companion table

For long-running plans (e.g. PLAY), add a **second scan table** directly under
**The Big Board**: deferred features, v2+ ideas, expression enhancements, and
tooling not in v1 scope. Use **W** (wish) or **E** (expression) IDs; link to
existing **D** / **S** / **I** / **T** rows when the item already has a decision
ID.

**Do not** duplicate **§ MSG** — v1/v1.1 must-ship firmware belongs there, not on the wish list.

Add a one-line row when an idea surfaces in chat; promote to a full **D**/**S**
row + detail section when design work starts.

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

**After MSG ships** — run **`/cleanup-docs`** (`.grok/skills/cleanup-docs/SKILL.md`): archive completed feature briefs, delete superseded session handoffs, fix stale links. Permanent template: [`focused-implementation-handoff-template.md`](focused-implementation-handoff-template.md).

### Handoff doc lifecycle (ephemeral vs permanent)

| Artifact | Pattern | Lifecycle |
| -------- | ------- | --------- |
| Session handoff | `*-session-handoff-YYYY-MM-DD.md` | Keep **newest 1**; delete older via `/cleanup-docs` |
| Account switch | `*-account-switch-handoff-*.md` | **Permanent** until next switch; resume steps + initial prompt |
| Feature impl brief | `<topic>-handoff.md` | Copy from template; **archive** when MSG **✅** |
| Template | `focused-implementation-handoff-template.md` | **Permanent** |
| Plan / MSG / I10 | `*-implementation-plan.md` | **Permanent** contract |

Automation: `python scripts/cleanup_planning_docs.py` (dry-run) · `--apply` to execute.

---

## Related project docs

| Doc | Role |
|-----|------|
| [AGENTS.md](../../AGENTS.md) | Agent contract; points here for planning workflow |
| [Docs/PLAY_language_design.md](../PLAY_language_design.md) | PLAY spec (contract once decisions land) |
| [.grok/memory/planning_decision_log_model.md](../../.grok/memory/planning_decision_log_model.md) | Memory index stub → this file |

**End of decision-log-model.md**
