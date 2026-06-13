---
name: flash
description: Flash the last build product (or current Debug artifact) to the board using the local bench hardware. Use for /flash or "flash it".
user_invocable: true
argument-hint: ""
---

# Flash Skill

**Bench defaults (source of truth):** [`scripts/bench.defaults.json`](../scripts/bench.defaults.json)  
Local override: `scripts/bench.defaults.local.json` (gitignored). Values below match the committed file.

**Bench defaults (this setup):**
- COM port: COM9
- ST-Link SN: 003C00193137510C39383538

When invoked as `/flash`:

1. Determine the artifact to flash:
   - If there was a recent successful /build or /cleanbuild in this session, use the artifact from that step (typically Debug\LED_Strip_Controller_G474.elf).
   - Otherwise, default to the Debug build: `Debug\LED_Strip_Controller_G474.elf`. Fall back to Release if Debug not present and context suggests it.
2. Run the flash command with the bench ST-Link:
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash.ps1 --stlink-sn 003C00193137510C39383538
   ```
   (The script will use the Debug config by default unless a specific Release artifact is targeted.)

3. Report:
   - The exact artifact being flashed and its timestamp.
   - Success or failure from the programmer output.
   - Brief diagnostics if ST-Link not found, busy, or other access issues (the script and discover.py provide good messages — surface them).

Do not proceed with flash if the source build had errors/warnings unless the user explicitly overrides (e.g. in a roundtrip context that already checked).
