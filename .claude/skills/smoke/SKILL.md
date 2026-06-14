---
name: smoke
description: Smoke-test LED_Strip_Controller_G474 — capture startup banner on COM9 after ST-Link reset, or probe live board with ESC+@ identify. Use after flash or to verify boot.
---

# smoke

## Bench defaults

[`scripts/bench.defaults.json`](scripts/bench.defaults.json): SN `003C00193137510C39383538`, COM `COM9`, baud `921600`.

## Reset-driven smoke (after flash)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1 --port COM9 --stlink-sn 003C00193137510C39383538 --baud 921600
```

Omitting `--port` / `--stlink-sn` is OK when `bench.defaults.json` is present.

## Live-board probe (no reset)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1 --port COM9 --identify --baud 921600
```

Sends 3× ESC then `@` to reprint the bordered startup banner.

## Report

- Log path (`smoke-YYYYMMDD-HHMMSS.log` in repo root)
- Extract and echo the `***`-bordered banner (Project, Target, Firmware version, Build #, Reset source)
- Surface COM/ST-Link "in-use" errors clearly
