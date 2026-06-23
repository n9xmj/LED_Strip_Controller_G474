---
name: bench
description: Show and edit the LED_Strip_Controller_G474 bench configuration — pretty-print bench.defaults.json, list connected ST-Links (SN + friendly name) and COM ports (friendly name), cross-check the config against what's attached, and optionally apply a terse freeform config change. Use for "/bench", "show bench config", "what's my bench setup", or "set bench port/SN/baud".
---

# bench

One-stop bench-configuration report + editor. Reads the single source of truth
([`scripts/bench.defaults.json`](scripts/bench.defaults.json); see [`BENCH.md`](BENCH.md)),
lists the live hardware, cross-checks them, and optionally applies a small config change
from a freeform argument.

All the discovery work lives in [`scripts/discover.py`](scripts/discover.py); this skill is a
thin wrapper that adds the cross-checks, the friendly report, and the edit step.

## 1 — Gather (always)

Run both, capture the output:

```powershell
python scripts\discover.py --show-bench --json
python scripts\discover.py --list
```

- `--show-bench --json` → the raw config object so the **member names are visible**
  (`stlink_sn`, `com_port`, `baud`) plus which file(s) supplied them (`sources`).
- `--list` → every ST-Link (SN, board name, FW, accessibility) and every COM port
  (friendly name, manufacturer, VID:PID, accessibility / lock status).

## 2 — Apply a change (only if an argument was given)

The argument is freeform and terse. Map it to the JSON keys, then edit the config file:

| User says (examples)                         | Key(s) set                          |
|----------------------------------------------|-------------------------------------|
| `port COM1`, `test port COM1, 921600`        | `com_port` (+ `baud` if a number given) |
| `baud 115200`                                | `baud` (integer, not a string)      |
| `STLink ID 0020...`, `sn 0020...`            | `stlink_sn` (store uppercase)       |

Rules:
- **Which file to edit:** if [`scripts/bench.defaults.local.json`](scripts/bench.defaults.json)
  exists it is the *active override* (its keys win) — edit it so the change actually takes
  effect; otherwise edit the committed `scripts/bench.defaults.json`. Read the target file
  first, then make a minimal edit (add the key if absent, preserve `_comment`). `baud` is an
  **integer**; `com_port` / `stlink_sn` are strings.
- If the arg is ambiguous (can't tell which key), don't guess — show the current config +
  member names and ask which field they meant.
- After editing, re-run `python scripts\discover.py --show-bench` to confirm the resolved value.
- Never touch firmware or `platform.h`. This skill only edits the bench JSON.

## 3 — Report (always)

Print a tidy report:

1. **Bench config** — pretty JSON of the resolved values with the member names shown
   (so the user can reference them next time), and the source file(s).
2. **ST-Links connected** — `SN  board/FW  (accessibility)` per probe.
3. **COM ports** — `PORT  friendly name  (accessibility)` per port; include manufacturer /
   VID:PID when useful.
4. **Cross-checks** (the value-add — call these out explicitly):
   - Does the configured `stlink_sn` match a connected probe? (✔ matched / ✗ not present —
     on a multi-probe bench, name which other probes are attached.)
   - Is the configured `com_port` present, and is it **free** or **in-use/locked** (e.g. a
     Tera Term session) or **missing**?
   - Firmware console baud is fixed at **921600** (USART2 / ST-Link VCP, per `AGENTS.md` +
     `platform.h`). If the bench `baud` ≠ 921600, flag it as a likely mismatch.
   - Override status: committed file only, or `bench.defaults.local.json` override active.
5. If a change was applied: show **before → after**, which file was edited, and the
   re-confirmed resolved config.

Close with the source-of-truth reminder: values live in `scripts/bench.defaults.json`
(or a gitignored `bench.defaults.local.json`); see `BENCH.md`. Editing one file is all it
takes — scripts and skills follow it.
