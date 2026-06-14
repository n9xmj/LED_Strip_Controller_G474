---
name: verify-env
description: Check LED_Strip_Controller_G474 build/flash/smoke prerequisites — STM32CubeIDE, STM32_Programmer_CLI, Python/pyserial, bench defaults file.
---

# verify-env

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-env.ps1
```

## Also verify bench config

```powershell
python scripts\discover.py --list
```

Confirm `scripts/bench.defaults.json` lists the intended `stlink_sn` and `com_port` when multiple ST-Links are attached.

Bench defaults for this project:

| Key | Value |
|-----|-------|
| `stlink_sn` | `003C00193137510C39383538` |
| `com_port` | `COM9` |
| `baud` | `921600` |

Report missing tools and any ST-Link/COM accessibility issues before build/flash.
