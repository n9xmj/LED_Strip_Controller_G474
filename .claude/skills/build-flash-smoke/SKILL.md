---
name: build-flash-smoke
description: Default G474 dev loop — incremental build, flash to bench ST-Link, smoke test. Use for "build flash and verify", "iterate on hardware", or after PLAY/firmware edits.
---

# build-flash-smoke

Composite skill (incremental, no build-number bump). Chains build → flash → smoke.

## Bench

SN / COM / baud resolve from [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — **never
hardcode** (see [`BENCH.md`](BENCH.md); values: `python scripts/discover.py --show-bench`).

## Steps

1. `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 --incremental`
2. On success: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash.ps1`
3. On success: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1`

Scripts pick ST-Link/COM from bench defaults when flags omitted.

For clean + bump use `roundtrip` skill instead.
