---
name: roundtrip
description: Performs a full roundtrip: clean build (with bump) + flash + smoke (reset-driven). Use for /roundtrip or /full (alias). Aborts early on build issues.
user_invocable: true
argument-hint: ""
---

# Roundtrip Skill

**Bench defaults:** COM9 / 003C00193137510C39383538

This is a multi-step skill: /build (actually clean with bump) + /flash + /smoke.

**Execution rules (strict):**

1. **Build step** (clean debug with bump — do NOT use incremental):
   - Run: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 --clean --bump-build-count`
   - The script will report the BUILD_NUMBER and FIRMWARE_VERSION in effect (look for those lines after Mode; also the bump message).
   - Immediately after: parse the output for the final error and warning counts.
   - If errors > 0 or warnings > 0: **abort immediately**. Report the counts (and the BUILD_NUMBER used), do not flash or smoke. Clearly state "Roundtrip aborted due to build issues."

2. **Flash step** (only if build was clean):
   - Use the artifact from the build step (Debug\LED_Strip_Controller_G474.elf).
   - Run: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash.ps1 --stlink-sn 003C00193137510C39383538`
   - Report success/failure + diagnostics for ST-Link issues.

3. **Smoke step** (reset-driven, only if previous steps succeeded):
   - Run the /smoke path: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1 --port COM9 --stlink-sn 003C00193137510C39383538 --baud 921600`
   - After capture: extract and echo the banner from the log file exactly as described in the smoke skill.

Use todo_write to track the three steps visibly.

Report at each step. On any failure or warning in build, stop and do not execute later steps.

This matches the roundtrip spec: /build (clean+bump) + /flash + /smoke, with early abort on problems.

Reference SCRIPTS.md for the underlying script behaviors.
