---
name: build
description: Performs an incremental debug build for this project using the standard bench settings. Use when the user invokes /build or asks for an incremental build.
user_invocable: true
argument-hint: ""
---

# Build Skill (Incremental Debug)

**Bench defaults:** resolved from [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — never hardcode SN/COM/baud; scripts auto-resolve. Values: `python scripts/discover.py --show-bench` (see [`BENCH.md`](BENCH.md)).

When invoked as `/build` or equivalent natural language for incremental build:

1. Run the build script in incremental mode for Debug (do NOT pass --bump-build-count):
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 --incremental
   ```
   (This matches the /build spec: incremental debug build. The script will use its default Debug config.)

2. After the command completes:
   - Report the exit code and overall success/failure.
   - Note the BUILD_NUMBER and FIRMWARE_VERSION reported by the script (they appear after the Mode line; the script always prints them for the build in effect).
   - Extract and report the final build summary: look for error and warning counts in the output (the headless build consoleLog or the "Build OK" / error messages). Example: "Build completed with 0 errors, 2 warnings" or surface the exact counts from the log.
   - If successful, note the artifact location (typically Debug\LED_Strip_Controller_G474.elf) and its filesystem timestamp if reported.
   - If there are errors or warnings, provide brief diagnostics (e.g. "See build.log or the console output above for details").

3. This skill is for the simple incremental case only. For clean builds, use /cleanbuild or /fullbuild instead.

Reference SCRIPTS.md for general invocation patterns. The skill supplies the bench-specific defaults and the "incremental, no bump" policy.

If the build fails or has warnings, report clearly so the user or higher-level skills (like /roundtrip) can decide whether to continue.
