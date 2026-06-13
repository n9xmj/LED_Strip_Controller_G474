---
name: playstr
description: Feed inline PLAY via top-level S hook (ESC×3 then S). Max 4096 chars; paced UART bursts.
---

# playstr

```powershell
python scripts/play_bench.py str "YOUR_STRING"
```

Automation: ESC×3 → **`S`** → `PLAY>` (not `m` → `s`). Paced 16-char bursts, 20 ms gap, 100 ms pre-send.
