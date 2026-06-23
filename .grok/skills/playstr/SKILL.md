---
name: playstr
description: Feed an inline PLAY string to the G474 via test-harness P op. Use /playstr "CQ4DEFGABC5 *".
user_invocable: true
argument-hint: "\"PLAY source string\""
---

# playstr — inline PLAY string on hardware

**Bench defaults:** [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — never hardcode COM/baud/SN; auto-resolved. Values: `python scripts/discover.py --show-bench`.

**Firmware path (automation):** 3× ESC → **0xDA** (harness) → **`P <hex>`** → await PLAY witnesses → **0xA5**. HuIL **S** / **m→s** uses the term line editor (≤255 chars, 1 KiB history).

When invoked as **`/playstr "…"`**:

```powershell
python scripts/play_bench.py str "USER_STRING_HERE"
```

- Max length **4096** chars on harness path (`PLAY_HARNESS_LINE_MAX`).
- Optional cold boot: `--reset --stlink-sn …` (COM opened first, then reset).

Pass = `PLAY ended @ off=…` and no `PLAY fault:`.

Saves the string to `scripts/.play_bench_last` for **`/replay`**.

Related: `/replay` · `/playfile` · `/playtest` · plan **T2** in [`Docs/planning/play-v1-implementation-plan.md`](Docs/planning/play-v1-implementation-plan.md).
