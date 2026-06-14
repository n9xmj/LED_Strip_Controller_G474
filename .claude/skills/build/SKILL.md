---
name: build
description: Incremental Debug build for LED_Strip_Controller_G474 via headless STM32CubeIDE. Use when the user asks to build, rebuild incrementally, or compile after code changes.
---

# build

## Bench hardware

Read **`scripts/bench.defaults.json`** (optional local override: `scripts/bench.defaults.local.json`, gitignored).

| Key | This bench |
|-----|------------|
| `stlink_sn` | `003C00193137510C39383538` |
| `com_port` | `COM9` |
| `baud` | `921600` |

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
