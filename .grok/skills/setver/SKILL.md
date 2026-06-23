---
name: setver
description: Set or bump FIRMWARE_VERSION in platform.h. Format: /setver [version-string]. If version provided, set it (add quotes if missing). If omitted, bump the least-significant part (e.g. 3.0.1 -> 3.0.2). Use when user says /setver, "set version", "bump firmware version", etc.
user_invocable: true
argument-hint: "[version-string]"
---

# SetVer Skill

**Bench defaults:** resolved from [`scripts/bench.defaults.json`](scripts/bench.defaults.json) — never hardcode SN/COM/baud; scripts auto-resolve. Values: `python scripts/discover.py --show-bench` (see [`BENCH.md`](BENCH.md)).

This skill provides the `/setver` command (and natural language equivalents) to manage FIRMWARE_VERSION in `App/Inc/platform.h`. It does not affect BUILD_NUMBER.

## When invoked as `/setver` (or `/setver <version-string>`, or natural language)

1. Run the helper script, passing the version-string if provided by the user:
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setver.ps1 [version-string if given]
   ```
   - If version-string is provided: the script will add surrounding quotes (") if they are missing.
   - If version-string is omitted: the script will bump the least-significant number in the current value (e.g. "3.0.11" -> "3.0.12").

2. After the command completes:
   - Report the before/after values from the script output.
   - Report success/failure with exit code.
   - If successful, optionally verify by reading the define from App/Inc/platform.h and echoing the new value.
   - Brief diagnostics: if the script fails to find/parse the define, or file not found, surface the error.

3. This skill is the source of truth for version management in this project. It is separate from build skills (which only report the current values; clean builds may bump BUILD_NUMBER via their own logic).

Reference SCRIPTS.md for general patterns (this is a new helper). The skill supplies the bench-specific context if needed for follow-on commands.

If the operation fails, report clearly. Do not modify platform.h directly unless the helper script cannot be used.
