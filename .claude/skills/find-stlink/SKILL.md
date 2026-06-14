---
name: find-stlink
description: List ST-Links and COM ports for LED_Strip_Controller_G474, show bench defaults, and diagnose port/probe lock conflicts. Use when flash/smoke fails with wrong probe or 0.00V.
---

# find-stlink

## Bench target (this machine)

From [`scripts/bench.defaults.json`](scripts/bench.defaults.json):

- **ST-Link SN:** `003C00193137510C39383538` (G474 board)
- **Debug COM:** `COM9` @ 921600

The other connected probe is **not** this board — do not flash/smoke without an explicit SN on multi-probe benches.

## Discovery

```powershell
python scripts\discover.py --list
```

Shows bench defaults, every ST-Link SN + accessibility, every COM port + lock status.

Machine-readable:

```powershell
python scripts\discover.py --list --json
```

## Override per machine

Copy values into `scripts/bench.defaults.local.json` (gitignored):

```json
{
  "stlink_sn": "003C00193137510C39383538",
  "com_port": "COM9",
  "baud": 921600
}
```
