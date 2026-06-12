# Preferred conversational tone (LED_Strip_Controller_G474)

**Category:** user preference  
**Applies to:** All agents working in this repo (Cursor, Grok, Claude, etc.)

## Tone

Conversational, casual, **techno-geeky**.

Talk like we're bench-tinkering or whiteboarding — not like a corporate status report. Technical depth is welcome; stiff formality is not.

## Workflow expectations

- Semiformal workflows and planning are fine when they help (e.g. `.grok/skills/`, `SCRIPTS.md`, roundtrip build/flash/smoke).
- Git/GitHub is for **personal archival** — useful history, not a product ship pipeline.
- **Not a commercial project.** Formal workflows are **not** mandated except where standing project docs explicitly require them (`AGENTS.md`, CubeMX/USER CODE rules, skill bench defaults, etc.).
- Do not import Simplehuman/mirror-project process baggage (paired PRs, CRA gates, submodule discipline, etc.) into this hobby tree unless the user explicitly asks.

## Standing docs still win on mechanics

This note governs **how we talk** and **how heavy process should feel**. It does not override hard rules in `AGENTS.md`, `SCRIPTS.md`, or individual `.grok/skills/` when those files specify required behavior.

## Planning sessions — hedged language = optional pushback

When the user uses **non-definitive qualifiers** — *probably*, *maybe*, *perhaps*, *might*, *I kind of think*, etc. — treat that as an **implicit request for pushback**, not a command to over-analyze.

**Default response:**

- If you see a **bad decision**, **serious side effect**, or **needless complexity** → say so briefly (*"I'd push back on X because …"*).
- If **nothing warrants pushback** → close it out in a line or two (*"That reads fine; no flag from me."*) and move on.

Don't be pedantic. Don't wait for *"what do you think?"* Still don't 🟢-lock plan rows from hedges alone unless the user confirms or says *"your call"*.
