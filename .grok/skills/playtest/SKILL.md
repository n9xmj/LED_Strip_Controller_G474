---
name: playtest
description: Run a named PLAY golden test (smoke, P0, loop, …) against hardware. Use /playtest smoke or /playtest P0. Lists tests with /playtest list.
user_invocable: true
argument-hint: "golden-test-name | list"
---

# playtest — named golden test on hardware

**Registry:** [`scripts/play_golden/tests.json`](scripts/play_golden/tests.json) + companion `.play` files.

**Bench defaults:** [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — never hardcode COM/baud/SN; auto-resolved. Values: `python scripts/discover.py --show-bench`.

When invoked as **`/playtest list`** (or user asks what golden tests exist):

```powershell
python scripts/play_bench.py list
```

When invoked as **`/playtest golden-test-name`** (e.g. `smoke`, `P0`, `loop`, `smoke-menu`):

```powershell
python scripts/play_bench.py test NAME
```

Each test entry uses either:
- **`menu_key`** — fires preset in **`m`** player submenu (`1` = smoke scale, `2` = loop test), or
- **`file`** — loads `scripts/play_golden/<file>` and feeds via **`s`**.

Report:
- Test id + description from manifest
- UART witness summary (fault / warn / ended)
- **`=== PLAY bench PASS/FAIL ===`**

**Scenario matrix (P0 regression):**

```powershell
python scripts/play_scenarios.py --scenario P0
# or
powershell -File scripts/run_play_tests.ps1
```

**Tier roadmap (T3):** Smoke → Smoke+ (Williams) → Feature → Torture. Add rows to `tests.json` as **`m` → `g`** and host parser catch up (**I10**).

**Related:** `/playstr` · `/playfile` · on-device **`m` → `g`** STRICT runner (planned **T2-4**).

Reference: [`SCRIPTS.md`](SCRIPTS.md) · **T2/T3** in [`Docs/planning/play-v1-implementation-plan.md`](Docs/planning/play-v1-implementation-plan.md).
