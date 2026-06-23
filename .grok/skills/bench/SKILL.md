---
name: bench
description: Show and edit the bench configuration — pretty-print bench.defaults.json, list connected ST-Links (SN + friendly name) and COM ports (friendly name), cross-check the config against attached hardware, and optionally apply a terse freeform config change. Use for /bench, "show bench config", or "set bench port/SN/baud".
user_invocable: true
argument-hint: "[optional terse change, e.g. 'port COM1, 921600' or 'sn 0049...']"
---

# Bench Skill

One-stop bench-configuration report + editor. Reads the single source of truth
([`scripts/bench.defaults.json`](scripts/bench.defaults.json); see [`BENCH.md`](BENCH.md)),
lists the live hardware, cross-checks them, and optionally applies a small config change
from the freeform argument.

All discovery lives in [`scripts/discover.py`](scripts/discover.py); this skill is a thin
wrapper that adds the cross-checks, the friendly report, and the edit step. Never hardcode
SN/COM/baud — they come from the JSON.

## 1 — Gather (always)

```
python scripts/discover.py --show-bench --json
python scripts/discover.py --list
```

- `--show-bench --json` → raw config object so the **member names are visible**
  (`stlink_sn`, `com_port`, `baud`) + which file(s) supplied them (`sources`).
- `--list` → every ST-Link (SN, board name, FW, accessibility) and every COM port
  (friendly name, manufacturer, VID:PID, accessibility / lock status).

## 2 — Apply a change (only if `/bench <arg>` was given)

The argument is freeform and terse. Map it to the JSON keys, then edit the config file:

| User says (examples)                    | Key(s) set                              |
|-----------------------------------------|-----------------------------------------|
| `port COM1`, `test port COM1, 921600`   | `com_port` (+ `baud` if a number given) |
| `baud 115200`                           | `baud` (integer, not a string)          |
| `STLink ID 0020...`, `sn 0020...`      | `stlink_sn` (store uppercase)           |

Rules:
- **Which file to edit:** if `scripts/bench.defaults.local.json` exists it is the *active
  override* (its keys win) — edit it so the change takes effect; otherwise edit the committed
  `scripts/bench.defaults.json`. Read the target first, then make a minimal edit (add the key
  if absent, preserve `_comment`). `baud` is an **integer**; `com_port` / `stlink_sn` strings.
- If the arg is ambiguous, don't guess — show the current config + member names and ask which
  field they meant.
- After editing, re-run `python scripts/discover.py --show-bench` to confirm the resolved value.
- Never touch firmware or `platform.h`. This skill only edits the bench JSON.

## 3 — Report (always)

1. **Bench config** — pretty JSON of the resolved values with member names shown, + source file(s).
2. **ST-Links connected** — `SN  board/FW  (accessibility)` per probe.
3. **COM ports** — `PORT  friendly name  (accessibility)`; manufacturer / VID:PID when useful.
4. **Cross-checks** (the value-add — call these out):
   - Does configured `stlink_sn` match a connected probe? (✔ matched / ✗ not present; on a
     multi-probe bench name the other probes.)
   - Is configured `com_port` present, and **free** / **in-use-locked** (e.g. a Tera Term
     session) / **missing**?
   - Firmware console baud is fixed at **921600** (USART2 / ST-Link VCP, per AGENTS.md +
     `platform.h`). Flag a mismatch if bench `baud` ≠ 921600.
   - Override status: committed file only, or `bench.defaults.local.json` override active.
5. If a change was applied: show **before → after**, which file was edited, and the
   re-confirmed resolved config.

Close with the source-of-truth reminder: values live in `scripts/bench.defaults.json` (or a
gitignored `bench.defaults.local.json`); see `BENCH.md`. Editing one file is all it takes.
