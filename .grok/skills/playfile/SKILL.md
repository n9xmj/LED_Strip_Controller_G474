---
name: playfile
description: Feed a .play file from disk to the G474 PLAY interpreter via the debug menu. Use /playfile path\to\score.play or "run this play file on the board".
user_invocable: true
argument-hint: "filesystem\\path\\to\\file.play"
---

# playfile — PLAY file on hardware

**Bench defaults:** [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — COM9, baud 921600.

When invoked as **`/playfile path\to\file.play`**:

1. Resolve the path (repo-relative or absolute). Golden sources live under **`scripts/play_golden/`** (e.g. `scripts/play_golden/smoke.play`).
2. Run:

   ```powershell
   python scripts/play_bench.py file "PATH_HERE"
   ```

   The runner strips `#` comments and blank lines, concatenates the remainder, and feeds it through menu **`m` → `s`** (same as manual bench entry).

3. Optional cold boot: `--reset --stlink-sn 003C00193137510C39383538`.

4. Report PASS/FAIL, faults, and whether `PLAY ended @ off=…` appeared.

**File format:** one logical PLAY document; line breaks are ignored after comment stripping. Keep files in sync with **`App/Src/play_presets.c`** when they mirror on-device presets.

**Related:** `/playstr` for inline strings · `/playtest` for registry names · **`python scripts/play_bench.py list`** for golden index.

Reference: **T2** / **T3** in [`Docs/planning/play-v1-implementation-plan.md`](Docs/planning/play-v1-implementation-plan.md).
