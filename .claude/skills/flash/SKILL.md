---
name: flash
description: Flash LED_Strip_Controller_G474 Debug/Release ELF via STM32_Programmer_CLI. Use when the user asks to flash, program the board, or after a build. Always targets the bench ST-Link SN from bench.defaults.json on multi-probe setups.
---

# flash

## Bench hardware (multi-ST-Link benches)

**Source of truth:** [`scripts/bench.defaults.json`](scripts/bench.defaults.json) (see [`BENCH.md`](BENCH.md);
current values: `python scripts/discover.py --show-bench`). **Never hardcode the SN here.**

`scripts/flash.ps1` reads `bench.defaults.json` via `discover.py` when `--stlink-sn` is omitted,
so on this bench you just run the command below. Only pass `--stlink-sn` to deliberately target a
*different* probe than the bench default.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash.ps1
```

Default artifact: `Debug\LED_Strip_Controller_G474.elf` (build first).

## Diagnostics

- `scripts\discover.py --list` — all ST-Links + COM ports + accessibility
- Port locked → close Tera Term / other serial monitor
- ST-Link locked → close CubeIDE debug session on that probe
