---
name: flash
description: Flash LED_Strip_Controller_G474 Debug/Release ELF via STM32_Programmer_CLI. Use when the user asks to flash, program the board, or after a build. Always targets the bench ST-Link SN from bench.defaults.json on multi-probe setups.
---

# flash

## Bench hardware (required on multi-ST-Link benches)

**Source of truth:** [`scripts/bench.defaults.json`](scripts/bench.defaults.json)

| Key | This bench |
|-----|------------|
| `stlink_sn` | `003C00193137510C39383538` |
| `com_port` | `COM9` |

When `--stlink-sn` is omitted, `scripts/discover.py` reads `bench.defaults.json`. Without it and with multiple probes connected, flash picks the **wrong** ST-Link (0.00 V / no target).

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash.ps1 --stlink-sn 003C00193137510C39383538
```

Or rely on bench defaults (after `bench.defaults.json` exists):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash.ps1
```

Default artifact: `Debug\LED_Strip_Controller_G474.elf` (build first).

## Diagnostics

- `scripts\discover.py --list` — all ST-Links + COM ports + accessibility
- Port locked → close Tera Term / other serial monitor
- ST-Link locked → close CubeIDE debug session on that probe
