---
name: build
description: Incremental Debug build for LED_Strip_Controller_G474 via headless STM32CubeIDE. Use when the user asks to build, rebuild incrementally, or compile after code changes.
---

# build

## Bench hardware

Build doesn't need a probe/port. Bench identifiers (when a later step needs them) live only in
**`scripts/bench.defaults.json`** — never hardcode them. See [`BENCH.md`](BENCH.md); current
values via `python scripts/discover.py --show-bench`.

## Command

From repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 --incremental
```

Default config is **Debug**. Do **not** pass `--bump-build-count` for incremental builds.

## Report

- Exit code and error/warning counts from build output
- `FIRMWARE_VERSION` and `BUILD_NUMBER` from script output
- Artifact: `Debug\LED_Strip_Controller_G474.elf`

For clean build + bump use `.claude/skills/cleanbuild` or `roundtrip`.
