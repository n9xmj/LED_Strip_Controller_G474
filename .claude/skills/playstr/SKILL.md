---
name: playstr
description: Feed inline PLAY via test-harness P op (0xDA then P hex). Max 4096 chars on harness path; HuIL S/s is 255 chars with line editor.
---

# playstr

```powershell
python scripts/play_bench.py str "YOUR_STRING"
```

Automation: ESC×3 → **0xDA** (harness) → **`P <hex>`** → await PLAY witnesses → **0xA5**. Not the paced top-level **S** menu path (HuIL line editor, ≤255 chars).

Saves the string to `scripts/.play_bench_last` for **`/replay`**.
