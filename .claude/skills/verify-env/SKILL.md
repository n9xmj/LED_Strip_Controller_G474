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

Confirm `scripts/bench.defaults.json` lists the intended `stlink_sn` and `com_port` when multiple ST-Links are attached. To print the resolved values (never hardcode them anywhere — see [`BENCH.md`](BENCH.md)):

```powershell
python scripts\discover.py --show-bench
```

Report missing tools and any ST-Link/COM accessibility issues before build/flash.
