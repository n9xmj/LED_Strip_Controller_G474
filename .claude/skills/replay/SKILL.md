---
name: replay
description: Replay the last /playstr argument on the G474 bench. Use /replay after /playstr when iterating by ear without retyping.
---

# replay

Replay the last inline PLAY string saved by `/playstr` (`play_bench.py str`).

```powershell
python scripts/play_bench.py replay
```

- Only **`str`** updates the saved body (`scripts/.play_bench_last`, gitignored).
- If nothing was saved yet, exits 2 with `run /playstr first`.
- Prints the replayed string before feeding the board (same UART path as `/playstr`).
