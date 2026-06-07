---
name: smoke
description: Run smoke tests using the local bench. /smoke does the full reset-driven test. /probe does the COM-only (no reset) test. Also handles natural language like "smoke test", "probe the board", "run smoke".
user_invocable: true
argument-hint: ""
---

# Smoke / Probe Skill

**Bench defaults (source of truth for this local setup):**
- COM port: COM9
- ST-Link SN: 003C00193137510C39383538
- Baud: 921600

**Two modes:**

- **/smoke** (reset-driven): Use the ST-Link to reset the target, then capture console output.
- **/probe** (COM-driven, no reset): Open the COM port only (equivalent to the --identify path with ESC unwind + @ for banner).

When invoked:

1. For /smoke:
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1 --port COM9 --stlink-sn 003C00193137510C39383538 --baud 921600
   ```

2. For /probe:
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-test.ps1 --port COM9 --identify --baud 921600
   ```
   (This uses the COM-driven path that sends the necessary keystrokes for menu/banner without ST-Link reset.)

3. After the command completes:
   - Report overall success/failure and any hardware diagnostics (port locked, STLink issues — the scripts print clear ERROR messages on access problems).
   - Locate the log file that was written (the output says "Log written to: smoke-YYYYMMDD-HHMMSS.log").
   - Read the log file.
   - Extract the banner block: the section delimited by lines of asterisks (****************************************************************) that contains the lines starting with "Project", "Target", "Firmware version", "Build #", and "Reset source".
   - Clearly echo the banner at the end of your response, e.g.:
     ```
     === Captured Banner from Board ===
     [the extracted block here]
     ================================
     ```
   This provides the user immediate visual confirmation of the test run and the exact FW version (including Build #) loaded on the board.

Always surface any "in-use" or "not found" errors from the capture/discover layer so the user knows if TeraTerm or another app is blocking the port.

Reference SCRIPTS.md for the general smoke test behavior and --list discovery if hardware issues arise.
