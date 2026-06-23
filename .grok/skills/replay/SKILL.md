---
name: replay
description: Replay the last /playstr argument on the G474 bench. Use /replay when iterating by ear without retyping the PLAY string.
user_invocable: true
---

# replay — repeat last inline PLAY

**Bench defaults:** [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — never hardcode COM/baud/SN; auto-resolved. Values: `python scripts/discover.py --show-bench`.

When invoked as **`/replay`**:

```powershell
python scripts/play_bench.py replay
```

- Re-feeds the body last passed to **`/playstr`** (`play_bench.py str`).
- Saved in **`scripts/.play_bench_last`** (gitignored, per-machine).
- **`/playfile`** and **`/playtest`** do not update the saved string.
- Fails with exit 2 if no prior **`/playstr`** in this workspace.

Related: `/playstr` · `/playfile` · `/playtest`
