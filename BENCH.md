# BENCH.md — bench configuration (single source of truth)

Human- and agent-readable explanation of how this project pins its bench hardware.

> **This file deliberately contains no literal SN / COM / baud values.** Those live in
> exactly one place so a board swap, machine move, or MCU migration is a one-file edit.

## Where the values live

The **only** place bench identifiers are stored:

- **`scripts/bench.defaults.json`** — committed defaults for this workspace.
- **`scripts/bench.defaults.local.json`** — optional per-machine override (gitignored). If
  present, its keys win over the committed file.

Keys: `stlink_sn`, `com_port`, `baud`.

## How to see the current values

```powershell
python scripts/discover.py --show-bench        # human-readable
python scripts/discover.py --show-bench --json # machine-readable
```

This reads the JSON live (including any local override) — it is always current, even if the
bench changed mid-session.

## How to change the bench (the whole point)

Editing **one** file is the entire procedure:

1. Edit `scripts/bench.defaults.json` (or drop a `scripts/bench.defaults.local.json` for a
   machine-specific override you don't want committed).
2. That's it. Scripts re-read it on every run; skills and docs only *point* here, so nothing
   else needs touching.

Use `python scripts/discover.py --list` to find the right ST-Link SN / COM port (with
accessibility status) before editing.

## Why nothing else holds the values

- **Scripts** (`flash.ps1`, `smoke-test.ps1`, `play_bench.py`, …) auto-resolve SN/port/baud
  from the JSON when you omit `--stlink-sn` / `--port` / `--baud`. Don't pass those flags
  unless you are deliberately overriding the bench.
- **Skills, `AGENTS.md`, `SCRIPTS.md`, memory, planning docs** reference this file by path (or
  tell you to run `--show-bench`). They never copy the digits — copies are what made a board
  swap a 25-file edit before this convention.

**Rule:** never hardcode bench identifiers anywhere but `bench.defaults.json`. See
`AGENTS.md` → *Bench configuration*.
