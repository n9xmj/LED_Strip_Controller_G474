---
name: roundtrip
description: Full dev loop for LED_Strip_Controller_G474 — clean Debug build with build-number bump, flash to bench ST-Link, reset-driven smoke test. Abort on build errors or warnings.
---

# roundtrip

Composite: **clean build + bump → flash → smoke**. Bench: [`scripts/bench.defaults.json`](scripts/bench.defaults.json).

## Steps (fail-fast)

1. **Clean build + bump**
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 --clean --bump-build-count
   ```
   Abort if errors or warnings > 0.

2. **Flash** (bench ST-Link)
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash.ps1 --stlink-sn 003C00193137510C39383538
   ```

3. **Smoke**
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1 --port COM9 --stlink-sn 003C00193137510C39383538 --baud 921600
   ```
   Echo captured banner from log file.

See individual skills under `.claude/skills/` for details.
