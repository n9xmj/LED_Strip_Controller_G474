---
name: playstr
description: Feed an inline PLAY string to the G474 over the debug menu top-level S hook. Use /playstr "CQ4DEFGABC5 *".
user_invocable: true
argument-hint: "\"PLAY source string\""
---

# playstr — inline PLAY string on hardware

**Bench defaults:** [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — COM9, baud 921600.

**Firmware path (automation):** 3× ESC → main menu → **`S`** (`PLAY_DEBUG_MENU_HOOK_KEY`) → `PLAY>` → paced line (not `m` → `s`).

When invoked as **`/playstr "…"`**:

```powershell
python scripts/play_bench.py str "USER_STRING_HERE"
```

- Max length **4096** chars (`PLAY_DEBUG_LINE_MAX`).
- Host sends the body in **16-char bursts**, **20 ms** gap, **100 ms** after `PLAY>` before first byte.
- Optional cold boot: `--reset --stlink-sn …` (COM opened first, then reset).

Pass = `PLAY ended @ off=…` and no `PLAY fault:`.

Saves the string to `scripts/.play_bench_last` for **`/replay`**.

Related: `/replay` · `/playfile` · `/playtest` · plan **T2** in [`Docs/planning/play-v1-implementation-plan.md`](Docs/planning/play-v1-implementation-plan.md).
