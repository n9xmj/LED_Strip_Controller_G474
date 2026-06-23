---
name: smoke
description: Smoke-test LED_Strip_Controller_G474 — capture startup banner on the bench COM port after ST-Link reset, or probe live board with ESC+@ identify. Use after flash or to verify boot.
---

# smoke

## Bench defaults

Port / SN / baud resolve from [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — **never
hardcode them** (see [`BENCH.md`](BENCH.md); values via `python scripts/discover.py --show-bench`).
`smoke-test.ps1` reads the file when the flags are omitted, so the commands below need no port/SN/baud.

## Reset-driven smoke (after flash)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1
```

## Live-board probe (no reset)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1 --identify
```

Sends 3× ESC then `@` to reprint the bordered startup banner.

## Report

- Log path (`smoke-YYYYMMDD-HHMMSS.log` in repo root)
- Extract and echo the `***`-bordered banner (Project, Target, Firmware version, Build #, Reset source)
- Surface COM/ST-Link "in-use" errors clearly
